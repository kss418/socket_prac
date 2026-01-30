#pragma once
#include <expected>
#include <array>
#include <cerrno>
#include <sys/socket.h>
constexpr int BUF_SIZE = 4096;

std::expected <void, int> echo_session(int client_fd);