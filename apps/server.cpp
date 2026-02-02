#include "../include/epoll_server.hpp"

int main(){
    epoll_server server;
    server.init();
    server.run();

    return 0;
}