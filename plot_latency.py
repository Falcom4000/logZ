#!/usr/bin/env python3
"""
延迟数据可视化脚本 - logZ Benchmark
使用matplotlib和seaborn绘制CDF和PDF图表
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
import glob
import os
import sys

# 设置seaborn样式
sns.set_theme(style="whitegrid")
sns.set_context("paper", font_scale=1.5)

# ============================================================================
# 读取最新的latency文件
# ============================================================================
def find_latest_latency_file():
    """查找data目录下最新的latency文件"""
    pattern = './data/latency_*.txt'
    files = glob.glob(pattern)
    if not files:
        print(f"错误: 在 ./data/ 目录下没有找到 latency_*.txt 文件")
        sys.exit(1)
    # 返回最新的文件
    latest_file = max(files, key=os.path.getctime)
    return latest_file

def load_latency_data(filename):
    """从文件加载latency数据（跳过注释行）"""
    data = []
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                try:
                    data.append(int(line))
                except ValueError:
                    continue
    return np.array(data)

# 读取数据
latency_file = find_latest_latency_file()
print(f"正在读取: {latency_file}")
latencies = load_latency_data(latency_file)
print(f"读取了 {len(latencies)} 个数据点")

# 计算统计信息
total_samples = len(latencies)
min_latency = np.min(latencies)
max_latency = np.max(latencies)
mean_latency = np.mean(latencies)
median_latency = np.median(latencies)
p50 = np.percentile(latencies, 50)
p90 = np.percentile(latencies, 90)
p95 = np.percentile(latencies, 95)
p99 = np.percentile(latencies, 99)
p99_9 = np.percentile(latencies, 99.9)

# 只保留P99以内的数据
print(f"P99值: {p99:.0f} cycles")
latencies_filtered = latencies[latencies <= p99]
print(f"过滤后保留 {len(latencies_filtered)} 个数据点 (原始: {total_samples})")
print(f"过滤掉 {total_samples - len(latencies_filtered)} 个异常值 ({(total_samples - len(latencies_filtered))/total_samples*100:.2f}%)")

# 使用过滤后的数据重新计算统计信息
filtered_mean = np.mean(latencies_filtered)
filtered_median = np.median(latencies_filtered)
filtered_min = np.min(latencies_filtered)
filtered_max = np.max(latencies_filtered)

# 创建图表
fig, axes = plt.subplots(1, 2, figsize=(16, 6))

# ============================================================================
# 1. CDF (累积分布函数)
# ============================================================================
ax1 = axes[0]

# 使用过滤后的数据（只保留P99以内）
sorted_latencies = np.sort(latencies_filtered)
cdf_y = np.arange(1, len(sorted_latencies) + 1) / len(sorted_latencies) * 100

ax1.plot(sorted_latencies, 
         cdf_y, 
         linewidth=2.5, 
         color='#2E86AB',
         alpha=0.8)

ax1.set_xlabel('Latency (CPU Cycles)', fontweight='bold')
ax1.set_ylabel('Cumulative Probability (%)', fontweight='bold')
ax1.set_title('LogZ Logging Latency: CDF (≤P99)', fontweight='bold', fontsize=16)
ax1.grid(True, alpha=0.3)

# 标注关键百分位数
ax1.axvline(x=p50, color='green', linestyle='--', alpha=0.7, linewidth=1.5, label=f'P50: {p50:.0f} cycles')
ax1.axvline(x=p90, color='orange', linestyle='--', alpha=0.7, linewidth=1.5, label=f'P90: {p90:.0f} cycles')
ax1.axvline(x=p99, color='red', linestyle='--', alpha=0.7, linewidth=1.5, label=f'P99: {p99:.0f} cycles')
ax1.legend(loc='lower right')

# 设置x轴范围：从最小值到P99
ax1.set_xlim(filtered_min * 0.95, p99 * 1.05)

# ============================================================================
# 2. PDF (概率密度函数 - 直方图)
# ============================================================================
ax2 = axes[1]

# 使用过滤后的数据（只保留P99以内）
# 使用自适应bin数量
n_bins = min(200, int(np.sqrt(len(latencies_filtered))))
counts, bins, patches = ax2.hist(latencies_filtered, 
                                   bins=n_bins,
                                   color='#A23B72',
                                   alpha=0.7,
                                   edgecolor='black',
                                   linewidth=0.5,
                                   density=True)

ax2.set_xlabel('Latency (CPU Cycles)', fontweight='bold')
ax2.set_ylabel('Probability Density', fontweight='bold')
ax2.set_title('LogZ Logging Latency: PDF (≤P99)', fontweight='bold', fontsize=16)
ax2.grid(True, alpha=0.3, axis='y')

# 设置x轴范围：从最小值到P99
ax2.set_xlim(filtered_min * 0.95, p99 * 1.05)

# 添加平均值线（使用过滤后的数据）
ax2.axvline(x=filtered_mean, color='red', linestyle='--', linewidth=2, 
            label=f'Mean: {filtered_mean:.1f} cycles')
ax2.axvline(x=filtered_median, color='blue', linestyle='--', linewidth=2, 
            label=f'Median: {filtered_median:.0f} cycles')
ax2.legend()

# ============================================================================
# 调整布局并保存
# ============================================================================
plt.tight_layout()
output_file = './data/latency_analysis.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight')
print(f"✓ 已保存图表: {output_file}")

# ============================================================================
# 打印统计摘要
# ============================================================================
print("\n" + "="*70)
print("LATENCY STATISTICS SUMMARY - LogZ Multi-threaded Logging")
print("="*70)

# 假设CPU频率为4.5GHz（可以根据实际情况调整）
cpu_freq_ghz = 4.5

print(f"\n数据文件: {latency_file}")
print(f"总样本数: {total_samples:,}")
print(f"\n延迟统计 (CPU Cycles):")
print(f"  Min:    {min_latency:>10.0f} cycles ({min_latency/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  Mean:   {mean_latency:>10.1f} cycles ({mean_latency/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  Median: {median_latency:>10.0f} cycles ({median_latency/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  P50:    {p50:>10.0f} cycles ({p50/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  P90:    {p90:>10.0f} cycles ({p90/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  P95:    {p95:>10.0f} cycles ({p95/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  P99:    {p99:>10.0f} cycles ({p99/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  P99.9:  {p99_9:>10.0f} cycles ({p99_9/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")
print(f"  Max:    {max_latency:>10.0f} cycles ({max_latency/cpu_freq_ghz:>8.1f} ns @ {cpu_freq_ghz}GHz)")

print("\n" + "="*70)

