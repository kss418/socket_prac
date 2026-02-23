#pragma once
#include "core/error_code.hpp"
#include "reactor/epoll_wakeup.hpp"
#include "net/io_helper.hpp"
#include "core/unique_fd.hpp"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <variant>
#include <cstddef>
#include <vector>

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

    struct set_user_id_command{
        int fd;
        std::string user_id;
    };

    struct set_joined_rooms_command{
        int fd;
        std::vector<std::int64_t> room_ids;
    };

    struct set_joined_rooms_for_user_command{
        std::string user_id;
        std::vector<std::int64_t> room_ids;
    };

    struct room_broadcast_command{
        int sender_fd;
        std::int64_t room_id;
        command_codec::command cmd;
    };

    using command = std::variant<
        register_command,
        unregister_command,
        send_one_command,
        broadcast_command,
        change_nickname_command,
        set_user_id_command,
        set_joined_rooms_command,
        set_joined_rooms_for_user_command,
        room_broadcast_command
    >;

    std::queue<command> cmd_q;
    std::mutex cmd_mtx;
    std::unordered_map <int, socket_info> infos;
    std::unordered_map<std::int64_t, std::unordered_set<int>> room_online_fds;
    std::unordered_map<std::string, std::unordered_set<int>> user_online_fds;
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
    void handle_command(set_user_id_command&& cmd);
    void handle_command(set_joined_rooms_command&& cmd);
    void handle_command(set_joined_rooms_for_user_command&& cmd);
    void handle_command(room_broadcast_command&& cmd);
    void remove_fd_from_room_index(socket_info& si);
    void remove_fd_from_user_index(socket_info& si);
    void set_fd_joined_rooms(socket_info& si, std::vector<std::int64_t>&& room_ids);
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
    void request_set_user_id(int fd, std::string user_id);
    void request_set_user_id(socket_info& si, std::string user_id);
    void request_set_joined_rooms(int fd, std::vector<std::int64_t> room_ids);
    void request_set_joined_rooms(socket_info& si, std::vector<std::int64_t> room_ids);
    void request_set_joined_rooms_for_user(std::string user_id, std::vector<std::int64_t> room_ids);
    void request_room_broadcast(int sender_fd, std::int64_t room_id, command_codec::command cmd);
    void request_room_broadcast(socket_info& si, std::int64_t room_id, command_codec::command cmd);

    void work();

    socket_info_it find(int fd);
    socket_info_it end();
};
