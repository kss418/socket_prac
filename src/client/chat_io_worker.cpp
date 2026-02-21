#include "client/chat_io_worker.hpp"
#include "protocol/line_parser.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <poll.h>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

chat_io_worker::chat_io_worker(
    socket_info& si, unique_fd& server_fd, chat_executor& executor, std::atomic_bool& logged_in
) : si(si), server_fd(server_fd), executor(executor), logged_in(logged_in){}

chat_io_worker::parsed_command chat_io_worker::parse(const std::string& line){
    parsed_command parsed{};

    std::size_t idx = 0;
    const std::size_t n = line.size();
    while(idx < n && line[idx] == ' ') ++idx;

    while(idx < n){
        const std::size_t start = idx;
        while(idx < n && line[idx] != ' ') ++idx;

        std::string token = line.substr(start, idx - start);
        if(parsed.cmd.empty()) parsed.cmd = std::move(token);
        else parsed.args.push_back(std::move(token));

        while(idx < n && line[idx] == ' ') ++idx;
    }

    return parsed;
}

short chat_io_worker::socket_events() const{
    short events = POLLERR | POLLHUP;
    if(!si.tls.is_handshake_done()){
        if(si.tls.needs_write()) events |= POLLOUT;
        if(si.tls.needs_read() || !si.tls.needs_write()) events |= POLLIN;
        return events;
    }

    events |= POLLIN;
    if(si.send.has_pending() || si.tls.needs_write()) events |= POLLOUT;
    return events;
}

std::expected<void, error_code> chat_io_worker::progress_tls_handshake(){
    if(si.tls.is_handshake_done()){
        if(peer_verified) return {};
        auto verify_exp = si.tls.verify_peer();
        if(!verify_exp) return std::unexpected(verify_exp.error());
        peer_verified = true;
        return {};
    }

    auto hs_exp = si.tls.handshake();
    if(!hs_exp) return std::unexpected(hs_exp.error());
    if(hs_exp->closed) return std::unexpected(error_code::from_errno(ECONNRESET));

    if(!si.tls.is_handshake_done()) return {};
    auto verify_exp = si.tls.verify_peer();
    if(!verify_exp) return std::unexpected(verify_exp.error());
    peer_verified = true;
    return {};
}

std::expected<void, error_code> chat_io_worker::flush_pending_send(){
    if(!si.send.has_pending()) return {};
    auto fs_exp = flush_send(si);
    if(!fs_exp) return std::unexpected(fs_exp.error());
    return {};
}

std::expected<void, error_code> chat_io_worker::run(std::stop_token stop_token){
    std::stop_callback on_stop(stop_token, [this](){
        ::shutdown(server_fd.get(), SHUT_RDWR);
    });

    std::array<pollfd, 2> fds{{
        { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
        { .fd = server_fd.get(), .events = socket_events(), .revents = 0 }
    }};

    auto hs_init_exp = progress_tls_handshake();
    if(!hs_init_exp) return std::unexpected(hs_init_exp.error());

    while(!stop_token.stop_requested()){
        fds[1].events = socket_events();
        int size = ::poll(fds.data(), fds.size(), -1);
        if(size < 0){
            int ec = errno;
            if(ec == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        if(fds[0].revents & (POLLIN | POLLHUP)){
            auto send_exp = send_stdin();
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

        if(fds[1].revents & (POLLIN | POLLOUT | POLLHUP)){
            auto hs_exp = progress_tls_handshake();
            if(!hs_exp) return std::unexpected(hs_exp.error());

            if((fds[1].revents & POLLHUP) && !si.tls.is_handshake_done()){
                return std::unexpected(error_code::from_errno(ECONNRESET));
            }
        }

        if(si.tls.is_handshake_done() && (fds[1].revents & POLLOUT)){
            auto flush_exp = flush_pending_send();
            if(!flush_exp) return std::unexpected(flush_exp.error());
        }

        if(si.tls.is_handshake_done() && (fds[1].revents & (POLLIN | POLLHUP))){
            auto recv_exp = recv_socket();
            if(!recv_exp) return std::unexpected(recv_exp.error());

            while(auto line = line_parser::parse_line(si.recv)){
                auto dec_exp = command_codec::decode(*line);
                if(!dec_exp){
                    handle_error("chat_io_worker/decode failed", dec_exp);
                    continue;
                }

                executor.request_execute(std::move(*dec_exp));
            }

            if(*recv_exp) break;
        }

        fds[0].revents = 0;
        fds[1].revents = 0;
    }

    return {};
}

std::expected<bool, error_code> chat_io_worker::recv_socket(){
    auto dr_exp = drain_recv(si);
    if(!dr_exp) return std::unexpected(dr_exp.error());
    return dr_exp->closed;
}

std::expected<bool, error_code> chat_io_worker::send_stdin(){
    char tmp[1024];
    ssize_t recv_byte = ::read(STDIN_FILENO, tmp, sizeof(tmp));
    if(recv_byte == 0) return true;

    if(recv_byte < 0){
        int ec = errno;
        if(ec == EINTR) return false;
        return std::unexpected(error_code::from_errno(ec));
    }

    stdin_buf.append(tmp, static_cast<std::size_t>(recv_byte));
    while(auto line = line_parser::parse_line(stdin_buf)){
        if(line->empty()) continue;

        const bool had_pending = si.send.has_pending();
        execute(*line);
        if(!had_pending && !si.send.has_pending()) continue;

        if(!si.tls.is_handshake_done()) continue;

        auto fs_exp = flush_send(si);
        if(!fs_exp) return std::unexpected(fs_exp.error());
    }

    return false;
}

void chat_io_worker::execute(const std::string& line){
    if(line.empty()) return;
    parsed_command parsed = parse(line);
    if(parsed.cmd.empty()) return;

    if(!logged_in.load() && parsed.cmd != "/login" && parsed.cmd != "/register"){
        std::cout << "login first: /login <id> <pw> or /register <id> <pw>" << "\n";
        return;
    }

    if(parsed.cmd[0] != '/'){
        say(line);
        return;
    }
    
    if(parsed.cmd == "/nick"){
        if(parsed.args.size() != 1){
            std::cout << "/nick <nickname>" << "\n";
            return;
        }
        change_nickname(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/login"){
        if(parsed.args.size() != 2){
            std::cout << "/login <id> <pw>" << "\n";
            return;
        }
        login(parsed.args[0], parsed.args[1]);
        return;
    }

    if(parsed.cmd == "/register"){
        if(parsed.args.size() != 2){
            std::cout << "/register <id> <pw>" << "\n";
            return;
        }
        signup(parsed.args[0], parsed.args[1]);
        return;
    }

    else{
        std::cout << "unknown command" << "\n";
    }
}

void chat_io_worker::say(const std::string& line){
    si.send.append(command_codec::cmd_say{line});
}

void chat_io_worker::change_nickname(const std::string& nick){
    si.send.append(command_codec::cmd_nick{nick});
}

void chat_io_worker::login(const std::string& id, const std::string& pw){
    si.send.append(command_codec::cmd_login{id, pw});
}

void chat_io_worker::signup(const std::string& id, const std::string& pw){
    si.send.append(command_codec::cmd_register{id, pw});
}
