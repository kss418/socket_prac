#pragma once
#include "../include/error_code.hpp"

std::expected <void, error_code> set_nonblocking(int fd);
std::expected <void, error_code> add_ep(int epfd, int fd, uint32_t events);
std::expected <void, error_code> mod_ep(int epfd, int fd, uint32_t events);
std::expected <void, error_code> del_ep(int epfd, int fd);