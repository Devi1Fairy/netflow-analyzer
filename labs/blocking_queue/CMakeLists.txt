# 指定本项目能够使用的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义项目名称，并声明本项目使用C语言。
project(blocking_queue_lib LANGUAGES C)

# 要求项目使用C11标准。
set(CMAKE_C_STANDARD 11)
# 如果编译器不支持C11，配置阶段直接报错。
set(CMAKE_C_STANDARD_REQUIRED ON)
# 禁用GNU扩展，尽量按照标准C11编写代码。
set(CMAKE_C_EXTENSIONS OFF)

# 使用main.c和queue.c生成名为queue_demo的可执行程序。
add_executable(queue_demo main.c queue.c)

# 为queue_demo启用常用的编译警告。
target_compile_options(
    queue_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)