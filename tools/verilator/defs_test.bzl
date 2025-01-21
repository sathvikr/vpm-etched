load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")
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
