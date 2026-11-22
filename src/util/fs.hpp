// WeBjammin — filesystem helpers: atomic writes, directory listing, path joins.
#pragma once

#include <string>
#include <vector>

namespace wj::fs {

bool read_file(const std::string& path, std::string& out);
// Writes to `path`.tmp, fsyncs, then renames over `path` — so a crash or a
// power cut can never leave a half-written config or score file behind.
bool write_file_atomic(const std::string& path, const std::string& data);
bool exists(const std::string& path);
bool is_directory(const std::string& path);
bool mkdir_p(const std::string& path);
std::string join(const std::string& a, const std::string& b);
std::string dirname(const std::string& path);
std::string basename(const std::string& path);
std::string extension(const std::string& path); // lower-case, without the dot
// Immediate children of `dir` (files and directories), sorted by name.
std::vector<std::string> list_dir(const std::string& dir);
bool remove_file(const std::string& path);
int64_t file_size(const std::string& path);
std::string home_dir();

} // namespace wj::fs
