#pragma once
#include "protocol/command_codec.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <stop_token>

class chat_executor{
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<command_codec::command> execute_q;
    std::atomic_bool& logged_in;

    void execute(const command_codec::command& cmd);
public:
    chat_executor(const chat_executor&) = delete;
    chat_executor& operator=(const chat_executor&) = delete;

    chat_executor(chat_executor&&) noexcept = delete;
    chat_executor& operator=(chat_executor&&) noexcept = delete;

    explicit chat_executor(std::atomic_bool& logged_in) : logged_in(logged_in) {}

    void request_execute(command_codec::command cmd);
    void run(std::stop_token stop_token);
};
