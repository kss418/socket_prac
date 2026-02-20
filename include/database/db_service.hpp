#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <mutex>
#include <string_view>

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
    std::expected<bool, error_code> login(std::string_view id, std::string_view pw) noexcept;
};
