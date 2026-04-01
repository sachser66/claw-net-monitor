cmake_minimum_required(VERSION 3.16)
project(claw_net_monitor_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(claw-net-monitor
  src/main.cpp
)

target_compile_options(claw-net-monitor PRIVATE -O2 -Wall -Wextra -pedantic)
target_include_directories(claw-net-monitor PRIVATE /usr/include /usr/include/ncursesw)
target_link_libraries(claw-net-monitor PRIVATE ncursesw tinfo)
