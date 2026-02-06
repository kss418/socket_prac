#include "client/chat_client.hpp"

int main(){
    chat_client client;
    auto conn_exp = client.connect("127.0.0.1", "8080");
    if(!conn_exp){
        handle_error("client connect failed", conn_exp);
        return 1;
    }

    auto run_exp = client.run();
    if(!run_exp){
        handle_error("client run failed", run_exp);
        return 1;
    }

    return 0;
}