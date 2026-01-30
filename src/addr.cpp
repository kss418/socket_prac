#include "../include/addr.hpp"

int addr::resolve(const char* host, const char* port, addr::option opt) noexcept{
    res.reset();
    addrinfo hints{};
    hints.ai_family = opt.family;
    hints.ai_socktype = opt.socktype;
    hints.ai_protocol = opt.protocol;
    hints.ai_flags = opt.flags;

    addrinfo* raw = nullptr;
    int ec = ::getaddrinfo(host, port, &hints, &raw);
    if(ec == 0) res.reset(raw);
    return ec;
}

int addr::resolve_client(std::string_view host, std::string_view port, option opt) noexcept{
    std::string host_str = std::string(host);
    std::string port_str = std::string(port);
    return resolve(host.empty() ? nullptr : host_str.c_str(), port_str.c_str(), opt);
}

int addr::resolve_server(std::string_view port, option opt) noexcept{
    std::string port_str = std::string(port);
    opt.flags |= AI_PASSIVE;
    return resolve(nullptr, port_str.c_str(), opt);
}

addrinfo* addr::get() const noexcept{ return res.get(); }
addr::operator bool() const noexcept{ return res != nullptr; }

std::expected <addr, int> get_addr_server(
    std::string_view port, addr::option opt
){
    addr ret;
    int ec = ret.resolve_server(port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};

std::expected <addr, int> get_addr_client(
    std::string_view host, std::string_view port, addr::option opt
){
    addr ret;
    int ec = ret.resolve_client(host, port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};