#pragma once
#include "database/db_service.hpp"
#include "protocol/command_codec.hpp"
#include "reactor/epoll_registry.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class db_executor{
    std::vector<std::jthread> workers;
    std::mutex mtx;
    std::condition_variable cv;
    bool run = true;
    db_service& db;

    struct task{
        command_codec::command cmd;
        epoll_registry& reg;
        int fd;
        std::string user_id;
    };

    std::queue<task> tasks;
    void worker_loop(std::stop_token st);
    void execute(const task& t);
    void execute_command(const command_codec::cmd_login& cmd, epoll_registry& reg, int fd);
    void execute_command(const command_codec::cmd_register& cmd, epoll_registry& reg, int fd);
    void execute_command(
        const command_codec::cmd_nick& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_friend_request& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_friend_accept& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_friend_reject& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_friend_remove& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_list_friend& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_list_friend_request& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_create_room& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_delete_room& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_invite_room& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(
        const command_codec::cmd_list_room& cmd, epoll_registry& reg, int fd, std::string_view user_id
    );
    void execute_command(const command_codec::cmd_say& cmd, epoll_registry& reg, int fd);
    void execute_command(const command_codec::cmd_response& cmd, epoll_registry& reg, int fd);

public:
    explicit db_executor(db_service& db, std::size_t sz = 1);
    ~db_executor();

    db_executor(const db_executor&) = delete;
    db_executor& operator=(const db_executor&) = delete;
    db_executor(db_executor&&) = delete;
    db_executor& operator=(db_executor&&) = delete;

    static bool is_db_command(const command_codec::command& cmd) noexcept;
    void stop();
    bool enqueue(command_codec::command cmd, epoll_registry& reg, int fd);
    bool enqueue(command_codec::command cmd, epoll_registry& reg, socket_info& si);
};
