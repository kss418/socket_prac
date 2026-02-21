#pragma once
#include "net/io_helper.hpp"
#include "net/tls_context.hpp"
#include "core/error_code.hpp"
#include <atomic>
#include <expected>
#include <string_view>

class chat_client{
    socket_info si{};
    unique_fd server_fd;
    tls_context tls_ctx;
    std::atomic_bool logged_in = false;
public:
    chat_client(socket_info si, unique_fd server_fd, tls_context tls_ctx);
    chat_client(const chat_client&) = delete;
    chat_client& operator=(const chat_client&) = delete;

    chat_client(chat_client&& other) noexcept = delete;
    chat_client& operator=(chat_client&& other) noexcept = delete;

    static std::expected <chat_client, error_code> create(
        const char* ip, const char* port, std::string_view ca_file_path = "certs/ca.crt.pem"
    );
    std::expected <void, error_code> run();
};
