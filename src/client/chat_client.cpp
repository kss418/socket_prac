#include "client/chat_client.hpp"
#include "net/addr.hpp"
#include "protocol/line_parser.hpp"
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>

std::expected <void, error_code> chat_client::connect(const char* ip, const char* port){
    auto addr_exp = get_addr_client(ip, port);
    if(!addr_exp){
        handle_error("get_addr_client failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        handle_error("make_server_fd failed", server_fd_exp);
        return std::unexpected(server_fd_exp.error());
    }

    server_fd = std::move(*server_fd_exp);
    si = {};
    return {};
}

std::expected <void, error_code> chat_client::run(){
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

    std::jthread execute_thread([this, &signal_stop](std::stop_token st){
        execute_loop(st);
        signal_stop();
    });

    std::jthread io_thread([this, &signal_stop](std::stop_token st){
        auto io_exp = io_loop(st);
        if(!io_exp){
            handle_error("io_loop error", io_exp);
            signal_stop(io_exp.error());
        }
        else signal_stop();
    });

    {
        std::unique_lock<std::mutex> lock(state_mtx);
        state_cv.wait(lock, [&](){ return stop_requested; });
    }

    execute_thread.request_stop();
    execute_cv.notify_one();

    io_thread.request_stop();
    ::shutdown(server_fd.get(), SHUT_RDWR);

    execute_thread.join();
    io_thread.join();

    if(error_opt) return std::unexpected(*error_opt);
    return {};
}

void chat_client::request_execute(const command_codec::command& cmd){
    {
        std::lock_guard<std::mutex> lock(execute_mtx);
        execute_q.emplace(cmd);
    }

    execute_cv.notify_one();
}

void chat_client::execute_loop(std::stop_token stop_token){
    while(!stop_token.stop_requested()){
        std::queue <command_codec::command> pending_q;
        {
            std::unique_lock<std::mutex> lock(execute_mtx);
            execute_cv.wait(lock, [&](){ return !execute_q.empty() || stop_token.stop_requested(); });
            std::swap(pending_q, execute_q);
        }
        
        while(!pending_q.empty()){
            auto cmd = std::move(pending_q.front());
            pending_q.pop();
            execute(cmd);
        }
    }
}

std::expected <void, error_code> chat_client::io_loop(std::stop_token stop_token){
    std::array <pollfd, 2> fds{{
        { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
        { .fd = server_fd.get(), .events = POLLIN | POLLHUP | POLLERR, .revents = 0 }
    }};

    while(!stop_token.stop_requested()){
        int size = ::poll(fds.data(), fds.size(), -1);
        if(size < 0){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        if(fds[0].revents & (POLLIN | POLLHUP)){
            auto send_exp = send();
            if(!send_exp) return std::unexpected(send_exp.error());
            if(*send_exp) break;
        }

        if(fds[1].revents & POLLERR){
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if(::getsockopt(server_fd.get(), SOL_SOCKET, SO_ERROR, &so_error, &len) == -1){
                return std::unexpected(error_code::from_errno(errno));
            }

            if(so_error == 0) so_error = EIO;
            return std::unexpected(error_code::from_errno(so_error));
        }
        
        if(fds[1].revents & (POLLIN | POLLHUP)){
            auto recv_exp = recv();
            if(!recv_exp) return std::unexpected(recv_exp.error());

            while(auto line = line_parser::parse_line(si.recv.raw())){
                auto dec_exp = command_codec::decode(*line);
                if(!dec_exp){
                    handle_error("execute_loop/decode failed", dec_exp);
                    continue;
                }

                request_execute(*dec_exp);
            }
            
            if(*recv_exp) break;
        }

        fds[0].revents = 0;
        fds[1].revents = 0;
    }

    return {};
}

void chat_client::execute(const command_codec::command& cmd){
    std::visit([&](const auto& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, command_codec::cmd_say>){

        }

        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){

        }

        if constexpr (std::is_same_v<T, command_codec::cmd_response>){
            std::cout << c.text << "\n"; 
        }
    }, cmd);
}

std::expected <bool, error_code> chat_client::send(){
    char tmp[1024];
    ssize_t recv_byte = ::read(STDIN_FILENO, tmp, sizeof(tmp));
    if(recv_byte == 0) return true;

    if(recv_byte < 0){
        int ec = errno;
        if(errno == EINTR) return false;
        return std::unexpected(error_code::from_errno(ec));
    }

    buf.append(tmp, static_cast<size_t>(recv_byte));
    while(auto line = line_parser::parse_line(buf)){
        if(line->empty()) continue;
        si.send.append(command_codec::cmd_say{*line});
        auto fs_exp = flush_send(server_fd.get(), si);
        if(!fs_exp) return std::unexpected(fs_exp.error());
    }

    return false;
}

std::expected <bool, error_code> chat_client::recv(){
    auto dr_exp = drain_recv(server_fd.get(), si);
    if(!dr_exp) return std::unexpected(dr_exp.error());
    return dr_exp->closed;
}
