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