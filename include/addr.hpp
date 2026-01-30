#pragma once;

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

    [[nodiscard]] int resolve(const char* host, const char* port, option opt) noexcept{
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
public:
    addr() = default;
    
    // delete copy constructor
    addr(const addr&) = delete;
    addr& operator=(const addr&) = delete;

    // default move constructor
    addr(addr&&) noexcept = default;
    addr& operator=(addr&&) noexcept = default;

    int resolve_client(std::string_view host, std::string_view port, option opt = {}) noexcept{
        std::string host_str = std::string(host);
        std::string port_str = std::string(port);
        return resolve(host.empty() ? nullptr : host_str.c_str(), port_str.c_str(), opt);
    }

    int resolve_server(std::string_view port, option opt = {}) noexcept{
        std::string port_str = std::string(port);
        opt.flags |= AI_PASSIVE;
        return resolve(nullptr, port_str.c_str(), opt);
    }

    addrinfo* get() const{ return res.get(); }
    explicit operator bool() const { return res != nullptr; }
};

std::expected <addr, int> get_addr_server(std::string_view port, addr::option opt = {}){
    addr ret;
    int ec = ret.resolve_server(port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};

std::expected <addr, int> get_addr_client(std::string_view host, std::string_view port, addr::option opt = {}){
    addr ret;
    int ec = ret.resolve_client(host, port, opt);
    if(ec) return std::unexpected(ec);
    return ret;
};