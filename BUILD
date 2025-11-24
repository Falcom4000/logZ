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
    copts = ["-std=c++20"],
    visibility = ["//visibility:public"],
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
