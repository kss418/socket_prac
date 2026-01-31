#pragma once
#include "../include/error_code.hpp"
#include <expected>

int set_nonblocking(int fd);
int ep_add(int epfd, int fd, uint32_t events);
int ep_mod(int epfd, int fd, uint32_t events);
int ep_del(int epfd, int fd);