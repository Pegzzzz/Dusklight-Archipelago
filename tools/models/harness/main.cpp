// Test-only: read the loaded data's internals directly
#define private public
#define protected public
// Loads a model archive the way the game does (dRes_info_c::loadResource + loaderBasicBmd for a
// BMDR directory, then mDoExt_J3DModel__create), with Dusklight's own JSystem and aurora display
// list code, and prints what came out. Game-only calls are not linked: reaching one crashes,
// which is a finding.
//
//   ap_model_harness <archive.arc>      (prints OK and exits 0 when the model loads)
#include "JSystem/J3DGraphLoader/J3DModelLoader.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DMaterialAnm.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DShapeDraw.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "aurora/dl.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

extern size_t g_harnessAllocated;

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: j3d_harness <file.arc>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<u8> arc((std::istreambuf_iterator<char>(in)), {});
    // RARC, as JKRArchive reads it
    if (arc.size() < 0x40 || std::memcmp(arc.data(), "RARC", 4) != 0) {
        std::printf("ERROR not a RARC\n");
        return 1;
    }
    const u8* info = arc.data() + 0x20;
    const u32 numNodes = be32(info), nodeOff = be32(info + 4), numFiles = be32(info + 8),
              fileOff = be32(info + 12), strOff = be32(info + 20);
    const u8* dataStart = info + be32(arc.data() + 0x0C);
    const char* strings = (const char*)info + strOff;
    void* bmd = nullptr;
    int bmdIndex = -1;
    for (u32 n = 0; n < numNodes; n++) {
        const u8* node = info + nodeOff + n * 16;
        const u32 type = be32(node);
        const u16 count = be16(node + 10);
        const u32 first = be32(node + 12);
        std::printf("node %u '%.4s' name '%s' entries %u first %u\n", n, (const char*)node, strings + be32(node + 4),
            count, first);
        for (u32 i = first; i < first + count; i++) {
            const u8* e = info + fileOff + i * 20;
            const u32 flags = be32(e + 4) >> 24;
            std::printf("  entry %u id %04x flags %02x name '%s' data %u size %u\n", i, be16(e), flags,
                strings + (be32(e + 4) & 0xFFFFFF), be32(e + 8), be32(e + 12));
            if ((flags & 1) && type == 'BMDR' && bmd == nullptr) {
                const u32 size = be32(e + 12);
                void* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
                std::memcpy(copy, dataStart + be32(e + 8), size);
                bmd = copy;
                bmdIndex = (int)i;
            }
        }
    }
    if (bmd == nullptr) {
        std::printf("ERROR no BMDR file\n");
        return 1;
    }
    std::printf("model at resource index %d\n", bmdIndex);

    // dRes_info_c::loaderBasicBmd('BMDR')
    J3DModelData* data = J3DModelLoaderDataBase::load(bmd, 0x59020010);
    if (data == nullptr) {
        std::printf("ERROR loader returned NULL\n");
        return 1;
    }
    std::printf("joints %u materials %u shapes %u textures %u\n", data->getJointNum(), data->getMaterialNum(),
        data->getShapeNum(), data->getTexture() ? data->getTexture()->getNum() : 0);
    auto& vtx = data->getVertexData();
    std::printf("vertices %u packets %u positions %u colors %u normals %u\n", vtx.getVtxNum(), vtx.mPacketNum,
        vtx.mVtxArrNum[0], vtx.getColNum(), vtx.getNrmNum());
    for (u16 i = 0; i < data->getJointNum(); i++) {
        J3DJoint* j = data->getJointNodePointer(i);
        std::printf("joint %u kind %u min (%.2f %.2f %.2f) max (%.2f %.2f %.2f) radius %.2f\n", i, j->getKind(),
            j->getMin()->x, j->getMin()->y, j->getMin()->z, j->getMax()->x, j->getMax()->y, j->getMax()->z,
            j->getRadius());
    }
    for (u16 i = 0; i < data->getMaterialNum(); i++) {
        J3DMaterial* m = data->getMaterialNodePointer(i);
        J3DColorChan* c0 = m->getColorChan(0);
        J3DColorChan* c1 = m->getColorChan(1);
        std::printf("material %u '%s' mode %u cull %u chans %u stages %u texgens %u | COLOR0 light %u matsrc %u mask %02x"
                    " | ALPHA0 light %u matsrc %u | zmode %04x blend %u alphacomp %04x joint %p shape %p\n",
            i, data->getMaterialName()->getName(i), m->getMaterialMode(), m->getColorBlock()->getCullMode(),
            m->getColorBlock()->getColorChanNum(), m->getTevBlock()->getTevStageNum(),
            m->getTexGenBlock()->getTexGenNum(), c0->getEnable(), c0->getMatSrc(), c0->getLightMask(),
            c1->getEnable(), c1->getMatSrc(), m->getPEBlock()->getZMode()->mZModeID,
            m->getPEBlock()->getBlend()->getBlendMode(), m->getPEBlock()->getAlphaComp()->mID,
            (void*)m->getJoint(), (void*)m->getShape());
        J3DTevStage* st = m->getTevBlock()->getTevStage(0);
        J3DTevOrder* order = m->getTevBlock()->getTevOrder(0);
        std::printf("  stage0 colorAB %02x colorCD %02x colorOp %02x alphaAB %02x swap %02x alphaOp %02x |"
                    " order coord %02x map %02x chan %02x\n",
            st->mTevColorAB, st->mTevColorCD, st->mTevColorOp, st->mTevAlphaAB, st->mTevSwapModeInfo,
            st->mTevAlphaOp, order->mTexCoord, order->getTexMap(), order->mColorChan);
    }
    for (u16 i = 0; i < data->getShapeNum(); i++) {
        J3DShape* s = data->getShapeNodePointer(i);
        std::printf("shape %u groups %u radius %.2f min (%.2f %.2f %.2f) max (%.2f %.2f %.2f) material %p\n", i,
            s->getMtxGroupNum(), s->mRadius, s->getMin()->x, s->getMin()->y, s->getMin()->z, s->getMax()->x,
            s->getMax()->y, s->getMax()->z, (void*)s->getMaterial());
        for (const GXVtxDescList* d = s->getVtxDesc(); d->attr != GX_VA_NULL; d++) {
            std::printf("  desc attr %d type %d\n", d->attr, d->type);
        }
        for (u16 g = 0; g < s->getMtxGroupNum(); g++) {
            J3DShapeDraw* draw = s->getShapeDraw(g);
            const u8* dl = (const u8*)draw->getDisplayList();
            const u32 size = draw->getDisplayListSize();
            std::printf("  group %u display list %u bytes (after Dusklight's optimize), first opcode %02x\n", g, size,
                size ? dl[0] : 0);
            // Read it back: count triangles and check indices are in range
            aurora::gx::dl::Reader reader(dl, size, s->getVtxDesc());
            u32 triangles = 0, maxPos = 0, commands = 0;
            bool bad = false;
            while (auto cmd = reader.next()) {
                commands++;
                if (cmd->kind == aurora::gx::dl::Command::Kind::DrawIndexed) {
                    triangles += cmd->draw.indexCount / 3;
                    for (u32 v = 0; v < cmd->draw.vtxCount; v++) {
                        const u16 pos = cmd->draw.attr_idx(v, GX_VA_POS);
                        const u16 clr = cmd->draw.attr_idx(v, GX_VA_CLR0);
                        if (pos > maxPos) maxPos = pos;
                        if (pos >= vtx.mVtxArrNum[0] || clr >= vtx.getColNum()) bad = true;
                    }
                } else if (cmd->kind == aurora::gx::dl::Command::Kind::Draw) {
                    aurora::gx::dl::expand_triangles(cmd->draw.prim, cmd->draw.vtxCount,
                        [&](u16, u16, u16) { triangles++; });
                    for (u32 v = 0; v < cmd->draw.vtxCount; v++) {
                        const u16 pos = cmd->draw.attr_idx(v, GX_VA_POS);
                        const u16 clr = cmd->draw.attr_idx(v, GX_VA_CLR0);
                        if (pos > maxPos) maxPos = pos;
                        if (pos != clr || pos >= vtx.getVtxNum()) bad = true;
                    }
                }
            }
            std::printf("  commands %u triangles %u max position index %u reader %s indices %s\n", commands, triangles,
                maxPos, reader.failed() ? "FAILED" : "ok", bad ? "OUT OF RANGE" : "in range");
            if (reader.failed() || bad) return 1;
        }
    }
    // The rest of loaderBasicBmd
    for (u16 i = 0; i < data->getMaterialNum(); i++) {
        J3DMaterial* material = data->getMaterialNodePointer(i);
        u8 lightMask = material->getColorChan(0)->getLightMask();
        material->getColorChan(0)->setLightMask(lightMask & 0xF);
        material->change();
        material->setMaterialAnm(JKR_NEW J3DMaterialAnm());
    }
    if (data->newSharedDisplayList(J3DMdlFlag_UseSingleDL) != kJ3DError_Success) {
        std::printf("ERROR newSharedDisplayList\n");
        return 1;
    }
    data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
    data->makeSharedDL();
    std::printf("shared display lists made\n");

    // mDoExt_J3DModel__create(modelData, 0x80000, 0x11000084), as daDitem_c / daItem_c do
    const size_t beforeInstance = g_harnessAllocated;
    J3DModel* model = JKR_NEW J3DModel();
    u32 flag = 0x80000;
    if (data->getMaterialNodePointer(0)->getSharedDisplayListObj() != NULL) {
        flag = data->isLocked() ? J3DMdlFlag_UseSharedDL : J3DMdlFlag_DifferedDLBuffer;
    }
    if (model->entryModelData(data, flag, 1) != kJ3DError_Success) {
        std::printf("ERROR entryModelData\n");
        return 1;
    }
    if (flag == J3DMdlFlag_DifferedDLBuffer && model->newDifferedDisplayList(0x11000084) != kJ3DError_Success) {
        std::printf("ERROR newDifferedDisplayList\n");
        return 1;
    }
    std::printf("model instance created (flag %x): %zu bytes for the instance, %zu in total\n", flag,
        g_harnessAllocated - beforeInstance, g_harnessAllocated);
    std::printf("OK\n");
    return 0;
}
