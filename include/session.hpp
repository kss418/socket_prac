#pragma once
#include <array>
#include <cerrno>
#include <sys/socket.h>
#include "../include/error_code.hpp"
constexpr int BUF_SIZE = 4096;

std::expected <void, error_code> echo_server(int client_fd);
std::expected <void, error_code> echo_client(int server_fd);