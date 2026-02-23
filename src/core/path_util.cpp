#include "core/path_util.hpp"
#include <algorithm>

namespace{
void push_unique_path(
    std::vector<std::filesystem::path>& out, const std::filesystem::path& path
){
    if(path.empty()) return;

    std::filesystem::path normalized = path_util::normalize(path);
    if(std::find(out.begin(), out.end(), normalized) != out.end()) return;
    out.push_back(std::move(normalized));
}

bool root_has_all_required_files(
    const std::filesystem::path& root,
    std::initializer_list<std::filesystem::path> required_relative_files
){
    for(const std::filesystem::path& rel : required_relative_files){
        if(!path_util::is_regular_file(root / rel)) return false;
    }
    return true;
}
}

bool path_util::is_regular_file(const std::filesystem::path& path){
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path path_util::normalize(const std::filesystem::path& path){
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if(!ec) return normalized;
    return path.lexically_normal();
}

std::filesystem::path path_util::executable_dir(char** argv){
    std::error_code ec;
    std::filesystem::path proc_exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if(!ec && !proc_exe.empty()){
        proc_exe = normalize(proc_exe);
        if(proc_exe.has_parent_path()) return proc_exe.parent_path();
    }

    if(argv == nullptr || argv[0] == nullptr || argv[0][0] == '\0') return {};

    std::filesystem::path exe_path = argv[0];
    ec.clear();
    if(exe_path.is_relative()){
        std::filesystem::path cwd = std::filesystem::current_path(ec);
        if(ec) return {};
        exe_path = cwd / exe_path;
    }

    exe_path = normalize(exe_path);
    if(exe_path.has_parent_path()) return exe_path.parent_path();
    return {};
}

std::vector<std::filesystem::path> path_util::default_search_roots(char** argv){
    std::vector<std::filesystem::path> roots;

    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if(!ec){
        push_unique_path(roots, cwd);
        if(cwd.has_parent_path()) push_unique_path(roots, cwd.parent_path());
    }

    std::filesystem::path exe_dir = executable_dir(argv);
    if(!exe_dir.empty()){
        push_unique_path(roots, exe_dir);
        if(exe_dir.has_parent_path()) push_unique_path(roots, exe_dir.parent_path());
    }

    return roots;
}

std::filesystem::path path_util::resolve_root_with_required_files(
    char** argv, std::initializer_list<std::filesystem::path> required_relative_files
){
    std::vector<std::filesystem::path> roots = default_search_roots(argv);

    for(const auto& root : roots){
        if(root_has_all_required_files(root, required_relative_files)) return root;
    }

    if(!roots.empty()) return roots.front();
    return ".";
}

std::filesystem::path path_util::resolve_from_root(
    const std::filesystem::path& root, const std::filesystem::path& raw_path
){
    if(raw_path.is_absolute()) return raw_path;
    return root / raw_path;
}

std::filesystem::path path_util::resolve_file_in_default_roots(
    char** argv,
    const std::filesystem::path& relative_file,
    const std::filesystem::path& fallback
){
    std::vector<std::filesystem::path> roots = default_search_roots(argv);
    for(const auto& root : roots){
        std::filesystem::path candidate = root / relative_file;
        if(path_util::is_regular_file(candidate)) return candidate;
    }
    return fallback;
}
