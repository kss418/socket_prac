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
        || std::holds_alternative<command_codec::cmd_nick>(cmd);
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
        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){
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
