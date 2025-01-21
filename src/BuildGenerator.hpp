#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <regex>
#include <filesystem>
#include <optional>
#include <stdexcept>

class BuildGenerator {
private:
    std::filesystem::path sv_file_path;
    std::vector<std::string> submodules;
    std::optional<std::filesystem::path> test_file_path;
    
    // Extracts submodule names from SystemVerilog file
    void parseSubmodules();
    
    // Generate a regular BUILD file for the SystemVerilog module
    void generateRegularBuildFile(std::ofstream& build_file, const std::string& module_name, const std::filesystem::path& output_path);
    
    // Generate a BUILD file for testing with Verilator testbench
    void generateTestBuildFile(std::ofstream& build_file, const std::string& module_name, const std::filesystem::path& output_path);

public:
    // Initialize workspace with necessary Bazel dependencies
    static void initWorkspace(const std::string& workspace_path);

    explicit BuildGenerator(const std::filesystem::path& path, 
                          const std::optional<std::filesystem::path>& test_path = std::nullopt);

    // Generate appropriate Bazel BUILD file based on whether it's a test or not
    void generateBuildFile(const std::string& output_path);

    // Get list of parsed submodules
    const std::vector<std::string>& getSubmodules() const { return submodules; }
};
