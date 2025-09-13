/**
 * File: google_breakpad.h
 *
 * Author: Wire, StackOverflow, and whoever made debuggerd
 *
 * Description: in-binary backtracing because debuggerd is stupid
 *
 **/

#include "tombstoneHooks.h"

#include <list>
#include <unordered_map>
#include <cstring>
#include <cerrno>

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <dlfcn.h>

#include "libunwind.h"

namespace {

const std::list<int> gHookSignals = { SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGQUIT };
std::unordered_map<int, struct sigaction> gHookStash;

}

static ssize_t safe_write_all(int fd, const void* buf, size_t len) {
  const unsigned char* p = (const unsigned char*)buf;
  size_t left = len;
  while (left) {
    ssize_t n = write(fd, p, left);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += n; left -= (size_t)n;
  }
  return (ssize_t)len;
}
static void safe_write_str(int fd, const char* s) { safe_write_all(fd, s, strlen(s)); }

static int format_hex(uintptr_t v, char* buf, int bufsize) {
  if (bufsize < 3) return 0;
  char* p = buf;
  *p++ = '0'; *p++ = 'x';
  const int max_nibbles = (int)(sizeof(uintptr_t) * 2);
  char tmp[max_nibbles];
  int i = 0;
  if (v == 0) tmp[i++] = '0';
  else {
    while (v && i < max_nibbles) { tmp[i++] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
  }
  for (int j = i - 1; j >= 0; --j) {
    if ((p - buf) >= bufsize - 1) break;
    *p++ = tmp[j];
  }
  *p = '\0';
  return (int)(p - buf);
}

static int capture_pcs(uintptr_t* out_frames, int max_frames) {
  if (max_frames <= 0) return 0;
  if (max_frames > 256) max_frames = 256;
  unw_context_t uc;
  if (unw_getcontext(&uc) < 0) return 0;
  unw_cursor_t cursor;
  if (unw_init_local(&cursor, &uc) < 0) return 0;
  int n = 0;
  while (n < max_frames) {
    unw_word_t ip = 0;
    if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0) break;
    if (ip == 0) break;
    out_frames[n++] = (uintptr_t)ip;
    int step = unw_step(&cursor);
    if (step <= 0) break;
  }
  return n;
}

static void child_symbolize_and_log(int fd, uintptr_t const* frames, int nframes) {
  char buf[512];
  safe_write_str(fd, "***********************************************\n");
  safe_write_str(fd, "BOOM: captured backtrace:\n");
  for (int i = 0; i < nframes; ++i) {
    uintptr_t pc = frames[i];
    Dl_info info;
    memset(&info, 0, sizeof(info));
    const char* symname = nullptr;
    const char* objname = nullptr;
    uintptr_t objbase = 0;
    if (dladdr((void*)pc, &info) && info.dli_fname) {
      objname = info.dli_fname;
      objbase = (uintptr_t)info.dli_fbase;
      if (info.dli_sname) symname = info.dli_sname;
    }

    {
      unw_cursor_t cursor;
      unw_context_t uc;
      if (unw_getcontext(&uc) >= 0 &&
          unw_init_local(&cursor, &uc) >= 0) {
      }
    }

    // "#00 pc 0x12345 funcname + 0xoff (libname)\n"
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "#%02d pc ", i);
    char hex[3 + 2 * sizeof(uintptr_t) + 1];
    format_hex(pc, hex, (int)sizeof(hex));
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s ", hex);

    if (symname) {
      uintptr_t symaddr = (uintptr_t)info.dli_saddr;
      uintptr_t offz = pc - symaddr;
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%s + 0x%zx ", symname, offz);
    } else {
      if (objname) {
        uintptr_t offobj = pc - objbase;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "(%s + 0x%zx) ", objname, offobj);
      }
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "[%p]\n", (void*)pc);
    safe_write_all(fd, buf, (size_t)pos);
  }
  safe_write_str(fd, "***********************************************\n");

  _exit(0);
}

static void DebuggerHook(int signum, siginfo_t* info, void* /*ctx*/) {
  const int fd = 2;
  safe_write_str(fd, "tombstone: caught signal ");
  char snum[16];
  int slen = snprintf(snum, sizeof(snum), "%d\n", signum);
  safe_write_all(fd, snum, (size_t)slen);

  const int MAX_FRAMES = 128;
  uintptr_t frames[MAX_FRAMES];
  int nframes = capture_pcs(frames, MAX_FRAMES);

  pid_t pid = fork();
  if (pid == 0) {
    child_symbolize_and_log(fd, frames, nframes);
    _exit(1);
  } else if (pid > 0) {
    int status = 0;
    waitpid(pid, &status, WNOHANG);
  } else {
    safe_write_str(fd, "tombstone: fork failed, dumping raw pcs:\n");
    for (int i = 0; i < nframes; ++i) {
      char line[64];
      int len = snprintf(line, sizeof(line), "#%02d pc %p\n", i, (void*)frames[i]);
      safe_write_all(fd, line, len);
    }
  }

    auto it = gHookStash.find(signum);
    if (it != gHookStash.end()) {
        sigaction(signum, &it->second, nullptr);
    } else {
        struct sigaction dfl = {};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(signum, &dfl, nullptr);
    }

    syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), signum);

    raise(signum);
}

static void InstallTombstoneHook(int signum) {
  struct sigaction newAction;
  struct sigaction oldAction;
  memset(&newAction, 0, sizeof(newAction));
  memset(&oldAction, 0, sizeof(oldAction));
  newAction.sa_flags = (SA_SIGINFO | SA_ONSTACK);
  newAction.sa_sigaction = DebuggerHook;
  if (sigaction(signum, &newAction, &oldAction) == 0) {
    gHookStash[signum] = oldAction;
  }
}

static void UninstallTombstoneHook(int signum) {
  const auto pos = gHookStash.find(signum);
  if (pos != gHookStash.end()) {
    sigaction(signum, &pos->second, nullptr);
  }

}

namespace Anki { namespace Vector {
void InstallTombstoneHooks() {
  for (auto signum : gHookSignals) InstallTombstoneHook(signum);
}
void UninstallTombstoneHooks() {
  for (auto signum : gHookSignals) UninstallTombstoneHook(signum);
}
}}
