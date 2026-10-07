#pragma once
// The names roma/ borrows from the inference layer (src/nn/), as loma/Common.h
// does for src/loma/.

#include "nn/core/Error.h"
#include "nn/core/Half.h"
#include "nn/core/Log.h"
#include "nn/core/Parallel.h"

namespace nn { namespace vk {} }

namespace roma {

namespace vk = ::nn::vk;
using ::nn::Error;
using ::nn::fail;
using ::nn::now_ms;
using ::nn::parallel_for;

}  // namespace roma
