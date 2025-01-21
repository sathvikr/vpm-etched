workspace(name = "verilog_workspace")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# C++ rules (needed for Verilator)
http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/refs/tags/0.0.9.tar.gz"],
    strip_prefix = "rules_cc-0.0.9",
    sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
)

# Local toolchain configuration
new_local_repository(
    name = "system_verilator",
    path = "/usr/local",  # Typical installation path for Verilator
    build_file_content = """
cc_library(
    name = "verilator",
    srcs = glob(["lib/libverilator.*"]),
    hdrs = glob([
        "include/verilator/**/*.h",
        "share/verilator/include/**/*.h",
        "share/verilator/include/**/*.cpp",
    ]),
    includes = [
        "include",
        "share/verilator/include",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "verilator_bin",
    srcs = ["bin/verilator"],
    visibility = ["//visibility:public"],
)
"""
)
