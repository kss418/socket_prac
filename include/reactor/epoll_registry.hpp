#pragma once
#include "core/error_code.hpp"
#include "reactor/epoll_wakeup.hpp"
#include "net/io_helper.hpp"
#include "core/unique_fd.hpp"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <variant>
#include <cstddef>

class tls_context;

class epoll_registry : public epoll_wakeup{
    struct register_command{
        unique_fd fd;
        uint32_t interest;
    };

    struct unregister_command{
        int fd;
    };

    struct send_one_command{
        int fd;
        command_codec::command cmd;
    };

    struct broadcast_command{
        int fd;
        command_codec::command cmd;
    };

    struct change_nickname_command{
        int fd;
        std::string nick;
    };

    using command = std::variant<register_command, unregister_command, send_one_command, broadcast_command, change_nickname_command>;

    std::queue<command> cmd_q;
    std::mutex cmd_mtx;
    std::unordered_map <int, socket_info> infos;
    std::size_t connected_client_count = 0;
    tls_context& tls_ctx;

    std::expected <int, error_code> register_fd(unique_fd fd, uint32_t interest);
    std::expected <void, error_code> unregister_fd(int fd);
    std::expected <void, error_code> sync_interest(socket_info& si);
    std::expected <void, error_code> append_send(socket_info& si, const command_codec::command& cmd);

    void handle_command(register_command&& cmd);
    void handle_command(const unregister_command& cmd);
    void handle_command(send_one_command&& cmd);
    void handle_command(broadcast_command&& cmd);
    void handle_command(change_nickname_command&& cmd);
public:
    using socket_info_it = std::unordered_map<int, socket_info>::iterator;
    epoll_registry(const epoll_registry&) = delete;
    epoll_registry& operator=(const epoll_registry&) = delete;

    epoll_registry(epoll_registry&& other) noexcept = delete;
    epoll_registry& operator=(epoll_registry&& other) noexcept = delete;

    epoll_registry(epoll_wakeup wakeup, tls_context& tls_ctx);

    void request_register(unique_fd fd, uint32_t interest);
    void request_unregister(int fd);
    void request_unregister(socket_info& si);
    void request_send(int fd, command_codec::command cmd);
    void request_send(socket_info& si, command_codec::command cmd);
    void request_broadcast(int send_fd, command_codec::command cmd);
    void request_broadcast(socket_info& si, command_codec::command cmd);
    void request_change_nickname(int send_fd, std::string nick);
    void request_change_nickname(socket_info& si, std::string nick);

    void work();

    socket_info_it find(int fd);
    socket_info_it end();
};
