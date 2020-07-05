#include "strace.h"
#include "error_prints.h"
#include "utils.h"

#include <elf.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#if defined(__x86_64__) || defined(_M_X64)
#define X64 1
#elif defined(__arm64__) || defined(__aarch64__)
#define A64 1
#else
#error "Unknown architecture"
#endif

namespace cache_dash_h {

typedef unsigned long kernel_ulong_t;
typedef std::pair<kernel_ulong_t, std::string> syscall_record;

/* Read *len* bytes from remote address *raddr* in child process with pid *pid*
   and copy them to local address *laddr*
*/
ssize_t
vm_read_mem(const pid_t pid, void* const laddr, const kernel_ulong_t raddr, const size_t len) {
    const unsigned long truncated_raddr = raddr;
    const struct iovec local = {.iov_base = laddr, .iov_len = len};
    const struct iovec remote = {.iov_base = (void*)truncated_raddr, .iov_len = len};

    const ssize_t rc = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (rc < 0 && errno == ENOSYS) {
        perror_func_msg_and_die("process_vm_readv not supported");
    }

    return rc;
}

/* Read a null-terminated string from the memory of a child process with
pid *pid* of maximum length *len*, starting at remote address *addr*, and
copy it to the local buffer *laddr*
*/
int umovestr(const pid_t pid, kernel_ulong_t addr, unsigned int len, char* laddr) {
    const size_t page_size = sysconf(_SC_PAGE_SIZE);
    const size_t page_mask = page_size - 1;
    unsigned int nread = 0;

    while (len) {
        /*
         * Don't cross pages, otherwise we can get EFAULT
         * and fail to notice that terminating NUL lies
         * in the existing (first) page.
         */
        unsigned int chunk_len = len > page_size ? page_size : len;
        unsigned int end_in_page = (addr + chunk_len) & page_mask;
        if (chunk_len > end_in_page) /* crosses to the next page */
            chunk_len -= end_in_page;

        int r = vm_read_mem(pid, laddr, addr, chunk_len);
        if (r > 0) {
            char* nul_addr = (char*)memchr((void*)laddr, '\0', r);

            if (nul_addr)
                return (nul_addr - laddr) + 1;
            addr += r;
            laddr += r;
            nread += r;
            len -= r;
            continue;
        }

        perror_msg_and_die("process_vm_readv failed");
        return -1;
    }

    return 0;
}

struct syscall_args_t {
#if X64
    syscall_args_t(pid_t pid, const user_regs_struct& regs)
        : pid{pid}
        , num{regs.orig_rax}
        , p0{regs.rdi}
        , p1{regs.rsi}
        , p2{regs.rdx}
        , returnval{regs.rax} {}
#elif A64
    syscall_args_t(pid_t pid, const user_regs_struct& regs)
        : pid{pid}
        , num{regs.regs[8]}
        , p0{regs.regs[0]}
        , p1{regs.regs[1]}
        , p2{regs.regs[2]}
        , returnval{regs.regs[0]} {}
#endif

    const int pid;
    const unsigned long long num;
    const unsigned long long p0;
    const unsigned long long p1;
    const unsigned long long p2;
    const unsigned long long returnval;
};

void process_chdir(syscall_args_t& call, std::vector<syscall_record>& records) {
    char path[PATH_MAX];
    int num_bytes = umovestr(call.pid, call.p0, sizeof(path), path);
    if (num_bytes <= 0)
        error_msg_and_die("failed to read memory");
    records.push_back({call.num, std::string(path)});
    return;
}

void process_openat(syscall_args_t& call, std::vector<syscall_record>& records) {
    if (call.p2 & O_DIRECTORY) {
        // if openat was passed with O_DIRECTORY
        // then it's not opening a file
        return;
    }
    if (call.p2 & O_WRONLY) {
        return;
    }
    if ((-call.returnval) & ENOENT) {
        return;
    }

    char path[PATH_MAX];
    int num_bytes = umovestr(call.pid, call.p1, sizeof(path), path);
    if (num_bytes <= 0)
        error_msg_and_die("failed to read memory");
    records.push_back({call.num, std::string(path)});
    return;
}

void process_open(syscall_args_t& call, std::vector<syscall_record>& records) {
    if (call.p1 & O_WRONLY) {
        return;
    }
    if (-call.returnval & ENOENT) {
        return;
    }

    char path[PATH_MAX];
    int num_bytes = umovestr(call.pid, call.p0, sizeof(path), path);
    if (num_bytes <= 0)
        error_msg_and_die("failed to read memory");
    records.push_back({call.num, std::string(path)});
    return;
}

int trace_child(pid_t pid, int* exit_status, std::vector<syscall_record>& records) {
    int status;
    struct iovec iov;
    struct user_regs_struct regs;
    bool start = true;

    while (1) {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            *exit_status = WEXITSTATUS(status);
            return -1;
        }

        iov.iov_base = &regs;
        iov.iov_len = sizeof(regs);
        if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov) == -1) {
            error_msg_and_die("ptrace failed to get registers");
        }
        syscall_args_t syscall(pid, regs);

        switch (syscall.num) {
        case SYS_chdir:
            if ((start = !start) == true)
                process_chdir(syscall, records);
            break;
        case SYS_openat:
            if ((start = !start) == true)
                process_openat(syscall, records);
            break;
#if defined(SYS_open)
        case SYS_open:
            if ((start = !start) == true)
                process_open(syscall, records);
            break;
#endif
        }

        if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
            perror_msg_and_die("Can't trace\n");
        }
    }
}

/* Fork and exec a child process, and return the stdout of the child process
   as well as the list of all of the files it opened.
*/
std::tuple<std::string, std::string, int>
exec_and_record_opened_files(std::vector<std::string>& cmd,
                             std::function<void(std::string const&)> open_callback) {
    int exit_status = -1;
    pid_t pid = 0;
    std::string curdir = path::getcwd();

    char stdout_fn[] = "/tmp/cache-dash-h-stdout-XXXXXX";
    char stderr_fn[] = "/tmp/cache-dash-h-stderr-XXXXXX";
    int stdout_fd = mkstemp(stdout_fn);
    int stderr_fd = mkstemp(stderr_fn);
    if (unlink(stdout_fn) < 0)
        perror_msg_and_die("Can't unlink");
    if (unlink(stderr_fn) < 0)
        perror_msg_and_die("Can't unlink");

    if (stdout_fd == -1)
        perror_msg_and_die("Can't open tempfile");
    if (stderr_fd == -1)
        perror_msg_and_die("Can't open tempfile");

    if ((pid = fork()) == -1)
        perror_msg_and_die("Can't fork");

    if (pid == 0) {
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);
        close(stdout_fd);
        close(stderr_fd);

        c_cmdline c_style(cmd);
        ptrace(PTRACE_TRACEME);
        kill(getpid(), SIGSTOP);
        execvp(c_style.argv[0], c_style.c_argv());
        perror_msg_and_die("Can't exec '%s'", c_style.argv[0]);

    } else {
        std::vector<syscall_record> records;
        trace_child(pid, &exit_status, records);
        for (auto const& r : records) {
            kernel_ulong_t syscall_num = r.first;
            const std::string& syscall_path = r.second;
            if (syscall_num == SYS_chdir) {
                if (path::isabs(syscall_path)) {
                    curdir = syscall_path;
                } else {
                    curdir = path::realpath(curdir + "/" + syscall_path);
                }
            } else {
                // assert(syscall_num == SYS_open || syscall_num == SYS_openat);
                if (path::isabs(syscall_path)) {
                    open_callback(syscall_path);
                } else {
                    open_callback(path::realpath(curdir + "/" + syscall_path));
                }
            }
        }
    }

    char buffer[4096];
    std::string stdout_, stderr_;
    lseek(stdout_fd, 0, SEEK_SET);
    while (int nread = read(stdout_fd, buffer, sizeof(buffer))) {
        stdout_.append(buffer, nread);
    }
    lseek(stderr_fd, 0, SEEK_SET);
    while (int nread = read(stderr_fd, buffer, sizeof(buffer))) {
        stderr_.append(buffer, nread);
    }
    return std::make_tuple(stdout_, stderr_, exit_status);
}

}; // namespace cache_dash_h
