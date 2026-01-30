#include <iostream>
#include <expected>
#include <memory>
#include <cerrno>
#include <string>
#include <string_view>
#include <cstring>
#include <array>
#include <utility>

#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include "../include/unique_fd.hpp"

constexpr int BUF_SIZE = 4096;

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

std::expected<unique_fd, int> make_listen_fd(addrinfo* head){
    int ec = 0;
    for(addrinfo* p = head; p; p = p->ai_next){
        unique_fd fd(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if(!fd){ ec = errno; continue; }

        int use = 1;
        if(::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &use, sizeof(use)) == -1){
            ec = errno;
            continue;
        }

        if(::bind(fd.get(), p->ai_addr, p->ai_addrlen) == 0){
            if(::listen(fd.get(), SOMAXCONN) == 0) return fd;
            ec = errno;
            return std::unexpected(ec);
        }

        ec = errno;
    }

    if(ec == 0) ec = EINVAL;
    return std::unexpected(ec);
}

std::expected <unique_fd, int> make_client_fd(int listen_fd){
    while(true){
        int client_fd = ::accept(listen_fd, nullptr, nullptr);
        if(client_fd != -1) return unique_fd(client_fd);

        int ec = errno;
        if(ec == EINTR) continue;
        return std::unexpected(ec);
    }
}

std::expected <void, int> echo_session(int client_fd){
    std::array <char, BUF_SIZE> buf{};
    while(true){
        ssize_t recv_byte = ::recv(client_fd, buf.data(), buf.size(), 0);
        if(recv_byte == 0) return {};
        if(recv_byte == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(ec);
        }

        ssize_t send_byte = 0;
        while(send_byte < recv_byte){
            ssize_t now = ::send(client_fd, buf.data() + send_byte, recv_byte - send_byte, MSG_NOSIGNAL);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(ec);
            }

            if(now == 0) return std::unexpected(EPIPE);
            send_byte += now;
        }
    }
}

int main(){
    auto addr_exp = get_addr("8080");
    if(!addr_exp){
        std::cerr << "try_get_addr failed: " << ::gai_strerror(addr_exp.error()) << "\n";
        return 1;
    }

    auto listen_fd_exp = make_listen_fd(addr_exp->get());
    if(!listen_fd_exp){
        std::cerr << "make_listen_fd failed: " << std::strerror(listen_fd_exp.error()) << "\n";
        return 1;
    }

    unique_fd listen_fd = std::move(*listen_fd_exp);
    while(true){
        auto client_fd_exp = make_client_fd(listen_fd.get());
        if(!client_fd_exp){
            std::cerr << "make_client_fd failed " << std::strerror(client_fd_exp.error()) << "\n";
            continue;
        }

        unique_fd client_fd = std::move(*client_fd_exp);
        auto session_exp = echo_session(client_fd.get());
        if(!session_exp){
            std::cerr << "echo_session failed: " << std::strerror(session_exp.error()) << "\n";
            continue;
        }
    }

    return 0;
}