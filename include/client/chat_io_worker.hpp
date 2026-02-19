#pragma once
#include "client/chat_executor.hpp"
#include "core/error_code.hpp"
#include "core/unique_fd.hpp"
#include "net/io_helper.hpp"
#include <expected>
#include <stop_token>
#include <string>
#include <vector>

class chat_io_worker{
    struct parsed_command{
        std::string cmd;
        std::vector<std::string> args;
    };

    socket_info& si;
    unique_fd& server_fd;
    chat_executor& executor;
    recv_buffer stdin_buf;

    static parsed_command parse(const std::string& line);

    std::expected<bool, error_code> recv_socket();
    std::expected<bool, error_code> send_stdin();

    void execute(const std::string& line);
    void say(const std::string& line);
    void change_nickname(const std::string& nick);
public:
    chat_io_worker(socket_info& si, unique_fd& server_fd, chat_executor& executor);

    chat_io_worker(const chat_io_worker&) = delete;
    chat_io_worker& operator=(const chat_io_worker&) = delete;

    std::expected<void, error_code> run(std::stop_token stop_token);
};
