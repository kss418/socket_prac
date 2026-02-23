#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class db_connector;

class db_service{
    db_connector& connector;
    std::mutex mtx;

public:
    explicit db_service(db_connector& connector) noexcept;

    db_service(const db_service&) = delete;
    db_service& operator=(const db_service&) = delete;
    db_service(db_service&&) = delete;
    db_service& operator=(db_service&&) = delete;

    std::expected<void, error_code> ping() noexcept;
    std::expected<std::optional<std::string>, error_code> login(
        std::string_view id, std::string_view pw
    ) noexcept;
    std::expected<bool, error_code> signup(std::string_view id, std::string_view pw) noexcept;
    std::expected<bool, error_code> change_nickname(
        std::string_view id, std::string_view nickname
    ) noexcept;
    std::expected<bool, error_code> add_friend(
        std::string_view user_id, std::string_view friend_id
    ) noexcept;
    std::expected<bool, error_code> remove_friend(
        std::string_view user_id, std::string_view friend_id
    ) noexcept;
    std::expected<std::vector<std::string>, error_code> list_friends(
        std::string_view user_id
    ) noexcept;
    std::expected<bool, error_code> request_friend(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<bool, error_code> accept_friend_request(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<bool, error_code> reject_friend_request(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<std::vector<std::string>, error_code> list_friend_requests(
        std::string_view to_user_id
    ) noexcept;
};
