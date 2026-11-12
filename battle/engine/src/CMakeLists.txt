cmake_minimum_required(VERSION 3.20)
project(ZhiShangTanBing CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # 用 -std=c++17 而不是 gnu++17（后者默认 -ffp-contract=fast，会合成 FMA，结果随优化级别变）

set(JOLT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/JoltPhysics" CACHE PATH "Jolt 源码目录（提交 6a985659a781c5a196b8f34a8394b2ccf6a9e1cc）")
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)  # Jolt 跨平台确定：换指令集、优化级别结果不变
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${JOLT_DIR}/Build jolt)

add_executable(war main.cpp sim.cpp scenario.cpp nav.cpp host.cpp)
target_link_libraries(war Jolt)
target_link_options(war PRIVATE -static)
# Jolt 的优化选项只作用在它自己的子目录，这里必须显式加 -O2；并关掉乘加合成
target_compile_options(war PRIVATE -O2 -ffp-contract=off)
