#pragma once
#include <netdb.h>
#include <memory>
#include <expected>
#include <string_view>

struct addr{
public:
    struct option{
        int family = AF_UNSPEC; // IPv4, IPv6
        int socktype = SOCK_STREAM; // TCP
        int protocol = 0;
        int flags = 0;
    };
private:
    struct deleter{
        void operator()(addrinfo* p) const noexcept { if(p) ::freeaddrinfo(p); }
    };
    std::unique_ptr<addrinfo, deleter> res{nullptr};

    [[nodiscard]] int resolve(const char* host, const char* port, option opt) noexcept;
public:
    addr() = default;
    
    addr(const addr&) = delete;
    addr& operator=(const addr&) = delete;

    addr(addr&&) noexcept = default;
    addr& operator=(addr&&) noexcept = default;

    [[nodiscard]] int resolve_client(std::string_view host, std::string_view port, option opt = {}) noexcept;
    [[nodiscard]] int resolve_server(std::string_view port, option opt = {}) noexcept;
    addrinfo* get() const noexcept;
    explicit operator bool() const noexcept;
};

[[nodiscard]] std::expected <addr, int> get_addr_server(
    std::string_view port, addr::option opt = {}
){
    addr ret;
    int ec = ret.resolve_server(port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};

[[nodiscard]] std::expected <addr, int> get_addr_client(
    std::string_view host, std::string_view port, addr::option opt = {}
){
    addr ret;
    int ec = ret.resolve_client(host, port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};