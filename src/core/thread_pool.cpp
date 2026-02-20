#include "core/thread_pool.hpp"
#include <optional>

thread_pool::~thread_pool(){ stop(); }

thread_pool::thread_pool(std::size_t sz){
    if(sz == 0) sz = 1;
    workers.reserve(sz);
    for(std::size_t i = 1;i <= sz;i++){
        workers.emplace_back([this](std::stop_token st){ worker_loop(st); });
    }
}

bool thread_pool::is_pool_command(const command_codec::command& cmd) noexcept{
    return false;
}

void thread_pool::stop(){
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
    
bool thread_pool::enqueue(command_codec::command cmd, epoll_registry& reg, int fd){
    {
        std::lock_guard<std::mutex> lock(mtx);
        if(!run) return false;
        tasks.emplace(task{std::move(cmd), reg, fd});
    }
    cv.notify_one();
    return true;
}

void thread_pool::worker_loop(std::stop_token st){
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

        if(task_opt) execute(*task_opt);
    }
}

void thread_pool::execute(const task& t){
    auto& [cmd, reg, fd] = t;
    std::visit([&](const auto& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, command_codec::cmd_say>){
            reg.request_broadcast(fd, command_codec::cmd_response{c.text});
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){
            reg.request_change_nickname(fd, c.nick);
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_response>){
            
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_login>){
            
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_register>){
            
        }
    }, cmd);
}
