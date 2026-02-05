#pragma once
#include "../include/epoll_registry.hpp"
#include "../include/error_code.hpp"
#include "../include/constant.hpp"
#include <array>
#include <functional>
#include <sys/epoll.h>

class event_loop{
    epoll_registry& registry;
    std::array<epoll_event, EVENT_SIZE> events;
public:
    explicit event_loop(epoll_registry& registry);
    std::expected<void, error_code> run(
        const std::function<void(int, socket_info&, uint32_t)>& on_recv,
        const std::function<void(int, socket_info&)>& on_send,
        const std::function<void(int)>& on_client_error
    );
};