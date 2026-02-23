#include "client/chat_io_worker.hpp"
#include "client/console_output.hpp"
#include "core/logger.hpp"
#include "protocol/line_parser.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
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
    bool stdin_eof = false;

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

        if(!stdin_eof && (fds[0].revents & (POLLIN | POLLHUP))){
            auto send_exp = send_stdin();
            if(!send_exp) return std::unexpected(send_exp.error());
            if(*send_exp){
                stdin_eof = true;
                fds[0].fd = -1;
            }
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
                    logger::log_warn("command_codec/decode failed", "chat_io_worker::run()", si, dec_exp);
                    continue;
                }

                executor.request_execute(std::move(*dec_exp));
            }

            if(*recv_exp) break;
        }

        if(stdin_eof && si.tls.is_handshake_done() && !si.send.has_pending()){
            break;
        }

        fds[0].revents = 0;
        fds[1].revents = 0;
    }

    if(si.tls.is_handshake_done() && !si.tls.is_closed()){
        auto shutdown_exp = si.tls.shutdown();
        if(!shutdown_exp) logger::log_warn("tls shutdown failed", "chat_io_worker::run()", si, shutdown_exp);
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

    if(!logged_in.load() && parsed.cmd != "/login" && parsed.cmd != "/register" && parsed.cmd != "/help"){
        client_console::print_line("login first: /login <id> <pw> or /register <id> <pw>");
        return;
    }

    if(parsed.cmd[0] != '/'){
        if(!selected_room_id){
            client_console::print_line("select room first: /select_room <room_id>");
            return;
        }
        say(line);
        return;
    }
    
    if(parsed.cmd == "/nick"){
        if(parsed.args.size() != 1){
            client_console::print_line("/nick <nickname>");
            return;
        }
        change_nickname(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/login"){
        if(parsed.args.size() != 2){
            client_console::print_line("/login <id> <pw>");
            return;
        }
        login(parsed.args[0], parsed.args[1]);
        return;
    }

    if(parsed.cmd == "/register"){
        if(parsed.args.size() != 2){
            client_console::print_line("/register <id> <pw>");
            return;
        }
        signup(parsed.args[0], parsed.args[1]);
        return;
    }

    if(parsed.cmd == "/friend_request"){
        if(parsed.args.size() != 1){
            client_console::print_line("/friend_request <user_id>");
            return;
        }
        request_friend(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/friend_accept"){
        if(parsed.args.size() != 1){
            client_console::print_line("/friend_accept <user_id>");
            return;
        }
        accept_friend_request(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/friend_reject"){
        if(parsed.args.size() != 1){
            client_console::print_line("/friend_reject <user_id>");
            return;
        }
        reject_friend_request(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/friend_remove"){
        if(parsed.args.size() != 1){
            client_console::print_line("/friend_remove <user_id>");
            return;
        }
        remove_friend(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/list_friend"){
        if(!parsed.args.empty()){
            client_console::print_line("/list_friend");
            return;
        }
        list_friend();
        return;
    }

    if(parsed.cmd == "/list_friend_request"){
        if(!parsed.args.empty()){
            client_console::print_line("/list_friend_request");
            return;
        }
        list_friend_request();
        return;
    }

    if(parsed.cmd == "/create_room"){
        if(parsed.args.size() != 1){
            client_console::print_line("/create_room <room_name>");
            return;
        }
        create_room(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/delete_room"){
        if(parsed.args.size() != 1){
            client_console::print_line("/delete_room <room_id>");
            return;
        }
        delete_room(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/invite_room"){
        if(parsed.args.size() != 2){
            client_console::print_line("/invite_room <room_id> <friend_user_id>");
            return;
        }
        invite_room(parsed.args[0], parsed.args[1]);
        return;
    }

    if(parsed.cmd == "/leave_room"){
        if(parsed.args.size() != 1){
            client_console::print_line("/leave_room <room_id>");
            return;
        }
        leave_room(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/select_room"){
        if(parsed.args.size() != 1){
            client_console::print_line("/select_room <room_id>");
            return;
        }
        select_room(parsed.args[0]);
        return;
    }

    if(parsed.cmd == "/list_room"){
        if(!parsed.args.empty()){
            client_console::print_line("/list_room");
            return;
        }
        list_room();
        return;
    }

    if(parsed.cmd == "/help"){
        if(parsed.args.size() != 0){
            client_console::print_line("/help");
            return;
        }
        help();
        return;
    }

    else{
        client_console::print_line("unknown command");
    }
}

void chat_io_worker::say(const std::string& line){
    if(!selected_room_id){
        client_console::print_line("select room first: /select_room <room_id>");
        return;
    }

    si.send.append(command_codec::cmd_say{std::to_string(*selected_room_id), line});
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

void chat_io_worker::request_friend(const std::string& to_user_id){
    si.send.append(command_codec::cmd_friend_request{to_user_id});
}

void chat_io_worker::accept_friend_request(const std::string& from_user_id){
    si.send.append(command_codec::cmd_friend_accept{from_user_id});
}

void chat_io_worker::reject_friend_request(const std::string& from_user_id){
    si.send.append(command_codec::cmd_friend_reject{from_user_id});
}

void chat_io_worker::remove_friend(const std::string& friend_user_id){
    si.send.append(command_codec::cmd_friend_remove{friend_user_id});
}

void chat_io_worker::list_friend(){
    si.send.append(command_codec::cmd_list_friend{});
}

void chat_io_worker::list_friend_request(){
    si.send.append(command_codec::cmd_list_friend_request{});
}

void chat_io_worker::create_room(const std::string& room_name){
    si.send.append(command_codec::cmd_create_room{room_name});
}

void chat_io_worker::delete_room(const std::string& room_id){
    si.send.append(command_codec::cmd_delete_room{room_id});
}

void chat_io_worker::invite_room(const std::string& room_id, const std::string& friend_user_id){
    si.send.append(command_codec::cmd_invite_room{room_id, friend_user_id});
}

void chat_io_worker::leave_room(const std::string& room_id){
    si.send.append(command_codec::cmd_leave_room{room_id});
}

void chat_io_worker::select_room(const std::string& room_id){
    std::int64_t parsed_room_id = 0;
    try{
        std::size_t pos = 0;
        parsed_room_id = std::stoll(room_id, &pos);
        if(pos != room_id.size() || parsed_room_id <= 0){
            client_console::print_line("invalid room id: /select_room <room_id>");
            return;
        }
    } catch(...){
        client_console::print_line("invalid room id: /select_room <room_id>");
        return;
    }

    selected_room_id = parsed_room_id;
    client_console::print_line("selected room: " + std::to_string(parsed_room_id));
}

void chat_io_worker::list_room(){
    si.send.append(command_codec::cmd_list_room{});
}

void chat_io_worker::help(){
    std::lock_guard<std::mutex> lock(client_console::output_mutex());
    std::cout << "commands:\n"
              << "  /register <id> <pw>\n"
              << "  /login <id> <pw>\n"
              << "  /friend_request <user_id>\n"
              << "  /friend_accept <user_id>\n"
              << "  /friend_reject <user_id>\n"
              << "  /friend_remove <user_id>\n"
              << "  /list_friend\n"
              << "  /list_friend_request\n"
              << "  /create_room <room_name>\n"
              << "  /delete_room <room_id>\n"
              << "  /invite_room <room_id> <friend_user_id>\n"
              << "  /leave_room <room_id>\n"
              << "  /select_room <room_id>\n"
              << "  /list_room\n"
              << "  /nick <nickname>\n"
              << "  /help\n"
              << "  <text> (send chat message, room must be selected)\n";
}
