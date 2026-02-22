#include "database/db_executor.hpp"
#include "core/logger.hpp"
#include <optional>
#include <string>

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
        || std::holds_alternative<command_codec::cmd_register>(cmd);
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
        tasks.emplace(task{std::move(cmd), reg, fd});
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
    auto& [cmd, reg, fd] = t;
    std::visit([this, &reg, fd](const auto& c){
        execute_command(c, reg, fd);
    }, cmd);
}

void db_executor::execute_command(
    const command_codec::cmd_login& cmd, epoll_registry& reg, int fd
){
    auto login_exp = db.login(cmd.id, cmd.pw);
    if(!login_exp){
        logger::log_warn("login failed", "db_executor::execute_command()", login_exp.error());
        reg.request_send(fd, command_codec::cmd_response{"login failed"});
        return;
    }

    reg.request_send(
        fd, command_codec::cmd_response{*login_exp ? "login success" : "login failed"}
    );
}

void db_executor::execute_command(
    const command_codec::cmd_register& cmd, epoll_registry& reg, int fd
){
    auto signup_exp = db.signup(cmd.id, cmd.pw);
    if(!signup_exp){
        logger::log_warn("register failed", "db_executor::execute_command", signup_exp.error());
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
    const command_codec::cmd_nick&, epoll_registry&, int
){}

void db_executor::execute_command(
    const command_codec::cmd_response&, epoll_registry&, int
){}
