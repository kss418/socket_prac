#pragma once
#include "client/chat_executor.hpp"
#include "core/error_code.hpp"
#include "core/unique_fd.hpp"
#include "net/io_helper.hpp"
#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

class chat_io_worker{
    struct parsed_command{
        std::string cmd;
        std::vector<std::string> args;
    };

    socket_info& si;
    unique_fd& server_fd;
    chat_executor& executor;
    std::atomic_bool& logged_in;
    recv_buffer stdin_buf;
    bool peer_verified = false;
    std::optional<std::int64_t> selected_room_id;

    static parsed_command parse(const std::string& line);
    short socket_events() const;
    std::expected<void, error_code> progress_tls_handshake();
    std::expected<void, error_code> flush_pending_send();

    std::expected<bool, error_code> recv_socket();
    std::expected<bool, error_code> send_stdin();

    void execute(const std::string& line);
    void say(const std::string& line);
    void change_nickname(const std::string& nick);
    void login(const std::string& id, const std::string& pw);
    void signup(const std::string& id, const std::string& pw);
    void request_friend(const std::string& to_user_id);
    void accept_friend_request(const std::string& from_user_id);
    void reject_friend_request(const std::string& from_user_id);
    void remove_friend(const std::string& friend_user_id);
    void list_friend();
    void list_friend_request();
    void create_room(const std::string& room_name);
    void delete_room(const std::string& room_id);
    void invite_room(const std::string& room_id, const std::string& friend_user_id);
    void leave_room(const std::string& room_id);
    void select_room(const std::string& room_id);
    void list_room();
    void help();
public:
    chat_io_worker(socket_info& si, unique_fd& server_fd, chat_executor& executor, std::atomic_bool& logged_in);

    chat_io_worker(const chat_io_worker&) = delete;
    chat_io_worker& operator=(const chat_io_worker&) = delete;

    std::expected<void, error_code> run(std::stop_token stop_token);
};
