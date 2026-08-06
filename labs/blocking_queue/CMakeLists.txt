# 指定本项目能够使用的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义项目名称，并声明本项目使用C语言。
project(blocking_queue_lib LANGUAGES C)

# 优先使用-pthread编译和链接选项。
# -pthread不仅负责链接线程库，也可能启用线程相关的编译器配置。
set(THREADS_PREFER_PTHREAD_FLAG ON)

# 查找当前平台提供的线程支持，并创建Threads::Threads目标。
# REQUIRED表示找不到线程支持时，CMake配置阶段直接报错。
find_package(Threads REQUIRED)

# 要求项目使用C11标准。
set(CMAKE_C_STANDARD 11)
# 如果编译器不支持C11，配置阶段直接报错。
set(CMAKE_C_STANDARD_REQUIRED ON)
# 禁用GNU扩展，尽量按照标准C11编写代码。
set(CMAKE_C_EXTENSIONS OFF)

# 引入CTest，并创建BUILD_TESTING配置选项。
# BUILD_TESTING默认开启，同时允许使用add_test注册测试。
include(CTest)

# 提供一个可选开关，用于启用内存错误和未定义行为检查。
# 默认关闭，只有配置时显式指定ENABLE_SANITIZERS=ON才会启用。
option(
    ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

# 当前Sanitizer配置支持GCC和Clang。
if(ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # 在编译阶段加入ASan和UBSan插桩。
        add_compile_options(
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        # 在链接阶段加入对应的Sanitizer运行库。
        add_link_options(
            -fsanitize=address,undefined
        )
    else()
        message(
            FATAL_ERROR
            "ENABLE_SANITIZERS currently supports GCC and Clang only"
        )
    endif()
endif()

# 将队列实现编译成静态库。
add_library(
    blocking_queue
    STATIC
    queue.c
)

# 队列实现使用POSIX线程同步接口。
#
# 使用PUBLIC是因为blocking_queue是静态库，它的最终使用者
# queue_demo和queue_tests也需要继承线程编译、链接参数。
target_link_libraries(
    blocking_queue
    PUBLIC
        Threads::Threads
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
    # 并发错误可能导致线程永久等待。
    # 超过10秒仍未结束时，由CTest终止测试并报告超时。
    set_tests_properties(
        queue_unit_tests
        PROPERTIES
            TIMEOUT 10
    )

endif()