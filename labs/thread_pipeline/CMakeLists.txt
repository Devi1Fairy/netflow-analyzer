# 指定本实验支持的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义A3实验名称，并声明项目使用C语言。
project(thread_pipeline_lab LANGUAGES C)

# 使用C11标准。
set(CMAKE_C_STANDARD 11)

# 如果编译器不支持C11，配置阶段直接报错。
set(CMAKE_C_STANDARD_REQUIRED ON)

# 禁止GNU语言扩展，尽量使用标准C和明确的POSIX接口。
set(CMAKE_C_EXTENSIONS OFF)

# 创建当前数据模型演示程序。
add_executable(
    pipeline_types_demo
    main.c
)

# pipeline_types.h位于当前源码目录。
target_include_directories(
    pipeline_types_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 保持编译零警告。
target_compile_options(
    pipeline_types_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)