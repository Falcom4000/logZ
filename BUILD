# logZ library
cc_library(
    name = "logZ",
    hdrs = [
        "include/Queue.h",
        "include/RingBytes.h",
        "include/Logger.h",
        "include/Backend.h",
        "include/Decoder.h",
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

