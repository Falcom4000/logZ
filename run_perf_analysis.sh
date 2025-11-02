#!/bin/bash

# perf性能分析脚本
# 使用方法: sudo ./run_perf_analysis.sh

echo "=========================================="
echo "使用perf分析logZ性能"
echo "=========================================="
echo ""

BENCHMARK="./bazel-bin/benchmark/logZ.benchmark"
DATA_DIR="./data"

# 创建data目录（如果不存在）
mkdir -p "$DATA_DIR"

# 检查benchmark是否存在
if [ ! -f "$BENCHMARK" ]; then
    echo "错误: 找不到benchmark可执行文件"
    echo "请先运行: bazel build //benchmark:logZ.benchmark"
    exit 1
fi

echo "1. 运行benchmark并收集perf数据..."
echo "=========================================="
# 使用perf record同时运行benchmark，只运行一次
perf record -g --call-graph dwarf -F 999 -o "$DATA_DIR/perf.data" \
    -e cycles,instructions,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,branches,branch-misses \
    $BENCHMARK

if [ $? -eq 0 ]; then
    echo ""
    echo "2. 生成perf统计报告..."
    echo "=========================================="
    perf report --stdio -i "$DATA_DIR/perf.data" > "$DATA_DIR/perf_report.txt"
    echo "✓ 详细报告已保存到: $DATA_DIR/perf_report.txt"
    
    echo ""
    echo "3. 显示热点函数（Top 20）..."
    echo "=========================================="
    perf report --stdio -i "$DATA_DIR/perf.data" --sort=dso,symbol --percent-limit=1 | head -60
    
    echo ""
    echo "4. 生成火焰图数据..."
    echo "=========================================="
    FLAMEGRAPH_PATH="/home/tiantian/Documents/codes/FlameGraph"
    if [ -d "$FLAMEGRAPH_PATH" ]; then
        perf script -i "$DATA_DIR/perf.data" | $FLAMEGRAPH_PATH/stackcollapse-perf.pl > "$DATA_DIR/perf_folded.txt"
        $FLAMEGRAPH_PATH/flamegraph.pl "$DATA_DIR/perf_folded.txt" > "$DATA_DIR/flamegraph.svg"
        echo "✓ 火焰图已生成: $DATA_DIR/flamegraph.svg"
        echo "  使用浏览器打开: firefox $DATA_DIR/flamegraph.svg"
    else
        echo "⚠ 未找到FlameGraph工具在 $FLAMEGRAPH_PATH"
        echo "  请检查路径是否正确"
    fi
else
    echo "✗ perf record 失败"
fi

echo ""
echo "=========================================="
echo "分析完成！"
echo "=========================================="
echo "生成的文件（在 $DATA_DIR/ 目录）:"
echo "  - latency_result.txt    : benchmark latency数据"
echo "  - perf.data             : perf原始数据"
echo "  - perf_report.txt       : 详细分析报告"
echo "  - flamegraph.svg        : 火焰图（如果生成）"
echo ""
echo "查看热点函数:"
echo "  perf report -i $DATA_DIR/perf.data"
echo ""
echo "查看特定函数的汇编:"
echo "  perf annotate -i $DATA_DIR/perf.data <function_name>"
