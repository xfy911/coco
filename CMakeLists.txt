cmake_minimum_required(VERSION 3.16)
project(coco VERSION 1.0.0 LANGUAGES C ASM)

# 编译选项
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# 覆盖率选项
option(COCO_ENABLE_COVERAGE "Enable code coverage" OFF)
if(COCO_ENABLE_COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# 平台检测
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM "linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_COCO_PLATFORM_LINUX")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM "macos")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_COCO_PLATFORM_MACOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM "windows")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_COCO_PLATFORM_WINDOWS")
endif()

# 架构检测
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(ARCH "arm64")
endif()

# 源文件
set(CORE_SOURCES
    src/core/coro.c
    src/core/sched.c
    src/core/context.c
    src/core/context_api.c
    src/core/context_coro.c
    src/core/stack.c
    src/core/signal.c
    src/core/stack_pool.c
    src/core/stack_pool_multi.c
    src/core/stack_pool_legacy.c
    src/core/stack_pool_mt.c
    src/core/cancel.c
    src/core/coro_go.c
    src/core/stack_map.c
    src/core/frame_walker.c
    src/core/stack_grow.c
    src/core/safety.c
    src/sched/locked_queue.c
    src/sched/safepoint.c
    src/sched/global_sched.c
    src/sched/runq.c
    src/sched/sched.c
    src/sched/sched_stats.c
    src/sched/sched_hooks.c
)

set(PLATFORM_SOURCES
    src/platform/coco_thread.c
    src/platform/coco_mutex.c
    src/platform/coco_cond.c
)

set(TIMER_SOURCES
    src/timer/timer_wheel.c
)

set(CHANNEL_SOURCES
    src/channel/channel.c
    src/channel/channel_mt.c
)

set(IO_SOURCES
    src/io/event_loop.c
    src/io/fd_table.c
    src/io/netpoller_mt.c
)

# 平台特定源文件
if(PLATFORM STREQUAL "linux")
    list(APPEND IO_SOURCES src/io/poll_linux.c)
    list(APPEND IO_SOURCES src/io/poll_iouring.c)
    if(ARCH STREQUAL "x86_64")
        list(APPEND PLATFORM_ASM src/platform/linux/ctx_x86_64.S)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND PLATFORM_ASM src/platform/linux/ctx_arm64.S)
    endif()
    # 检查 liburing
    find_library(LIBURING NAMES uring)
    if(LIBURING)
        message(STATUS "Found liburing: ${LIBURING}")
    else()
        message(WARNING "liburing not found. io_uring support will be disabled.")
    endif()
elseif(PLATFORM STREQUAL "macos")
    list(APPEND IO_SOURCES src/io/poll_macos.c)
    if(ARCH STREQUAL "x86_64")
        list(APPEND PLATFORM_ASM src/platform/macos/ctx_x86_64.S)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND PLATFORM_ASM src/platform/macos/ctx_arm64.S)
    endif()
elseif(PLATFORM STREQUAL "windows")
    list(APPEND IO_SOURCES src/io/poll_windows.c)
    list(APPEND PLATFORM_ASM src/platform/windows/ctx_x86_64.S)
endif()

# 创建库
add_library(coco STATIC
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
    ${TIMER_SOURCES}
    ${CHANNEL_SOURCES}
    ${IO_SOURCES}
    ${PLATFORM_ASM}
)

target_include_directories(coco PUBLIC include)
target_include_directories(coco PRIVATE src)

# Linux: 链接 liburing (如果可用)
if(PLATFORM STREQUAL "linux" AND LIBURING)
    target_link_libraries(coco ${LIBURING})
endif()

# 测试
option(COCO_BUILD_TESTS "Build tests" ON)
if(COCO_BUILD_TESTS)
    enable_testing()

    # 单元测试
    set(UNIT_TESTS
        test_coro
        test_channel
        test_timer
        test_io
        test_stack
        test_stack_overflow
        test_cancel
        test_fd_table
        test_iouring
        test_io_backend
        test_batch_io
        test_io_options
        test_syscall_count
        test_poll_macos
        test_event_loop
        test_aging
        test_lifecycle
        test_priority
        test_signal
        test_exit
        test_sleep
        test_aging_trigger
        test_stack_pool_multi
        test_stack_pool_mt
        test_locked_queue
        test_safepoint
        test_global_sched
        test_runq
        test_sched
        test_channel_mt
        test_netpoller_mt
        test_context_api
        test_context_coro
        test_platform_abstraction
        test_sched_stats
        test_coco_go
        test_sched_hooks
        test_context_value
        test_frame_walker
    )

    foreach(test_name ${UNIT_TESTS})
        add_executable(${test_name} tests/unit/${test_name}.c)
        target_link_libraries(${test_name} coco)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    # 基准测试
    set(BENCH_TESTS
        bench_switch
        bench_channel
        bench_io
        bench_io_backend
    )

    foreach(bench_name ${BENCH_TESTS})
        add_executable(${bench_name} tests/benchmark/${bench_name}.c)
        target_link_libraries(${bench_name} coco)
    endforeach()
endif()

# 示例
option(COCO_BUILD_EXAMPLES "Build examples" ON)
if(COCO_BUILD_EXAMPLES)
    set(EXAMPLES
        basic
        echo_server
        pipeline
        memory_test
    )

    foreach(example_name ${EXAMPLES})
        add_executable(${example_name} examples/${example_name}.c)
        target_link_libraries(${example_name} coco)
    endforeach()
endif()

# 覆盖率目标
if(COCO_BUILD_TESTS AND COCO_ENABLE_COVERAGE)
    find_program(LCOV_PATH lcov)
    find_program(GENHTML_PATH genhtml)

    if(LCOV_PATH AND GENHTML_PATH)
        add_custom_target(coverage
            # 清理旧的覆盖率数据
            COMMAND ${CMAKE_COMMAND} -E remove -f coverage.base coverage.test coverage.total coverage.cleaned.info
            # 初始化基准数据
            COMMAND ${LCOV_PATH} --capture --initial --directory . --output-file coverage.base
            # 运行测试
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            # 捕获测试覆盖率数据
            COMMAND ${LCOV_PATH} --capture --directory . --output-file coverage.test
            # 合并基准和测试数据
            COMMAND ${LCOV_PATH} --add-tracefile coverage.base --add-tracefile coverage.test --output-file coverage.total
            # 过滤系统头文件和测试文件
            COMMAND ${LCOV_PATH} --remove coverage.total '/usr/*' '/opt/homebrew/*' '*/tests/*' '*/examples/*' --output-file coverage.cleaned.info --ignore-errors unused
            # 生成 HTML 报告
            COMMAND ${GENHTML_PATH} coverage.cleaned.info --output-directory coverage_report
            # 输出摘要
            COMMAND ${LCOV_PATH} --summary coverage.cleaned.info
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report..."
        )
        add_custom_command(TARGET coverage POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in: ${CMAKE_BINARY_DIR}/coverage_report/"
        )
    else()
        message(WARNING "lcov or genhtml not found. Coverage target will not be available.")
    endif()
endif()