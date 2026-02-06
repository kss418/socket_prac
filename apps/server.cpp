#include "../include/epoll_server.hpp"

int main(){
    auto create_exp = epoll_server::create("8080");
    if(!create_exp) return 1;

    epoll_server server = std::move(*create_exp);
    auto run_exp = server.run();
    if(!run_exp){
        handle_error("server run failed", run_exp);
        return 1;
    }

    return 0;
}
