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
endif()