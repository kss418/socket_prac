#include "client/chat_client.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv){
#if defined(SIGPIPE)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    std::string ip = "127.0.0.1";
    std::string port = "8080";
    std::filesystem::path ca_path = std::filesystem::path(PROJECT_ROOT_DIR) / "certs/ca.crt.pem";

    if(argc > 4){
        std::cerr << "usage: " << argv[0] << " [ip] [port] [ca_path]" << "\n";
        return 1;
    }
    if(argc >= 2) ip = argv[1];
    if(argc >= 3) port = argv[2];
    if(argc >= 4) ca_path = argv[3];

    auto client_exp = chat_client::create(ip.c_str(), port.c_str(), ca_path.string());
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
