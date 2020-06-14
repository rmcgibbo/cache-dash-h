#include "database.h"
#include "error_prints.h"
#include "strace.h"
#include "utils.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <unistd.h>

using namespace std;
using namespace cache_dash_h;
extern int optind;
extern char* optarg;

std::vector<std::string> load_stable_paths() {
    char* stablepaths = getenv("CACHEDASHH_STABLEPATH");
    if (stablepaths == NULL) {
        return {
            "/usr/", "/etc/",  "/lib/",      "/lib64/", "/dev/",  "/proc/",
            "/sys/", "/boot/", "/nix/store", "/gdn/",   "/proj/",
        };
    } else {
        std::vector<std::string> paths;
        str::split(std::string(stablepaths), ":",
                   [&](const std::string& p) { paths.push_back(p); });
        return paths;
    }
}

struct options_t {
    bool verbose{false};
    std::string db_path{"/tmp/cache-dash-h.db"};
    int length{-1};
    std::vector<std::string> cmd;
};

options_t parse_our_cmdline(std::vector<std::string> cmd) {

    auto print_usage_and_die = [&]() {
        printf(R"(usage: %s [-h] [-v] [-l LENGTH] [-c CACHE] COMMAND [ARGS]

optional arguments:
    -h, --help          show this help message and exit
    -n NUM              If supplied, cache the text based on only the
                        first NUM arguments to the inner command.
                        (default: uses the entire inner command)
    -p CACHE --path CACHE
                        Path to cache. (default: "/tmp/cache-dash-h.db")
                        If CACHE starts with $ORIGIN0, it will be expanded
                        to the directory conaining the inner command. If CACHE
                        startswith $ORIGIN1, it will be expanded to the
                        directory containing the first argument to the inner
                        command.
    -v, --verbose       Verbose mode

required arguments:
    COMMAND [ARGS...]
        Command to run, and arguments to pass to it

example:
    $ %s python slow-script.py --help

)",
               program_invocation_short_name, program_invocation_short_name);
        exit(EXIT_SUCCESS);
    };

    options_t options;
    char* envvar = getenv("CACHEDASHH_DB");
    if (envvar != NULL) {
        options.db_path = std::string(envvar);
    }

    if (cmd.size() > 1 && cmd[1].find(' ') != std::string::npos) {
        std::vector<std::string> newcmd{cmd[0]};
        str::split_whitespace(cmd[1], [&](const std::string& s) { newcmd.push_back(s); });
        for (size_t i = 2; i < cmd.size(); i++)
            newcmd.push_back(cmd[i]);
        cmd = newcmd;
    }
    // printf("\n");
    // for (auto c : cmd) {
    //     printf("'%s'\n", c.c_str());
    // }
    static const char optstring[] = "+hvn:p:";
    static struct option longopts[] = {{"help", no_argument, 0, 'h'},
                                       {"num", optional_argument, 0, 'n'},
                                       {"path", optional_argument, 0, 'p'},
                                       {"verbose", optional_argument, 0, 'v'},
                                       {0, 0, 0, 0}};

    int lopt_idx = -1;
    int c;

    c_cmdline c_style(cmd);
    while ((c = getopt_long(c_style.argc, c_style.c_argv(), optstring, longopts, &lopt_idx)) !=
           EOF) {
        switch (c) {
        case 'h':
            print_usage_and_die();
            break;
        case 'n':
            if (sscanf(optarg, "%d", &options.length) != 1) {
                error_msg_and_die("error: argument -l/--length: invalid int value: '%s'", optarg);
            }
            break;
        case 'p':
            options.db_path = std::string(optarg);
            break;
        case 'v':
            options.verbose = true;
            break;
        default:
            print_usage_and_die();
        }
    }

    // copy the remaining unprocessed arguments into the options struct
    // this is the subcommand that we're going to exec.
    for (size_t i = optind; i < cmd.size(); i++) {
        options.cmd.push_back(cmd[i]);
    }
    if (options.cmd.size() == 0)
        print_usage_and_die();

    // expand first argument
    options.cmd[0] = find_in_path(options.cmd[0]);
    if (str::startswith(options.db_path, "$ORIGIN0")) {
        options.db_path = str::replace(options.db_path, "$ORIGIN0", path::dirname(options.cmd[0]));
    } else if (str::startswith(options.db_path, "$ORIGIN1") && options.cmd.size() > 1) {
        options.db_path = str::replace(options.db_path, "$ORIGIN1", path::dirname(options.cmd[1]));
    }

    return options;
}

int main(int argc, char** argv) {
    std::vector<std::string> cmd;
    for (int i = 0; i < argc; i++)
        cmd.push_back(argv[i]);

    auto options = parse_our_cmdline(cmd);
    bool have_dash_h = cmd_has_dash_h(options.cmd);

    if (!have_dash_h) {
        c_cmdline c_style(options.cmd);
        execvp(c_style.argv[0], c_style.c_argv());
        perror_msg_and_die("Can't exec '%s'", c_style.argv[0]);
    }

    auto stable_paths = load_stable_paths();
    auto ignore_file = [&](const std::string& path) {
        for (auto const& p : stable_paths) {
            if (str::startswith(path, p)) {
                return true;
            }
        }
        return false;
    };

    std::unique_ptr<Database> db;
    try {
        db.reset(new Database(options.db_path, options.verbose));
    } catch (const SQLite::Exception& e) {
        perror_msg_and_die("Can't access %s", options.db_path.c_str());
    }
    auto cmdhash = hash_command_line(options.length, options.cmd);

    // See if we already have the help text. If so, print it and exit
    db->QueryAndPrintHelpAndExitIfPossible(cmdhash);

    if (db->is_readonly_) {
        // if the database is read only and we don't have the cmdline in
        // the cache then there's no point tracing the process, just run
        // it.
        c_cmdline c_style(options.cmd);
        execvp(c_style.argv[0], c_style.c_argv());
        perror_msg_and_die("Can't exec '%s'", c_style.argv[0]);
    }

    // exec process under tracing, gather -h, and store it
    std::vector<std::string> deps;
    if (!ignore_file(options.cmd[0]))
        deps.push_back(options.cmd[0]);

    auto out = exec_and_record_opened_files(options.cmd, [&](const std::string& path) {
        if (ignore_file(path))
            return;
        if (options.verbose)
            printf("%s: loaded file: %s\n", program_invocation_short_name, path.c_str());
        deps.push_back(path);
    });

    printf("%s", out.first.c_str());
    db->Insert(options.cmd, cmdhash, out, deps);
    if (options.verbose) {
        printf("%s: Saved to cache '%s'\n", program_invocation_short_name, options.db_path.c_str());
    }
    exit(out.second);
}
