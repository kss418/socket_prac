#include "server/epoll_server.hpp"
#include "database/db_connector.hpp"
#include "database/db_service.hpp"

int main(){
    auto db_exp = db_connector::create(
        "127.0.0.1", "5432", "socket_app_db", "postgres", ""
    );
    if(!db_exp){
        handle_error("db connect failed", db_exp);
        return 1;
    }

    db_service db(*db_exp);

    auto server_exp = epoll_server::create("8080", db);
    if(!server_exp) return 1;

    auto run_exp = server_exp->run();
    if(!run_exp){
        handle_error("server run failed", run_exp);
        return 1;
    }

    return 0;
}
