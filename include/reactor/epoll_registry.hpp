#pragma once
#include "core/error_code.hpp"
#include "reactor/epoll_wakeup.hpp"
#include "net/io_helper.hpp"
#include "core/unique_fd.hpp"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <variant>

class epoll_registry : public epoll_wakeup{
    struct register_command{
        unique_fd fd;
        uint32_t interest;
    };

    struct unregister_command{
        int fd;
    };

    using command = std::variant<register_command, unregister_command>;

    std::queue<command> cmd_q;
    std::mutex cmd_mtx;
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
