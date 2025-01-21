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

public:
    // Initialize workspace with necessary Bazel dependencies
    static void initWorkspace(const std::string& workspace_path);

    explicit BuildGenerator(const std::filesystem::path& path, 
                          const std::optional<std::filesystem::path>& test_path = std::nullopt);

    // Generate Bazel BUILD file
    void generateBuildFile(const std::string& output_path);

    // Get list of parsed submodules
    const std::vector<std::string>& getSubmodules() const { return submodules; }
};
