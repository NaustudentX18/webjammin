#include "util/fs.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace wj::fs {

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool write_file_atomic(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) {
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        off += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp.c_str());
        return false;
    }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    // Also flush the directory entry so the rename survives a power cut.
    const std::string dir = dirname(path);
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
}

bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool is_directory(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool mkdir_p(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    size_t i = 0;
    const bool absolute = path[0] == '/';
    if (absolute) {
        cur = "/";
        i = 1;
    }
    while (i <= path.size()) {
        size_t slash = path.find('/', i);
        const std::string part =
            path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (!cur.empty() && cur.back() != '/') cur += '/';
            cur += part;
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return true;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string dirname(const std::string& path) {
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return path.substr(0, p);
}

std::string basename(const std::string& path) {
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return path;
    return path.substr(p + 1);
}

std::string extension(const std::string& path) {
    std::string base = basename(path);
    size_t p = base.find_last_of('.');
    if (p == std::string::npos || p == 0) return "";
    std::string ext = base.substr(p + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return ext;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        out.push_back(name);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool remove_file(const std::string& path) { return ::unlink(path.c_str()) == 0; }

int64_t file_size(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<int64_t>(st.st_size);
}

std::string home_dir() {
    const char* h = ::getenv("HOME");
    return h ? std::string(h) : std::string("/root");
}

} // namespace wj::fs
