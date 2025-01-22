#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <csignal>
#include "BuildGenerator.hpp"

extern "C" {
    #include <stdlib.h>
}

namespace {
    // Execute a shell command and return its exit code
    int executeCommand(const std::string& command) {
        std::cout << "Executing: " << command << "\n";
        const char* cmd = command.c_str();
        return std::system(cmd);
    }
}

void printUsage() {
    std::cout << "Usage: vpm [options] [files...]\n"
              << "Options:\n"
              << "  --init                            Initialize Bazel workspace\n"
              << "  --build <file1.sv> [file2.sv ...]  Build specified SystemVerilog files\n"
              << "  --test <file.sv> <test.cpp>        Build with test file\n"
              << "  --emulate <file.sv> [file2.sv ...] --xdc <constraints.xdc>  Synthesize and emulate on Xilinx FPGA\n"
              << "  --help                             Display this help message\n";
}

bool hasValidExtension(const std::string& filename, bool is_test_file = false) {
    const std::string extension = is_test_file ? ".cpp" : ".sv";
    if (filename.length() < extension.length()) {
        return false;
    }
    return filename.substr(filename.length() - extension.length()) == extension;
}

void buildFiles(const std::vector<std::string>& files, const std::optional<std::string>& test_file = std::nullopt) {
    if (files.empty()) {
        std::cout << "Error: No input files specified for build command\n";
        return;
    }

    // Validate file extensions
    bool hasInvalidFiles = false;
    for (const auto& file : files) {
        if (!hasValidExtension(file)) {
            std::cout << "Error: File '" << file << "' does not have .sv extension\n";
            hasInvalidFiles = true;
        }
    }

    if (test_file && !hasValidExtension(*test_file, true)) {
        std::cout << "Error: Test file '" << *test_file << "' does not have .cpp extension\n";
        hasInvalidFiles = true;
    }

    if (hasInvalidFiles) {
        return;
    }

    std::vector<std::string> bazel_targets;

    // Process each file and generate BUILD files
    for (const auto& file : files) {
        try {
            std::filesystem::path file_path = std::filesystem::absolute(file);
            std::filesystem::path dir_path = file_path.parent_path();
            std::filesystem::path build_path = dir_path / "BUILD";

            std::cout << "Generating BUILD file for: " << file << "\n";
            
            std::optional<std::filesystem::path> test_path = 
                test_file ? std::make_optional(std::filesystem::absolute(*test_file)) : std::nullopt;
            
            BuildGenerator generator(file_path, test_path);
            generator.generateBuildFile(build_path.string());
            
            std::cout << "Created BUILD file at: " << build_path << "\n";
            
            // Print detected submodules
            const auto& submodules = generator.getSubmodules();
            if (!submodules.empty()) {
                std::cout << "Detected submodules:\n";
                for (const auto& submodule : submodules) {
                    std::cout << "  - " << submodule << "\n";
                }
            }

            // Add Bazel target for this file
            std::string module_name = file_path.stem().string();
            std::string target_path = std::filesystem::relative(dir_path).string();
            
            if (test_file) {
                // For test mode, we only need the test target
                std::string test_target = "//" + target_path + ":" + module_name + "_test";
                bazel_targets.push_back(test_target);
            } else {
                // For build mode, we need the verilated target
                std::string bazel_target = "//" + target_path + ":" + module_name + "_verilated";
                bazel_targets.push_back(bazel_target);
            }

        } catch (const std::exception& e) {
            std::cerr << "Error processing file '" << file << "': " << e.what() << "\n";
            return;
        }
    }

    // Build all targets with Bazel
    if (!bazel_targets.empty()) {
        std::string bazel_command = test_file ? "bazel test" : "bazel build";
        for (const auto& target : bazel_targets) {
            bazel_command += " " + target;
        }

        std::cout << "\nBuilding Verilator targets...\n";
        if (int exit_code = executeCommand(bazel_command); exit_code != 0) {
            std::cerr << "Error: Bazel build failed with exit code " << exit_code << "\n";
            return;
        }
        std::cout << "Build completed successfully.\n";
    }
}

void emulateFiles(const std::vector<std::string>& files, const std::string& xdc_file) {
    // Validate file extensions
    bool hasInvalidFiles = false;
    for (const auto& file : files) {
        if (!hasValidExtension(file)) {
            std::cout << "Error: File '" << file << "' does not have .sv extension\n";
            hasInvalidFiles = true;
        }
    }

    if (xdc_file.substr(xdc_file.length() - 4) != ".xdc") {
        std::cout << "Error: Constraints file '" << xdc_file << "' does not have .xdc extension\n";
        hasInvalidFiles = true;
    }

    if (hasInvalidFiles) {
        return;
    }

    std::filesystem::path first_file_path = std::filesystem::absolute(files[0]);
    std::string top_module = first_file_path.stem().string();
    std::string output_dir = "build_" + top_module;

    // Create build directory
    std::filesystem::create_directories(output_dir);

    // Step 1: Synthesis with Yosys
    std::cout << "Synthesizing design with Yosys...\n";
    std::string read_files;
    for (const auto& file : files) {
        read_files += "read_verilog -sv " + file + "; ";
    }
    
    std::string yosys_cmd = "yosys -p \"" + read_files + 
                           "hierarchy -check -top " + top_module + "; " +
                           "proc; flatten; opt; fsm; opt; memory; opt; techmap; opt; " +
                           "write_json " + output_dir + "/" + top_module + ".json\"";
    
    if (int exit_code = executeCommand(yosys_cmd); exit_code != 0) {
        std::cerr << "Error: Yosys synthesis failed\n";
        return;
    }

    // Step 2: Place and Route with nextpnr-xilinx
    std::cout << "Running place and route with nextpnr-xilinx...\n";
    std::string nextpnr_cmd = std::string("nextpnr-xilinx") +
                             " --xdc " + xdc_file +
                             " --json " + output_dir + "/" + top_module + ".json" +
                             " --fasm " + output_dir + "/" + top_module + ".fasm" +
                             " --arch xilinx" +
                             " --family xc7" +
                             " --part xc7a35tcsg324-1";
    
    if (int exit_code = executeCommand(nextpnr_cmd); exit_code != 0) {
        std::cerr << "Error: nextpnr place and route failed\n";
        return;
    }

    // Step 3: Convert FASM to frames
    std::cout << "Converting FASM to frame data...\n";
    std::string fasm_cmd = std::string("fasm2frames") +
                          " --part xc7a35tcsg324-1" +
                          " --db-root /usr/share/f4pga/database" +
                          " --sparse" +
                          " --roi " + output_dir + "/" + top_module + ".fasm" +
                          " -o " + output_dir + "/" + top_module + ".frames";
    
    if (int exit_code = executeCommand(fasm_cmd); exit_code != 0) {
        std::cerr << "Error: FASM to frames conversion failed\n";
        return;
    }

    // Step 4: Convert frames to bitstream
    std::cout << "Generating Xilinx bitstream...\n";
    std::string bit_cmd = std::string("xc7frames2bit") +
                         " --part_file /usr/share/f4pga/database/artix7/xc7a35tcsg324-1/part.yaml" +
                         " --part_name xc7a35tcsg324-1" +
                         " --frm_file " + output_dir + "/" + top_module + ".frames" +
                         " --output_file " + output_dir + "/" + top_module + ".bit";
    
    if (int exit_code = executeCommand(bit_cmd); exit_code != 0) {
        std::cerr << "Error: Bitstream generation failed\n";
        return;
    }

    // Step 5: Program FPGA with OpenOCD
    std::cout << "Programming FPGA...\n";
    std::string openocd_cmd = std::string("openocd") +
                             " -f interface/ftdi/digilent_jtag_hs2.cfg" +
                             " -f target/xc7_ft2232.cfg" +
                             " -c \"init; pld load 0 " + output_dir + "/" + top_module + ".bit; exit\"";
    
    if (int exit_code = executeCommand(openocd_cmd); exit_code != 0) {
        std::cerr << "Error: FPGA programming failed\n";
        return;
    }

    std::cout << "FPGA emulation completed successfully.\n";
    std::cout << "Output files are in directory: " << output_dir << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    
    if (command == "--help") {
        printUsage();
        return 0;
    }

    if (command == "--init") {
        try {
            std::filesystem::path current_dir = std::filesystem::current_path();
            BuildGenerator::initWorkspace(current_dir.string());
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Error initializing workspace: " << e.what() << "\n";
            return 1;
        }
    }
    
    if (command == "--build") {
        if (argc < 3) {
            std::cout << "Error: --build requires at least one input file\n";
            printUsage();
            return 1;
        }

        std::vector<std::string> files;
        for (int i = 2; i < argc; i++) {
            files.push_back(argv[i]);
        }
        
        buildFiles(files);
        return 0;
    }

    if (command == "--test") {
        if (argc != 4) {
            std::cout << "Error: --test requires exactly two files: source and test\n";
            printUsage();
            return 1;
        }

        std::vector<std::string> files = {argv[2]};
        buildFiles(files, argv[3]);
        return 0;
    }

    if (command == "--emulate") {
        if (argc < 5) {
            std::cout << "Error: --emulate requires at least one input file and a constraints file\n";
            std::cout << "Usage: vpm --emulate <file1.sv> [file2.sv ...] --xdc <constraints.xdc>\n";
            return 1;
        }

        std::vector<std::string> files;
        std::string xdc_file;
        bool found_xdc = false;

        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--xdc") {
                if (i + 1 < argc) {
                    xdc_file = argv[++i];
                    found_xdc = true;
                } else {
                    std::cout << "Error: --xdc requires a constraints file\n";
                    return 1;
                }
            } else {
                files.push_back(arg);
            }
        }

        if (!found_xdc) {
            std::cout << "Error: No constraints file specified (use --xdc <file.xdc>)\n";
            return 1;
        }

        if (files.empty()) {
            std::cout << "Error: No input files specified\n";
            return 1;
        }

        emulateFiles(files, xdc_file);
        return 0;
    }

    std::cout << "Error: Unknown command '" << command << "'\n";
    printUsage();
    return 1;
}
