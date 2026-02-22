cmake_minimum_required(VERSION 3.20)

project(socket_prac LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(socket_prac
    src/core/error_code.cpp
    src/core/config_loader.cpp
    src/core/logger.cpp
    src/core/unique_fd.cpp
    src/net/addr.cpp
    src/net/fd_helper.cpp
    src/net/io_helper.cpp
    src/net/tls_error.cpp
    src/net/tls_context.cpp
    src/net/tls_session.cpp
    src/reactor/epoll_wakeup.cpp
    src/reactor/epoll_utility.cpp
    src/reactor/epoll_registry.cpp
    src/reactor/event_loop.cpp
    src/server/epoll_listener.cpp
    src/server/epoll_acceptor.cpp
    src/server/epoll_server.cpp
    src/client/chat_client.cpp
    src/client/chat_executor.cpp
    src/client/chat_io_worker.cpp
    src/protocol/command_codec.cpp
    src/protocol/line_parser.cpp
    src/core/thread_pool.cpp
    src/database/db_connector.cpp
    src/database/db_service.cpp
    src/database/db_executor.cpp
)

target_include_directories(socket_prac PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

find_package(libpqxx CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(socket_prac PUBLIC libpqxx::pqxx OpenSSL::SSL OpenSSL::Crypto)

add_executable(server apps/server.cpp)
target_compile_definitions(server PRIVATE PROJECT_ROOT_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(server PRIVATE socket_prac)

add_executable(client apps/client.cpp)
target_compile_definitions(client PRIVATE PROJECT_ROOT_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(client PRIVATE socket_prac)
