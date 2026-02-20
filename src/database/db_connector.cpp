#include "database/db_connector.hpp"
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>

std::string db_connector::db_strerror(int code){
    if(code == static_cast<int>(db_error::unknown_exception)) return "unknown_db_exception";
    else if(code == static_cast<int>(db_error::missing_password_env)) return "missing_password_env";
    else if(code == static_cast<int>(db_error::broken_connection)) return "broken_connection";
    else if(code == static_cast<int>(db_error::sql_error)) return "sql_error";
    else if(code == static_cast<int>(db_error::transaction_rollback)) return "transaction_rollback";
    else if(code == static_cast<int>(db_error::serialization_failure)) return "serialization_failure";
    else if(code == static_cast<int>(db_error::deadlock_detected)) return "deadlock_detected";
    else if(code == static_cast<int>(db_error::in_doubt_error)) return "in_doubt_error";
    else if(code == static_cast<int>(db_error::permission_denied)) return "permission_denied";
    else if(code == static_cast<int>(db_error::unique_violation)) return "unique_violation";
    else if(code == static_cast<int>(db_error::foreign_key_violation)) return "foreign_key_violation";
    else if(code == static_cast<int>(db_error::not_null_violation)) return "not_null_violation";
    else if(code == static_cast<int>(db_error::check_violation)) return "check_violation";
    return "unknown_db_error";
}

error_code db_connector::map_exception(const std::exception& ex) noexcept{
    if(dynamic_cast<const pqxx::broken_connection*>(&ex)){
        return error_code::from_db(static_cast<int>(db_error::broken_connection));
    }

    if(dynamic_cast<const pqxx::in_doubt_error*>(&ex)){
        return error_code::from_db(static_cast<int>(db_error::in_doubt_error));
    }

    if(dynamic_cast<const pqxx::transaction_rollback*>(&ex)){
        return error_code::from_db(static_cast<int>(db_error::transaction_rollback));
    }

    if(dynamic_cast<const pqxx::sql_error*>(&ex)){
        return error_code::from_db(static_cast<int>(db_error::sql_error));
    }

    return error_code::from_db(static_cast<int>(db_error::unknown_exception));
}

std::string db_connector::make_conninfo(
    std::string_view host, std::string_view port, std::string_view db_name,
    std::string_view user, std::string_view password
){
    auto append_escaped = [](std::string& dst, std::string_view src){
        for(char ch : src){
            if(ch == '\\' || ch == '\''){
                dst.push_back('\\');
            }
            dst.push_back(ch);
        }
    };

    std::string conninfo;
    conninfo.reserve(
        host.size() + port.size() + db_name.size() + user.size() + password.size() + 64
    );

    conninfo += "host='";
    append_escaped(conninfo, host);
    conninfo += "' port='";
    append_escaped(conninfo, port);
    conninfo += "' dbname='";
    append_escaped(conninfo, db_name);
    conninfo += "' user='";
    append_escaped(conninfo, user);
    conninfo += "' password='";
    append_escaped(conninfo, password);
    conninfo += "'";
    return conninfo;
}

db_connector::db_connector(
    std::string host, std::string port, std::string db_name,
    std::string user, std::string password
) : conn(make_conninfo(host, port, db_name, user, password)) {}

pqxx::connection& db_connector::connection() noexcept{ return conn; }
const pqxx::connection& db_connector::connection() const noexcept{ return conn; }

std::expected<db_connector, error_code> db_connector::create(
    std::string host, std::string port, std::string db_name,
    std::string user, std::string password
) noexcept{
    if(password.empty()){
        const char* env_password = std::getenv("DB_PASSWORD");
        if(env_password == nullptr || env_password[0] == '\0'){
            return std::unexpected(error_code::from_db(static_cast<int>(db_error::missing_password_env)));
        }
        password = env_password;
    }

    try{
        return std::expected<db_connector, error_code>(
            std::in_place,
            std::move(host), std::move(port), std::move(db_name),
            std::move(user), std::move(password)
        );
    } catch(const std::exception& ex){
        return std::unexpected(map_exception(ex));
    } catch(...){
        return std::unexpected(error_code::from_db(static_cast<int>(db_error::unknown_exception)));
    }
}
