#pragma once
#include <iostream>
#include <mutex>
#include <string_view>

namespace client_console{
    inline std::mutex& output_mutex() noexcept{
        static std::mutex mtx;
        return mtx;
    }

    inline void print_line(std::string_view line){
        std::lock_guard<std::mutex> lock(output_mutex());
        std::cout << line << "\n";
    }
}
