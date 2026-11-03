cmake_minimum_required(VERSION 3.16)

project(mysql_demo LANGUAGES CXX)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 pkg-config
find_package(PkgConfig REQUIRED)

# 查找 MySQL 客户端库
pkg_check_modules(
    MYSQL
    REQUIRED
    IMPORTED_TARGET
    mysqlclient
)

# 创建可执行程序
add_executable(mysql_demo main.cpp)

# 链接 MySQL 客户端库
target_link_libraries(
    mysql_demo
    PRIVATE
    PkgConfig::MYSQL
)