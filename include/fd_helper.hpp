#pragma once;
#include <expected>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cerrno>
#include "../include/unique_fd.hpp"

std::expected<unique_fd, int> make_listen_fd(addrinfo* head);
std::expected <unique_fd, int> make_client_fd(int listen_fd);
