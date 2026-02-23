#include "database/db_executor.hpp"
#include "core/logger.hpp"
#include <optional>
#include <string>
#include <type_traits>

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
        || std::holds_alternative<command_codec::cmd_nick>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_request>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_accept>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_reject>(cmd)
        || std::holds_alternative<command_codec::cmd_friend_remove>(cmd)
        || std::holds_alternative<command_codec::cmd_list_friend>(cmd)
        || std::holds_alternative<command_codec::cmd_list_friend_request>(cmd);
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
            std::is_same_v<T, command_codec::cmd_nick>
            || std::is_same_v<T, command_codec::cmd_friend_request>
            || std::is_same_v<T, command_codec::cmd_friend_accept>
            || std::is_same_v<T, command_codec::cmd_friend_reject>
            || std::is_same_v<T, command_codec::cmd_friend_remove>
            || std::is_same_v<T, command_codec::cmd_list_friend>
            || std::is_same_v<T, command_codec::cmd_list_friend_request>
        ){
            execute_command(c, reg, fd, user_id);
        }
        else{
            execute_command(c, reg, fd);
        }
    }, cmd);
}

void db_executor::execute_command(
    const command_codec::cmd_login& cmd, epoll_registry& reg, int fd
){
    auto login_exp = db.login(cmd.id, cmd.pw);
    if(!login_exp){
        logger::log_error("login failed", "db_executor::execute_command()", login_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"login failed"});
        reg.request_set_user_id(fd, "");
        reg.request_change_nickname(fd, "guest");
        return;
    }

    if(*login_exp){
        reg.request_set_user_id(fd, cmd.id);
        reg.request_change_nickname(fd, **login_exp);
        reg.request_send(fd, command_codec::cmd_response{"login success"});
    }
    else{
        reg.request_set_user_id(fd, "");
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
    const command_codec::cmd_say&, epoll_registry&, int
){}

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

    reg.request_send(
        fd, command_codec::cmd_response{"friends: " + std::to_string(list_exp->size())}
    );
    for(const std::string& friend_id : *list_exp){
        reg.request_send(fd, command_codec::cmd_response{"friend: " + friend_id});
    }

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
