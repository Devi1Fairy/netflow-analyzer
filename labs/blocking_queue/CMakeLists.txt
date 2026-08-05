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

# 引入CTest，并创建BUILD_TESTING配置选项。
# BUILD_TESTING默认开启，同时允许使用add_test注册测试。
include(CTest)

# 将队列实现编译成静态库。
add_library(
    blocking_queue
    STATIC
    queue.c
)

# 将当前源码目录设置为公共头文件搜索目录。
# 链接blocking_queue的目标也能够找到queue.h。
target_include_directories(
    blocking_queue
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 为队列实现启用常用编译警告。
target_compile_options(
    blocking_queue
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 创建人工演示程序。
add_executable(
    queue_demo
    main.c
)

# 将演示程序与队列静态库链接。
target_link_libraries(
    queue_demo
    PRIVATE
        blocking_queue
)

# 为演示程序启用常用编译警告。
target_compile_options(
    queue_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 只有开启BUILD_TESTING时才构建和注册测试程序。
if(BUILD_TESTING)
    # 创建队列测试程序。
    add_executable(
        queue_tests
        tests/test_queue.c
    )

    # 测试程序使用与演示程序相同的队列实现。
    target_link_libraries(
        queue_tests
        PRIVATE
            blocking_queue
    )

    # 测试代码同样必须保持零编译警告。
    target_compile_options(
        queue_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    # 将queue_tests注册到CTest。
    add_test(
        NAME queue_unit_tests
        COMMAND queue_tests
    )
endif()