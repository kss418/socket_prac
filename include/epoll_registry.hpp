#pragma once
#include "../include/error_code.hpp"
#include "../include/epoll_wakeup.hpp"
#include "../include/io_helper.hpp"
#include "../include/unique_fd.hpp"
#include <unordered_map>
#include <queue>
#include <mutex>

class epoll_registry : public epoll_wakeup{
    std::queue <std::pair<unique_fd, uint32_t>> reg_q;
    std::queue <int> unreg_q;
    std::mutex reg_mtx;
    std::mutex unreg_mtx;
    std::unordered_map <int, socket_info> infos;

    std::expected <int, error_code> register_fd(unique_fd fd, uint32_t interest);
    std::expected <void, error_code> unregister_fd(int fd);
public:
    using socket_info_it = std::unordered_map<int, socket_info>::iterator;
    epoll_registry(const epoll_registry&) = delete;
    epoll_registry& operator=(const epoll_registry&) = delete;

    epoll_registry(epoll_registry&& other) noexcept;
    epoll_registry& operator=(epoll_registry&& other) noexcept;

    epoll_registry() = default;
    explicit epoll_registry(epoll_wakeup wakeup) : epoll_wakeup(std::move(wakeup)){}

    void request_register(unique_fd fd, uint32_t interest);
    void request_unregister(int fd);
    void work();

    socket_info_it find(int fd);
    socket_info_it end();
};
