load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

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
