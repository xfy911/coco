cmake_minimum_required(VERSION 3.16)
project(coco VERSION 1.0.0 LANGUAGES C ASM)

# 编译选项
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

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
    src/core/stack.c
)

set(TIMER_SOURCES
    src/timer/timer_wheel.c
)

set(CHANNEL_SOURCES
    src/channel/channel.c
)

set(IO_SOURCES
    src/io/event_loop.c
)

# 平台特定源文件
if(PLATFORM STREQUAL "linux")
    list(APPEND IO_SOURCES src/io/poll_linux.c)
    if(ARCH STREQUAL "x86_64")
        list(APPEND PLATFORM_ASM src/platform/linux/ctx_x86_64.S)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND PLATFORM_ASM src/platform/linux/ctx_arm64.S)
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
    ${TIMER_SOURCES}
    ${CHANNEL_SOURCES}
    ${IO_SOURCES}
    ${PLATFORM_ASM}
)

target_include_directories(coco PUBLIC include)

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
    )

    foreach(example_name ${EXAMPLES})
        add_executable(${example_name} examples/${example_name}.c)
        target_link_libraries(${example_name} coco)
    endforeach()
endif()