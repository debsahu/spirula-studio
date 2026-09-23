#pragma once

// stb_image's allocator. On a thread inside a PageAllocScope, blocks of 1 MiB and up
// come from mmap / VirtualAlloc and are unmapped at free: macOS malloc keeps a freed
// large block resident for 5-20 s, which a per-frame decode pool accumulates
// (docs/notes/mask-editor.md, memory9). Every other thread gets plain malloc. Each
// block's header records how it was made, so it is freed right from any thread.
// SS_STB_PAGE_ALLOC=0 maps nothing anywhere; it exists for the A/B measurement.

#include "core/Env.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

namespace spirula {

inline constexpr size_t kPageAllocMin = 1u << 20;

namespace page_detail {

// 64 keeps stbi_load_16's output and any SIMD load aligned.
inline constexpr size_t kHeader = 64;
struct Header {
    size_t len;         // the whole block, header included
    uint32_t mapped;
};
inline thread_local bool t_opted_in = false;

// Read once: the A/B arm runs as its own process.
inline bool knob_on() {
    static const bool on = [] {
        const char* v = env("STB_PAGE_ALLOC");
        return !(v && v[0] == '0' && v[1] == '\0');
    }();
    return on;
}

inline bool maps(size_t bytes) { return t_opted_in && bytes >= kPageAllocMin && knob_on(); }

inline Header* header_of(const void* p) {
    return (Header*)((char*)p - kHeader);
}

}  // namespace page_detail

inline void* page_alloc(size_t bytes) {
    using namespace page_detail;
    const size_t len = bytes + kHeader;
    if (len < bytes) return nullptr;
    char* base = nullptr;
    const bool mapped = maps(bytes);
    if (mapped) {
#if defined(_WIN32)
        base = (char*)VirtualAlloc(nullptr, len, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        void* m = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        base = m == MAP_FAILED ? nullptr : (char*)m;
#endif
    } else {
        base = (char*)std::malloc(len);
    }
    if (!base) return nullptr;
    Header* h = (Header*)base;
    h->len = len;
    h->mapped = mapped ? 1u : 0u;
    return base + kHeader;
}

inline void page_free(void* p) {
    using namespace page_detail;
    if (!p) return;
    Header* h = header_of(p);
    if (!h->mapped) {
        std::free(h);
        return;
    }
#if defined(_WIN32)
    VirtualFree(h, 0, MEM_RELEASE);
#else
    munmap(h, h->len);
#endif
}

// A malloc'd block that stays below the mapping rule is realloc'd, as stb always did;
// anything else moves (stb only grows the PNG stream this way, 1.5 MB at 8K).
inline void* page_realloc(void* p, size_t new_bytes) {
    using namespace page_detail;
    if (!p) return page_alloc(new_bytes);
    Header* h = header_of(p);
    if (!h->mapped && !maps(new_bytes)) {
        const size_t len = new_bytes + kHeader;
        if (len < new_bytes) return nullptr;
        Header* g = (Header*)std::realloc(h, len);
        if (!g) return nullptr;
        g->len = len;
        return (char*)g + kHeader;
    }
    void* q = page_alloc(new_bytes);
    if (!q) return nullptr;
    const size_t old_bytes = h->len - kHeader;
    std::memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
    page_free(p);
    return q;
}

inline bool page_is_mapped(const void* p) { return p && page_detail::header_of(p)->mapped != 0; }
inline bool page_alloc_on_this_thread() { return page_detail::t_opted_in; }

// Opts the calling thread in for its lifetime; restores what it found.
class PageAllocScope {
public:
    PageAllocScope() : _prev(page_detail::t_opted_in) { page_detail::t_opted_in = true; }
    ~PageAllocScope() { page_detail::t_opted_in = _prev; }
    PageAllocScope(const PageAllocScope&) = delete;
    PageAllocScope& operator=(const PageAllocScope&) = delete;

private:
    bool _prev;
};

}  // namespace spirula
