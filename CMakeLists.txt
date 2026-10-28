cmake_minimum_required(VERSION 3.15)
project(radar_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 目标平台：飞腾 FT-2000/4（ARMv8）+ 银河麒麟 V10。
# 本文件不硬编码 Qt 安装路径或 CPU 架构参数；架构参数由工具链/命令行传入。
# 在 FT2000 上编译时，CMAKE_PREFIX_PATH 应指向麒麟系统上安装的 Qt 5.12.12 路径，例如：
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<麒麟系统上的 Qt 5.12.12 安装前缀>
# 并确保已安装 Qt5 的 Core、Network、WebSockets 开发组件（如 qt5-websockets-dev）。
find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets)

# 物理层不依赖 Qt，可独立移植。
add_library(radar_core STATIC
    src/radar_model.cpp
    src/jammer_model.cpp
    src/esm_model.cpp
    src/detection.cpp
)
target_include_directories(radar_core PUBLIC src)
set_target_properties(radar_core PROPERTIES AUTOMOC OFF)

add_executable(radar_sim
    src/main.cpp
    src/http_server.cpp
)
target_link_libraries(radar_sim PRIVATE
    radar_core
    Qt5::Core
    Qt5::Network
    Qt5::WebSockets
)

# 物理模型数值验证
add_executable(sim_test src/sim_test.cpp)
target_link_libraries(sim_test PRIVATE radar_core)
set_target_properties(sim_test PROPERTIES AUTOMOC OFF)
