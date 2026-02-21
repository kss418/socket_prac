#include "client/chat_client.hpp"

int main(){
    auto client_exp = chat_client::create("127.0.0.1", "8080");
    if(!client_exp){
        handle_error("client create failed", client_exp);
        return 1;
    }

    auto run_exp = client_exp->run();
    if(!run_exp){
        handle_error("client run failed", run_exp);
        return 1;
    }

    return 0;
}
