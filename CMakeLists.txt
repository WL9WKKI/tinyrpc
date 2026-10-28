cmake_minimum_required(VERSION 3.14)
project(diag_agent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 依赖：Drogon（含 ORM）。需预先通过 vcpkg/系统包安装，且启用 mysql 支持。
find_package(Drogon REQUIRED)

# Redis 客户端：hiredis（SessionManager 会话缓存用）
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h REQUIRED)
find_library(HIREDIS_LIBRARY NAMES hiredis REQUIRED)

# RabbitMQ 客户端：librabbitmq（慢任务异步队列）
find_path(RABBITMQ_INCLUDE_DIR amqp.h REQUIRED)
find_library(RABBITMQ_LIBRARY NAMES rabbitmq REQUIRED)

# HdrHistogram（延迟统计）
find_path(HDR_HISTOGRAM_INCLUDE_DIR hdr/hdr_histogram.h REQUIRED)
find_library(HDR_HISTOGRAM_LIBRARY NAMES hdr_histogram REQUIRED)

# 收集 src 下所有实现文件
file(GLOB_RECURSE DIAG_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(diag_agent ${DIAG_SOURCES})
target_include_directories(diag_agent PRIVATE src ${HIREDIS_INCLUDE_DIR} ${RABBITMQ_INCLUDE_DIR} ${HDR_HISTOGRAM_INCLUDE_DIR})
target_link_libraries(diag_agent PRIVATE Drogon::Drogon ${HIREDIS_LIBRARY} ${RABBITMQ_LIBRARY} ${HDR_HISTOGRAM_LIBRARY})
