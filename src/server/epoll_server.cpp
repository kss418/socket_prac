#include "server/epoll_server.hpp"
#include "database/db_service.hpp"
#include "net/addr.hpp"
#include "reactor/epoll_utility.hpp"
#include "protocol/line_parser.hpp"
#include <cerrno>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <sys/socket.h>

std::expected <epoll_server, error_code> epoll_server::create(const char* port, db_service& db){
    auto addr_exp = get_addr_server(port);
    if(!addr_exp){
        handle_error("create/get_addr_server failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto listen_fd_exp = epoll_listener::create(addr_exp->get());
    if(!listen_fd_exp){
        handle_error("create/epoll_listener::create failed", listen_fd_exp);
        return std::unexpected(listen_fd_exp.error());
    }

    auto wakeup_exp = epoll_wakeup::create();
    if(!wakeup_exp){
        handle_error("create/epoll_wakeup::create failed", wakeup_exp);
        return std::unexpected(wakeup_exp.error());
    }

    epoll_registry registry(std::move(*wakeup_exp));
    epoll_listener listener = std::move(*listen_fd_exp);
    return std::expected<epoll_server, error_code>(
        std::in_place, std::move(registry), std::move(listener), db
    );
}

epoll_server::epoll_server(
    epoll_registry registry, epoll_listener listener, db_service& db
) : registry(std::move(registry)), listener(std::move(listener)), db_pool(db){}

std::expected <void, error_code> epoll_server::run(){
    std::stop_source stop_source;
    return run(stop_source.get_token());
}

std::expected <void, error_code> epoll_server::run(const std::stop_token& stop_token){
    std::mutex state_mtx;
    std::condition_variable state_cv;
    std::optional<error_code> error_opt;
    bool stop_requested = false;

    auto signal_stop = [&](std::optional<error_code> ec = std::nullopt){
        {
            std::lock_guard<std::mutex> lock(state_mtx);
            if(ec && !error_opt) error_opt = *ec;
            stop_requested = true;
        }
        state_cv.notify_one();
    };

    std::jthread accept_thread([this, &signal_stop](std::stop_token st){
        epoll_acceptor acceptor(listener, registry);
        auto accept_exp = acceptor.run(st);
        if(!accept_exp){
            handle_error("acceptor thread error", accept_exp);
            signal_stop(accept_exp.error());
        }
    });

    std::jthread event_thread([this, &signal_stop](std::stop_token st){
        event_loop loop(registry);
        auto run_exp = loop.run(
            st,
            [this](int fd, socket_info& si, uint32_t event){ return handle_recv(fd, si, event); },
            [this](int fd, socket_info& si){ handle_send(fd, si); },
            [this](int fd, socket_info& si){ return handle_execute(fd); },
            [this](int fd, uint32_t event){ handle_client_error(fd, event); }
        );

        if(!run_exp){
            handle_error("event loop thread error", run_exp);
            signal_stop(run_exp.error());
        }
    });

    std::stop_callback on_external_stop(stop_token, [&](){ signal_stop(); });

    {
        std::unique_lock<std::mutex> lock(state_mtx);
        state_cv.wait(lock, [&](){ return stop_requested; });
    }

    event_thread.request_stop();
    accept_thread.request_stop();
    registry.request_wakeup();
    listener.request_wakeup();
    event_thread.join();
    accept_thread.join();

    if(error_opt) return std::unexpected(*error_opt);
    return {};
}

void epoll_server::handle_send(int fd, socket_info& si){
    auto fs_exp = flush_send(fd, si);
    if(!fs_exp){
        handle_error("run/handle_send/flush_send failed", fs_exp);
        registry.request_unregister(fd);
        return; 
    }
                
    if(si.send.has_pending()) return;
    si.interest &= ~EPOLLOUT;
                
    auto mod_ep_exp = epoll_utility::update_interest(registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) registry.request_unregister(fd);
}

bool epoll_server::handle_recv(int fd, socket_info& si, uint32_t event){
    auto dr_exp = drain_recv(fd, si);
    if(!dr_exp){
        handle_error(to_string(si.ep) + " run/handle_recv/drain_recv failed", dr_exp);
        registry.request_unregister(fd);
        return false;
    }

    auto recv_info = *dr_exp;
    std::cout << to_string(si.ep) << " sends " << recv_info.byte 
        << " byte" << (recv_info.byte == 1 ? "\n" : "s\n");

    if(recv_info.closed || event & EPOLLRDHUP){ // peer closed
        handle_close(fd, si);
        return false;
    }

    return true;
}

void epoll_server::handle_close(int fd, socket_info& si){
    std::cout << to_string(si.ep) << " is disconnected" << "\n";
    registry.request_unregister(fd);
}

void epoll_server::handle_client_error(int fd, uint32_t event){
    int ec = ECONNRESET;
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1){
        ec = errno;
    }
    else if(so_error != 0){
        ec = so_error;
    }

    handle_error("run/handle_client_error", error_code::from_errno(ec));
    registry.request_unregister(fd);
}

bool epoll_server::handle_execute(int fd){
    auto it = registry.find(fd);
    if(it == registry.end()) return false;
    auto line = line_parser::parse_line(it->second.recv);
    if(!line) return false;

    auto dec_exp = command_codec::decode(*line);
    if(!dec_exp){
        handle_error("decode failed", dec_exp);
        return true;
    }

    auto cmd = std::move(*dec_exp);
    if(db_executor::is_db_command(cmd)){
        return db_pool.enqueue(std::move(cmd), registry, fd);
    }

    if(thread_pool::is_pool_command(cmd)){
        return pool.enqueue(std::move(cmd), registry, fd);
    }

    std::visit([this, fd](auto&& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, command_codec::cmd_say>){
            registry.request_broadcast(fd, command_codec::cmd_response{c.text});
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){
            registry.request_change_nickname(fd, c.nick);
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_response>){
            registry.request_send(fd, command_codec::cmd_response{c.text});
        }
    }, std::move(cmd));

    return true;
}
