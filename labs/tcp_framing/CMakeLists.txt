# 指定本实验支持的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义实验名称，并声明项目使用C语言。
project(tcp_framing_lab LANGUAGES C)

# 使用C11标准。
set(CMAKE_C_STANDARD 11)

# 如果编译器不支持C11，配置阶段直接报错。
set(CMAKE_C_STANDARD_REQUIRED ON)

# 禁止GNU扩展，尽量使用标准C和明确的POSIX接口。
set(CMAKE_C_EXTENSIONS OFF)

# 将消息编解码实现构建成静态库。
#
# STATIC表示生成静态库，最终链接时所需代码会进入可执行程序。
add_library(
    message_protocol
    STATIC
    message.c
)

# 将当前源码目录作为公共头文件搜索路径。
#
# PUBLIC表示：
# 1. message_protocol自己可以找到message.h；
# 2. 链接message_protocol的目标也会继承该头文件路径。
target_include_directories(
    message_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 为消息协议实现启用常见编译警告。
#
# PRIVATE表示这些编译选项只用于message_protocol本身，
# 不自动传递给其他目标。
target_compile_options(
    message_protocol
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 创建人工演示程序。
add_executable(
    message_demo
    main.c
)

# 将演示程序与消息协议静态库链接。
target_link_libraries(
    message_demo
    PRIVATE
        message_protocol
)

# 演示程序同样要求保持零编译警告。
target_compile_options(
    message_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)