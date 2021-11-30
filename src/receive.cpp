#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _Windows

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
	if (argc != 2 || inet_addr(argv[1]) == INADDR_NONE)
	{
		std::cerr << "usage: " << argv[0] << " [ip address]\n";
		return 1;
	}

// open a socket
#ifdef _Windows
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
	{
		std::cerr << "WSAStartup failed\n";
		return 1;
	}

	addrinfo *client_info;
	const addrinfo &&hints = {
		0,
		AF_UNSPEC,
		SOCK_STREAM,
		IPPROTO_TCP,

	};
	if (getaddrinfo(argv[1], "48763", &hints, &client_info) != 0)
	{
		WSACleanup();
		throw std::system_error(errno, std::generic_category(), "getaddrinfo");
	}

	SOCKET socket_fd = socket(client_info->ai_family, client_info->ai_socktype,
							  client_info->ai_protocol);
	if (socket_fd == INVALID_SOCKET)
#else
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
#endif
		throw std::system_error(errno, std::generic_category(), "socket");

#ifdef _Windows
	if (connect(socket_fd, client_info->ai_addr,
				client_info->ai_addrlen) == SOCKET_ERROR)
#else
	// connect to server
	const sockaddr_in &&connect_addr = {
		PF_INET,
		htons(48763),
		{
			inet_addr(argv[1]),
		},
		{},
	};
	if (connect(socket_fd, (const sockaddr *)&connect_addr,
				sizeof(connect_addr)) < 0)
#endif
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "connect");
	}
	std::cerr << "connect server successfully\n";
#ifdef _Windows
	freeaddrinfo(client_info);
#endif

	std::vector<std::uint8_t> buf;

	// read filename size
	buf.resize(16);
	if (recv(socket_fd, (char *)buf.data(), buf.size(), 0) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "recv filename_size");
	}
	std::streamsize filename_size = std::stol(std::string(buf.cbegin(), buf.cend()));
	std::cerr << "filename size: " << filename_size << '\n';

	// send ack message
	const std::uint8_t ack = 0;
#ifdef _Windows
	const int send_flag = 0;
#else
	const int send_flag = MSG_NOSIGNAL;
#endif

	if (send(socket_fd, (const char *)&ack, sizeof(ack), send_flag) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "send filename_size ack");
	}

	// read filename
	buf.assign(filename_size, 0U);
	if (recv(socket_fd, (char *)buf.data(), filename_size, MSG_WAITALL) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "recv filename");
	}
	const std::string filename(buf.cbegin(), buf.cend());
	std::cerr << "filename: " << filename << '\n';

	// send ack message
	if (send(socket_fd, (const char *)&ack, sizeof(ack), send_flag) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "send filename_ack");
	}

	// check file existance
	if (std::filesystem::exists(filename))
	{
		std::cerr << "the file " << filename << " already exists\n";
		return 1;
	}

	// read file size
	buf.assign(16, 0U);
	if (recv(socket_fd, (char *)buf.data(), buf.size(), 0) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "recv filesize");
	}
	std::streamsize filesize = std::stol(std::string(buf.cbegin(), buf.cend()));
	std::cerr << "file size: " << filesize << '\n';

	// send ack message
	if (send(socket_fd, (const char *)&ack, sizeof(ack), send_flag) < 0)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), "send filesize_ack");
	}

	// open file to write
	std::filebuf file;
	if (file.open(filename, std::ios_base::out | std::ios_base::binary) == nullptr)
	{
#ifdef _Windows
		closesocket(socket_fd);
		WSACleanup();
#else
		close(socket_fd);
#endif
		throw std::system_error(errno, std::generic_category(), filename);
	}

	// read data
	std::streamsize received = 0;
	while (received < filesize)
	{
		std::size_t bufsize = filesize - received > 65536 ? 65536 : filesize - received;
		buf.assign(bufsize, 0);
		ssize_t result = recv(socket_fd, (char *)buf.data(), bufsize, MSG_WAITALL);
		if (result < 0)
		{
#ifdef _Windows
			closesocket(socket_fd);
			WSACleanup();
#else
			close(socket_fd);
#endif
			file.close();
			std::filesystem::remove(filename);
			throw std::system_error(errno, std::generic_category(), "recv file");
		}
#ifdef DEBUG
		std::cerr << "wrote " << result << " bytes\n";
#endif
		file.sputn((const char *)buf.data(), result);
		received += result;
	}

	std::cerr << "file transferred successfully\n";
#ifdef _Windows
	closesocket(socket_fd);
	WSACleanup();
#else
	close(socket_fd);
#endif
	return 0;
}
