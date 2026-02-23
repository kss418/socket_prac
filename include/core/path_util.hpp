#pragma once
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace path_util{
    bool is_regular_file(const std::filesystem::path& path);
    std::filesystem::path normalize(const std::filesystem::path& path);
    std::filesystem::path executable_dir(char** argv);
    std::vector<std::filesystem::path> default_search_roots(char** argv);

    std::filesystem::path resolve_root_with_required_files(
        char** argv, std::initializer_list<std::filesystem::path> required_relative_files
    );

    std::filesystem::path resolve_from_root(
        const std::filesystem::path& root, const std::filesystem::path& raw_path
    );

    std::filesystem::path resolve_file_in_default_roots(
        char** argv,
        const std::filesystem::path& relative_file,
        const std::filesystem::path& fallback
    );
}
