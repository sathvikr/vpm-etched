workspace(name = "verilog_workspace")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

# C++ rules (needed for Verilator)
http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/refs/tags/0.0.9.tar.gz"],
    strip_prefix = "rules_cc-0.0.9",
    sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
)

# Google Test
http_archive(
    name = "gtest",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    strip_prefix = "googletest-1.14.0",
    sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
)

# Local Verilator configuration
new_local_repository(
    name = "verilator",
    path = "/usr/local/Cellar/verilator/5.026",
    build_file_content = """
cc_library(
    name = "verilator_runtime",
    srcs = glob(["share/verilator/include/*.cpp"]),
    hdrs = glob(["share/verilator/include/*.h"]),
    strip_include_prefix = "share/verilator/include",
    visibility = ["//visibility:public"],
)
""",
)
