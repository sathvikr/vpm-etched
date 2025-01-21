#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include "BuildGenerator.hpp"

namespace {
    // Execute a shell command and return its exit code
    int executeCommand(const std::string& command) {
        std::cout << "Executing: " << command << "\n";
        return std::system(command.c_str());
    }
}

void printUsage() {
    std::cout << "Usage: vpm [options] [files...]\n"
              << "Options:\n"
              << "  --init                            Initialize Bazel workspace\n"
              << "  --build <file1.sv> [file2.sv ...]  Build specified SystemVerilog files\n"
              << "  --test <file.sv> <test.cpp>        Build with test file\n"
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

    std::cout << "Error: Unknown command '" << command << "'\n";
    printUsage();
    return 1;
}
