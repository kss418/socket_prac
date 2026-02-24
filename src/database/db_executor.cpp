#include "database/db_executor.hpp"
#include "core/logger.hpp"
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

db_executor::~db_executor(){ stop(); }

db_executor::db_executor(db_service& db, std::size_t sz) : db(db){
    if(sz == 0) sz = 1;
    workers.reserve(sz);
    for(std::size_t i = 0; i < sz; ++i){
        workers.emplace_back([this](std::stop_token st){ worker_loop(st); });
    }
}

bool db_executor::is_db_command(const command_codec::command& cmd) noexcept{
    return std::holds_alternative<command_codec::cmd_login>(cmd)
        || std::holds_alternative<command_codec::cmd_register>(cmd)
        || std::holds_alternative<command_codec::cmd_say>(cmd)
        || std::holds_alternative<command_codec::cmd_nick>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_request>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_accept>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_reject>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_remove>(cmd)
        || std::holds_alternative<command_codec::cmd_list_friend>(cmd)
        || std::holds_alternative<command_codec::cmd_list_friend_request>(cmd)
        || std::holds_alternative<command_codec::cmd_create_room>(cmd)
        || std::holds_alternative<command_codec::cmd_delete_room>(cmd)
        || std::holds_alternative<command_codec::cmd_invite_room>(cmd)
        || std::holds_alternative<command_codec::cmd_leave_room>(cmd)
        || std::holds_alternative<command_codec::cmd_list_room>(cmd)
        || std::holds_alternative<command_codec::cmd_history>(cmd);
}

void db_executor::stop(){
    {
        std::lock_guard<std::mutex> lock(mtx);
        if(!run) return;
        run = false;
    }

    cv.notify_all();
    for(auto& w : workers){
        if(w.joinable()) w.join();
    }
    workers.clear();
}

bool db_executor::enqueue(command_codec::command cmd, epoll_registry& reg, int fd){
    if(!is_db_command(cmd)) return false;

    {
        std::lock_guard<std::mutex> lock(mtx);
        if(!run) return false;
        tasks.emplace(task{std::move(cmd), reg, fd, ""});
    }
    cv.notify_one();
    return true;
}

bool db_executor::enqueue(command_codec::command cmd, epoll_registry& reg, socket_info& si){
    if(!is_db_command(cmd)) return false;

    {
        std::lock_guard<std::mutex> lock(mtx);
        if(!run) return false;
        tasks.emplace(task{std::move(cmd), reg, si.ufd.get(), si.user_id});
    }
    cv.notify_one();
    return true;
}

void db_executor::worker_loop(std::stop_token st){
    while(true){
        std::optional<task> task_opt;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&](){
                return !run || st.stop_requested() || !tasks.empty();
            });

            if((!run || st.stop_requested()) && tasks.empty()) return;
            task_opt.emplace(std::move(tasks.front()));
            tasks.pop();
        }

        execute(*task_opt);
    }
}

void db_executor::execute(const task& t){
    auto& [cmd, reg, fd, user_id] = t;
    std::visit([this, &reg, fd, &user_id](const auto& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (
            std::is_same_v<T, command_codec::cmd_say>
            || std::is_same_v<T, command_codec::cmd_nick>
            || std::is_same_v<T, command_codec::cmd_friend_request>
            || std::is_same_v<T, command_codec::cmd_friend_accept>
            || std::is_same_v<T, command_codec::cmd_friend_reject>
            || std::is_same_v<T, command_codec::cmd_friend_remove>
            || std::is_same_v<T, command_codec::cmd_list_friend>
            || std::is_same_v<T, command_codec::cmd_list_friend_request>
            || std::is_same_v<T, command_codec::cmd_create_room>
            || std::is_same_v<T, command_codec::cmd_delete_room>
            || std::is_same_v<T, command_codec::cmd_invite_room>
            || std::is_same_v<T, command_codec::cmd_leave_room>
            || std::is_same_v<T, command_codec::cmd_list_room>
            || std::is_same_v<T, command_codec::cmd_history>
        ){
            execute_command(c, reg, fd, user_id);
        }
        else{
            execute_command(c, reg, fd);
        }
    }, cmd);
}

std::expected<std::vector<std::int64_t>, error_code> db_executor::load_joined_room_ids(std::string_view user_id){
    auto list_rooms_exp = db.list_rooms(user_id);
    if(!list_rooms_exp){
        return std::unexpected(list_rooms_exp.error());
    }

    std::vector<std::int64_t> joined_room_ids;
    joined_room_ids.reserve(list_rooms_exp->size());
    for(const auto& room : *list_rooms_exp){
        joined_room_ids.push_back(room.id);
    }
    return joined_room_ids;
}

void db_executor::execute_command(
    const command_codec::cmd_login& cmd, epoll_registry& reg, int fd
){
    auto login_exp = db.login(cmd.id, cmd.pw);
    if(!login_exp){
        logger::log_error("login failed", "db_executor::execute_command()", login_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"login failed"});
        reg.request_set_user_id(fd, "");
        reg.request_set_joined_rooms(fd, {});
        reg.request_change_nickname(fd, "guest");
        return;
    }

    if(*login_exp){
        auto joined_room_ids_exp = load_joined_room_ids(cmd.id);
        if(!joined_room_ids_exp){
            logger::log_error(
                "load joined rooms failed",
                "db_executor::execute_command()",
                joined_room_ids_exp.error()
            );
            reg.request_send(fd, command_codec::cmd_response{"login failed"});
            reg.request_set_user_id(fd, "");
            reg.request_set_joined_rooms(fd, {});
            reg.request_change_nickname(fd, "guest");
            return;
        }

        reg.request_set_user_id(fd, cmd.id);
        reg.request_set_joined_rooms(fd, std::move(*joined_room_ids_exp));
        reg.request_change_nickname(fd, **login_exp);
        reg.request_send(fd, command_codec::cmd_response{"login success"});
    }
    else{
        reg.request_set_user_id(fd, "");
        reg.request_set_joined_rooms(fd, {});
        reg.request_change_nickname(fd, "guest");
        reg.request_send(fd, command_codec::cmd_response{"login failed"});
    }
}

void db_executor::execute_command(
    const command_codec::cmd_register& cmd, epoll_registry& reg, int fd
){
    auto signup_exp = db.signup(cmd.id, cmd.pw);
    if(!signup_exp){
        logger::log_error("register failed", "db_executor::execute_command", signup_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"register failed"});
        return;
        
    }

    reg.request_send(
        fd, command_codec::cmd_response{*signup_exp ? "register success" : "id already exists"}
    );
}

void db_executor::execute_command(
    const command_codec::cmd_say& cmd, epoll_registry& reg, int fd, std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    std::int64_t room_id = 0;
    try{
        std::size_t pos = 0;
        room_id = std::stoll(cmd.room_id, &pos);
        if(pos != cmd.room_id.size() || room_id <= 0){
            reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
            return;
        }
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
        return;
    }

    auto msg_exp = db.create_room_message(room_id, user_id, cmd.text);
    if(!msg_exp){
        logger::log_error("create room message failed", "db_executor::execute_command()", msg_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"send failed"});
        return;
    }

    if(!*msg_exp){
        reg.request_send(fd, command_codec::cmd_response{"room not found or no permission"});
        return;
    }

    reg.request_room_broadcast(fd, room_id, command_codec::cmd_response{cmd.text});
}

void db_executor::execute_command(
    const command_codec::cmd_nick& cmd, epoll_registry& reg, int fd, std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto nick_exp = db.change_nickname(user_id, cmd.nick);
    if(!nick_exp){
        logger::log_error("change nickname failed", "db_executor::execute_command()", nick_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"nick change failed"});
        return;
    }

    if(!*nick_exp){
        reg.request_send(fd, command_codec::cmd_response{"nick change failed"});
        return;
    }

    reg.request_change_nickname(fd, cmd.nick);
    reg.request_send(fd, command_codec::cmd_response{"nick change success"});
}

void db_executor::execute_command(
    const command_codec::cmd_response&, epoll_registry&, int
){}

void db_executor::execute_command(
    const command_codec::cmd_friend_request& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    if(user_id == cmd.to_user_id){
        reg.request_send(fd, command_codec::cmd_response{"cannot request yourself"});
        return;
    }

    auto request_exp = db.request_friend(user_id, cmd.to_user_id);
    if(!request_exp){
        logger::log_error("friend request failed", "db_executor::execute_command()", request_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend request failed"});
        return;
    }

    if(!*request_exp){
        reg.request_send(fd, command_codec::cmd_response{"friend request already exists or already friends"});
        return;
    }

    reg.request_send(fd, command_codec::cmd_response{"friend request sent"});
    logger::log_info(std::string(user_id) + " sent friend request to " + std::string(cmd.to_user_id));
}

void db_executor::execute_command(
    const command_codec::cmd_friend_accept& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto accept_exp = db.accept_friend_request(cmd.from_user_id, user_id);
    if(!accept_exp){
        logger::log_error("friend request accept failed", "db_executor::execute_command()", accept_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend request accept failed"});
        return;
    }

    if(!*accept_exp){
        reg.request_send(fd, command_codec::cmd_response{"no pending friend request"});
        return;
    }

    reg.request_send(fd, command_codec::cmd_response{"friend request accepted"});
    logger::log_info(std::string(user_id) + " accepet friend request to " + std::string(cmd.from_user_id));
}

void db_executor::execute_command(
    const command_codec::cmd_friend_reject& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto reject_exp = db.reject_friend_request(cmd.from_user_id, user_id);
    if(!reject_exp){
        logger::log_error("friend request reject failed", "db_executor::execute_command()", reject_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend request reject failed"});
        return;
    }

    if(!*reject_exp){
        reg.request_send(fd, command_codec::cmd_response{"no pending friend request"});
        return;
    }

    reg.request_send(fd, command_codec::cmd_response{"friend request rejected"});
    logger::log_info(std::string(user_id) + " reject friend request to " + std::string(cmd.from_user_id));
}

void db_executor::execute_command(
    const command_codec::cmd_friend_remove& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    if(user_id == cmd.friend_user_id){
        reg.request_send(fd, command_codec::cmd_response{"cannot remove yourself"});
        return;
    }

    auto remove_exp = db.remove_friend(user_id, cmd.friend_user_id);
    if(!remove_exp){
        logger::log_error("friend remove failed", "db_executor::execute_command()", remove_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend remove failed"});
        return;
    }

    if(!*remove_exp){
        reg.request_send(fd, command_codec::cmd_response{"friend not found"});
        return;
    }

    reg.request_send(fd, command_codec::cmd_response{"friend removed"});
    logger::log_info(std::string(user_id) + " removed friend " + std::string(cmd.friend_user_id));
}

void db_executor::execute_command(
    const command_codec::cmd_list_friend&,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto list_exp = db.list_friends(user_id);
    if(!list_exp){
        logger::log_error("friend list failed", "db_executor::execute_command()", list_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend list failed"});
        return;
    }

    if(list_exp->empty()){
        reg.request_send(fd, command_codec::cmd_response{"no friends"});
        return;
    }

    reg.request_send_friend_list(fd, *list_exp);
    logger::log_info(std::string(user_id) + " request list_friend");
}

void db_executor::execute_command(
    const command_codec::cmd_list_friend_request&,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto list_exp = db.list_friend_requests(user_id);
    if(!list_exp){
        logger::log_error("friend requests list failed", "db_executor::execute_command()", list_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"friend requests list failed"});
        return;
    }

    if(list_exp->empty()){
        reg.request_send(fd, command_codec::cmd_response{"no pending friend requests"});
        return;
    }

    reg.request_send(
        fd, command_codec::cmd_response{"pending friend requests: " + std::to_string(list_exp->size())}
    );

    for(const std::string& from_user_id : *list_exp){
        reg.request_send(fd, command_codec::cmd_response{"from: " + from_user_id});
    }

    logger::log_info(std::string(user_id) + " request list_friend_request");
}

void db_executor::execute_command(
    const command_codec::cmd_create_room& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    if(cmd.room_name.empty()){
        reg.request_send(fd, command_codec::cmd_response{"room name is empty"});
        return;
    }

    auto create_exp = db.create_room(user_id, cmd.room_name);
    if(!create_exp){
        logger::log_error("create room failed", "db_executor::execute_command()", create_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"create room failed"});
        return;
    }

    reg.request_send(
        fd,
        command_codec::cmd_response{
            "room created: " + std::to_string(*create_exp) + " (" + cmd.room_name + ")"
        }
    );
    auto joined_room_ids_exp = load_joined_room_ids(user_id);
    if(!joined_room_ids_exp){
        logger::log_warn(
            "refresh joined rooms index failed",
            "db_executor::execute_command()",
            joined_room_ids_exp
        );
    }
    else{
        reg.request_set_joined_rooms(fd, std::move(*joined_room_ids_exp));
    }
    logger::log_info(std::string(user_id) + " created room " + std::to_string(*create_exp));
}

void db_executor::execute_command(
    const command_codec::cmd_delete_room& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    std::int64_t room_id = 0;
    try{
        std::size_t pos = 0;
        room_id = std::stoll(cmd.room_id, &pos);
        if(pos != cmd.room_id.size() || room_id <= 0){
            reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
            return;
        }
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
        return;
    }

    auto delete_exp = db.delete_room(user_id, room_id);
    if(!delete_exp){
        logger::log_error("delete room failed", "db_executor::execute_command()", delete_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"delete room failed"});
        return;
    }

    if(!*delete_exp){
        reg.request_send(fd, command_codec::cmd_response{"room not found or no permission"});
        return;
    }

    auto joined_room_ids_exp = load_joined_room_ids(user_id);
    if(!joined_room_ids_exp){
        logger::log_warn(
            "refresh joined rooms index failed",
            "db_executor::execute_command()",
            joined_room_ids_exp
        );
    }
    else{
        reg.request_set_joined_rooms(fd, std::move(*joined_room_ids_exp));
    }
    reg.request_send(fd, command_codec::cmd_response{"room deleted: " + std::to_string(room_id)});
    logger::log_info(std::string(user_id) + " deleted room " + std::to_string(room_id));
}

void db_executor::execute_command(
    const command_codec::cmd_invite_room& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    if(user_id == cmd.friend_user_id){
        reg.request_send(fd, command_codec::cmd_response{"cannot invite yourself"});
        return;
    }

    std::int64_t room_id = 0;
    try{
        std::size_t pos = 0;
        room_id = std::stoll(cmd.room_id, &pos);
        if(pos != cmd.room_id.size() || room_id <= 0){
            reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
            return;
        }
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
        return;
    }

    auto invite_exp = db.invite_room(user_id, room_id, cmd.friend_user_id);
    if(!invite_exp){
        logger::log_error("invite room failed", "db_executor::execute_command()", invite_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"invite room failed"});
        return;
    }

    switch(*invite_exp){
        case db_service::invite_room_result::invited:
            {
                auto inviter_rooms_exp = load_joined_room_ids(user_id);
                if(!inviter_rooms_exp){
                    logger::log_warn(
                        "refresh inviter joined rooms index failed",
                        "db_executor::execute_command()",
                        inviter_rooms_exp
                    );
                }
                else{
                    reg.request_set_joined_rooms_for_user(std::string(user_id), std::move(*inviter_rooms_exp));
                }
            }
            {
                auto invitee_rooms_exp = load_joined_room_ids(cmd.friend_user_id);
                if(!invitee_rooms_exp){
                    logger::log_warn(
                        "refresh invitee joined rooms index failed",
                        "db_executor::execute_command()",
                        invitee_rooms_exp
                    );
                }
                else{
                    reg.request_set_joined_rooms_for_user(cmd.friend_user_id, std::move(*invitee_rooms_exp));
                }
            }
            reg.request_send(
                fd,
                command_codec::cmd_response{
                    "room invite sent: room=" + std::to_string(room_id) + " user=" + cmd.friend_user_id
                }
            );
            logger::log_info(
                std::string(user_id)
                + " invited " + cmd.friend_user_id
                + " to room " + std::to_string(room_id)
            );
            return;
        case db_service::invite_room_result::already_member:
            reg.request_send(fd, command_codec::cmd_response{"user already in room"});
            return;
        case db_service::invite_room_result::not_friend:
            reg.request_send(fd, command_codec::cmd_response{"can invite friends only"});
            return;
        case db_service::invite_room_result::room_not_found_or_no_permission:
            reg.request_send(fd, command_codec::cmd_response{"room not found or no permission"});
            return;
    }
}

void db_executor::execute_command(
    const command_codec::cmd_leave_room& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    std::int64_t room_id = 0;
    try{
        std::size_t pos = 0;
        room_id = std::stoll(cmd.room_id, &pos);
        if(pos != cmd.room_id.size() || room_id <= 0){
            reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
            return;
        }
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
        return;
    }

    auto leave_exp = db.leave_room(user_id, room_id);
    if(!leave_exp){
        logger::log_error("leave room failed", "db_executor::execute_command()", leave_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"leave room failed"});
        return;
    }

    switch(*leave_exp){
        case db_service::leave_room_result::left:
            {
                auto joined_room_ids_exp = load_joined_room_ids(user_id);
                if(!joined_room_ids_exp){
                    logger::log_warn(
                        "refresh joined rooms index failed",
                        "db_executor::execute_command()",
                        joined_room_ids_exp
                    );
                }
                else{
                    reg.request_set_joined_rooms_for_user(std::string(user_id), std::move(*joined_room_ids_exp));
                }
            }
            reg.request_send(fd, command_codec::cmd_response{"left room: " + std::to_string(room_id)});
            logger::log_info(std::string(user_id) + " left room " + std::to_string(room_id));
            return;
        case db_service::leave_room_result::not_member_or_room_not_found:
            reg.request_send(fd, command_codec::cmd_response{"room not found or not joined"});
            return;
        case db_service::leave_room_result::owner_cannot_leave:
            reg.request_send(fd, command_codec::cmd_response{"room owner cannot leave (delete room instead)"});
            return;
    }
}

void db_executor::execute_command(
    const command_codec::cmd_list_room&,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    auto list_exp = db.list_rooms(user_id);
    if(!list_exp){
        logger::log_error("list room failed", "db_executor::execute_command()", list_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"list room failed"});
        return;
    }

    if(list_exp->empty()){
        reg.request_send(fd, command_codec::cmd_response{"no rooms"});
        return;
    }

    reg.request_send(fd, command_codec::cmd_response{"rooms: " + std::to_string(list_exp->size())});
    for(const auto& room : *list_exp){
        reg.request_send(
            fd,
            command_codec::cmd_response{
                "room: id=" + std::to_string(room.id)
                + " name=" + room.name
                + " owner=" + room.owner_user_id
                + " members=" + std::to_string(room.member_count)
            }
        );
    }
    logger::log_info(std::string(user_id) + " request list_room");
}

void db_executor::execute_command(
    const command_codec::cmd_history& cmd,
    epoll_registry& reg,
    int fd,
    std::string_view user_id
){
    if(user_id.empty()){
        reg.request_send(fd, command_codec::cmd_response{"login first"});
        return;
    }

    std::int64_t room_id = 0;
    try{
        std::size_t pos = 0;
        room_id = std::stoll(cmd.room_id, &pos);
        if(pos != cmd.room_id.size() || room_id <= 0){
            reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
            return;
        }
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid room id"});
        return;
    }

    std::int32_t limit = 0;
    try{
        std::size_t pos = 0;
        const long long parsed = std::stoll(cmd.limit, &pos);
        if(pos != cmd.limit.size() || parsed <= 0 || parsed > 100){
            reg.request_send(fd, command_codec::cmd_response{"invalid limit (1-100)"});
            return;
        }
        limit = static_cast<std::int32_t>(parsed);
    } catch(...){
        reg.request_send(fd, command_codec::cmd_response{"invalid limit (1-100)"});
        return;
    }

    auto history_exp = db.list_room_messages(user_id, room_id, limit);
    if(!history_exp){
        logger::log_error("history query failed", "db_executor::execute_command()", history_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"history query failed"});
        return;
    }

    if(!*history_exp){
        reg.request_send(fd, command_codec::cmd_response{"room not found or no permission"});
        return;
    }

    const auto& history = **history_exp;
    reg.request_send(
        fd,
        command_codec::cmd_response{
            "history: room=" + std::to_string(room_id) + " count=" + std::to_string(history.size())
        }
    );
    for(const auto& msg : history){
        reg.request_send(
            fd,
            command_codec::cmd_response{
                "history: id=" + std::to_string(msg.id)
                + " at=" + msg.created_at
                + " from=" + msg.sender_user_id
                + " text=" + msg.body
            }
        );
    }

    logger::log_info(
        std::string(user_id)
        + " request history room=" + std::to_string(room_id)
        + " limit=" + std::to_string(limit)
    );
}
