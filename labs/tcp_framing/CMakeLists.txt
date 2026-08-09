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

# 引入CTest，并创建BUILD_TESTING配置选项。
#
# BUILD_TESTING默认开启。只有包含CTest，才能使用add_test注册测试。
include(CTest)

# 将消息编解码实现构建成静态库。
add_library(
    message_protocol
    STATIC
    message.c
)

# 将完整消息收发功能构建成静态库。
add_library(
    message_io
    STATIC
    message_io.c
)

# message_io.h属于该库的公共接口。
target_include_directories(
    message_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# message_io组合使用消息编解码和可靠流式收发。
#
# PUBLIC表示最终链接message_io的程序也会自动获得这两个静态库。
target_link_libraries(
    message_io
    PUBLIC
        message_protocol
        stream_io
)

# 为完整消息收发实现启用编译警告。
target_compile_options(
    message_io
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 将可靠流式收发函数构建成独立静态库。
add_library(
    stream_io
    STATIC
    stream_io.c
)

# 链接stream_io的目标可以找到stream_io.h。
target_include_directories(
    stream_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# stream_io实现也要求保持零编译警告。
target_compile_options(
    stream_io
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 将当前源码目录作为公共头文件搜索路径。
target_include_directories(
    message_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 为消息协议实现启用常见编译警告。
target_compile_options(
    message_protocol
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 将TCP服务器创建、监听和关闭功能构建成静态库。
add_library(
    tcp_server
    STATIC
    tcp_server.c
)

# tcp_server.h是该库的公共接口。
target_include_directories(
    tcp_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# TCP服务器实现保持零编译警告。
target_compile_options(
    tcp_server
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

# 演示程序使用消息协议静态库。
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

# 创建TCP服务器人工演示程序。
add_executable(
    tcp_server_demo
    server_main.c
)

# 演示程序使用TCP服务器静态库。
target_link_libraries(
    tcp_server_demo
    PRIVATE
        tcp_server
)

# 演示程序同样保持零编译警告。
target_compile_options(
    tcp_server_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 只有开启BUILD_TESTING时才构建和注册自动化测试。
if(BUILD_TESTING)
    # 创建消息头单元测试程序。
    add_executable(
        message_tests
        tests/test_message.c
    )

    # 测试程序调用message_protocol中的编解码函数。
    target_link_libraries(
        message_tests
        PRIVATE
            message_protocol
    )

    # 测试代码也必须保持零编译警告。
    target_compile_options(
        message_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    # 将message_tests注册到CTest。
    add_test(
        NAME message_unit_tests
        COMMAND message_tests
    )

    # 当前测试不涉及阻塞，但仍设置超时，避免异常代码永久运行。
    set_tests_properties(
        message_unit_tests
        PROPERTIES
            TIMEOUT 5
    )

        # 创建流式Socket收发单元测试程序。
    add_executable(
        stream_io_tests
        tests/test_stream_io.c
    )

    # 测试程序调用stream_io中的send_all和recv_exact。
    target_link_libraries(
        stream_io_tests
        PRIVATE
            stream_io
    )

    # 测试代码保持零编译警告。
    target_compile_options(
        stream_io_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    # 注册到CTest。
    add_test(
        NAME stream_io_unit_tests
        COMMAND stream_io_tests
    )

    # 网络错误可能导致阻塞，因此设置5秒超时。
    set_tests_properties(
        stream_io_unit_tests
        PROPERTIES
            TIMEOUT 5
    )

        # 创建完整消息收发测试程序。
    add_executable(
        message_io_tests
        tests/test_message_io.c
    )

    # 测试程序链接message_io，并继承其依赖。
    target_link_libraries(
        message_io_tests
        PRIVATE
            message_io
    )

    target_compile_options(
        message_io_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    # 注册第三组CTest。
    add_test(
        NAME message_io_unit_tests
        COMMAND message_io_tests
    )

    set_tests_properties(
        message_io_unit_tests
        PROPERTIES
            TIMEOUT 5
    )

endif()