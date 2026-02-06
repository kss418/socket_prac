#pragma once
#include "net/io_helper.hpp"
#include "core/error_code.hpp"

class chat_client{
    socket_info si{};
    unique_fd server_fd;
public:
    chat_client(const chat_client&) = delete;
    chat_client& operator=(const chat_client&) = delete;

    chat_client(chat_client&& other) noexcept = default;
    chat_client& operator=(chat_client&& other) noexcept = default;

    chat_client() = default;
    std::expected <void, error_code> connect(const char* ip, const char* port);
    std::expected <void, error_code> run();
};