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

cc_binary(
    name = "Quill.benchmark",
    srcs = ["Quill.benchmark.cpp"],
    deps = [
        "//third_party/quill:quill",
    ],
    linkopts = ["-pthread"],
    copts = ["-std=c++20"],
)
