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

# 优先使用-pthread编译和链接参数。
set(THREADS_PREFER_PTHREAD_FLAG ON)

# 查找平台提供的POSIX线程支持。
find_package(Threads REQUIRED)

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

# 将通用指针阻塞队列构建成静态库。
add_library(
    pointer_queue
    STATIC
    pointer_queue.c
)

# 指针队列使用pthread互斥锁和条件变量。
target_link_libraries(
    pointer_queue
    PUBLIC
        Threads::Threads
)

# pointer_queue.h位于当前源码目录。
target_include_directories(
    pointer_queue
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 指针队列实现保持零编译警告。
target_compile_options(
    pointer_queue
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 创建指针队列演示程序。
add_executable(
    pointer_queue_demo
    queue_demo.c
)

# 演示程序调用pointer_queue接口。
target_link_libraries(
    pointer_queue_demo
    PRIVATE
        pointer_queue
)

# 演示程序保持零编译警告。
target_compile_options(
    pointer_queue_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 将三个线程入口函数构建成静态库。
add_library(
    pipeline_threads
    STATIC
    pipeline_threads.c
)

# pipeline_threads的公开头文件位于当前源码目录。
target_include_directories(
    pipeline_threads
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 线程处理函数调用pointer_queue接口。
#
# 使用PUBLIC是因为pipeline_threads.h本身包含pointer_queue.h，
# 使用这个库的目标也需要知道pointer_queue的头文件和链接依赖。
target_link_libraries(
    pipeline_threads
    PUBLIC
        pointer_queue
)

# 线程处理代码保持零编译警告。
target_compile_options(
    pipeline_threads
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 创建完整的多线程流水线演示程序。
add_executable(
    thread_pipeline_demo
    pipeline_demo.c
)

# pipeline_demo直接调用pthread_create和pthread_join，因此明确链接
# Threads::Threads，而不是只依赖间接传递。
target_link_libraries(
    thread_pipeline_demo
    PRIVATE
        pipeline_threads
        Threads::Threads
)

# 演示程序保持零编译警告。
target_compile_options(
    thread_pipeline_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 引入CTest，并创建BUILD_TESTING配置选项。
include(CTest)

# 只有开启BUILD_TESTING时才构建并注册自动化测试。
if(BUILD_TESTING)
    # 创建统一的指针阻塞队列测试程序。
    add_executable(
        pointer_queue_tests
        tests/test_pointer_queue.c
    )

    # 测试程序使用pointer_queue静态库。
    #
    # pointer_queue通过PUBLIC传递Threads::Threads依赖，
    # 所以测试程序会自动继承正确的线程编译和链接配置。
    target_link_libraries(
        pointer_queue_tests
        PRIVATE
            pointer_queue
    )

    # 测试代码同样保持零编译警告。
    target_compile_options(
        pointer_queue_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    # 把测试程序注册到CTest。
    add_test(
        NAME pointer_queue_tests
        COMMAND pointer_queue_tests
    )

    # 如果条件变量或关闭逻辑错误，线程可能永久等待。
    #
    # 超过10秒后CTest会终止程序并报告测试失败。
    set_tests_properties(
        pointer_queue_tests
        PROPERTIES
            TIMEOUT 10
    )

    # 完整运行五线程流水线，并验证生成的CSV内容。
    add_test(
        NAME thread_pipeline_acceptance
        COMMAND
            ${CMAKE_COMMAND}
            "-DPIPELINE_PROGRAM=$<TARGET_FILE:thread_pipeline_demo>"
            "-DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/pipeline_acceptance.csv"
            -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_pipeline_csv.cmake
    )

    # 流水线如果发生线程死锁，测试不能永久等待。
    set_tests_properties(
        thread_pipeline_acceptance
        PROPERTIES
            TIMEOUT 10
    )
endif()
