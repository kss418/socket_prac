#include "server/epoll_server.hpp"
#include "core/logger.hpp"
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

std::expected <epoll_server, error_code> epoll_server::create(
    const char* port, db_service& db, tls_context tls_ctx
){
    auto addr_exp = get_addr_server(port);
    if(!addr_exp){
        logger::log_error("get_addr_server failed", "epoll_server::create()", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto listen_fd_exp = epoll_listener::create(addr_exp->get());
    if(!listen_fd_exp){
        logger::log_error("epoll_listener/create failed", "epoll_server::create()", listen_fd_exp);
        return std::unexpected(listen_fd_exp.error());
    }

    auto wakeup_exp = epoll_wakeup::create();
    if(!wakeup_exp){
        logger::log_error("epoll_wakeup/create failed", "epoll_server::create()", wakeup_exp);
        return std::unexpected(wakeup_exp.error());
    }

    return std::expected<epoll_server, error_code>(
        std::in_place, std::move(*wakeup_exp), std::move(*listen_fd_exp), std::move(tls_ctx), db, port
    );
}

epoll_server::epoll_server(
    epoll_wakeup wakeup, epoll_listener listener, tls_context tls_ctx, db_service& db, const char* port
) : tls_ctx(std::move(tls_ctx)),
    registry(std::move(wakeup), this->tls_ctx),
    listener(std::move(listener)),
    db_pool(db), port(port){}

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
            logger::log_error("acceptor thread error", "epoll_server::run()", accept_exp);
            signal_stop(accept_exp.error());
        }
    });

    std::jthread event_thread([this, &signal_stop](std::stop_token st){
        event_loop loop(registry);
        auto run_exp = loop.run(
            st,
            [this](socket_info& si, uint32_t event){ return handle_recv(si, event); },
            [this](socket_info& si){ handle_send(si); },
            [this](socket_info& si){ return handle_execute(si); },
            [this](int fd, uint32_t event){ handle_client_error(fd, event); }
        );

        if(!run_exp){
            logger::log_error("event loop thread error", "epoll_server::run()", run_exp);
            signal_stop(run_exp.error());
        }
    });

    logger::log_info("server is on port:" + port);
    std::stop_callback on_external_stop(stop_token, [&](){ signal_stop(); });

    {
        std::unique_lock<std::mutex> lock(state_mtx);
        state_cv.wait(lock, [&](){ return stop_requested; });
    }

    logger::log_info("server is requested stop");
    event_thread.request_stop();
    accept_thread.request_stop();
    registry.request_wakeup();
    listener.request_wakeup();
    event_thread.join();
    accept_thread.join();

    if(error_opt) return std::unexpected(*error_opt);
    logger::log_info("server is stopped");
    return {};
}

std::expected <void, error_code> epoll_server::sync_tls_interest(socket_info& si){
    uint32_t next_interest = si.interest;
    if(si.send.has_pending() || si.tls.needs_write()) next_interest |= EPOLLOUT;
    else next_interest &= ~EPOLLOUT;

    if(next_interest == si.interest) return {};
    auto mod_ep_exp = epoll_utility::update_interest(registry.get_epfd(), si, next_interest);
    if(!mod_ep_exp) return std::unexpected(mod_ep_exp.error());
    return {};
}

void epoll_server::request_unregister(socket_info& si){
    registry.request_unregister(si);
}

std::expected <void, error_code> epoll_server::progress_tls_handshake(socket_info& si){
    if(si.tls.get() == nullptr) return {};
    bool was_handshake_done = si.tls.is_handshake_done();
    if(was_handshake_done) return {};

    auto hs_exp = si.tls.handshake();
    if(!hs_exp){
        logger::log_error("tls_handshake failed", "epoll_server::progress_tls_handshake()", si, hs_exp);
        request_unregister(si);
        return std::unexpected(hs_exp.error());
    }

    if(hs_exp->closed){
        handle_close(si);
        return std::unexpected(error_code::from_errno(ECONNRESET));
    }

    auto sync_exp = sync_tls_interest(si);
    if(!sync_exp){
        logger::log_error("sync_tls_interest failed", "epoll_server::progress_tls_handshake()", si, sync_exp);
        request_unregister(si);
        return std::unexpected(sync_exp.error());
    }

    if(!was_handshake_done && si.tls.is_handshake_done()){
        logger::log_info("tls handshake done", si);
    }

    return {};
}

void epoll_server::handle_send(socket_info& si){
    auto hs_exp = progress_tls_handshake(si);
    if(!hs_exp) return;
    if(si.tls.get() != nullptr && !si.tls.is_handshake_done()) return;

    auto fs_exp = flush_send(si);
    if(!fs_exp){
        logger::log_error("flush_send failed", "epoll_server::handle_send()", si, fs_exp);
        request_unregister(si);
        return; 
    }

    logger::log_info("send " + std::to_string(*fs_exp) + " byte" + (*fs_exp == 1 ? "" : "s"), si);

    auto sync_exp = sync_tls_interest(si);
    if(!sync_exp){
        logger::log_error("sync_tls_interest failed", "epoll_server::handle_send()", si, sync_exp);
        request_unregister(si);
    }
}

bool epoll_server::handle_recv(socket_info& si, uint32_t event){
    auto hs_exp = progress_tls_handshake(si);
    if(!hs_exp) return false;
    if(si.tls.get() != nullptr && !si.tls.is_handshake_done()) return true;

    auto dr_exp = drain_recv(si);
    if(!dr_exp){
        logger::log_error("drain_recv failed", "epoll_server::handle_recv()", si, dr_exp);
        request_unregister(si);
        return false;
    }

    auto recv_info = *dr_exp;
    logger::log_info("recv " + std::to_string(recv_info.byte) + " byte" + (recv_info.byte == 1 ? "" : "s"), si);

    auto sync_exp = sync_tls_interest(si);
    if(!sync_exp){
        logger::log_error("sync_tls_interest failed", "epoll_server::handle_recv()", si, sync_exp);
        request_unregister(si);
        return false;
    }

    if(recv_info.closed || event & EPOLLRDHUP){ // peer closed
        handle_close(si);
        return false;
    }

    return true;
}

void epoll_server::handle_close(socket_info& si){
    if(si.tls.is_handshake_done() && !si.tls.is_closed()){
        auto shutdown_exp = si.tls.shutdown();
        if(!shutdown_exp){
            logger::log_warn("tls_shutdown failed", "epoll_server::handle_close()", si, shutdown_exp);
        }
    }

    logger::log_info("is disconnected", si);
    request_unregister(si);
}

void epoll_server::handle_client_error(int fd, uint32_t event){
    auto it = registry.find(fd);
    if(it == registry.end()){
        return;
    }

    if(it->second.tls.is_handshake_done()){
        auto shutdown_exp = it->second.tls.shutdown();
        if(!shutdown_exp) logger::log_warn("shutdown_failed", "epoll_server::handle_client_error()", it->second, shutdown_exp);
    }

    int ec = ECONNRESET;
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1){
        ec = errno;
    }
    else if(so_error != 0){
        ec = so_error;
    }

    logger::log_error("client_error", "epoll_server::handle_client_error()", it->second, error_code::from_errno(ec));
    request_unregister(it->second);
}

bool epoll_server::handle_execute(socket_info& si){
    auto line = line_parser::parse_line(si.recv);
    if(!line) return false;

    auto dec_exp = command_codec::decode(*line);
    if(!dec_exp){
        logger::log_warn("decode failed", "epoll_server::handle_execute()", si, dec_exp);
        return true;
    }

    auto cmd = std::move(*dec_exp);
    if(db_executor::is_db_command(cmd)){
        return db_pool.enqueue(std::move(cmd), registry, si);
    }

    if(thread_pool::is_pool_command(cmd)){
        return pool.enqueue(std::move(cmd), registry, si);
    }

    std::visit([this, &si](auto&& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, command_codec::cmd_say>){
            registry.request_broadcast(si, command_codec::cmd_response{c.text});
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){
            registry.request_change_nickname(si, c.nick);
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_response>){
            registry.request_send(si, command_codec::cmd_response{c.text});
        }
    }, std::move(cmd));

    return true;
}
