#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

int main(int argc, char const *argv[])
{
	if (argc != 2)
	{
		std::cerr << "usage: " << argv[0] << " [file]\n";
		return 1;
	}

	// check file existance
	std::filebuf file;
	const std::filesystem::path filepath = argv[1];
	if (file.open(filepath, std::ios_base::in | std::ios_base::binary) == nullptr)
	{
		std::cerr << filepath << ": file not found\n";
		return 1;
	}

	// get file size
	const std::string &&filesize =
		std::to_string(file.pubseekoff(0, std::ios_base::end));
	file.pubseekoff(0, std::ios_base::beg);

	// open a socket
#ifdef _WIN32
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
	{
		std::cerr << "WSAStartup failed\n";
		return 1;
	}
	addrinfo *server_info = NULL;
	addrinfo &&hints = {
		AI_PASSIVE,
		AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
	};
	if (getaddrinfo(nullptr, "48763", &hints, &server_info) != 0)
	{
		WSACleanup();
		throw std::system_error(errno, std::generic_category(), "getaddrinfo");
	}

	SOCKET socket_fd = socket(server_info->ai_family, server_info->ai_socktype,
							  server_info->ai_protocol);
	if (socket_fd == INVALID_SOCKET)
#else
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
#endif
	{
		file.close();
#ifdef _WIN32
		freeaddrinfo(server_info);
		WSACleanup();
#endif
		throw std::system_error(errno, std::generic_category(), "socket");
	}

	// set reuse ip address
#ifdef _WIN32
	const char yes = 1;
#else
	const int yes = 1;
#endif
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
				   &yes, sizeof(yes)) != 0)
	{
		file.close();
#ifdef _WIN32
		closesocket(socket_fd);
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "setsockopt");
	}

	// bind socket to ip address and port
#ifdef _WIN32
	if (bind(socket_fd, server_info->ai_addr, (int)server_info->ai_addrlen) != 0)
#else
	const sockaddr_in &&server_addr = {
		AF_INET,
		htons(48763),
		{
			htonl(INADDR_ANY),
		},
		{},
	};
	if (bind(socket_fd, (const sockaddr *)&server_addr, sizeof(server_addr)) != 0)
#endif
	{
		file.close();
#ifdef _WIN32
		freeaddrinfo(server_info);
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "bind");
	}

#ifdef _WIN32
	freeaddrinfo(server_info);
#endif

	// listen on socket
	if (listen(socket_fd, 1) != 0)
	{
		file.close();
#ifdef _WIN32
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "listen");
	}

	std::cerr << "server is listening on port 48763\n";

	std::vector<std::uint8_t> buf;

	while (true)
	{
		// accept new connection
		sockaddr_in client_addr;
#ifdef _WIN32
		int client_addr_len = sizeof(client_addr);
		SOCKET connection_fd = accept(socket_fd, (sockaddr *)&client_addr, &client_addr_len);
		if (connection_fd == INVALID_SOCKET)
#else
		socklen_t client_addr_len = sizeof(client_addr);
		int connection_fd = accept(socket_fd, (sockaddr *)&client_addr, &client_addr_len);
		if (connection_fd < 0)
#endif
		{
			if (errno == EBADF)
				continue;
			file.close();
#ifdef _WIN32
			closesocket(socket_fd);
			WSACleanup();
#else
			close(socket_fd);
#endif
			throw std::system_error(errno, std::generic_category(), "accept");
		}
		std::cerr << "getting new connection from " << inet_ntoa(client_addr.sin_addr)
				  << ':' << client_addr.sin_port << '\n';

		// send filename size
#ifdef _WIN32
		int send_flag = 0;
#else
		int send_flag = MSG_NOSIGNAL;
#endif

		const std::string &&basename = filepath.filename().string();
		const std::string &&basename_len = std::to_string(basename.length());
		if (send(connection_fd, basename_len.c_str(),
				 basename_len.length(), send_flag) < 0)
		{
			std::cerr << "send filename size: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		// wait for client to ack filename size
		std::uint8_t status = 0;
		if (recv(connection_fd, (char *)&status, sizeof(status), 0) < 0)
		{
			std::cerr << "recv filename size ack: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		// send filename
		if (send(connection_fd, basename.c_str(),
				 basename.length(), send_flag) < 0)
		{
			std::cerr << "send filename: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		// wait for client to ack filename
		if (recv(connection_fd, (char *)&status, sizeof(status), 0) < 0)
		{
			std::cerr << "recv filename ack: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		// send file size
		if (send(connection_fd, filesize.c_str(),
				 filesize.length(), send_flag) < 0)
		{
			std::cerr << "send filesize: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		// wait for client to ack file size
		if (recv(connection_fd, (char *)&status, sizeof(status), 0) < 0)
		{
			std::cerr << "recv filesize ack: connection closed\n";
#ifdef _WIN32
			closesocket(connection_fd);
#else
			close(connection_fd);
#endif
			continue;
		}

		std::cerr << "start transferring\n";
		while (true)
		{
			// read file data to buf
			buf.assign(65536, 0U);
			std::streamsize read_size =
				file.sgetn((char *)buf.data(), buf.size());
			if (read_size == 0)
				break;
#ifdef DEBUG
			std::cerr << "read " << read_size << " bytes\n";
#endif
			// write to client
#ifdef _WIN32
			int sent_total = 0;
			int sent;
#else
			ssize_t sent_total = 0;
			ssize_t sent;
#endif
			bool connection_closed = false;
			while (sent_total < read_size)
			{
				sent = send(connection_fd, (const char *)buf.data() + sent_total,
							read_size - sent_total, send_flag);
				if (sent < 0)
				{
					connection_closed = true;
					break;
				}
				sent_total += sent;
			}
#ifdef DEBUG
			std::cerr << "sent " << sent_total << " bytes\n";
#endif
			if (connection_closed)
			{
				std::cerr << "send file: connection closed\n";
#ifdef _WIN32
				closesocket(connection_fd);
#else
				close(connection_fd);
#endif
				break;
			}
		}
		std::cerr << "file transferred successfully\n";
#ifdef _WIN32
		closesocket(connection_fd);
#else
		close(connection_fd);
#endif
		file.pubseekpos(0);
	}

	file.close();
#ifdef _WIN32
	closesocket(socket_fd);
	WSACleanup();
#else
	close(socket_fd);
#endif
	return 0;
}
