#include "server/epoll_server.hpp"

int main(){
    auto server_exp = epoll_server::create("8080");
    if(!server_exp) return 1;

    auto run_exp = server_exp->run();
    if(!run_exp){
        handle_error("server run failed", run_exp);
        return 1;
    }

    return 0;
}
