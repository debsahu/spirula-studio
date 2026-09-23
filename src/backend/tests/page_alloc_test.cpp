// core/PageAlloc.h and the stb_image hook behind it: only opted-in threads map, a
// mapped block leaves the resident set when freed from any thread, and
// SS_STB_PAGE_ALLOC=0 maps nothing. Run twice: plain, and with the knob as
// `SS_STB_PAGE_ALLOC=0 page_alloc_test --hook-off` (the knob is read once a process).
// No GPU. Exit code 0 = every check passed.

#include "core/PageAlloc.h"
#include "external/stb_image.h"
#include "external/stb_image_write.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#  include <mach/mach.h>
#  include <malloc/malloc.h>
#endif
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace {

int g_fail = 0, g_ok = 0;
void check(bool cond, const char* name) {
    if (cond) {
        g_ok++;
        std::printf("ok   %s\n", name);
    } else {
        g_fail++;
        std::printf("FAIL %s\n", name);
    }
}

constexpr size_t kMiB = 1u << 20;

double rss_mb() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS)
        return -1.0;
    return (double)info.resident_size / (double)kMiB;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS c{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c))) return -1.0;
    return (double)c.WorkingSetSize / (double)kMiB;
#else
    long pages = 0, resident = 0;
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return -1.0;
    const int got = std::fscanf(f, "%ld %ld", &pages, &resident);
    std::fclose(f);
    return got == 2 ? (double)resident * (double)sysconf(_SC_PAGESIZE) / (double)kMiB : -1.0;
#endif
}

void touch(void* p, size_t n) {
    for (size_t i = 0; i < n; i += 4096) ((volatile uint8_t*)p)[i] = (uint8_t)(1 + i / 4096);
}

// Runs `fn` on a fresh thread and returns what it returned.
void* on_thread(bool opted_in, const std::function<void*()>& fn) {
    void* out = nullptr;
    std::thread t([&] {
        if (opted_in) {
            spirula::PageAllocScope pages;
            out = fn();
        } else {
            out = fn();
        }
    });
    t.join();
    return out;
}

// A check whose failure mode is a crash (a munmap'd block handed to free())
// runs in a child, so the mutant fails by name instead of taking the run down.
void check_in_child(const char* name, const std::function<bool()>& body) {
#if defined(_WIN32)
    check(body(), name);
#else
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) _exit(body() ? 0 : 1);
    int status = 0;
    const bool ran = pid > 0 && waitpid(pid, &status, 0) == pid;
    check(ran && WIFEXITED(status) && WEXITSTATUS(status) == 0, name);
    if (ran && WIFSIGNALED(status)) std::printf("     (child killed by signal %d)\n", WTERMSIG(status));
#endif
}

std::vector<unsigned char> png_of(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 3);
    for (size_t i = 0; i < px.size(); i++) px[i] = (uint8_t)((i * 7) ^ (i >> 9));
    std::vector<unsigned char> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* v = (std::vector<unsigned char>*)ctx;
            v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + size);
        },
        &out, w, h, 3, px.data(), w * 3);
    return out;
}

// Four 84 MB blocks freed together: macOS malloc keeps all of them resident for
// seconds, where whether it keeps one lone block varies.
double drop_after_freeing_four(bool opted_in, bool& all_mapped) {
    std::vector<void*> blocks(4);
    all_mapped = true;
    for (void*& b : blocks) {
        b = on_thread(opted_in, [] { return spirula::page_alloc(84 * kMiB); });
        touch(b, 84 * kMiB);
        all_mapped = all_mapped && spirula::page_is_mapped(b);
    }
    const double before = rss_mb();
    for (void* b : blocks) spirula::page_free(b);
    return before - rss_mb();
}

int hook_off() {
    const char* v = std::getenv("SS_STB_PAGE_ALLOC");
    check(v && std::strcmp(v, "0") == 0, "the knob is set in this process (SS_STB_PAGE_ALLOC=0)");
    void* p = on_thread(true, [] { return spirula::page_alloc(130 * kMiB); });
    check(p && !spirula::page_is_mapped(p), "SS_STB_PAGE_ALLOC=0: an opted-in thread maps nothing");
    spirula::page_free(p);
    bool mapped = true;
    const double drop = drop_after_freeing_four(true, mapped);
    std::printf("     SS_STB_PAGE_ALLOC=0: 4 x 84 MB freed, resident set fell %.1f MB\n", drop);
#if defined(__APPLE__)
    check(drop < 100.0, "SS_STB_PAGE_ALLOC=0: 4 x 84 MB freed together stay resident (fall < 100 MB)");
#endif
    std::printf("%d ok, %d failure(s)\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--hook-off") == 0) return hook_off();

    // Children first, while this process has no other thread to lose in a fork.
    check_in_child("a block mapped on an opted-in thread is unmapped when freed on one that is not", [] {
        void* p = on_thread(true, [] { return spirula::page_alloc(130 * kMiB); });
        if (!p || !spirula::page_is_mapped(p)) return false;
        touch(p, 130 * kMiB);
        const double before = rss_mb();
        spirula::page_free(p);   // this thread never opted in
        return before - rss_mb() >= 100.0;
    });
    check_in_child("a block malloc'd on a thread that is not opted in is freed on one that is", [] {
        void* p = spirula::page_alloc(2 * kMiB);
        if (!p || spirula::page_is_mapped(p)) return false;
        std::memset(p, 3, 2 * kMiB);
        on_thread(true, [p] {
            spirula::page_free(p);
            return (void*)nullptr;
        });
        void* q = std::malloc(2 * kMiB);
        std::memset(q, 4, 2 * kMiB);
        std::free(q);
        return true;
    });

    check_in_child("page_free(nullptr) returns", [] {
        void* volatile nil = nullptr;   // not a constant the inlined free can fold away
        spirula::page_free(nil);
        return true;
    });
    check_in_child("stbi_image_free(nullptr) returns", [] {
        stbi_image_free(nullptr);
        return true;
    });

    // Which threads map.
    {
        void* p = spirula::page_alloc(130 * kMiB);
        check(p && !spirula::page_is_mapped(p), "a thread that is not opted in maps nothing, even 130 MB");
        spirula::page_free(p);
        check(!spirula::page_alloc_on_this_thread(), "the main thread starts not opted in");
    }
    {
        std::atomic<int> phase{0};
        void* other = nullptr;
        std::thread holder([&] {
            spirula::PageAllocScope pages;
            phase = 1;
            while (phase.load() != 2) std::this_thread::yield();
        });
        while (phase.load() != 1) std::this_thread::yield();
        std::thread probe([&] { other = spirula::page_alloc(2 * kMiB); });
        probe.join();
        phase = 2;
        holder.join();
        check(other && !spirula::page_is_mapped(other),
              "the opt-in is per thread: another thread's scope maps nothing here");
        spirula::page_free(other);
    }
    {
        bool nested_on = false, after_inner = false, after_outer = true;
        std::thread t([&] {
            {
                spirula::PageAllocScope outer;
                {
                    spirula::PageAllocScope inner;
                    nested_on = spirula::page_alloc_on_this_thread();
                }
                after_inner = spirula::page_alloc_on_this_thread();
            }
            after_outer = spirula::page_alloc_on_this_thread();
        });
        t.join();
        check(nested_on && after_inner && !after_outer, "a scope restores the state it found");
    }

    // The threshold, on an opted-in thread.
    {
        void* small_b = on_thread(true, [] { return spirula::page_alloc(512 * 1024); });
        void* under = on_thread(true, [] { return spirula::page_alloc(kMiB - 1); });
        void* at = on_thread(true, [] { return spirula::page_alloc(kMiB); });
        check(small_b && !spirula::page_is_mapped(small_b), "512 KiB is malloc'd even when opted in");
        check(under && !spirula::page_is_mapped(under), "1 MiB - 1 is malloc'd even when opted in");
        check(at && spirula::page_is_mapped(at), "exactly 1 MiB is mapped when opted in");
        std::vector<uint8_t> want(512 * 1024);
        for (size_t i = 0; i < want.size(); i++) want[i] = (uint8_t)(i * 13);
        std::memcpy(small_b, want.data(), want.size());
        check(std::memcmp(small_b, want.data(), want.size()) == 0, "a 512 KiB block round-trips");
        spirula::page_free(small_b);
        spirula::page_free(under);
        spirula::page_free(at);
    }

    // The resident set.
    {
        const double base = rss_mb();
        void* p = on_thread(true, [] { return spirula::page_alloc(130 * kMiB); });
        touch(p, 130 * kMiB);
        const double touched = rss_mb();
        check(touched - base >= 100.0, "a touched 130 MB block raises the resident set by >= 100 MB");
        spirula::page_free(p);
        check(touched - rss_mb() >= 100.0, "freeing the 130 MB block lowers it by >= 100 MB");
        bool mapped = false;
        const double drop = drop_after_freeing_four(true, mapped);
        std::printf("     4 x 84 MB mapped, freed together: resident set fell %.1f MB\n", drop);
        check(mapped, "four 84 MB blocks on an opted-in thread are mapped");
        check(drop >= 300.0, "4 x 84 MB freed together leave the resident set at once (fall >= 300 MB)");
    }

    // page_realloc.
    {
        auto grow = [](bool opted_in, size_t from, size_t to, bool& mapped_after) {
            std::vector<uint8_t> want(from);
            for (size_t i = 0; i < from; i++) want[i] = (uint8_t)(i * 31 + 7);
            void* p = on_thread(opted_in, [from] { return spirula::page_alloc(from); });
            std::memcpy(p, want.data(), from);
            void* q = on_thread(opted_in, [p, to] { return spirula::page_realloc(p, to); });
            if (!q) return false;
            std::memset((uint8_t*)q + from, 9, to - from);
            const bool same = std::memcmp(q, want.data(), from) == 0;
            mapped_after = spirula::page_is_mapped(q);
            spirula::page_free(q);
            return same;
        };
        bool m = false;
        check(grow(true, 1536 * 1024, 3 * kMiB, m) && m,
              "page_realloc 1.5 MB -> 3 MB, opted in: keeps the first 1.5 MB, stays mapped");
        check(grow(true, 512 * 1024, 3 * kMiB, m) && m,
              "page_realloc 512 KiB -> 3 MB, opted in: keeps the 512 KiB, becomes mapped");
        check(grow(false, 1536 * 1024, 3 * kMiB, m) && !m,
              "page_realloc 1.5 MB -> 3 MB, not opted in: keeps the first 1.5 MB, stays malloc'd");
        void* p = spirula::page_realloc(nullptr, 3 * kMiB);
        if (p) std::memset(p, 5, 3 * kMiB);
        check(p != nullptr, "page_realloc(nullptr, 3 MB) returns a usable block");
        spirula::page_free(p);
        // realloc'd in place first, then moved: the move copies what the header says.
        std::vector<uint8_t> want(3 * kMiB);
        for (size_t i = 0; i < want.size(); i++) want[i] = (uint8_t)(i * 17 + 1);
        void* a = spirula::page_alloc(kMiB);
        void* b = spirula::page_realloc(a, 3 * kMiB);
        std::memcpy(b, want.data(), want.size());
        void* c = on_thread(true, [b] { return spirula::page_realloc(b, 6 * kMiB); });
        check(c && spirula::page_is_mapped(c) && std::memcmp(c, want.data(), want.size()) == 0,
              "a block grown in place, then moved to a mapping, keeps all 3 MB");
        spirula::page_free(c);
    }

    // stb itself: a 1024x512 RGB decode is 1.5 MB, over the threshold.
    {
        const std::vector<unsigned char> png = png_of(1024, 512);
        auto decode = [&png]() -> void* {
            int w = 0, h = 0, c = 0;
            return stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &c, 3);
        };
        void* on = on_thread(true, decode);
        void* off = on_thread(false, decode);
        check(on && spirula::page_is_mapped(on), "stbi_load on an opted-in thread returns a mapped buffer");
        check(off && !spirula::page_is_mapped(off),
              "stbi_load on a thread that is not opted in returns a malloc'd buffer");
#if defined(__APPLE__)
        check(off && malloc_size((const char*)off - spirula::page_detail::kHeader) >=
                         (size_t)1024 * 512 * 3,
              "that buffer sits in a block malloc owns (the old allocator path)");
#endif
        check(on && off && std::memcmp(on, off, (size_t)1024 * 512 * 3) == 0,
              "both paths decode the same pixels");
        stbi_image_free(on);
        stbi_image_free(off);
    }

    std::printf("%d ok, %d failure(s)\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
