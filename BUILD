# logZ library
cc_library(
    name = "logZ",
    hdrs = [
        "include/Queue.h",
        "include/RingBytes.h",
        "include/LogTypes.h",
        "include/Logger.h",
        "include/Backend.h",
        "include/Decoder.h",
        "include/Encoder.h",
        "include/Sinker.h",
        "include/StringRingBuffer.h",
        "include/Fixedstring.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
)

# Test binary with Google Test
cc_test(
    name = "test_queue",
    srcs = ["test/test_queue.cpp"],
    deps = [
        ":logZ",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
    linkopts = ["-pthread"],
)

# Logger test with single/multi-thread tests
cc_test(
    name = "test_logger",
    srcs = ["test/test_logger.cpp"],
    deps = [
        ":logZ",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
    linkopts = ["-pthread"],
    copts = ["-std=c++20"],
)


# Example program
cc_binary(
    name = "example",
    srcs = ["example.cpp"],
    deps = [":logZ"],
    copts = ["-std=c++20"],
)

# Simple test program
cc_binary(
    name = "test_simple2",
    srcs = ["test_simple2.cpp"],
    deps = [":logZ"],
    copts = ["-std=c++20"],
)


# Example program with AddressSanitizer
cc_binary(
    name = "example_asan",
    srcs = ["example.cpp"],
    deps = [":logZ"],
    linkopts = ["-pthread", "-fsanitize=address"],
    copts = ["-std=c++20", "-fsanitize=address", "-fno-omit-frame-pointer", "-g"],
)
