/* ============================================================================
 *
 *     #define BESTASSERT_IMPLEMENTATION
 *     #include "bestassert.h"
 *
 * Opt-out:
 *     BESTASSERT_NO_SIGNAL_HANDLERS
 *
 * ========================================================================== */
#ifndef BESTASSERT_H
#define BESTASSERT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BESTASSERT_ACTION_CONTINUE      = 0,
    BESTASSERT_ACTION_TERMINATE     = 1,
    BESTASSERT_ACTION_DEBUGGER      = 2,
    BESTASSERT_ACTION_IGNORE_ALWAYS = 3
} bestassert_action_t;

bestassert_action_t bestassert_handle(
    const char *file, int line, const char *function,
    const char *expr, int count, const char *signal_name, int is_fatal);

#ifdef __cplusplus
}
#endif

#define BESTASSERT(expr)                                                                      \
    do {                                                                                      \
        static atomic_int _ba_count  = 0;                                                     \
        static int _ba_ignore = 0;                                                            \
        if (!(expr) && !_ba_ignore) {                                                         \
            int _ba_new = atomic_fetch_add_explicit(&_ba_count, 1, memory_order_relaxed) + 1; \
            bestassert_action_t _ba_act = bestassert_handle(                                  \
                __FILE__, __LINE__, __PRETTY_FUNCTION__,                                      \
                #expr, _ba_new, NULL, 0);                                                     \
            if (_ba_act == BESTASSERT_ACTION_TERMINATE)      exit(3);                         \
            if (_ba_act == BESTASSERT_ACTION_IGNORE_ALWAYS)  _ba_ignore = 1;                  \
        }                                                                                     \
    } while (0)

#define BESTPANIC()                                                                           \
    do {                                                                                      \
        bestassert_action_t _ba_act = bestassert_handle(                                      \
            __FILE__, __LINE__, __PRETTY_FUNCTION__,                                          \
            "<panic>", 1, NULL, 1);                                                           \
        if (_ba_act == BESTASSERT_ACTION_TERMINATE)      exit(3);                             \
        puts("Unable to continue from panic");                                                \
        exit(3);                                                                              \
    } while (0)

#ifdef assert
    #undef assert
#endif
#ifdef NDEBUG
    #define assert(expr) ((void)0)
#else
    #define assert(expr) BESTASSERT(expr)
#endif

#ifdef panic
    #undef panic
#endif
#ifdef NDEBUG
    #define panic() __builtin_unreachable();
#else
    #define panic() BESTPANIC()
#endif

#ifdef BESTASSERT_IMPLEMENTATION

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <io.h>

    static SRWLOCK g_ba_lock = SRWLOCK_INIT;
    #define BA_LOCK()   AcquireSRWLockExclusive(&g_ba_lock)
    #define BA_UNLOCK() ReleaseSRWLockExclusive(&g_ba_lock)
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <errno.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <threads.h>

    static mtx_t g_ba_lock;
    static once_flag g_ba_lock_once = ONCE_FLAG_INIT;
    static void ba_lock_init_fn(void) { mtx_init(&g_ba_lock, mtx_plain); }
    #define BA_LOCK()   do { call_once(&g_ba_lock_once, ba_lock_init_fn); mtx_lock(&g_ba_lock); } while (0)
    #define BA_UNLOCK() mtx_unlock(&g_ba_lock)
#endif

#define BA_TEMP_BUF 1024

#define BA_RED     "\033[1;31m"
#define BA_GREEN   "\033[1;32m"
#define BA_YELLOW  "\033[1;33m"
#define BA_MAGENTA "\033[1;35m"
#define BA_CYAN    "\033[1;36m"
#define BA_BOLD    "\033[1m"
#define BA_RESET   "\033[0m"

#ifdef _WIN32
static void ba_exe_part(char *dst, size_t dst_sz, const char *path)
{
    for (const char *p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            snprintf(dst, dst_sz, "\"%s\"", path);
            return;
        }
    }
    snprintf(dst, dst_sz, "%s", path);
}
#endif

static int ba_mktemp(char *buf)
{
#ifdef _WIN32
    char dir[MAX_PATH];
    if (!GetTempPathA(sizeof(dir), dir)) return -1;
    if (!GetTempFileNameA(dir, "bsa", 0, buf)) return -1;
    return 0;
#else
    strcpy(buf, "/tmp/bestassert_XXXXXX");
    int fd = mkstemp(buf);
    if (fd < 0) return -1;
    close(fd);
    return 0;
#endif
}
static int ba_write_py(const char *path, unsigned long pid, const char *add_code)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f,
        "import lldb\n"
        "target = lldb.debugger.CreateTarget('')\n"
        "target.AttachToProcessWithID(lldb.SBListener(), %lu, lldb.SBError())\n"
        "process = target.GetProcess()\n"
        "found = False\n"
        "for ti in range(process.GetNumThreads()):\n"
        "    if found:\n"
        "        break\n"
        "    t = process.GetThreadAtIndex(ti)\n"
        "    for i in range(t.GetNumFrames()):\n"
        "        name = t.GetFrameAtIndex(i).GetFunctionName() or ''\n"
        "        if 'bestassert_handle' in name:\n"
        "            process.SetSelectedThread(t)\n"
        "            if i + 1 < t.GetNumFrames():\n"
        "                t.SetSelectedFrame(i + 1)\n"
        "            found = True\n"
        "            break\n"
        "if not found:\n"
        "    print('\\033[33mWARING: Cannot find symbols in any thread backtrace. The program is probably compiled without debug info. Auto detecting frame was disabled\\033[0m')\n"
        "%s\n",
        pid, add_code);
    fclose(f);
    return 0;
}



static int ba_write_cmd(const char *path, const char *py_path, const char *extra)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "script exec(open(r'%s').read())\n", py_path);
    fputs(extra, f);
    fclose(f);
    return 0;
}

static void ba_unlink(const char *path)
{
    if (!path || !path[0]) return;
#ifdef _WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

static void ba_lldb_print_backtrace(void)
{
    fflush(stdout);
    fflush(stderr);

    char py_path[BA_TEMP_BUF]  = {0};
    char cmd_path[BA_TEMP_BUF] = {0};

#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = getpid();
#endif

    if (ba_mktemp(py_path) < 0) return;
    if (ba_write_py(py_path, pid, 
                     "t = lldb.debugger.GetSelectedTarget().GetProcess().GetSelectedThread()\n"
                     "print(f\"\\n* thread #{t.GetIndexID()}, name = '{t.GetName() or 'unnamed'}'\")\n"
                     "frames = list(process.GetSelectedThread()); idx = next((i for i, f in enumerate(frames) if 'bestassert_handle' in (f.GetFunctionName() or '')), -1); print('\\n'.join(f'    {str(f)}' for f in frames[idx+1:]), flush=True)\n"
                     "lldb.debugger.HandleCommand(\"fr se -r 0\")\n"
                     "lldb.SBDebugger.SetInternalVariable(\"auto-confirm\", \"true\", lldb.debugger.GetInstanceName())\n") < 0) goto done;
    if (ba_mktemp(cmd_path) < 0) goto done;
    if (ba_write_cmd(cmd_path, py_path, "quit") < 0) {
        goto done;
     }

#ifdef _WIN32
    {
        char lldb_path[MAX_PATH] = {0};
        if (SearchPathA(NULL, "lldb.exe", NULL, sizeof(lldb_path), lldb_path, NULL) == 0) {
            strcpy(lldb_path, "lldb.exe");
        }

        char exe_part[MAX_PATH + 4];
        ba_exe_part(exe_part, sizeof(exe_part), lldb_path);

        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "%s -e -s \"%s\"", exe_part, cmd_path);

        STARTUPINFOA si = {0};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "bestassert: failed to launch lldb (error %lu)\n", (unsigned long)GetLastError());
            goto done;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    {
        pid_t child = fork();
        if (child == 0) {
            execlp("lldb", "lldb", "-e", "-s", cmd_path, (char *)NULL);
            _exit(127);
        }
        if (child < 0) {
            fprintf(stderr, "bestassert: failed to fork for lldb\n");
            goto done;
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            fprintf(stderr, "bestassert: lldb not found in PATH\n");
        }
    }
#endif

done:
    ba_unlink(py_path);
    ba_unlink(cmd_path);
}

static void ba_launch_debugger(void)
{
    fflush(stdout);
    fflush(stderr);

    char py_path[BA_TEMP_BUF]  = {0};
    char cmd_path[BA_TEMP_BUF] = {0};

    #ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
    #else
    unsigned long pid = getpid();
    #endif

    if (ba_mktemp(py_path) < 0) return;
    if (ba_write_py(py_path, pid, 
                        "lldb.debugger.HandleCommand(\"bt\")\n"
                        "lldb.debugger.HandleCommand(\"fr se -r 0\")\n") < 0) goto done;
    if (ba_mktemp(cmd_path) < 0) goto done;
    if (ba_write_cmd(cmd_path, py_path, "") < 0) {
        goto done;
    }

#ifdef _WIN32
    {
        char lldb_path[MAX_PATH] = {0};
        if (SearchPathA(NULL, "lldb.exe", NULL, sizeof(lldb_path), lldb_path, NULL) == 0) {
            strcpy(lldb_path, "lldb.exe");
        }

        char exe_part[MAX_PATH + 4];
        ba_exe_part(exe_part, sizeof(exe_part), lldb_path);

        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "%s -s \"%s\"", exe_part, cmd_path);

        STARTUPINFOA si = {0};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "bestassert: failed to launch lldb (error %lu)\n", (unsigned long)GetLastError());
            goto done;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    {
        pid_t child = fork();
        if (child == 0) {
            execlp("lldb", "lldb", "-s", cmd_path, (char *)NULL);
            _exit(127);
        }
        if (child < 0) {
            fprintf(stderr, "bestassert: failed to fork for lldb\n");
            goto done;
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
            fprintf(stderr, "bestassert: lldb not found in PATH\n");
    }
#endif

done:
    ba_unlink(py_path);
    ba_unlink(cmd_path);
}

static bestassert_action_t ba_show_menu(int allow_continue, int allow_ignore_always)
{

    printf("\n" BA_BOLD "Actions:" BA_RESET "\n");
    printf("  " BA_GREEN   "[d]" BA_RESET " Open in debugger\n");
    if (allow_continue)
        printf("  " BA_YELLOW  "[c]" BA_RESET " Continue (ignore this occurrence)\n");
    if (allow_ignore_always)
        printf("  " BA_MAGENTA "[a]" BA_RESET " Ignore (this and all subsequent)\n");
    printf("  " BA_RED     "[q]" BA_RESET " Quit program\n");
    printf(BA_BOLD "> " BA_RESET);
    fflush(stdout);

    int ch;
    while ((ch = getchar()) != EOF) {
        if (ch == '\n' || ch == '\r') continue;
        if (ch == 'd' || ch == 'D') return BESTASSERT_ACTION_DEBUGGER;
        if (ch == 'q' || ch == 'Q') return BESTASSERT_ACTION_TERMINATE;
        if (allow_continue && (ch == 'c' || ch == 'C'))
            return BESTASSERT_ACTION_CONTINUE;
        if (allow_ignore_always && (ch == 'a' || ch == 'A'))
            return BESTASSERT_ACTION_IGNORE_ALWAYS;
        printf(BA_BOLD "> " BA_RESET);
        fflush(stdout);
    }
    return BESTASSERT_ACTION_TERMINATE;
}

char ba_details[32 * 1024];
char ba_sys_err[2048] = {0};
#ifdef _WIN32
char ba_win_err[1024] = {0};
#endif

bestassert_action_t bestassert_handle(
    const char *file, int line, const char *function,
    const char *expr, int count, const char *signal_name, int is_fatal)
{
    int saved_errno = errno;
#ifdef _WIN32
    DWORD win_last_err = GetLastError();
#endif
    
    BA_LOCK();

#ifdef _WIN32
    static int color_init_done = 0;
    if (!color_init_done) {
        color_init_done = 1;
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(h, &mode))
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif

    size_t off = 0;

    if (saved_errno != 0) {
        off += snprintf(ba_sys_err + off, sizeof(ba_sys_err) - off, 
            "  " BA_BOLD "Errno   " BA_RESET ": %d (%s)\n", 
            saved_errno, strerror(saved_errno));
    }

#ifdef _WIN32
    if (win_last_err != 0 && off < sizeof(ba_sys_err)) {
        DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, win_last_err, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), ba_win_err, sizeof(ba_win_err), NULL);
        while (len > 0 && (ba_win_err[len - 1] == '\r' || ba_win_err[len - 1] == '\n' || ba_win_err[len - 1] == '.')) {
            ba_win_err[--len] = '\0';
        }
        for (DWORD i = 0; i < len; ++i) {
            if (ba_win_err[i] == '\r' || ba_win_err[i] == '\n')
                ba_win_err[i] = ' ';
        }
        off += snprintf(ba_sys_err + off, sizeof(ba_sys_err) - off, 
            "  " BA_BOLD "WinError" BA_RESET ": %lu (0x%08lX%s%s)\n", 
            (unsigned long)win_last_err, (unsigned long)win_last_err, len > 0 ? ": " : "", ba_win_err);
    }
#endif

    if (signal_name) {
        snprintf(ba_details, sizeof(ba_details),
            "  " BA_BOLD "Signal  " BA_RESET ": " BA_RED "%s" BA_RESET "\n"
            "  " BA_BOLD "At      " BA_RESET ": " BA_CYAN "%s:%d" BA_RESET "\n"
            "  " BA_BOLD "Context " BA_RESET ": %s\n"
            "%s",
            signal_name, file, line, function, ba_sys_err);
    } else {
        snprintf(ba_details, sizeof(ba_details),
            "  " BA_BOLD "Expr    " BA_RESET ": " BA_YELLOW "%s" BA_RESET "\n"
            "  " BA_BOLD "At      " BA_RESET ": " BA_CYAN "%s:%d" BA_RESET "\n"
            "  " BA_BOLD "Function" BA_RESET ": %s\n"
            "  " BA_BOLD "Count   " BA_RESET ": " BA_MAGENTA "%d" BA_RESET " time(s)\n"
            "%s",
            expr, file, line, function, count, ba_sys_err);
    }

    printf("\n");
    printf(BA_RED "========================================================================================\n" BA_RESET);
    printf(BA_RED "                               %s\n" BA_RESET, signal_name ? "< < signal caught > >" : "< < assertion occurred > >");
    printf(BA_RED "========================================================================================\n" BA_RESET);
    if (ba_details && ba_details[0]) {
        printf("%s", ba_details);
        printf(BA_RED "========================================================================================\n" BA_RESET);
    }
    ba_lldb_print_backtrace();
    printf(BA_RED "========================================================================================\n" BA_RESET);
    
    int allow_continue      = !is_fatal;
    int allow_ignore_always = !is_fatal;

    bestassert_action_t act;

    while (true) {
        act = ba_show_menu(allow_continue, allow_ignore_always);
        if (act == BESTASSERT_ACTION_DEBUGGER) {
            ba_launch_debugger();
            // reset text to not show it again
            printf(BA_RED "========================================================================================\n" BA_RESET);
        } else {
            break;
        }
    }

    if (act == BESTASSERT_ACTION_TERMINATE) {
        puts("Terminating...");
    }

    BA_UNLOCK();
    return act;
}

#ifndef BESTASSERT_NO_SIGNAL_HANDLERS

#ifdef _WIN32
static void ba_format_win_exception(EXCEPTION_RECORD *er, char *buf, size_t size)
{
    DWORD code = er->ExceptionCode;
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: {
            const char *op = "access";
            if (er->NumberParameters >= 2) {
                if (er->ExceptionInformation[0] == 0) op = "read";
                else if (er->ExceptionInformation[0] == 1) op = "write";
                else if (er->ExceptionInformation[0] == 8) op = "execute";
                snprintf(buf, size, "Access violation (%s at 0x%p)", op, (void *)er->ExceptionInformation[1]);
                return;
            }
            snprintf(buf, size, "Access violation (0x%08lX)", (unsigned long)code);
            return;
        }
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: snprintf(buf, size, "Array bounds exceeded"); return;
        case EXCEPTION_BREAKPOINT:            snprintf(buf, size, "Breakpoint"); return;
        case EXCEPTION_DATATYPE_MISALIGNMENT: snprintf(buf, size, "Datatype misalignment"); return;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    snprintf(buf, size, "Float divide by zero"); return;
        case EXCEPTION_FLT_OVERFLOW:          snprintf(buf, size, "Float overflow"); return;
        case EXCEPTION_FLT_UNDERFLOW:         snprintf(buf, size, "Float underflow"); return;
        case EXCEPTION_ILLEGAL_INSTRUCTION:   snprintf(buf, size, "Illegal instruction"); return;
        case EXCEPTION_IN_PAGE_ERROR:         snprintf(buf, size, "In-page I/O error"); return;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    snprintf(buf, size, "Integer divide by zero"); return;
        case EXCEPTION_INT_OVERFLOW:          snprintf(buf, size, "Integer overflow"); return;
        case EXCEPTION_PRIV_INSTRUCTION:      snprintf(buf, size, "Privileged instruction"); return;
        case EXCEPTION_STACK_OVERFLOW:        snprintf(buf, size, "Stack overflow"); return;
        case 0xC0000374:                      snprintf(buf, size, "Heap corruption"); return;
        default:
            snprintf(buf, size, "WinExc 0x%08lX", (unsigned long)code);
            return;
    }
}
static LONG WINAPI ba_win_exception_filter(EXCEPTION_POINTERS *ei)
{
    char name[128];
    ba_format_win_exception(ei->ExceptionRecord, name, sizeof(name));

    bestassert_handle("<crash>", 0, "<unhandled exception>",
                      NULL, 1, name, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void ba_posix_signal(int signum, siginfo_t *info, void *ctx)
{
    (void)info; (void)ctx;
    const char *name  = "SIG???";
    int         fatal = 0;
    switch (signum) {
        case SIGSEGV: name = "SIGSEGV"; fatal = 1; break;
        case SIGFPE:  name = "SIGFPE";  fatal = 1; break;
        case SIGBUS:  name = "SIGBUS";  fatal = 1; break;
        case SIGILL:  name = "SIGILL";  fatal = 1; break;
        case SIGABRT: name = "SIGABRT"; fatal = 1; break;
        case SIGINT:  name = "SIGINT";  fatal = 0; break;
        case SIGTERM: name = "SIGTERM"; fatal = 0; break;
        case SIGHUP:  name = "SIGHUP";  fatal = 0; break;
    }
    bestassert_action_t act = bestassert_handle(
        "<signal>", signum, "<signal handler>",
        NULL, 1, name, fatal);
    if (act == BESTASSERT_ACTION_IGNORE_ALWAYS) {
        struct sigaction ign;
        memset(&ign, 0, sizeof(ign));
        ign.sa_handler = SIG_IGN;
        sigaction(signum, &ign, NULL);
        return;
    }
    if (act == BESTASSERT_ACTION_TERMINATE) _exit(3);
}
#endif

__attribute__((constructor)) static void ba_signal_init_ctor(void)
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(ba_win_exception_filter);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ba_posix_signal;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
#endif
}

#endif // BESTASSERT_NO_SIGNAL_HANDLERS

#undef BA_TEMP_BUF
#undef BA_RED
#undef BA_GREEN
#undef BA_YELLOW
#undef BA_MAGENTA
#undef BA_CYAN
#undef BA_BOLD
#undef BA_RESET
#undef BA_LOCK
#undef BA_UNLOCK

#endif // BESTASSERT_IMPLEMENTATION
#endif // BESTASSERT_H
