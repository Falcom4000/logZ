# Latency benchmark
cc_binary(
    name = "logZ.benchmark",
    srcs = ["logZ.benchmark.cpp"],
    deps = [
        "//:logZ",
    ],
    linkopts = ["-pthread"],
    copts = ["-std=c++20"],
)
