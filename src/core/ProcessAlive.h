#pragma once

// Whether a process id names a running process. A pid that is reused by an
// unrelated process reads as alive. Header-only: the densify library and the
// GUI's model listing both need it and share no other library.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#include <unistd.h>
#endif

inline bool process_alive(long pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return running;
#else
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}
