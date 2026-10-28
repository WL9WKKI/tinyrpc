cmake_minimum_required(VERSION 3.14)
project(diag_agent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 依赖：Drogon（含 ORM）。需预先通过 vcpkg/系统包安装，且启用 mysql 支持。
find_package(Drogon REQUIRED)

# 收集 src 下所有实现文件
file(GLOB_RECURSE DIAG_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(diag_agent ${DIAG_SOURCES})
target_include_directories(diag_agent PRIVATE src)
target_link_libraries(diag_agent PRIVATE Drogon::Drogon)
