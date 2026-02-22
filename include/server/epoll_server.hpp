#pragma once
#include "core/error_code.hpp"
#include "net/io_helper.hpp"
#include "reactor/epoll_registry.hpp"
#include "reactor/event_loop.hpp"
#include "server/epoll_listener.hpp"
#include "server/epoll_acceptor.hpp"
#include "protocol/command_codec.hpp"
#include "core/thread_pool.hpp"
#include "database/db_executor.hpp"
#include "net/tls_context.hpp"
#include <stop_token>

class db_service;

class epoll_server{
    tls_context tls_ctx;
    epoll_registry registry;
    epoll_listener listener;
    thread_pool pool{};
    db_executor db_pool;
    std::string port;

    std::expected <void, error_code> sync_tls_interest(socket_info& si);
    std::expected <void, error_code> progress_tls_handshake(socket_info& si);
    void request_unregister(socket_info& si);
    void handle_send(socket_info& si);
    bool handle_recv(socket_info& si, uint32_t event);
    void handle_close(socket_info& si);
    void handle_client_error(int fd, uint32_t event);
    bool handle_execute(socket_info& si);
public:
    epoll_server(const epoll_server&) = delete;
    epoll_server& operator=(const epoll_server&) = delete;

    epoll_server(epoll_server&& other) noexcept = delete;
    epoll_server& operator=(epoll_server&& other) noexcept = delete;

    static std::expected <epoll_server, error_code> create(
        const char* port, db_service& db, tls_context tls_ctx
    );
    epoll_server(
        epoll_wakeup wakeup, epoll_listener listener, tls_context tls_ctx, db_service& db, const char* port
    );
    std::expected <void, error_code> run();
    std::expected <void, error_code> run(const std::stop_token& stop_token);
};
