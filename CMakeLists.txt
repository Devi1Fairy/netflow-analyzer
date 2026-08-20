# 指定主项目支持的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义正式项目名称和当前版本。
project(
    netflow_analyzer
    VERSION 0.0.1
    DESCRIPTION "Linux network traffic analyzer"
    LANGUAGES C
)

# 使用ISO C11标准。
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 导出每个C文件真实使用的编译器、宏、头文件目录和编译参数。
#
# VS Code、clangd和静态分析工具可以读取compile_commands.json，
# 从而使代码补全和实际CMake构建保持一致。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 将不同类型的构建产物统一放在build的子目录中。
set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

# 为当前项目目标统一启用严格警告。
function(enable_project_warnings target_name)
    target_compile_options(
        ${target_name}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endfunction()

# app及未来核心模块构建成静态库。
#
# main程序和单元测试都链接这个库，避免测试复制业务代码。
add_library(
    analyzer_core
    STATIC
    src/app/app.c
)

# PUBLIC表示使用analyzer_core的目标自动继承公开头文件目录。
target_include_directories(
    analyzer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 把CMake项目版本作为字符串宏传给app.c。
target_compile_definitions(
    analyzer_core
    PRIVATE
        ANALYZER_VERSION="${PROJECT_VERSION}"
)

enable_project_warnings(analyzer_core)

# 创建命令行主程序目标。
add_executable(
    netflow_analyzer_cli
    src/main.c
)

# 最终可执行文件名使用更符合命令行习惯的netflow-analyzer。
set_target_properties(
    netflow_analyzer_cli
    PROPERTIES
        OUTPUT_NAME "netflow-analyzer"
)

target_link_libraries(
    netflow_analyzer_cli
    PRIVATE
        analyzer_core
)

enable_project_warnings(netflow_analyzer_cli)

# 引入CTest并创建BUILD_TESTING选项。
include(CTest)

if(BUILD_TESTING)
    # 创建阶段0冒烟测试程序。
    add_executable(
        analyzer_smoke_tests
        tests/unit/test_smoke.c
    )

    target_link_libraries(
        analyzer_smoke_tests
        PRIVATE
            analyzer_core
    )

    enable_project_warnings(analyzer_smoke_tests)

    # 注册到CTest。
    add_test(
        NAME analyzer_smoke_tests
        COMMAND analyzer_smoke_tests
    )

    set_tests_properties(
        analyzer_smoke_tests
        PROPERTIES
            TIMEOUT 5
    )
endif()