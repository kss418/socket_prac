#pragma once
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"
#include "../include/unique_fd.hpp"
#include <unordered_map>
#include <queue>

class epoll_registry{
    unique_fd epfd;
    std::queue <std::pair<unique_fd, uint32_t>> reg_q;
    std::queue <int> unreg_q;
    std::unordered_map <int, socket_info> infos;

    std::expected <int, error_code> register_fd(unique_fd fd, uint32_t interest);
    std::expected <void, error_code> unregister_fd(int fd);
public:
    using socket_info_it = std::unordered_map<int, socket_info>::iterator;
    epoll_registry(const epoll_registry&) = delete;
    epoll_registry& operator=(const epoll_registry&) = delete;

    epoll_registry(epoll_registry&& other) noexcept = default;
    epoll_registry& operator=(epoll_registry&& other) noexcept = default;

    epoll_registry() = default;
    epoll_registry(unique_fd epfd) : epfd(std::move(epfd)){}

    void request_register(unique_fd fd, uint32_t interest);
    void request_unregister(int fd);
    void work();

    socket_info_it find(int fd);
    socket_info_it end();
    int get_epfd() const;
};