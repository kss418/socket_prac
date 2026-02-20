#pragma once
#include "core/error_code.hpp"
#include <pqxx/pqxx>
#include <expected>
#include <exception>
#include <string>
#include <string_view>

class db_connector{
public:
    enum class db_error : int{
        unknown_exception = 1,
        missing_password_env,
        broken_connection,
        sql_error,
        transaction_rollback,
        serialization_failure,
        deadlock_detected,
        in_doubt_error,
        permission_denied,
        unique_violation,
        foreign_key_violation,
        not_null_violation,
        check_violation
    };

    static std::string db_strerror(int code);
    static error_code map_exception(const std::exception& ex) noexcept;
private:
    pqxx::connection conn;
    static std::string make_conninfo(
        std::string_view host, std::string_view port, std::string_view db_name,
        std::string_view user, std::string_view password
    );

public:
    static std::expected<db_connector, error_code> create(
        std::string host, std::string port, std::string db_name,
        std::string user, std::string password
    ) noexcept;

    pqxx::connection& connection() noexcept;
    const pqxx::connection& connection() const noexcept;

    db_connector(std::string host, std::string port, std::string db_name, std::string user, std::string password);
    db_connector(const db_connector&) = delete;
    db_connector& operator=(const db_connector&) = delete;
    db_connector(db_connector&&) = delete;
    db_connector& operator=(db_connector&&) = delete;
};
