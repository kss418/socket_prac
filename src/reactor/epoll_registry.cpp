#include "reactor/epoll_registry.hpp"
#include "core/logger.hpp"
#include "net/tls_context.hpp"
#include "reactor/epoll_utility.hpp"
#include <cerrno>
#include <sys/epoll.h>

epoll_registry::epoll_registry(epoll_wakeup wakeup, tls_context& tls_ctx) :
    epoll_wakeup(std::move(wakeup)), tls_ctx(tls_ctx){}

std::expected <int, error_code> epoll_registry::register_fd(unique_fd client_fd, uint32_t interest){
    int fd = client_fd.get();
    if(fd == -1){
        logger::log_error("fd error", "epoll_registry::register_fd()", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    if(infos.contains(fd)){
        logger::log_warn("fd existed error", "epoll_registry::register_fd()", error_code::from_errno(EEXIST));
        return std::unexpected(error_code::from_errno(EEXIST));
    }

    auto nonblocking_exp = epoll_utility::set_nonblocking(fd);
    if(!nonblocking_exp){
        logger::log_error("set_nonblocking failed", "epoll_registry::register_fd()", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto ep_exp = make_peer_endpoint(fd);
    if(!ep_exp){
        logger::log_error("make_peer_endpoint failed", "epoll_registry::register_fd()", ep_exp);
        return std::unexpected(ep_exp.error());
    }

    auto init_str_exp = ep_exp->init_string();
    if(!init_str_exp){
        logger::log_error("init_string failed", "epoll_registry::register_fd()", init_str_exp);
        return std::unexpected(init_str_exp.error());
    }

    auto add_ep_exp = epoll_utility::add_fd(epfd.get(), fd, interest);
    if(!add_ep_exp){
        logger::log_error("add_fd failed", "epoll_registry::register_fd()", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }

    auto tls_exp = tls_session::create_server(tls_ctx, fd);
    if(!tls_exp){
        logger::log_error("tls_session create failed", "epoll_registry::register_fd()", tls_exp);
        return std::unexpected(tls_exp.error());
    }
    
    auto [it, inserted] = infos.emplace(
        fd,
        socket_info{
            .tls = std::move(*tls_exp),
            .interest = interest,
            .ufd = std::move(client_fd),
            .ep = std::move(*ep_exp)
        }
    );
    (void)inserted;

    connected_client_count = infos.size();
    logger::log_info("is connected", it->second);
    logger::log_info("active clients: " + std::to_string(connected_client_count));
    return fd;
}

std::expected <void, error_code> epoll_registry::unregister_fd(int fd){
    if(fd == -1){
        logger::log_error("fd error", "epoll_registry::unregister_fd()", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto it = infos.find(fd);
    if(it == infos.end()) return {};

    auto del_ep_exp = epoll_utility::del_fd(epfd.get(), fd);
    if(!del_ep_exp){
        const error_code& ec = del_ep_exp.error();
        bool ignorable = ec.domain == error_domain::errno_domain
            && (ec.code == ENOENT || ec.code == EBADF);
        if(!ignorable) logger::log_error("del_fd failed", "epoll_registry::unregister_fd()", it->second, del_ep_exp);
    }

    remove_fd_from_room_index(it->second);
    remove_fd_from_user_index(it->second);
    infos.erase(it);
    connected_client_count = infos.size();
    logger::log_info("active clients: " + std::to_string(connected_client_count));
    return {};
}

std::expected <void, error_code> epoll_registry::sync_interest(socket_info& si){
    int fd = si.ufd.get();
    auto mod_exp = epoll_utility::update_interest(epfd.get(), si, si.interest);
    if(!mod_exp){
        unregister_fd(fd);
        return std::unexpected(mod_exp.error());
    }
    return {};
}

std::expected <void, error_code> epoll_registry::append_send(
    socket_info& si,
    const command_codec::command& cmd
){
    if(!si.send.append(cmd)) return {};

    si.interest |= EPOLLOUT;
    auto sync_exp = sync_interest(si);
    if(!sync_exp){
        logger::log_warn("sync_interest failed", "epoll_registry::append_send()", si, sync_exp);
        return std::unexpected(sync_exp.error());
    }

    return {};
}

void epoll_registry::request_register(unique_fd fd, uint32_t interest){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(register_command{std::move(fd), interest});
    }
    request_wakeup();
}

void epoll_registry::request_unregister(int fd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(unregister_command{fd});
    }
    request_wakeup();
}

void epoll_registry::request_unregister(socket_info& si){
    request_unregister(si.ufd.get());
}

void epoll_registry::request_send(int fd, command_codec::command cmd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(send_one_command{fd, std::move(cmd)});
    }
    request_wakeup();
}

void epoll_registry::request_send(socket_info& si, command_codec::command cmd){
    request_send(si.ufd.get(), std::move(cmd));
}

void epoll_registry::request_broadcast(int send_fd, command_codec::command cmd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(broadcast_command{send_fd, std::move(cmd)});
    }
    request_wakeup();
}

void epoll_registry::request_broadcast(socket_info& si, command_codec::command cmd){
    request_broadcast(si.ufd.get(), std::move(cmd));
}

void epoll_registry::request_change_nickname(int send_fd, std::string nick){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(change_nickname_command{send_fd, std::move(nick)});
    }
    request_wakeup();
}

void epoll_registry::request_change_nickname(socket_info& si, std::string nick){
    request_change_nickname(si.ufd.get(), std::move(nick));
}

void epoll_registry::request_set_user_id(int fd, std::string user_id){
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(set_user_id_command{fd, std::move(user_id)});
    }
    request_wakeup();
}

void epoll_registry::request_set_user_id(socket_info& si, std::string user_id){
    request_set_user_id(si.ufd.get(), std::move(user_id));
}

void epoll_registry::request_set_joined_rooms(int fd, std::vector<std::int64_t> room_ids){
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(set_joined_rooms_command{fd, std::move(room_ids)});
    }
    request_wakeup();
}

void epoll_registry::request_set_joined_rooms(socket_info& si, std::vector<std::int64_t> room_ids){
    request_set_joined_rooms(si.ufd.get(), std::move(room_ids));
}

void epoll_registry::request_set_joined_rooms_for_user(
    std::string user_id,
    std::vector<std::int64_t> room_ids
){
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(set_joined_rooms_for_user_command{std::move(user_id), std::move(room_ids)});
    }
    request_wakeup();
}

void epoll_registry::request_send_friend_list(int fd, std::vector<std::string> friend_ids){
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(send_friend_list_command{fd, std::move(friend_ids)});
    }
    request_wakeup();
}

void epoll_registry::request_room_broadcast(
    int sender_fd,
    std::int64_t room_id,
    command_codec::command cmd
){
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(room_broadcast_command{sender_fd, room_id, std::move(cmd)});
    }
    request_wakeup();
}

void epoll_registry::request_room_broadcast(
    socket_info& si,
    std::int64_t room_id,
    command_codec::command cmd
){
    request_room_broadcast(si.ufd.get(), room_id, std::move(cmd));
}

void epoll_registry::handle_command(register_command&& cmd){
    auto reg_exp = register_fd(std::move(cmd.fd), cmd.interest);
}

void epoll_registry::handle_command(const unregister_command& cmd){
    auto unreg_exp = unregister_fd(cmd.fd);
}

void epoll_registry::handle_command(send_one_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    auto& si = it->second;
    auto append_exp = append_send(si, cmd.cmd);
    if(!append_exp) return;
}

void epoll_registry::handle_command(broadcast_command&& cmd){
    if(const auto* response = std::get_if<command_codec::cmd_response>(&cmd.cmd)){
        std::string nickname = "guest";
        auto sender_it = infos.find(cmd.fd);
        if(sender_it != infos.end() && !sender_it->second.nickname.empty()){
            nickname = sender_it->second.nickname;
        }

        command_codec::command named_msg =
            command_codec::cmd_response{nickname + ": " + response->text};
        for(auto& [fd, si] : infos){
            auto append_exp = append_send(si, named_msg);
            if(!append_exp) continue;
        }
        return;
    }

    for(auto& [fd, si] : infos){
        auto append_exp = append_send(si, cmd.cmd);
        if(!append_exp) continue;
    }
}

void epoll_registry::handle_command(change_nickname_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    it->second.nickname = std::move(cmd.nick);
}

void epoll_registry::handle_command(set_user_id_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    remove_fd_from_user_index(it->second);
    set_fd_joined_rooms(it->second, {});
    it->second.user_id = std::move(cmd.user_id);
    if(!it->second.user_id.empty()){
        user_online_fds[it->second.user_id].insert(it->second.ufd.get());
    }
}

void epoll_registry::handle_command(set_joined_rooms_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    set_fd_joined_rooms(it->second, std::move(cmd.room_ids));
    logger::log_info(
        "joined rooms indexed: " + std::to_string(it->second.joined_room_ids.size()),
        it->second
    );
}

void epoll_registry::handle_command(set_joined_rooms_for_user_command&& cmd){
    auto user_it = user_online_fds.find(cmd.user_id);
    if(user_it == user_online_fds.end()) return;

    const std::unordered_set<int> fds = user_it->second;
    for(int fd : fds){
        auto it = infos.find(fd);
        if(it == infos.end()) continue;

        std::vector<std::int64_t> rooms_copy = cmd.room_ids;
        set_fd_joined_rooms(it->second, std::move(rooms_copy));
        logger::log_info(
            "joined rooms indexed: " + std::to_string(it->second.joined_room_ids.size()),
            it->second
        );
    }
}

void epoll_registry::handle_command(send_friend_list_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    auto header_exp = append_send(
        it->second,
        command_codec::cmd_response{"friends: " + std::to_string(cmd.friend_ids.size())}
    );
    if(!header_exp) return;

    for(const auto& friend_id : cmd.friend_ids){
        const bool is_online = user_online_fds.contains(friend_id);
        auto send_exp = append_send(
            it->second,
            command_codec::cmd_response{
                "friend: " + friend_id + " (" + (is_online ? "online" : "offline") + ")"
            }
        );
        if(!send_exp) continue;
    }
}

void epoll_registry::handle_command(room_broadcast_command&& cmd){
    auto room_it = room_online_fds.find(cmd.room_id);
    if(room_it == room_online_fds.end()) return;

    command_codec::command payload = cmd.cmd;
    if(const auto* response = std::get_if<command_codec::cmd_response>(&cmd.cmd)){
        std::string nickname = "guest";
        auto sender_it = infos.find(cmd.sender_fd);
        if(sender_it != infos.end() && !sender_it->second.nickname.empty()){
            nickname = sender_it->second.nickname;
        }
        payload = command_codec::cmd_response{nickname + ": " + response->text};
    }

    const std::unordered_set<int> targets = room_it->second;
    for(int fd : targets){
        auto it = infos.find(fd);
        if(it == infos.end()) continue;

        auto append_exp = append_send(it->second, payload);
        if(!append_exp) continue;
    }
}

void epoll_registry::remove_fd_from_room_index(socket_info& si){
    int fd = si.ufd.get();
    for(std::int64_t room_id : si.joined_room_ids){
        auto room_it = room_online_fds.find(room_id);
        if(room_it == room_online_fds.end()) continue;

        room_it->second.erase(fd);
        if(room_it->second.empty()){
            room_online_fds.erase(room_it);
        }
    }
    si.joined_room_ids.clear();
}

void epoll_registry::remove_fd_from_user_index(socket_info& si){
    if(si.user_id.empty()) return;

    auto user_it = user_online_fds.find(si.user_id);
    if(user_it == user_online_fds.end()) return;

    user_it->second.erase(si.ufd.get());
    if(user_it->second.empty()){
        user_online_fds.erase(user_it);
    }
}

void epoll_registry::set_fd_joined_rooms(socket_info& si, std::vector<std::int64_t>&& room_ids){
    remove_fd_from_room_index(si);

    int fd = si.ufd.get();
    for(std::int64_t room_id : room_ids){
        if(room_id <= 0) continue;
        si.joined_room_ids.insert(room_id);
        room_online_fds[room_id].insert(fd);
    }
}

void epoll_registry::work(){
    consume_wakeup();

    std::queue<command> pending_cmd;
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        std::swap(pending_cmd, cmd_q);
    }

    while(!pending_cmd.empty()){
        command cmd = std::move(pending_cmd.front());
        pending_cmd.pop();
        std::visit([this](auto&& c){ handle_command(std::move(c)); }, std::move(cmd));
    }
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
