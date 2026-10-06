#pragma once
// Stands in for aurora's lib/gx/gx.hpp (which needs WebGPU) when compiling lib/gx/dl.cpp for the
// model harness: dl.cpp only needs these two helpers from it.
#include <dolphin/gx.h>
#include "internal.hpp"

namespace aurora::gx {
u8 comp_type_size(GXAttr attr, GXCompType type) noexcept;
u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept;
}  // namespace aurora::gx
