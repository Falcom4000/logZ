# Benchmark binary with Google Benchmark
cc_binary(
    name = "bench_concurrent_logging",
    srcs = ["bench_concurrent_logging.cpp"],
    deps = [
        "//:logZ",
        "@com_google_benchmark//:benchmark",
    ],
    linkopts = ["-pthread"],
)
