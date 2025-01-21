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
    file << "load(\"@bazel_tools//tools/build_defs/repo:http.bzl\", \"http_archive\")\n";
    file << "load(\"@bazel_tools//tools/build_defs/repo:utils.bzl\", \"maybe\")\n\n";

    // Add C++ rules
    file << "# C++ rules (needed for Verilator)\n";
    file << "http_archive(\n";
    file << "    name = \"rules_cc\",\n";
    file << "    urls = [\"https://github.com/bazelbuild/rules_cc/archive/refs/tags/0.0.9.tar.gz\"],\n";
    file << "    strip_prefix = \"rules_cc-0.0.9\",\n";
    file << "    sha256 = \"2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf\",\n";
    file << ")\n\n";

    // Add GTest dependency
    file << "# Google Test\n";
    file << "http_archive(\n";
    file << "    name = \"gtest\",\n";
    file << "    urls = [\"https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz\"],\n";
    file << "    strip_prefix = \"googletest-1.14.0\",\n";
    file << "    sha256 = \"8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7\",\n";
    file << ")\n\n";

    // Add local Verilator configuration
    file << "# Local Verilator configuration\n";
    file << "new_local_repository(\n";
    file << "    name = \"verilator\",\n";
    file << "    path = \"/usr/local/Cellar/verilator/5.026\",\n";
    file << "    build_file_content = \"\"\"\n";
    file << "cc_library(\n";
    file << "    name = \"verilator_runtime\",\n";
    file << "    srcs = glob([\"share/verilator/include/*.cpp\"]),\n";
    file << "    hdrs = glob([\"share/verilator/include/*.h\"]),\n";
    file << "    strip_include_prefix = \"share/verilator/include\",\n";
    file << "    visibility = [\"//visibility:public\"],\n";
    file << ")\n";
    file << "\"\"\",\n";
    file << ")\n";

    std::cout << "Created WORKSPACE file at: " << workspace_file << "\n";

    // Create tools/verilator directory and files
    std::filesystem::path tools_dir = ws_path / "tools" / "verilator";
    std::filesystem::create_directories(tools_dir);

    // Create BUILD file
    std::ofstream build_file(tools_dir / "BUILD");
    build_file << "package(default_visibility = [\"//visibility:public\"])\n\n";
    build_file << "exports_files([\"defs.bzl\", \"defs_test.bzl\"])\n";

    // Create defs.bzl file with our custom rule for regular builds
    std::ofstream defs_file(tools_dir / "defs.bzl");
    defs_file << R"BAZEL(load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

def _verilator_hdl_library_impl(ctx):
    output_dir = ctx.actions.declare_directory(ctx.attr.name + "_verilated")
    
    verilate_action = ctx.actions.declare_file(ctx.attr.name + "_verilate.sh")
    ctx.actions.write(
        output = verilate_action,
        content = '''\
#!/bin/bash
set -e
mkdir -p {output_dir}
/usr/local/bin/verilator --cc {input} --Mdir {output_dir}
rm -f {output_dir}/*.mk {output_dir}/*.dat {output_dir}/*.d
'''.format(
            input = ctx.file.src.path,
            output_dir = output_dir.path,
        ),
        is_executable = True,
    )
    
    ctx.actions.run(
        outputs = [output_dir],
        inputs = [ctx.file.src],
        tools = [verilate_action],
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
    },
    fragments = ["cpp"],
    provides = [CcInfo],
)
)BAZEL";

    // Create defs_test.bzl file with our custom rule for tests
    std::ofstream defs_test_file(tools_dir / "defs_test.bzl");
    defs_test_file << R"BAZEL(load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")
load("@rules_cc//cc:defs.bzl", "cc_test")

def _verilator_hdl_test_impl(ctx):
    output_dir = ctx.actions.declare_directory(ctx.attr.name + "_verilated")
    output_exe = ctx.actions.declare_file(ctx.attr.name)
    
    # Create a script to handle the Verilator compilation process
    verilate_action = ctx.actions.declare_file(ctx.attr.name + "_verilate.sh")
    ctx.actions.write(
        output = verilate_action,
        content = '''\
#!/bin/bash
set -ex
echo "Current directory: $(pwd)"
echo "Output directory: {output_dir}"
echo "Output executable: {exe}"

# Convert relative paths to absolute
WORKSPACE_ROOT=$(pwd)
OUTPUT_DIR="$WORKSPACE_ROOT/{output_dir}"
OUTPUT_EXE="$WORKSPACE_ROOT/{exe}"

mkdir -p "$OUTPUT_DIR"
cp {input} "$OUTPUT_DIR"/
cp {testbench} "$OUTPUT_DIR"/
cd "$OUTPUT_DIR"

echo "Changed to directory: $(pwd)"
ls -la

# Get Verilator include path
VERILATOR_ROOT=/usr/local/Cellar/verilator/5.026/share/verilator

/usr/local/bin/verilator --cc --exe --trace {input_name} {testbench_name} \\
    --Mdir . --prefix V{top_name} \\
    --top-module {top_name} \\
    -CFLAGS "-I. -I/usr/local/include -I$VERILATOR_ROOT/include -I/usr/local/include/gtest -std=c++17"

sed -i.bak 's|#include "test/rtl/V{top_name}.h"|#include "V{top_name}.h"|' {testbench_name}

echo "Compiling Verilator runtime..."
# Compile Verilator runtime
c++ -c -I. -I$VERILATOR_ROOT/include -std=c++17 \\
    $VERILATOR_ROOT/include/verilated.cpp \\
    $VERILATOR_ROOT/include/verilated_vcd_c.cpp \\
    $VERILATOR_ROOT/include/verilated_threads.cpp

# Create archive
ar rvs libverilated.a verilated.o verilated_vcd_c.o verilated_threads.o

echo "Compiling generated Verilator files..."
# Compile the generated Verilator files
c++ -c -I. -I$VERILATOR_ROOT/include -I/usr/local/include \\
    -std=c++17 -Os \\
    -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 \\
    Vcounter.cpp \\
    Vcounter___024root__DepSet_h0dd033c2__0.cpp \\
    Vcounter___024root__DepSet_h5086c508__0.cpp \\
    Vcounter__Trace__0.cpp \\
    Vcounter___024root__Slow.cpp \\
    Vcounter___024root__DepSet_h5086c508__0__Slow.cpp \\
    Vcounter__Syms.cpp \\
    Vcounter__Trace__0__Slow.cpp \\
    Vcounter__TraceDecls__0__Slow.cpp

echo "Compiling test..."
# Compile the test
c++ -c -I. -I$VERILATOR_ROOT/include -I/usr/local/include \\
    -std=c++17 -Os \\
    -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 \\
    counter_tb.cpp

echo "Creating output directory and linking..."
# Create output directory and link everything together
mkdir -p $(dirname "$OUTPUT_EXE")
echo "Output directory created: $(ls -la $(dirname "$OUTPUT_EXE"))"

echo "Linking with debug output..."
c++ -v -o counter_hdl_test \\
    counter_tb.o \\
    Vcounter.o \\
    Vcounter___024root__DepSet_h0dd033c2__0.o \\
    Vcounter___024root__DepSet_h5086c508__0.o \\
    Vcounter__Trace__0.o \\
    Vcounter___024root__Slow.o \\
    Vcounter___024root__DepSet_h5086c508__0__Slow.o \\
    Vcounter__Syms.o \\
    Vcounter__Trace__0__Slow.o \\
    Vcounter__TraceDecls__0__Slow.o \\
    -L/usr/local/lib -L. \\
    -lpthread -lverilated \\
    -lgtest -lgtest_main

cp counter_hdl_test "$OUTPUT_EXE"
echo "Final output: $(ls -la "$OUTPUT_EXE")"

# Test the executable
echo "Testing executable..."
ldd "$OUTPUT_EXE" || otool -L "$OUTPUT_EXE"
'''.format(
            input = ctx.file.src.path,
            testbench = ctx.file.testbench.path,
            output_dir = output_dir.path,
            exe = output_exe.path,
            input_name = ctx.file.src.basename,
            testbench_name = ctx.file.testbench.basename,
            top_name = ctx.attr.top_module if ctx.attr.top_module else ctx.file.src.basename.replace(".sv", ""),
        ),
        is_executable = True,
    )
    
    # Run the compilation script
    ctx.actions.run(
        outputs = [output_dir, output_exe],
        inputs = [ctx.file.src, ctx.file.testbench],
        tools = [verilate_action],
        executable = verilate_action,
        mnemonic = "VerilateTest",
        use_default_shell_env = True,
    )
    
    return [
        DefaultInfo(
            files = depset([output_dir]),
            executable = output_exe,
            runfiles = ctx.runfiles(files = [output_exe]),
        ),
        CcInfo(
            compilation_context = cc_common.create_compilation_context(
                headers = depset([output_dir]),
                includes = depset([output_dir.path]),
            ),
        ),
    ]

verilator_hdl_test = rule(
    implementation = _verilator_hdl_test_impl,
    attrs = {
        "src": attr.label(
            allow_single_file = [".v", ".sv"],
            mandatory = True,
        ),
        "testbench": attr.label(
            allow_single_file = [".cpp"],
            mandatory = True,
        ),
        "top_module": attr.string(
            mandatory = False,
            doc = "Name of the top module. If not specified, derived from src filename",
        ),
    },
    fragments = ["cpp"],
    test = True,
    executable = True,
)
)BAZEL";
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

void BuildGenerator::generateRegularBuildFile(std::ofstream& build_file, const std::string& module_name, const std::filesystem::path& output_path) {
    // Write filegroup for the source file
    build_file << "filegroup(\n";
    build_file << "    name = \"" << module_name << "_sv\",\n";
    build_file << "    srcs = [\"" << sv_file_path.filename().string() << "\"],\n";
    build_file << "    visibility = [\"//visibility:public\"],\n";
    build_file << ")\n\n";

    // Generate Verilator HDL library target
    build_file << "verilator_hdl_library(\n";
    build_file << "    name = \"" << module_name << "_verilated\",\n";
    build_file << "    src = \"" << sv_file_path.filename().string() << "\",\n";
    build_file << ")\n";
}

void BuildGenerator::generateTestBuildFile(std::ofstream& build_file, const std::string& module_name, const std::filesystem::path& output_path) {
    // Write filegroup for the source file
    build_file << "filegroup(\n";
    build_file << "    name = \"" << module_name << "_sv\",\n";
    build_file << "    srcs = [\"" << sv_file_path.filename().string() << "\"],\n";
    build_file << "    visibility = [\"//visibility:public\"],\n";
    build_file << ")\n\n";

    // Generate Verilator test target
    build_file << "verilator_hdl_test(\n";
    build_file << "    name = \"" << module_name << "_test\",\n";
    build_file << "    src = \"" << sv_file_path.filename().string() << "\",\n";
    build_file << "    testbench = \"" << test_file_path->filename().string() << "\",\n";
    build_file << ")\n";
}

void BuildGenerator::generateBuildFile(const std::string& output_path) {
    std::ofstream build_file(output_path);
    if (!build_file.is_open()) {
        throw std::runtime_error("Failed to create BUILD file: " + output_path);
    }

    // Write Bazel build file header with required rules
    build_file << "load(\"@rules_cc//cc:defs.bzl\", \"cc_library\", \"cc_test\")\n";
    
    if (test_file_path) {
        build_file << "load(\"//tools/verilator:defs_test.bzl\", \"verilator_hdl_test\")\n\n";
    } else {
        build_file << "load(\"//tools/verilator:defs.bzl\", \"verilator_hdl_library\")\n\n";
    }
    
    // Get the module name from the file name
    std::string module_name = sv_file_path.stem().string();
    
    if (test_file_path) {
        generateTestBuildFile(build_file, module_name, output_path);
    } else {
        generateRegularBuildFile(build_file, module_name, output_path);
    }
}
