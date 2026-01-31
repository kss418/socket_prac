#pragma once
#include "../include/error_code.hpp"
#include <expected>

std::expected <void, error_code> set_nonblocking(int fd);
std::expected <void, error_code> ep_add(int epfd, int fd, uint32_t events);
std::expected <void, error_code> ep_mod(int epfd, int fd, uint32_t events);
std::expected <void, error_code> ep_del(int epfd, int fd);