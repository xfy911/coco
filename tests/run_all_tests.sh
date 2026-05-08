#!/bin/bash
# run_all_tests.sh - 运行所有单元测试和基准测试

set -e

BUILD_DIR="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=== Coco 测试套件 ==="
echo "构建目录: $BUILD_DIR"
echo ""

# 检查构建目录
if [ ! -d "$BUILD_DIR" ]; then
    echo "错误：构建目录不存在，请先运行 cmake --build"
    exit 1
fi

# 运行单元测试
echo "=== 单元测试 ==="
echo ""

cd "$BUILD_DIR"

# 运行 ctest
ctest --output-on-failure -j$(nproc)

echo ""
echo "=== 基准测试 ==="
echo ""

# 运行上下文切换基准测试
if [ -f ./bench_switch ]; then
    echo "--- 上下文切换基准测试 ---"
    ./bench_switch
    echo ""
fi

# 运行抢占延迟基准测试
if [ -f ./bench_preempt ]; then
    echo "--- 抢占延迟基准测试 ---"
    ./bench_preempt || true  # 允许失败
    echo ""
fi

# 运行栈增长基准测试
if [ -f ./bench_stack ]; then
    echo "--- 栈增长基准测试 ---"
    ./bench_stack || true  # 允许失败
    echo ""
fi

echo ""
echo "=== 测试完成 ==="
