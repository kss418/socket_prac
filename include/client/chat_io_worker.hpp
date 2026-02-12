#pragma once
#include "client/chat_executor.hpp"
#include "core/error_code.hpp"
#include "core/unique_fd.hpp"
#include "net/io_helper.hpp"
#include <expected>
#include <stop_token>
#include <string>

class chat_io_worker{
    socket_info& si;
    unique_fd& server_fd;
    chat_executor& executor;
    std::string stdin_buf;

    std::expected<bool, error_code> recv_socket();
    std::expected<bool, error_code> send_stdin();
public:
    chat_io_worker(socket_info& si, unique_fd& server_fd, chat_executor& executor);

    chat_io_worker(const chat_io_worker&) = delete;
    chat_io_worker& operator=(const chat_io_worker&) = delete;

    std::expected<void, error_code> run(std::stop_token stop_token);
};
