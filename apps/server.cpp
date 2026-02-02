#include "../include/epoll_server.hpp"

int main(){
    epoll_server server;
    auto init_exp = server.init();
    if(!init_exp){
        handle_error("server init failed", init_exp);
        return 1;
    }

    auto run_exp = server.run();
    if(!run_exp){
        handle_error("server run failed", run_exp);
        return 1;
    }

    return 0;
}