#include "BuildGenerator.hpp"
#include <iostream>

// This file is intentionally empty as the template implementation is in the header file
// Template classes typically require their implementation to be available in the header

void BuildGenerator::parseSubmodules() {
    std::ifstream file(sv_file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + sv_file_path.string());
    }

    std::string line;
    // Regex pattern to match module instantiations
    std::regex module_pattern(R"(\b(\w+)\s+\w+\s*\()");
    
    while (std::getline(file, line)) {
        std::smatch matches;
        if (std::regex_search(line, matches, module_pattern)) {
            // First capture group contains the module name
            submodules.push_back(matches[1]);
        }
    }
}

void BuildGenerator::initWorkspace(const std::string& workspace_path) {
    std::filesystem::path ws_path(workspace_path);
    std::filesystem::path workspace_file = ws_path / "WORKSPACE";
    
    std::ofstream file(workspace_file);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create WORKSPACE file: " + workspace_file.string());
    }

    // Write workspace name
    file << "workspace(name = \"verilog_workspace\")\n\n";

    // Load http_archive rule
    file << "load(\"@bazel_tools//tools/build_defs/repo:http.bzl\", \"http_archive\")\n\n";

    // Add C++ rules
    file << "# C++ rules (needed for Verilator)\n";
    file << "http_archive(\n";
    file << "    name = \"rules_cc\",\n";
    file << "    urls = [\"https://github.com/bazelbuild/rules_cc/archive/refs/tags/0.0.9.tar.gz\"],\n";
    file << "    strip_prefix = \"rules_cc-0.0.9\",\n";
    file << "    sha256 = \"2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf\",\n";
    file << ")\n\n";

    // Add local Verilator configuration
    file << "# Local toolchain configuration\n";
    file << "new_local_repository(\n";
    file << "    name = \"system_verilator\",\n";
    file << "    path = \"/usr/local\",  # Typical installation path for Verilator\n";
    file << "    build_file_content = \"\"\"\n";
    file << "cc_library(\n";
    file << "    name = \"verilator\",\n";
    file << "    srcs = glob([\"lib/libverilator.*\"]),\n";
    file << "    hdrs = glob([\n";
    file << "        \"include/verilator/**/*.h\",\n";
    file << "        \"share/verilator/include/**/*.h\",\n";
    file << "        \"share/verilator/include/**/*.cpp\",\n";
    file << "    ]),\n";
    file << "    includes = [\n";
    file << "        \"include\",\n";
    file << "        \"share/verilator/include\",\n";
    file << "    ],\n";
    file << "    visibility = [\"//visibility:public\"],\n";
    file << ")\n\n";
    file << "filegroup(\n";
    file << "    name = \"verilator_bin\",\n";
    file << "    srcs = [\"bin/verilator\"],\n";
    file << "    visibility = [\"//visibility:public\"],\n";
    file << ")\n";
    file << "\"\"\"\n";
    file << ")\n";

    std::cout << "Created WORKSPACE file at: " << workspace_file << "\n";

    // Create tools/verilator directory and files
    std::filesystem::path tools_dir = ws_path / "tools" / "verilator";
    std::filesystem::create_directories(tools_dir);

    // Create BUILD file
    std::ofstream build_file(tools_dir / "BUILD");
    build_file << "package(default_visibility = [\"//visibility:public\"])\n\n";
    build_file << "exports_files([\"defs.bzl\"])\n";

    // Create defs.bzl file with our custom rule
    std::ofstream defs_file(tools_dir / "defs.bzl");
    defs_file << R"(load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

def _verilator_hdl_library_impl(ctx):
    output_dir = ctx.actions.declare_directory(ctx.attr.name + "_verilated")
    
    verilate_action = ctx.actions.declare_file(ctx.attr.name + "_verilate.sh")
    ctx.actions.write(
        output = verilate_action,
        content = """#!/bin/bash
        set -e
        mkdir -p {output_dir}
        {verilator} --cc {input} --Mdir {output_dir}
        rm -f {output_dir}/*.mk {output_dir}/*.dat {output_dir}/*.d
        """.format(
            verilator = ctx.executable._verilator.path,
            input = ctx.file.src.path,
            output_dir = output_dir.path,
        ),
        is_executable = True,
    )
    
    ctx.actions.run(
        outputs = [output_dir],
        inputs = [ctx.file.src],
        tools = [ctx.executable._verilator, verilate_action],
        executable = verilate_action,
        mnemonic = "Verilate",
    )
    
    return [
        DefaultInfo(files = depset([output_dir])),
        CcInfo(
            compilation_context = cc_common.create_compilation_context(
                headers = depset([output_dir]),
                includes = depset([output_dir.path]),
            ),
        ),
    ]

verilator_hdl_library = rule(
    implementation = _verilator_hdl_library_impl,
    attrs = {
        "src": attr.label(
            allow_single_file = [".v", ".sv"],
            mandatory = True,
        ),
        "_verilator": attr.label(
            default = "@system_verilator//:verilator_bin",
            executable = True,
            cfg = "exec",
        ),
    },
    fragments = ["cpp"],
    provides = [CcInfo],
)
)";
}

BuildGenerator::BuildGenerator(const std::filesystem::path& path, 
                             const std::optional<std::filesystem::path>& test_path)
    : sv_file_path(path), test_file_path(test_path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File does not exist: " + path.string());
    }
    if (test_file_path && !std::filesystem::exists(*test_file_path)) {
        throw std::runtime_error("Test file does not exist: " + test_file_path->string());
    }
    parseSubmodules();
}

void BuildGenerator::generateBuildFile(const std::string& output_path) {
    std::ofstream build_file(output_path);
    if (!build_file.is_open()) {
        throw std::runtime_error("Failed to create BUILD file: " + output_path);
    }

    // Write Bazel build file header with required rules
    build_file << "load(\"@rules_cc//cc:defs.bzl\", \"cc_library\")\n";
    build_file << "load(\"//tools/verilator:defs.bzl\", \"verilator_hdl_library\")\n\n";
    
    // Get the module name from the file name
    std::string module_name = sv_file_path.stem().string();
    
    // Write filegroup for the source file
    build_file << "filegroup(\n";
    build_file << "    name = \"" << module_name << "_sv\",\n";
    build_file << "    srcs = [\"" << sv_file_path.filename().string() << "\"],\n";
    build_file << "    visibility = [\"//visibility:public\"],\n";
    build_file << ")\n\n";

    // Generate Verilator HDL library target
    build_file << "verilator_hdl_library(\n";
    build_file << "    name = \"" << module_name << "_verilated_hdl\",\n";
    build_file << "    src = \"" << sv_file_path.filename().string() << "\",\n";
    build_file << ")\n\n";

    // Generate C++ library target
    build_file << "cc_library(\n";
    build_file << "    name = \"" << module_name << "_verilated\",\n";
    build_file << "    srcs = [\":" << module_name << "_verilated_hdl\"],\n";
    build_file << "    deps = [\"@system_verilator//:verilator\"],\n";
    build_file << "    copts = [\"-I$(GENDIR)/" << std::filesystem::path(output_path).parent_path().string() << "\"],\n";
    build_file << "    visibility = [\"//visibility:public\"],\n";
    build_file << ")\n";

    // If test file is provided, generate test target
    if (test_file_path) {
        std::string test_name = module_name + "_test";
        build_file << "\ncc_test(\n";
        build_file << "    name = \"" << test_name << "\",\n";
        build_file << "    srcs = [\"" << test_file_path->filename().string() << "\"],\n";
        build_file << "    deps = [\":" << module_name << "_verilated\"],\n";
        build_file << ")\n";
    }
}
