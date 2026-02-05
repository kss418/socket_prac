cmake_minimum_required(VERSION 3.20)

project(socket_prac LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(socket_prac
    src/addr.cpp
    src/error_code.cpp
    src/fd_helper.cpp
    src/unique_fd.cpp
    src/io_helper.cpp
    src/epoll_server.cpp
    src/chat_client.cpp
    src/epoll_registry.cpp
    src/epoll_utility.cpp
    src/event_loop.cpp
    src/epoll_listener.cpp
    src/epoll_acceptor.cpp
    src/server_factory.cpp
)

target_include_directories(socket_prac PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

add_executable(server apps/server.cpp)
target_link_libraries(server PRIVATE socket_prac)

add_executable(client apps/client.cpp)
target_link_libraries(client PRIVATE socket_prac)