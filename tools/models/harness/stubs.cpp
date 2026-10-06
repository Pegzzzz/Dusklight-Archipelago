// What the model harness needs besides Dusklight's JSystem and aurora sources: allocation in
// place of JKRHeap, no-op platform calls, and the frame interpolation hooks J3D calls.
#include "JSystem/JKernel/JKRHeap.h"
#include "harness_aurora.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

size_t g_harnessAllocated = 0;

static void* HarnessAlloc(size_t size, int alignment) {
    size_t boundary = alignment < 0 ? -alignment : alignment;
    if (boundary < 32) {
        boundary = 32;
    }
    void* p = nullptr;
    if (posix_memalign(&p, boundary, size != 0 ? size : 1) != 0) {
        return nullptr;
    }
    std::memset(p, 0xCD, size);  // like a fresh game heap: nothing is zeroed for free
    g_harnessAllocated += size;
    return p;
}

alignas(64) static char s_dummyHeap[512];
JKRHeap* JKRHeap::getCurrentHeap() { return reinterpret_cast<JKRHeap*>(s_dummyHeap); }
s32 JKRHeap::getTotalFreeSize() { return 0x1000000; }

void* operator new(size_t size, JKRHeapToken) noexcept { return HarnessAlloc(size, 4); }
void* operator new(size_t size, JKRHeapToken, int alignment) noexcept { return HarnessAlloc(size, alignment); }
void* operator new(size_t size, JKRHeapToken, JKRHeap*, int alignment) noexcept {
    return HarnessAlloc(size, alignment);
}
void* operator new[](size_t size, JKRHeapToken) noexcept { return HarnessAlloc(size, 4); }
void* operator new[](size_t size, JKRHeapToken, int alignment) noexcept { return HarnessAlloc(size, alignment); }
void* operator new[](size_t size, JKRHeapToken, JKRHeap*, int alignment) noexcept {
    return HarnessAlloc(size, alignment);
}
void operator delete(void* p, JKRHeapToken) noexcept { std::free(p); }
void operator delete[](void* p, JKRHeapToken) noexcept { std::free(p); }

bool StubLogEnabled = false;

extern "C" {
void DCStoreRange(void*, uint32_t) {}
void DCStoreRangeNoSync(void*, uint32_t) {}
void DCFlushRange(void*, uint32_t) {}
void DCFlushRangeNoSync(void*, uint32_t) {}
void DCInvalidateRange(void*, uint32_t) {}
void PPCSync() {}
int OSDisableInterrupts() { return 0; }
int OSEnableInterrupts() { return 0; }
int OSRestoreInterrupts(int) { return 0; }
int OSDisableScheduler() { return 0; }
int OSEnableScheduler() { return 0; }
void OSReport(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}
}

class J3DVertexBuffer;
struct J3DDeformData;
class J3DModel;
class J3DFrameCtrl;
class J3DAnmBase;
namespace dusk::interp::vertex {
void capture(J3DVertexBuffer*, const J3DDeformData*) {}
void* positions(const J3DVertexBuffer*, void* current) { return current; }
void* normals(const J3DVertexBuffer*, void* current) { return current; }
void reset(const J3DVertexBuffer*) {}
void invalidate(const J3DDeformData*) {}
}  // namespace dusk::interp::vertex
namespace dusk::interp::material {
void record_model(J3DModel*) {}
void reset(const J3DFrameCtrl*) {}
void reset(const J3DAnmBase*) {}
}  // namespace dusk::interp::material

// aurora lib/gx/attr_fmt.cpp
namespace aurora::gx {
u8 comp_type_size(GXAttr attr, GXCompType type) noexcept {
    if (attr >= GX_VA_PNMTXIDX && attr <= GX_VA_TEX7MTXIDX) {
        return 1;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (type) {
        case GX_RGB565:
        case GX_RGBA4:
            return 2;
        case GX_RGB8:
        case GX_RGBA6:
            return 3;
        default:
            return 4;
        }
    }
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        std::fprintf(stderr, "comp_type_size: unsupported component type %d\n", type);
        std::abort();
    }
}

u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept {
    switch (attr) {
    case GX_VA_POS:
        return cnt == GX_POS_XY ? 2 : 3;
    case GX_VA_NRM:
        return cnt == GX_NRM_XYZ ? 3 : 9;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        return 1;
    default:
        if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
            return cnt == GX_TEX_S ? 1 : 2;
        }
        return 1;
    }
}
}  // namespace aurora::gx
