cmake_minimum_required(VERSION 3.20)

project(socket_prac LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(socket_prac
    src/core/error_code.cpp
    src/core/unique_fd.cpp
    src/net/addr.cpp
    src/net/fd_helper.cpp
    src/net/io_helper.cpp
    src/reactor/epoll_wakeup.cpp
    src/reactor/epoll_utility.cpp
    src/reactor/epoll_registry.cpp
    src/reactor/event_loop.cpp
    src/server/epoll_listener.cpp
    src/server/epoll_acceptor.cpp
    src/server/epoll_server.cpp
    src/client/chat_client.cpp
    src/protocol/command_codec.cpp
    src/protocol/line_parser.cpp
)

target_include_directories(socket_prac PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

add_executable(server apps/server.cpp)
target_link_libraries(server PRIVATE socket_prac)

add_executable(client apps/client.cpp)
target_link_libraries(client PRIVATE socket_prac)
