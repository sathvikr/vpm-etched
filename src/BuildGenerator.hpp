#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <regex>
#include <filesystem>
#include <optional>
#include <stdexcept>

template<typename Path>
class BuildGenerator {
private:
    Path sv_file_path;
    std::vector<std::string> submodules;
    std::optional<Path> test_file_path;
    
    // Extracts submodule names from SystemVerilog file
    void parseSubmodules() {
        std::ifstream file(sv_file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + std::string(sv_file_path));
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

public:
    // Initialize workspace with necessary Bazel dependencies
    static void initWorkspace(const std::string& workspace_path) {
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

        // Add rules_verilator dependency
        file << "# Verilator rules\n";
        file << "http_archive(\n";
        file << "    name = \"rules_verilator\",\n";
        file << "    urls = [\"https://github.com/hdl/bazel_rules_verilator/archive/refs/heads/main.zip\"],\n";
        file << "    strip_prefix = \"bazel_rules_verilator-main\",\n";
        file << ")\n\n";

        // Add rules_verilog dependency
        file << "# Verilog rules\n";
        file << "http_archive(\n";
        file << "    name = \"rules_verilog\",\n";
        file << "    urls = [\"https://github.com/hdl/bazel_rules_verilog/archive/refs/heads/main.zip\"],\n";
        file << "    strip_prefix = \"bazel_rules_verilog-main\",\n";
        file << ")\n\n";

        // Load and initialize Verilator workspace
        file << "# Initialize Verilator workspace\n";
        file << "load(\"@rules_verilator//verilator:deps.bzl\", \"verilator_deps\")\n";
        file << "verilator_deps()\n\n";

        // Load and initialize Verilog workspace
        file << "# Initialize Verilog workspace\n";
        file << "load(\"@rules_verilog//verilog:deps.bzl\", \"verilog_deps\")\n";
        file << "verilog_deps()\n\n";

        // Register Verilator toolchain
        file << "# Register Verilator toolchain\n";
        file << "load(\"@rules_verilator//verilator:toolchain.bzl\", \"register_verilator_toolchain\")\n";
        file << "register_verilator_toolchain()\n";

        std::cout << "Created WORKSPACE file at: " << workspace_file << "\n";
    }

    explicit BuildGenerator(Path path, std::optional<Path> test_path = std::nullopt) 
        : sv_file_path(path), test_file_path(test_path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("File does not exist: " + std::string(path));
        }
        if (test_file_path && !std::filesystem::exists(*test_file_path)) {
            throw std::runtime_error("Test file does not exist: " + std::string(*test_file_path));
        }
        parseSubmodules();
    }

    // Generate Bazel BUILD file
    void generateBuildFile(const std::string& output_path) {
        std::ofstream build_file(output_path);
        if (!build_file.is_open()) {
            throw std::runtime_error("Failed to create BUILD file: " + output_path);
        }

        // Write Bazel build file header with required rules
        build_file << "load(\"@rules_verilog//verilog:defs.bzl\", \"verilog_library\", \"verilog_test\")\n";
        build_file << "load(\"@rules_verilator//verilator:defs.bzl\", \"verilator_cc_library\", \"verilator_lint\")\n\n";
        
        // Get the module name from the file name
        std::string module_name = std::filesystem::path(sv_file_path).stem().string();
        
        // Write verilog_library rule
        build_file << "verilog_library(\n";
        build_file << "    name = \"" << module_name << "\",\n";
        build_file << "    srcs = [\"" << std::filesystem::path(sv_file_path).filename().string() << "\"],\n";
        
        // Add dependencies if there are submodules
        if (!submodules.empty()) {
            build_file << "    deps = [\n";
            for (const auto& submodule : submodules) {
                build_file << "        \":" << submodule << "\",\n";
            }
            build_file << "    ],\n";
        }
        
        build_file << "    visibility = [\"//visibility:public\"],\n";
        build_file << ")\n\n";

        // Generate Verilator lint target
        build_file << "verilator_lint(\n";
        build_file << "    name = \"" << module_name << "_lint\",\n";
        build_file << "    srcs = [\":" << module_name << "\"],\n";
        if (!submodules.empty()) {
            build_file << "    deps = [\n";
            for (const auto& submodule : submodules) {
                build_file << "        \":" << submodule << "\",\n";
            }
            build_file << "    ],\n";
        }
        build_file << "    warnings_as_errors = True,\n";
        build_file << ")\n\n";

        // Generate Verilator library target
        build_file << "verilator_cc_library(\n";
        build_file << "    name = \"" << module_name << "_verilated\",\n";
        build_file << "    srcs = [\":" << module_name << "\"],\n";
        build_file << "    top_module = \"" << module_name << "\",\n";
        if (!submodules.empty()) {
            build_file << "    deps = [\n";
            for (const auto& submodule : submodules) {
                build_file << "        \":" << submodule << "_verilated\",\n";
            }
            build_file << "    ],\n";
        }
        build_file << "    visibility = [\"//visibility:public\"],\n";
        build_file << ")\n\n";

        // If test file is provided, generate test target
        if (test_file_path) {
            std::string test_name = module_name + "_test";
            build_file << "verilog_test(\n";
            build_file << "    name = \"" << test_name << "\",\n";
            build_file << "    srcs = [\"" << std::filesystem::path(*test_file_path).filename().string() << "\"],\n";
            build_file << "    deps = [\n";
            build_file << "        \":" << module_name << "\",\n";
            build_file << "        \":" << module_name << "_verilated\",\n";
            if (!submodules.empty()) {
                for (const auto& submodule : submodules) {
                    build_file << "        \":" << submodule << "\",\n";
                }
            }
            build_file << "    ],\n";
            build_file << "    toolchain = \"@rules_verilator//verilator:toolchain\",\n";
            build_file << ")\n";
        }
    }

    // Get list of parsed submodules
    const std::vector<std::string>& getSubmodules() const {
        return submodules;
    }
};
