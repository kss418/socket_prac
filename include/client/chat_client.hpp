#pragma once
#include "net/io_helper.hpp"
#include "core/error_code.hpp"
#include "protocol/command_codec.hpp"
#include <condition_variable>
#include <thread>
#include <queue>

class chat_client{
    socket_info si{};
    unique_fd server_fd;
    std::string buf;

    std::mutex execute_mtx;
    std::queue <command_codec::command> execute_q;
    std::condition_variable execute_cv;

    std::expected <bool, error_code> recv();
    std::expected <bool, error_code> send();

    void execute_loop(std::stop_token stop_token);
    std::expected <void, error_code> io_loop(std::stop_token stop_token);

    void execute(const command_codec::command& cmd);
    void request_execute(const command_codec::command& cmd);
public:
    chat_client(const chat_client&) = delete;
    chat_client& operator=(const chat_client&) = delete;

    chat_client(chat_client&& other) noexcept = delete;
    chat_client& operator=(chat_client&& other) noexcept = delete;

    chat_client() = default;
    std::expected <void, error_code> connect(const char* ip, const char* port);
    std::expected <void, error_code> run();
};