#pragma once
// A torch.save zip checkpoint (data.pkl + one raw file per storage) read as a
// state dict, without Python: the pickle is walked by a small opcode machine
// that knows only what a state dict contains, and every tensor payload is read
// straight out of its zip entry. Same surface as SafetensorsFile.
//
// Refuses anything it does not understand (an unknown opcode or class, a
// non-contiguous tensor, a compressed storage) rather than guessing; it never
// executes pickled code, so an arbitrary .pt cannot run anything here.

#include "nn/io/Onnx.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nn {

class TorchCheckpoint {
public:
    struct Entry {
        std::string          dtype;   // "F32", "F16", "BF16", "F64", "I64", "I32", "I16", "I8", "U8", "BOOL"
        std::vector<int64_t> shape;   // empty for a 0-d tensor
        std::string          storage; // zip entry holding the payload
        uint64_t             offset = 0;  // elements into that storage
        int64_t numel() const;
        uint64_t elem_bytes() const;
    };

    // Throws nn::Error naming `path` on anything malformed or unsupported. A
    // top-level {"state_dict": {...}} wrapper is looked through.
    explicit TorchCheckpoint(const std::string& path);

    const std::string& path() const { return path_; }
    bool has(const std::string& name) const { return entries_.count(name) != 0; }
    const Entry& entry(const std::string& name) const;
    // In the order the file lists them.
    const std::vector<std::string>& names() const { return order_; }

    // Converted to f32 on the host (BF16 exactly; `was_f16` only for an F16
    // payload). Throws for a missing name or a non-float dtype.
    OnnxTensor read(const std::string& name) const;

    // The payload as stored, `numel() * elem_bytes()` bytes, any dtype.
    std::vector<uint8_t> read_raw(const std::string& name) const;

private:
    std::string path_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string>               order_;
};

}  // namespace nn
