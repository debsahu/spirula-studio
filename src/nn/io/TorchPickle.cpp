#include "nn/io/TorchPickle.h"

#include "external/miniz.h"
#include "nn/core/Error.h"
#include "nn/core/Half.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>

namespace nn {
namespace {

// A real state dict is ~150 KB of pickle, ~15 objects a tensor, nested ~6 deep.
// These bounds are orders above that and far below what a hostile file can spend
// (a 48 KB deflated pickle of `N` opcodes otherwise reaches 5.6 GB).
constexpr uint64_t kMaxPickleBytes = 32ull << 20;
constexpr uint64_t kMaxObjects = 250000;
// Memo entries and stack slots are not mk() objects (BINGET re-pushes an existing
// one, LONG_BINPUT mints a map node per distinct key), so they get their own
// bound: 6M five-byte opcodes fit under the pickle cap and cost ~300 MB unbounded.
constexpr uint64_t kMaxMemo = kMaxObjects;
constexpr uint64_t kMaxStack = kMaxObjects;
constexpr int      kMaxNesting = 64;
constexpr uint64_t kMaxElems = 1ull << 48;
constexpr size_t   kMaxDims = 16;

// False when a * b does not fit in 64 bits.
bool mul_fits(uint64_t a, uint64_t b, uint64_t* out) {
    if (a != 0 && b > UINT64_MAX / a) return false;
    *out = a * b;
    return true;
}

// ---- the pickle value model: only what a state dict is made of ------------

struct Obj;
using P = std::shared_ptr<Obj>;

struct Obj {
    enum Kind { Mark, None, Bool, Int, Float, Str, Tuple, List, Dict, Global, Storage, Tensor };
    Kind kind = None;
    int64_t i = 0;
    double f = 0;
    std::string s;                       // Str; Global: "module.name"; Storage: zip entry key
    std::vector<P> items;                // Tuple, List, Dict (keys and values interleaved)
    std::string dtype;                   // Storage, Tensor
    int64_t numel = 0;                   // Storage: elements it holds
    P storage;                           // Tensor
    std::vector<int64_t> shape, stride;  // Tensor
    int64_t offset = 0;                  // Tensor: elements into the storage
    int depth = 0;                       // longest chain of children below this
};

const char* storage_dtype(const std::string& cls) {
    static const std::map<std::string, const char*> t = {
        {"torch.FloatStorage", "F32"},   {"torch.HalfStorage", "F16"},
        {"torch.BFloat16Storage", "BF16"}, {"torch.DoubleStorage", "F64"},
        {"torch.LongStorage", "I64"},    {"torch.IntStorage", "I32"},
        {"torch.ShortStorage", "I16"},   {"torch.CharStorage", "I8"},
        {"torch.ByteStorage", "U8"},     {"torch.BoolStorage", "BOOL"}};
    auto it = t.find(cls);
    return it == t.end() ? nullptr : it->second;
}

bool allowed_global(const std::string& g) {
    return g == "collections.OrderedDict" || g == "torch._utils._rebuild_tensor_v2" ||
           g == "torch._utils._rebuild_tensor" || g == "torch._utils._rebuild_parameter" ||
           storage_dtype(g) != nullptr;
}

class Machine {
public:
    Machine(const uint8_t* d, size_t n, const std::string& path) : d_(d), n_(n), path_(path) {}

    P run() {
        while (true) {
            const uint8_t op = u8();
            switch (op) {
                case 0x80: u8(); break;                              // PROTO
                case 0x95: skip(8); break;                           // FRAME
                case '.': {                                          // STOP
                    NN_CHECK(!st_.empty(), "%s: pickle ends with an empty stack", path_.c_str());
                    return st_.back();
                }
                case '}': push(mk(Obj::Dict)); break;              // EMPTY_DICT
                case ']': push(mk(Obj::List)); break;              // EMPTY_LIST
                case ')': push(mk(Obj::Tuple)); break;             // EMPTY_TUPLE
                case '(': push(mk(Obj::Mark)); break;              // MARK
                case 'N': push(mk(Obj::None)); break;
                case 0x88: push(boolean(true)); break;               // NEWTRUE
                case 0x89: push(boolean(false)); break;              // NEWFALSE
                case 'J': push(integer((int32_t)le(4))); break;      // BININT
                case 'K': push(integer((int64_t)le(1))); break;      // BININT1
                case 'M': push(integer((int64_t)le(2))); break;      // BININT2
                case 0x8a: push(integer(long_int(u8()))); break;     // LONG1
                case 0x8b: push(integer(long_int((size_t)le(4)))); break;  // LONG4
                case 'G': {                                          // BINFLOAT, big-endian
                    uint64_t v = 0;
                    for (int k = 0; k < 8; ++k) v = (v << 8) | u8();
                    P o = mk(Obj::Float);
                    std::memcpy(&o->f, &v, 8);
                    push(o);
                    break;
                }
                case 'X': push(str((size_t)le(4))); break;           // BINUNICODE
                case 0x8c: push(str(u8())); break;                   // SHORT_BINUNICODE
                case 'T': push(str((size_t)le(4))); break;           // BINSTRING
                case 'U': push(str(u8())); break;                    // SHORT_BINSTRING
                case 'q': put((uint64_t)u8()); break;                // BINPUT
                case 'r': put(le(4)); break;                         // LONG_BINPUT
                case 0x94: put(memo_.size()); break;                 // MEMOIZE
                case 'h': push(memo_at(u8())); break;                // BINGET
                case 'j': push(memo_at(le(4))); break;               // LONG_BINGET
                case 'c': {                                          // GLOBAL
                    std::string mod = line(), name = line();
                    push(global(mod + "." + name));
                    break;
                }
                case 0x93: {                                         // STACK_GLOBAL
                    P name = pop(), mod = pop();
                    push(global(mod->s + "." + name->s));
                    break;
                }
                case 't': push(collect(Obj::Tuple)); break;          // TUPLE
                case 0x85: push(take(1)); break;                     // TUPLE1
                case 0x86: push(take(2)); break;                     // TUPLE2
                case 0x87: push(take(3)); break;                     // TUPLE3
                case 'R': reduce(); break;                           // REDUCE
                case 'Q': persid(); break;                           // BINPERSID
                case 'u': setitems(); break;                         // SETITEMS
                case 's': setitem(); break;                          // SETITEM
                case 'e': appends(); break;                          // APPENDS
                case 'a': append(); break;                           // APPEND
                case 'b': build(); break;                            // BUILD
                case '0': pop(); break;                              // POP
                default:
                    fail("%s: unsupported pickle opcode 0x%02x at byte %zu", path_.c_str(), op,
                         pos_ - 1);
            }
        }
    }

private:
    const uint8_t* d_;
    size_t n_, pos_ = 0;
    const std::string& path_;
    std::vector<P> st_;
    std::map<uint64_t, P> memo_;
    uint64_t nobj_ = 0;

    P mk(Obj::Kind k) {
        NN_CHECK(++nobj_ <= kMaxObjects, "%s: the pickle builds more than %llu objects",
                 path_.c_str(), (unsigned long long)kMaxObjects);
        auto o = std::make_shared<Obj>();
        o->kind = k;
        return o;
    }
    // Containers record how deep they nest, so a pickle of a million nested
    // tuples is refused instead of freed recursively.
    void adopt(Obj& parent, const P& child) {
        parent.depth = std::max(parent.depth, child->depth + 1);
        NN_CHECK(parent.depth <= kMaxNesting, "%s: the pickle nests deeper than %d levels",
                 path_.c_str(), kMaxNesting);
    }

    void need(size_t k) const {
        NN_CHECK(pos_ + k <= n_, "%s: pickle truncated at byte %zu", path_.c_str(), pos_);
    }
    uint8_t u8() {
        need(1);
        return d_[pos_++];
    }
    void skip(size_t k) {
        need(k);
        pos_ += k;
    }
    uint64_t le(int bytes) {
        need((size_t)bytes);
        uint64_t v = 0;
        for (int k = bytes - 1; k >= 0; --k) v = (v << 8) | d_[pos_ + (size_t)k];
        pos_ += (size_t)bytes;
        return v;
    }
    int64_t long_int(size_t bytes) {
        NN_CHECK(bytes <= 8, "%s: pickled integer wider than 64 bits", path_.c_str());
        if (bytes == 0) return 0;
        uint64_t v = le((int)bytes);
        if (bytes < 8 && (v >> (bytes * 8 - 1)) & 1) v |= ~0ull << (bytes * 8);
        return (int64_t)v;
    }
    std::string line() {
        std::string s;
        while (true) {
            const char c = (char)u8();
            if (c == '\n') return s;
            s.push_back(c);
        }
    }
    P str(size_t len) {
        need(len);
        P o = mk(Obj::Str);
        o->s.assign((const char*)d_ + pos_, len);
        pos_ += len;
        return o;
    }
    P boolean(bool v) {
        P o = mk(Obj::Bool);
        o->i = v;
        return o;
    }
    P integer(int64_t v) {
        P o = mk(Obj::Int);
        o->i = v;
        return o;
    }
    P global(const std::string& g) {
        NN_CHECK(allowed_global(g),
                 "%s: unsupported global '%s' in the pickle (a state dict imports only "
                 "OrderedDict, torch's tensor rebuilders and its storage classes)",
                 path_.c_str(), g.c_str());
        P o = mk(Obj::Global);
        o->s = g;
        return o;
    }
    void push(P o) {
        NN_CHECK(st_.size() < kMaxStack, "%s: the pickle stack grows past %llu entries (limit)",
                 path_.c_str(), (unsigned long long)kMaxStack);
        st_.push_back(std::move(o));
    }
    void put(uint64_t k) {
        P o = top();
        NN_CHECK(memo_.count(k) || memo_.size() < kMaxMemo,
                 "%s: the pickle memoizes more than %llu entries (limit)", path_.c_str(),
                 (unsigned long long)kMaxMemo);
        memo_[k] = std::move(o);
    }
    P top() {
        NN_CHECK(!st_.empty(), "%s: pickle stack underflow", path_.c_str());
        return st_.back();
    }
    P pop() {
        P o = top();
        st_.pop_back();
        return o;
    }
    P memo_at(uint64_t k) {
        auto it = memo_.find(k);
        NN_CHECK(it != memo_.end(), "%s: pickle reads memo %llu before it is written",
                 path_.c_str(), (unsigned long long)k);
        return it->second;
    }
    // Pops back to the last MARK and returns what was above it.
    std::vector<P> above_mark() {
        size_t m = st_.size();
        while (m > 0 && st_[m - 1]->kind != Obj::Mark) --m;
        NN_CHECK(m > 0, "%s: pickle has no MARK to return to", path_.c_str());
        std::vector<P> v(st_.begin() + (long)m, st_.end());
        st_.resize(m - 1);
        return v;
    }
    P collect(Obj::Kind k) {
        P o = mk(k);
        o->items = above_mark();
        for (const P& c : o->items) adopt(*o, c);
        return o;
    }
    P take(size_t n) {
        NN_CHECK(st_.size() >= n, "%s: pickle stack underflow", path_.c_str());
        P o = mk(Obj::Tuple);
        o->items.assign(st_.end() - (long)n, st_.end());
        st_.resize(st_.size() - n);
        for (const P& c : o->items) adopt(*o, c);
        return o;
    }

    int64_t as_int(const P& o) const {
        NN_CHECK(o->kind == Obj::Int || o->kind == Obj::Bool, "%s: expected an integer in the pickle",
                 path_.c_str());
        return o->i;
    }
    std::vector<int64_t> int_tuple(const P& o) const {
        NN_CHECK(o->kind == Obj::Tuple, "%s: expected a tuple of integers", path_.c_str());
        std::vector<int64_t> v;
        for (const P& e : o->items) v.push_back(as_int(e));
        return v;
    }

    void reduce() {
        P args = pop(), fn = pop();
        NN_CHECK(fn->kind == Obj::Global && args->kind == Obj::Tuple,
                 "%s: pickle calls something that is not a known function", path_.c_str());
        if (fn->s == "collections.OrderedDict") {
            NN_CHECK(args->items.empty(), "%s: OrderedDict built from arguments", path_.c_str());
            push(mk(Obj::Dict));
        } else if (fn->s == "torch._utils._rebuild_parameter") {
            NN_CHECK(!args->items.empty() && args->items[0]->kind == Obj::Tensor,
                     "%s: malformed _rebuild_parameter", path_.c_str());
            push(args->items[0]);
        } else if (fn->s == "torch._utils._rebuild_tensor_v2" ||
                   fn->s == "torch._utils._rebuild_tensor") {
            NN_CHECK(args->items.size() >= 4 && args->items[0]->kind == Obj::Storage,
                     "%s: malformed tensor rebuild", path_.c_str());
            P t = mk(Obj::Tensor);
            t->storage = args->items[0];
            t->dtype = t->storage->dtype;
            t->offset = as_int(args->items[1]);
            t->shape = int_tuple(args->items[2]);
            t->stride = int_tuple(args->items[3]);
            NN_CHECK(t->stride.size() == t->shape.size() && t->shape.size() <= kMaxDims,
                     "%s: a tensor's shape and stride have %zu and %zu entries",
                     path_.c_str(), t->shape.size(), t->stride.size());
            adopt(*t, t->storage);
            push(t);
        } else {
            fail("%s: unsupported call to '%s'", path_.c_str(), fn->s.c_str());
        }
    }

    void persid() {
        P pid = pop();
        NN_CHECK(pid->kind == Obj::Tuple && pid->items.size() >= 5 &&
                     pid->items[0]->kind == Obj::Str && pid->items[0]->s == "storage" &&
                     pid->items[1]->kind == Obj::Global && pid->items[2]->kind == Obj::Str,
                 "%s: unsupported persistent id (not a ('storage', class, key, ...) tuple)",
                 path_.c_str());
        P o = mk(Obj::Storage);
        o->dtype = storage_dtype(pid->items[1]->s) ? storage_dtype(pid->items[1]->s) : "";
        NN_CHECK(!o->dtype.empty(), "%s: persistent id names '%s', not a storage class",
                 path_.c_str(), pid->items[1]->s.c_str());
        o->s = pid->items[2]->s;
        o->numel = as_int(pid->items[4]);
        NN_CHECK(o->numel >= 0 && (uint64_t)o->numel <= kMaxElems,
                 "%s: a storage claims %lld elements", path_.c_str(), (long long)o->numel);
        push(o);
    }

    void setitem() {
        P v = pop(), k = pop(), d = top();
        NN_CHECK(d->kind == Obj::Dict, "%s: SETITEM on a non-dict", path_.c_str());
        adopt(*d, k);
        adopt(*d, v);
        d->items.push_back(k);
        d->items.push_back(v);
    }
    void setitems() {
        std::vector<P> kv = above_mark();
        P d = top();
        NN_CHECK(d->kind == Obj::Dict && kv.size() % 2 == 0, "%s: malformed SETITEMS",
                 path_.c_str());
        for (const P& c : kv) adopt(*d, c);
        d->items.insert(d->items.end(), kv.begin(), kv.end());
    }
    void append() {
        P v = pop(), l = top();
        NN_CHECK(l->kind == Obj::List, "%s: APPEND to a non-list", path_.c_str());
        adopt(*l, v);
        l->items.push_back(v);
    }
    void appends() {
        std::vector<P> v = above_mark();
        P l = top();
        NN_CHECK(l->kind == Obj::List, "%s: APPENDS to a non-list", path_.c_str());
        for (const P& c : v) adopt(*l, c);
        l->items.insert(l->items.end(), v.begin(), v.end());
    }
    // nn.Module.state_dict() hangs `_metadata` on the OrderedDict, which
    // arrives as a BUILD after the items. Nothing in it is a tensor.
    void build() {
        P state = pop(), target = top();
        NN_CHECK(target->kind == Obj::Dict && state->kind == Obj::Dict,
                 "%s: unsupported BUILD (only a dict's attribute state is understood)",
                 path_.c_str());
    }
};

struct Zip {
    mz_zip_archive z{};
    explicit Zip(const std::string& path) {
        std::memset(&z, 0, sizeof z);
        NN_CHECK(mz_zip_reader_init_file(&z, path.c_str(), 0),
                 "%s: not a readable zip (a torch.save file is one): %s", path.c_str(),
                 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
    }
    ~Zip() { mz_zip_reader_end(&z); }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    int find(const std::string& name) { return mz_zip_reader_locate_file(&z, name.c_str(), nullptr, 0); }
    mz_zip_archive_file_stat stat(int idx, const std::string& path, const std::string& name) {
        mz_zip_archive_file_stat st;
        NN_CHECK(mz_zip_reader_file_stat(&z, (mz_uint)idx, &st), "%s: bad zip entry '%s'",
                 path.c_str(), name.c_str());
        return st;
    }
    // The size is the header's claim, so it is bounded before anything is
    // allocated: by `max_bytes`, and for a stored entry by the archive itself.
    std::vector<uint8_t> extract(int idx, const std::string& path, const std::string& name,
                                 uint64_t max_bytes, bool stored_only) {
        const mz_zip_archive_file_stat st = stat(idx, path, name);
        NN_CHECK(!stored_only || st.m_method == 0, "%s: zip entry '%s' is compressed",
                 path.c_str(), name.c_str());
        NN_CHECK(st.m_uncomp_size <= max_bytes, "%s: zip entry '%s' claims %llu bytes (limit %llu)",
                 path.c_str(), name.c_str(), (unsigned long long)st.m_uncomp_size,
                 (unsigned long long)max_bytes);
        if (st.m_method == 0)
            NN_CHECK(st.m_comp_size == st.m_uncomp_size && st.m_comp_size <= z.m_archive_size,
                     "%s: stored zip entry '%s' is larger than the file", path.c_str(),
                     name.c_str());
        std::vector<uint8_t> v((size_t)st.m_uncomp_size);
        if (v.empty()) return v;
        NN_CHECK(mz_zip_reader_extract_to_mem(&z, (mz_uint)idx, v.data(), v.size(), 0),
                 "%s: cannot read zip entry '%s' (truncated file, or CRC mismatch): %s",
                 path.c_str(), name.c_str(), mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        return v;
    }
};

}  // namespace

uint64_t TorchCheckpoint::Entry::elem_bytes() const {
    if (dtype == "F64" || dtype == "I64") return 8;
    if (dtype == "F32" || dtype == "I32") return 4;
    if (dtype == "F16" || dtype == "BF16" || dtype == "I16") return 2;
    return 1;
}

int64_t TorchCheckpoint::Entry::numel() const {
    uint64_t n = 1;
    for (int64_t d : shape)
        if (d < 0 || !mul_fits(n, (uint64_t)d, &n) || n > kMaxElems) return -1;
    return (int64_t)n;
}

TorchCheckpoint::TorchCheckpoint(const std::string& path) : path_(path) {
    Zip zip(path);
    std::string root;
    int pkl = -1;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip.z); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip.z, i, name, sizeof name);
        const std::string n = name;
        if (n == "data.pkl" || (n.size() > 9 && n.compare(n.size() - 9, 9, "/data.pkl") == 0)) {
            pkl = (int)i;
            root = n.substr(0, n.size() - 8);
            break;
        }
    }
    NN_CHECK(pkl >= 0, "%s: no data.pkl in the zip (not a torch.save file)", path.c_str());
    const std::vector<uint8_t> bytes = zip.extract(pkl, path, root + "data.pkl", kMaxPickleBytes, false);
    Machine m(bytes.data(), bytes.size(), path_);
    P top = m.run();
    NN_CHECK(top->kind == Obj::Dict, "%s: the pickle is not a dict of tensors", path.c_str());
    if (top->items.size() == 2 && top->items[0]->kind == Obj::Str &&
        top->items[0]->s == "state_dict" && top->items[1]->kind == Obj::Dict)
        top = top->items[1];

    for (size_t i = 0; i + 1 < top->items.size(); i += 2) {
        const P& k = top->items[i];
        const P& v = top->items[i + 1];
        NN_CHECK(k->kind == Obj::Str, "%s: a state dict key that is not a string", path.c_str());
        NN_CHECK(v->kind == Obj::Tensor, "%s: '%s' is not a tensor", path.c_str(), k->s.c_str());
        Entry e;
        e.dtype = v->dtype;
        e.shape = v->shape;
        e.storage = root + "data/" + v->storage->s;
        NN_CHECK(v->offset >= 0, "%s: '%s' has a negative storage offset", path.c_str(),
                 k->s.c_str());
        e.offset = (uint64_t)v->offset;
        uint64_t n = 1, expect = 1;
        for (size_t d = v->shape.size(); d-- > 0;) {
            NN_CHECK(v->shape[d] >= 0, "%s: '%s' has a negative dimension", path.c_str(),
                     k->s.c_str());
            NN_CHECK(mul_fits(n, (uint64_t)v->shape[d], &n) && n <= kMaxElems,
                     "%s: '%s' has more than 2^48 elements", path.c_str(), k->s.c_str());
            if (v->shape[d] != 1 && (uint64_t)v->stride[d] != expect)
                fail("%s: '%s' is not contiguous (stride %lld at dim %zu, want %llu)",
                     path.c_str(), k->s.c_str(), (long long)v->stride[d], d,
                     (unsigned long long)expect);
            expect = n;
        }
        // The storage is what the zip actually holds, not what the pickle says.
        uint64_t snum = (uint64_t)v->storage->numel, sbytes = 0;
        NN_CHECK(mul_fits(snum, e.elem_bytes(), &sbytes), "%s: storage '%s' is too large",
                 path.c_str(), v->storage->s.c_str());
        const int idx = zip.find(e.storage);
        NN_CHECK(idx >= 0, "%s: tensor '%s' names storage '%s', which the zip lacks",
                 path.c_str(), k->s.c_str(), e.storage.c_str());
        const mz_zip_archive_file_stat st = zip.stat(idx, path, e.storage);
        NN_CHECK(st.m_method == 0, "%s: storage '%s' is compressed; torch.save stores them",
                 path.c_str(), e.storage.c_str());
        NN_CHECK(st.m_uncomp_size == sbytes,
                 "%s: the pickle says storage '%s' holds %llu bytes, the zip entry holds %llu",
                 path.c_str(), e.storage.c_str(), (unsigned long long)sbytes,
                 (unsigned long long)st.m_uncomp_size);
        NN_CHECK(e.offset <= snum && n <= snum - e.offset,
                 "%s: '%s' runs past the end of its storage", path.c_str(), k->s.c_str());
        NN_CHECK(entries_.count(k->s) == 0, "%s: duplicate tensor '%s'", path.c_str(),
                 k->s.c_str());
        order_.push_back(k->s);
        entries_[k->s] = std::move(e);
    }
}

const TorchCheckpoint::Entry& TorchCheckpoint::entry(const std::string& name) const {
    auto it = entries_.find(name);
    NN_CHECK(it != entries_.end(), "%s: no tensor '%s'", path_.c_str(), name.c_str());
    return it->second;
}

std::vector<uint8_t> TorchCheckpoint::read_raw(const std::string& name) const {
    const Entry& e = entry(name);
    Zip zip(path_);
    const int idx = zip.find(e.storage);
    NN_CHECK(idx >= 0, "%s: tensor '%s' names storage '%s', which the zip lacks", path_.c_str(),
             name.c_str(), e.storage.c_str());
    const std::vector<uint8_t> all = zip.extract(idx, path_, e.storage, kMaxElems, true);
    uint64_t begin = 0, len = 0;
    NN_CHECK(mul_fits(e.offset, e.elem_bytes(), &begin) &&
                 mul_fits((uint64_t)e.numel(), e.elem_bytes(), &len) && len <= all.size() &&
                 begin <= all.size() - len,
             "%s: tensor '%s' does not fit its %zu-byte storage", path_.c_str(), name.c_str(),
             all.size());
    return std::vector<uint8_t>(all.begin() + (long)begin, all.begin() + (long)(begin + len));
}

OnnxTensor TorchCheckpoint::read(const std::string& name) const {
    const Entry& e = entry(name);
    NN_CHECK(e.dtype == "F32" || e.dtype == "F16" || e.dtype == "BF16" || e.dtype == "F64",
             "%s: tensor '%s' is %s; only float tensors are read", path_.c_str(), name.c_str(),
             e.dtype.c_str());
    const std::vector<uint8_t> raw = read_raw(name);
    OnnxTensor t;
    t.name = name;
    t.shape = e.shape;
    const int64_t n = e.numel();
    t.data.resize((size_t)n);
    for (int64_t i = 0; i < n; ++i) {
        if (e.dtype == "F32") {
            std::memcpy(&t.data[(size_t)i], raw.data() + 4 * i, 4);
        } else if (e.dtype == "F64") {
            double v;
            std::memcpy(&v, raw.data() + 8 * i, 8);
            t.data[(size_t)i] = (float)v;
        } else {
            uint16_t h;
            std::memcpy(&h, raw.data() + 2 * i, 2);
            if (e.dtype == "F16") {
                t.data[(size_t)i] = half_to_float(h);
            } else {
                const uint32_t bits = (uint32_t)h << 16;
                std::memcpy(&t.data[(size_t)i], &bits, 4);
            }
        }
    }
    t.was_f16 = e.dtype == "F16";
    return t;
}

}  // namespace nn
