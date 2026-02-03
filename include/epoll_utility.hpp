#pragma once
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"

namespace epoll_utility{
    std::expected <void, error_code> set_nonblocking(int fd);
    std::expected <void, error_code> add_fd(int epfd, int fd, uint32_t interest);
    std::expected <void, error_code> del_fd(int epfd, int fd);
    std::expected <void, error_code> update_interest(int epfd, int fd, socket_info& si, uint32_t interest);
}