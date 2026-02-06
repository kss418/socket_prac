#pragma once
#include <netdb.h>
#include <memory>
#include <string_view>
#include "core/error_code.hpp"

struct addr_option{
    int family = AF_UNSPEC; // IPv4, IPv6
    int socktype = SOCK_STREAM; // TCP
    int protocol = 0;
    int flags = 0;
};

struct addr{
private:
    struct deleter{
        void operator()(addrinfo* p) const noexcept { if(p) ::freeaddrinfo(p); }
    };
    std::unique_ptr<addrinfo, deleter> res{nullptr};

    [[nodiscard]] int resolve(const char* host, const char* port, addr_option opt) noexcept;
public:
    addr() = default;
    
    addr(const addr&) = delete;
    addr& operator=(const addr&) = delete;

    addr(addr&&) noexcept = default;
    addr& operator=(addr&&) noexcept = default;

    [[nodiscard]] int resolve_client(std::string_view host, std::string_view port, addr_option opt = {}) noexcept;
    [[nodiscard]] int resolve_server(std::string_view port,addr_option opt = {}) noexcept;
    addrinfo* get() const noexcept;
    explicit operator bool() const noexcept;
};

[[nodiscard]] std::expected <addr, error_code> get_addr_server(std::string_view port, addr_option opt = {});
[[nodiscard]] std::expected <addr, error_code> get_addr_client(std::string_view host, std::string_view port, addr_option opt = {});