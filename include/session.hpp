#pragma once
#include <array>
#include <cerrno>
#include <sys/socket.h>
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"

std::expected <void, error_code> echo_server(int client_fd, socket_info& si);
std::expected <void, error_code> echo_client(int server_fd, socket_info& si);