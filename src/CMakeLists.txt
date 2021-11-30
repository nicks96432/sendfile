include_directories(${sendfile_SOURCE_DIR}/src)

add_executable(send send.cpp)
add_executable(receive receive.cpp)

if (WIN32)
	target_link_libraries(send ws2_32)
	target_link_libraries(receive ws2_32)
endif()
