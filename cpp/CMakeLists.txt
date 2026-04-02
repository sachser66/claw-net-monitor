cmake_minimum_required(VERSION 3.16)
project(claw_net_monitor_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(claw-net-monitor
  src/app/main.cpp
  src/util/exec.cpp
  src/util/json.cpp
  src/collectors/network_collector.cpp
  src/collectors/openclaw_collector.cpp
  src/collectors/docker_collector.cpp
  src/renderers/terminal_renderer.cpp
  src/core/state_json.cpp
  src/core/triggers.cpp
  src/server/http_server.cpp
)

target_compile_options(claw-net-monitor PRIVATE -O2 -Wall -Wextra -pedantic)
target_include_directories(claw-net-monitor PRIVATE /usr/include /usr/include/ncursesw src)
target_link_libraries(claw-net-monitor PRIVATE ncursesw tinfo)
