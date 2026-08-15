# 验证是否提供了待测试程序路径。
if(NOT DEFINED PIPELINE_PROGRAM OR
   PIPELINE_PROGRAM STREQUAL "")
    message(FATAL_ERROR
        "PIPELINE_PROGRAM was not provided")
endif()

# 验证是否提供了测试CSV路径。
if(NOT DEFINED OUTPUT_FILE OR
   OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR
        "OUTPUT_FILE was not provided")
endif()

# 删除上一次测试可能留下的文件，防止旧文件造成假通过。
file(REMOVE "${OUTPUT_FILE}")

# 运行完整的多线程流水线程序。
execute_process(
    COMMAND
        "${PIPELINE_PROGRAM}"
        "${OUTPUT_FILE}"
    RESULT_VARIABLE
        program_result
    OUTPUT_VARIABLE
        program_output
    ERROR_VARIABLE
        program_error
)

# 非0退出状态表示程序本身执行失败。
if(NOT program_result STREQUAL "0")
    message(FATAL_ERROR
        "Pipeline program failed.\n"
        "stdout:\n${program_output}\n"
        "stderr:\n${program_error}")
endif()

# 程序成功退出后，CSV文件必须真实存在。
if(NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR
        "Pipeline did not create ${OUTPUT_FILE}")
endif()

# 按行读取CSV文件。
file(STRINGS
    "${OUTPUT_FILE}"
    csv_lines
)

# 1行表头加12行结果，总共应当是13行。
list(LENGTH
    csv_lines
    csv_line_count
)

if(NOT csv_line_count EQUAL 13)
    message(FATAL_ERROR
        "Expected 13 CSV lines, got ${csv_line_count}")
endif()

# 验证CSV表头。
list(GET
    csv_lines
    0
    csv_header
)

if(NOT csv_header STREQUAL
   "task_id,input_value,output_value,worker_index")
    message(FATAL_ERROR
        "Unexpected CSV header: ${csv_header}")
endif()

# 两个工作线程并行运行，所以结果行顺序不固定。
#
# 测试不依赖行顺序，而是检查1到12号任务是否各自出现一次。
foreach(task_id RANGE 1 12)
    math(EXPR
        expected_square
        "${task_id} * ${task_id}"
    )

    set(matching_line_count 0)

    foreach(csv_line IN LISTS csv_lines)
        # worker_index只能是0或1。
        if(csv_line MATCHES
           "^${task_id},${task_id},${expected_square},[01]$")
            math(EXPR
                matching_line_count
                "${matching_line_count} + 1"
            )
        endif()
    endforeach()

    if(NOT matching_line_count EQUAL 1)
        message(FATAL_ERROR
            "Task ${task_id} appeared "
            "${matching_line_count} times")
    endif()
endforeach()

message(STATUS
    "CSV acceptance passed: 12 valid results")