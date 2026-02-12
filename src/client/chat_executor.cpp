#include "client/chat_executor.hpp"
#include <iostream>
#include <type_traits>

void chat_executor::request_execute(command_codec::command cmd){
    {
        std::lock_guard<std::mutex> lock(mtx);
        execute_q.emplace(std::move(cmd));
    }
    cv.notify_one();
}

void chat_executor::run(std::stop_token stop_token){
    std::stop_callback on_stop(stop_token, [this](){ cv.notify_all(); });

    while(true){
        command_codec::command cmd;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&](){
                return stop_token.stop_requested() || !execute_q.empty();
            });

            if(stop_token.stop_requested() && execute_q.empty()) return;
            cmd = std::move(execute_q.front());
            execute_q.pop();
        }

        execute(cmd);
    }
}

void chat_executor::execute(const command_codec::command& cmd){
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
