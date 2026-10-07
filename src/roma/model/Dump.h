#pragma once
// SS_ROMA_DUMP=<dir>: one .npy per stage, for tools/roma/compare_torch.py, and
// manifest.json naming the files this run wrote and how it ended.
//
// A forward pass must never round-trip through the host, so nothing here runs
// unless the variable is set.

#include "nn/Tensor.h"

#include <vector>

namespace roma {

// The first call (of any of these) empties a stale dump directory and writes
// an unfinished manifest; call it at startup so a run that dies early leaves one.
bool dump_enabled();
void dump_tensor(const char* name, const nn::Tensor& t, const std::vector<int64_t>& shape);
void dump_host(const char* name, const float* data, const std::vector<int64_t>& shape);
// Marks the manifest finished with the process's exit status. A run that
// never calls it reads as unfinished, which compare_torch.py refuses.
void dump_finish(int exit_status);

}  // namespace roma
