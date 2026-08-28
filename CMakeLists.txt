# 指定主项目支持的最低CMake版本。
cmake_minimum_required(VERSION 3.16)

# 定义正式项目名称和当前版本。
project(
    netflow_analyzer
    VERSION 0.2.0
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

# 使用pkg-config查找系统中的libpcap开发包。
find_package(PkgConfig REQUIRED)

# IMPORTED_TARGET会创建PkgConfig::PCAP目标。
#
# 这个目标包含libpcap所需的头文件目录、链接库和平台相关参数，
# 不需要手动硬编码-lpcap或/usr/include路径。
pkg_check_modules(
    PCAP
    REQUIRED
    IMPORTED_TARGET
    libpcap
)

# app及未来核心模块构建成静态库。
#
# main程序和单元测试都链接这个库，避免测试复制业务代码。
add_library(
    analyzer_core
    STATIC
    src/app/app.c
    src/common/byte_reader.c
    src/capture/capture.c
    src/protocol/packet_info.c
    src/protocol/ethernet.c
    src/protocol/ipv4.c
    src/protocol/tcp.c
    src/protocol/udp.c
    src/protocol/icmp.c
    src/protocol/ipv4_dispatch.c
    src/flow/flow_key.c
    src/flow/flow_record.c
    src/flow/flow_table.c
    src/output/flow_export.c
    src/flow/flow_expiration.c
    src/metrics/runtime_metrics.c
)

# PUBLIC表示使用analyzer_core的目标自动继承公开头文件目录。
target_include_directories(
    analyzer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# capture.c内部使用libpcap，但公开头文件不暴露pcap_t，
# 因此该实现依赖声明为PRIVATE。
target_link_libraries(
    analyzer_core
    PRIVATE
        PkgConfig::PCAP
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

    # 创建安全字节读取工具的单元测试程序。
    add_executable(
        byte_reader_tests
        tests/unit/test_byte_reader.c
    )

    # 测试程序调用的byte_cursor函数实现在analyzer_core中。
    #
    # 由于analyzer_core公开了include目录，链接后测试目标也会自动获得
    # include/analyzer/byte_reader.h所需的头文件搜索路径。
    target_link_libraries(
        byte_reader_tests
        PRIVATE
            analyzer_core
    )

    # 为测试代码启用和正式代码相同的严格编译警告。
    enable_project_warnings(byte_reader_tests)

    # 把测试程序注册到CTest。
    #
    # 只有add_executable并不能让ctest自动执行它，必须通过add_test注册。
    add_test(
        NAME byte_reader_tests
        COMMAND byte_reader_tests
    )

    # 防止测试因为意外死循环而永久占用测试流程。
    set_tests_properties(
        byte_reader_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建离线采集模块测试程序。
    add_executable(
        capture_tests
        tests/unit/test_capture.c
    )

    # analyzer_core提供capture接口实现。
    #
    # 测试程序还直接调用libpcap创建临时PCAP输入文件，因此这里也需要
    # 显式链接PkgConfig::PCAP。
    target_link_libraries(
        capture_tests
        PRIVATE
            analyzer_core
            PkgConfig::PCAP
    )

    enable_project_warnings(capture_tests)

    add_test(
        NAME capture_tests
        COMMAND capture_tests
    )

    set_tests_properties(
        capture_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建统一数据包结果对象的单元测试程序。
    add_executable(
        packet_info_tests
        tests/unit/test_packet_info.c
    )

    # 测试程序通过核心静态库调用packet_info接口实现。
    target_link_libraries(
        packet_info_tests
        PRIVATE
            analyzer_core
    )

    # 为测试启用与正式代码相同的严格编译警告。
    enable_project_warnings(packet_info_tests)

    # 将测试程序注册到CTest。
    add_test(
        NAME packet_info_tests
        COMMAND packet_info_tests
    )

    # 防止测试发生异常死循环时永久阻塞测试流程。
    set_tests_properties(
        packet_info_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建Ethernet II解析器单元测试程序。
    add_executable(
        ethernet_tests
        tests/unit/test_ethernet.c
    )

    # Ethernet解析器、packet_info和byte_cursor都由核心库提供。
    target_link_libraries(
        ethernet_tests
        PRIVATE
            analyzer_core
    )

    # 使用与正式代码相同的严格编译警告。
    enable_project_warnings(ethernet_tests)

    # 把Ethernet测试注册到CTest。
    add_test(
        NAME ethernet_tests
        COMMAND ethernet_tests
    )

    set_tests_properties(
        ethernet_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建IPv4解析器单元测试程序。
    add_executable(
        ipv4_tests
        tests/unit/test_ipv4.c
    )

    # IPv4解析器及其依赖都由analyzer_core提供。
    target_link_libraries(
        ipv4_tests
        PRIVATE
            analyzer_core
    )

    # 测试代码使用和正式代码相同的严格警告。
    enable_project_warnings(ipv4_tests)

    # 将IPv4测试注册到CTest自动化测试集合。
    add_test(
        NAME ipv4_tests
        COMMAND ipv4_tests
    )

    # 防止意外死循环永久阻塞测试流程。
    set_tests_properties(
        ipv4_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建TCP解析器单元测试程序。
    add_executable(
        tcp_tests
        tests/unit/test_tcp.c
    )

    # TCP解析器及其Ethernet、IPv4等依赖由核心库提供。
    target_link_libraries(
        tcp_tests
        PRIVATE
            analyzer_core
    )

    # 使用与正式代码相同的严格编译警告。
    enable_project_warnings(tcp_tests)

    # 把TCP测试注册到CTest自动化测试集合。
    add_test(
        NAME tcp_tests
        COMMAND tcp_tests
    )

    # 防止测试意外死循环。
    set_tests_properties(
        tcp_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建UDP解析器单元测试程序。
    add_executable(
        udp_tests
        tests/unit/test_udp.c
    )

    # UDP解析器及其依赖由核心静态库提供。
    target_link_libraries(
        udp_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(udp_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME udp_tests
        COMMAND udp_tests
    )

    set_tests_properties(
        udp_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建ICMP解析器单元测试程序。
    add_executable(
        icmp_tests
        tests/unit/test_icmp.c
    )

    # ICMP解析器及其依赖由核心静态库提供。
    target_link_libraries(
        icmp_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(icmp_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME icmp_tests
        COMMAND icmp_tests
    )

    set_tests_properties(
        icmp_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建IPv4上层协议分发器单元测试程序。
    add_executable(
        ipv4_dispatch_tests
        tests/unit/test_ipv4_dispatch.c
    )

    # 分发器和各协议解析器由核心静态库提供。
    target_link_libraries(
        ipv4_dispatch_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(ipv4_dispatch_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME ipv4_dispatch_tests
        COMMAND ipv4_dispatch_tests
    )

    set_tests_properties(
        ipv4_dispatch_tests
        PROPERTIES
            TIMEOUT 5
    )

        # 创建双向五元组流键单元测试程序。
    add_executable(
        flow_key_tests
        tests/unit/test_flow_key.c
    )

    # 流键实现及packet_info依赖由核心静态库提供。
    target_link_libraries(
        flow_key_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(flow_key_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME flow_key_tests
        COMMAND flow_key_tests
    )

    set_tests_properties(
        flow_key_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建单条流记录单元测试程序。
    add_executable(
        flow_record_tests
        tests/unit/test_flow_record.c
    )

    # 流记录、流键和packet_info由核心静态库提供。
    target_link_libraries(
        flow_record_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(flow_record_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME flow_record_tests
        COMMAND flow_record_tests
    )

    set_tests_properties(
        flow_record_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建固定容量流表单元测试程序。
    add_executable(
        flow_table_tests
        tests/unit/test_flow_table.c
    )

    # 流表、流记录和流键由核心静态库提供。
    target_link_libraries(
        flow_table_tests
        PRIVATE
            analyzer_core
    )

    # 启用与正式代码相同的严格编译警告。
    enable_project_warnings(flow_table_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME flow_table_tests
        COMMAND flow_table_tests
    )

    set_tests_properties(
        flow_table_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 查找Python解释器。
    #
    # Python只用于集成测试，不属于正式程序运行依赖。
    # BUILD_TESTING=OFF时不会要求目标系统安装Python。
    find_package(
        Python3
        REQUIRED
        COMPONENTS Interpreter
    )

    # 注册离线PCAP流聚合端到端验收测试。
    #
    # $<TARGET_FILE:netflow_analyzer_cli>由CMake在生成阶段替换为
    # 最终可执行程序的完整路径，不需要硬编码build/bin目录。
    add_test(
        NAME offline_flow_acceptance
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_offline_flow.py
            --program
            $<TARGET_FILE:netflow_analyzer_cli>
            --work-dir
            ${CMAKE_CURRENT_BINARY_DIR}
    )

    # 防止被测程序或测试脚本意外永久阻塞。
    set_tests_properties(
        offline_flow_acceptance
        PROPERTIES
            TIMEOUT 10
    )

    # 创建CSV流记录导出单元测试程序。
    add_executable(
        flow_export_tests
        tests/unit/test_flow_export.c
    )

    # CSV导出器和IPv4地址格式化函数由核心静态库提供。
    target_link_libraries(
        flow_export_tests
        PRIVATE
            analyzer_core
    )

    # 使用与正式代码相同的严格编译警告。
    enable_project_warnings(flow_export_tests)

    # 注册到CTest自动化测试集合。
    add_test(
        NAME flow_export_tests
        COMMAND flow_export_tests
    )

    set_tests_properties(
        flow_export_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建流过期时间调度单元测试程序。
    add_executable(
        flow_expiration_tests
        tests/unit/test_flow_expiration.c
    )

    target_link_libraries(
        flow_expiration_tests
        PRIVATE
            analyzer_core
    )

    enable_project_warnings(flow_expiration_tests)

    add_test(
        NAME flow_expiration_tests
        COMMAND flow_expiration_tests
    )

    set_tests_properties(
        flow_expiration_tests
        PROPERTIES
            TIMEOUT 5
    )

    # 创建周期运行指标计算单元测试程序。
    add_executable(
        runtime_metrics_tests
        tests/unit/test_runtime_metrics.c
    )

    target_link_libraries(
        runtime_metrics_tests
        PRIVATE
            analyzer_core
    )

    enable_project_warnings(runtime_metrics_tests)

    add_test(
        NAME runtime_metrics_tests
        COMMAND runtime_metrics_tests
    )

    set_tests_properties(
        runtime_metrics_tests
        PROPERTIES
            TIMEOUT 5
    )

endif()
