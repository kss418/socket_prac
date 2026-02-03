#pragma once
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"
#include "../include/unique_fd.hpp"
#include <unordered_map>

class epoll_registry{
    unique_fd epfd;
    std::unordered_map <int, socket_info> infos;
    std::expected <void, error_code> set_nonblocking(int fd);
    std::expected <void, error_code> add_fd(int fd, uint32_t interest);
    std::expected <void, error_code> del_fd(int fd);
public:
    using socket_info_it = std::unordered_map<int, socket_info>::iterator;
    epoll_registry(const epoll_registry&) = delete;
    epoll_registry& operator=(const epoll_registry&) = delete;

    epoll_registry(epoll_registry&& other) noexcept = default;
    epoll_registry& operator=(epoll_registry&& other) noexcept = default;

    epoll_registry() = default;
    epoll_registry(unique_fd epfd) : epfd(std::move(epfd)){}

    std::expected <int, error_code> register_client(unique_fd client_fd, uint32_t interest);
    std::expected <int, error_code> register_listener(int fd);
    std::expected <void, error_code> unregister(int fd);
    std::expected <void, error_code> update_interest(int fd, uint32_t interest);

    socket_info_it find(int fd);
    socket_info_it end();
    int get_epfd() const;
};