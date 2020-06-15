#pragma once
#include <functional>
#include <limits.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace cache_dash_h {

struct c_cmdline {
    int argc;
    char const* argv[_POSIX_ARG_MAX + 1];

    c_cmdline(const std::vector<std::string>& cmd)
        : argc(cmd.size())
        , cmd_(cmd) {
        for (size_t i = 0; i < cmd_.size(); i++) {
            argv[i] = cmd_[i].c_str();
        }
        argv[cmd.size()] = NULL;
    }

    char* const* c_argv() const { return const_cast<char* const*>(argv); }

  private:
    const std::vector<std::string> cmd_;
};

namespace str {
std::string replace(const std::string& s, const std::string& from, const std::string& to);

bool startswith(std::string const& str, std::string const& prefix);

void split(const std::string& s,
           const std::string& delimiter,
           std::function<void(const std::string&)> f);

void split_whitespace(const std::string& s, std::function<void(const std::string&)> f);

std::string join(const std::vector<std::string>& vec, const char* delim);

} // namespace str

bool cmd_has_dash_h(const std::vector<std::string>& cmd);

std::string hash_command_line(int length, const std::vector<std::string>& cmd);

std::string hash_filename(const std::string& fn, bool allow_ENOENT);

std::string find_in_path(const std::string& filename);

namespace path {
std::string getcwd();

std::string realpath(const std::string& path);

std::string dirname(const std::string& path);

bool isabs(const std::string& path);

}; // namespace path

}; // namespace cache_dash_h