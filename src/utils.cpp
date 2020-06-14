#include "utils.h"
#include "SpookyV2.h"
#include "error_prints.h"
#include "unistd.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <inttypes.h>
#include <iterator>
#include <libgen.h>
#include <linux/limits.h>
#include <sstream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>

namespace cache_dash_h {
static const std::vector<std::vector<std::string>> HELP_FLAGS{
    {"-h", "--help"}, {"-showparams", "--showparams"}, {"-hh", "--help-all"}};

std::string str::replace(const std::string& s, const std::string& from, const std::string& to) {
    size_t start_pos = s.find(from);
    if (start_pos == std::string::npos)
        return s;
    std::string news = s;
    news.replace(start_pos, from.length(), to);
    return news;
}
bool str::startswith(std::string const& str, std::string const& prefix) {
    return prefix.length() <= str.length() && std::equal(prefix.begin(), prefix.end(), str.begin());
}

void str::split(const std::string& s,
                const std::string& sep,
                std::function<void(const std::string&)> f) {

    std::string::size_type i = 0;
    std::string::size_type j = 0;
    auto len = s.size();
    auto n = sep.size();

    while (i + n <= len) {
        if (s[i] == sep[0] && s.substr(i, n) == sep) {

            f(s.substr(j, i - j));
            i = j = i + n;
        } else {
            i++;
        }
    }

    f(s.substr(j, len - j));
}

void str::split_whitespace(const std::string& s, std::function<void(const std::string&)> f) {
    std::string::size_type i = 0;
    std::string::size_type j = 0;
    std::string::size_type len = s.size();

    for (i = j = 0; i < len;) {
        while (i < len && ::isspace(s[i]))
            i++;
        j = i;

        while (i < len && !::isspace(s[i]))
            i++;

        if (j < i) {
            f(s.substr(j, i - j));

            while (i < len && ::isspace(s[i]))
                i++;
            j = i;
        }
    }
    if (j < len) {
        f(s.substr(j, len - j));
    }
}

std::string str::join(const std::vector<std::string>& vec, const char* delim) {
    std::stringstream res;
    std::copy(vec.begin(), vec.end(), std::ostream_iterator<std::string>(res, delim));
    return res.str();
}

bool cmd_has_dash_h(const std::vector<std::string>& cmd) {
    bool have_dash_h = false;
    for (auto const& item : cmd) {

        for (auto const& flaglist : HELP_FLAGS) {
            for (auto const& flag : flaglist) {
                if (item == flag) {
                    have_dash_h = true;
                    break;
                }
            }
        }
    }
    return have_dash_h;
}

std::string hexdigest(SpookyHash& spooky) {
    uint64_t hash1;
    uint64_t hash2;
    spooky.Final(&hash1, &hash2);
    char buf[33];
    sprintf(buf, "%016" PRIx64 "%016" PRIx64, hash1, hash2);
    return std::string(buf, 32);
}

std::string hash_command_line(int length, const std::vector<std::string>& cmd) {
    SpookyHash spooky;
    spooky.Init(0, 0);

    if (length < 0)
        length = static_cast<int>(cmd.size());
    length = std::min(length, static_cast<int>(cmd.size()));
    int i = 0;
    for (i = 0; i < length; i++) {
        spooky.Update(static_cast<const void*>(cmd[i].data()), cmd[i].size());
    }
    for (; i < static_cast<int>(cmd.size()); i++) {
        for (auto const& flaglist : HELP_FLAGS) {

            bool any_flaglist = false;
            for (auto const& f : flaglist) {
                if (cmd[i] == f) {
                    any_flaglist = true;
                    break;
                }
            }
            if (any_flaglist) {
                spooky.Update(static_cast<const void*>(flaglist[0].data()), flaglist[0].size());
            }
        }
    }

    return hexdigest(spooky);
}

std::string hash_filename(const std::string& fn) {
    // Get the size of the file by its file descriptor
    auto get_size_by_fd = [&](int fd) {
        struct stat statbuf;
        if (fstat(fd, &statbuf) < 0) {
            perror_msg_and_die("Can't stat: '%s'", fn.c_str());
        }
        return statbuf.st_size;
    };
    auto file_descript = open(fn.c_str(), O_RDONLY);
    if (file_descript < 0) {
        perror_msg_and_die("Can't open: '%s'", fn.c_str());
    }
    auto file_size = get_size_by_fd(file_descript);

    SpookyHash spooky;
    spooky.Init(0, 0);

    if (file_size > 0) {
        auto file_buffer = mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0);
        spooky.Update(file_buffer, file_size);

        if (munmap(file_buffer, file_size) < 0) {
            perror_msg_and_die("Can't unmap: '%s'", fn.c_str());
        }
    }

    if (close(file_descript) < 0) {
        perror_msg_and_die("Can't close: '%s'", fn.c_str());
    }

    return hexdigest(spooky);
}

std::string find_in_path(const std::string& filename_) {
    struct stat statbuf;
    const char* filename = filename_.c_str();
    char pathname[PATH_MAX+1];

    size_t filename_len = strlen(filename);

    if (filename_len > sizeof(pathname) - 1) {
        errno = ENAMETOOLONG;
        perror_msg_and_die("exec");
    }
    if (strchr(filename, '/')) {
        strncpy(pathname, filename, PATH_MAX);
    } else {
        const char* path;
        size_t m, n, len;

        for (path = getenv("PATH"); path && *path; path += m) {
            const char* colon = strchr(path, ':');
            if (colon) {
                n = colon - path;
                m = n + 1;
            } else
                m = n = strlen(path);
            if (n == 0) {
                if (!getcwd(pathname, PATH_MAX))
                    continue;
                len = strlen(pathname);
            } else if (n > sizeof(pathname) - 1)
                continue;
            else {
                strncpy(pathname, path, n);
                len = n;
            }
            if (len && pathname[len - 1] != '/')
                pathname[len++] = '/';
            if (filename_len + len > sizeof(pathname) - 1)
                continue;
            strncpy(pathname + len, filename, PATH_MAX-len);
            if (stat(pathname, &statbuf) == 0 &&
                /* Accept only regular files
                   with some execute bits set.
                   XXX not perfect, might still fail */
                S_ISREG(statbuf.st_mode) && (statbuf.st_mode & 0111))
                break;
        }
        if (!path || !*path)
            pathname[0] = '\0';
    }
    if (stat(pathname, &statbuf) < 0) {
        perror_msg_and_die("Can't stat '%s'", filename);
    }

    return std::string(pathname);
}

std::string path::getcwd() {
    char temp[PATH_MAX];
    return (::getcwd(temp, sizeof(temp)) ? std::string(temp) : std::string(""));
}

std::string path::realpath(const std::string& path) {
    char temp[PATH_MAX];
    return (::realpath(path.c_str(), temp) ? std::string(temp) : std::string(""));
}

std::string path::dirname(const std::string& path) {
    std::vector<char> cstr(path.c_str(), path.c_str() + path.size() + 1);
    return ::dirname(&cstr[0]);
}

bool path::isabs(const std::string& path) { return (path.size() > 0 && path[0] == '/'); }
} // namespace cache_dash_h