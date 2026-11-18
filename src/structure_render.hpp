#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace mcstruct {

enum class StructureDisplayMode {
    Icons = 0,
    SimplifiedModels = 1,
    Both = 2,
};

inline ImU32 shadeRgb(uint32_t rgb, float mul, int alpha = 245) {
    int r = std::clamp(int(((rgb >> 16) & 255) * mul), 0, 255);
    int g = std::clamp(int(((rgb >> 8) & 255) * mul), 0, 255);
    int b = std::clamp(int((rgb & 255) * mul), 0, 255);
    return IM_COL32(r, g, b, alpha);
}

// Minecraft-style pixel-block icons so every structure feels immediately recognizable.
inline void drawStructureIcon(ImDrawList* dl, ImVec2 sp, const std::string& shortId, uint32_t rgb, float r = 7.5f) {
    if (r < 4.5f) {
        dl->AddRectFilled({sp.x - r, sp.y - r}, {sp.x + r, sp.y + r}, IM_COL32(12, 16, 20, 175), 1.0f);
        dl->AddRectFilled({sp.x - r + 1.0f, sp.y - r + 1.0f}, {sp.x + r - 1.0f, sp.y + r - 1.0f}, shadeRgb(rgb, 1.1f, 245));
        return;
    }

    float u = r / 4.0f; // 8x8 pixel-block grid centered at sp
    auto blk = [&](float gx, float gy, float gw, float gh, ImU32 col) {
        dl->AddRectFilled({sp.x + gx * u, sp.y + gy * u}, {sp.x + (gx + gw) * u, sp.y + (gy + gh) * u}, col);
    };

    // Crisp square Minecraft map-pin backing
    dl->AddRectFilled({sp.x - 4.6f * u, sp.y - 4.6f * u}, {sp.x + 4.6f * u, sp.y + 4.6f * u}, IM_COL32(12, 16, 22, 205), 2.0f);
    dl->AddRect({sp.x - 4.6f * u, sp.y - 4.6f * u}, {sp.x + 4.6f * u, sp.y + 4.6f * u}, shadeRgb(rgb, 0.95f, 175), 2.0f, 0, 1.0f);

    if (shortId == "villages") {
        // Oak roof steps + cobble/oak house + emerald/wood door
        blk(-1.5f, -3.5f, 3.0f, 1.0f, IM_COL32(146, 94, 52, 255));
        blk(-2.8f, -2.5f, 5.6f, 1.0f, IM_COL32(174, 118, 68, 255));
        blk(-3.6f, -1.5f, 7.2f, 1.0f, IM_COL32(128, 82, 44, 255));
        blk(-2.8f, -0.5f, 5.6f, 2.2f, IM_COL32(214, 194, 152, 255));
        blk(-2.8f,  1.7f, 5.6f, 1.8f, IM_COL32(138, 142, 146, 255));
        blk(-0.8f,  0.6f, 1.6f, 2.9f, IM_COL32(96, 62, 34, 255));
        blk(-2.2f,  0.2f, 1.0f, 1.0f, IM_COL32(76, 224, 106, 255));
    } else if (shortId == "pillager_outposts") {
        // Cobble base + dark oak tower + overhanging deck + red Illager banner
        blk(-2.0f,  1.2f, 4.0f, 2.4f, IM_COL32(126, 130, 134, 255));
        blk(-1.6f, -1.2f, 3.2f, 2.4f, IM_COL32(92, 62, 38, 255));
        blk(-3.2f, -2.4f, 6.4f, 1.2f, IM_COL32(118, 82, 52, 255));
        blk(-2.2f, -3.6f, 4.4f, 1.2f, IM_COL32(74, 48, 30, 255));
        blk( 1.2f, -3.8f, 1.6f, 2.6f, IM_COL32(224, 54, 54, 255));
    } else if (shortId == "desert_pyramids") {
        // Stepped sandstone pyramid + twin towers + orange terracotta diamond
        blk(-1.2f, -3.4f, 2.4f, 1.2f, IM_COL32(232, 212, 160, 255));
        blk(-2.4f, -2.2f, 4.8f, 1.4f, IM_COL32(220, 198, 142, 255));
        blk(-3.6f, -0.8f, 7.2f, 4.2f, IM_COL32(206, 184, 128, 255));
        blk(-3.8f, -2.0f, 1.4f, 5.4f, IM_COL32(236, 216, 166, 255));
        blk( 2.4f, -2.0f, 1.4f, 5.4f, IM_COL32(236, 216, 166, 255));
        blk(-0.8f,  0.4f, 1.6f, 1.6f, IM_COL32(218, 114, 46, 255));
    } else if (shortId == "igloos") {
        blk(-1.8f, -2.8f, 3.6f, 1.2f, IM_COL32(246, 252, 255, 255));
        blk(-3.0f, -1.6f, 6.0f, 1.8f, IM_COL32(232, 244, 252, 255));
        blk(-3.6f,  0.2f, 7.2f, 3.2f, IM_COL32(214, 232, 246, 255));
        blk(-0.9f,  0.8f, 1.8f, 2.6f, IM_COL32(42, 54, 72, 255));
        blk(-2.6f,  0.6f, 1.0f, 1.0f, IM_COL32(132, 214, 255, 255));
    } else if (shortId == "jungle_temples") {
        blk(-1.6f, -3.2f, 3.2f, 1.4f, IM_COL32(118, 152, 92, 255));
        blk(-2.8f, -1.8f, 5.6f, 1.8f, IM_COL32(102, 134, 82, 255));
        blk(-3.6f,  0.0f, 7.2f, 3.4f, IM_COL32(88, 114, 74, 255));
        blk(-0.9f,  0.8f, 1.8f, 2.6f, IM_COL32(34, 46, 28, 255));
    } else if (shortId == "swamp_huts") {
        blk(-2.6f,  1.2f, 1.0f, 2.4f, IM_COL32(88, 62, 38, 255));
        blk( 1.6f,  1.2f, 1.0f, 2.4f, IM_COL32(88, 62, 38, 255));
        blk(-3.0f, -1.0f, 6.0f, 2.4f, IM_COL32(112, 82, 52, 255));
        blk(-3.6f, -2.4f, 7.2f, 1.4f, IM_COL32(76, 54, 34, 255));
        blk(-1.8f, -3.6f, 3.6f, 1.2f, IM_COL32(94, 68, 44, 255));
        blk(-0.6f, -0.4f, 1.2f, 1.2f, IM_COL32(168, 92, 224, 255));
    } else if (shortId == "ocean_monuments") {
        blk(-3.8f,  0.8f, 7.6f, 2.6f, IM_COL32(76, 178, 166, 255));
        blk(-2.6f, -1.2f, 5.2f, 2.0f, IM_COL32(92, 204, 190, 255));
        blk(-1.4f, -3.0f, 2.8f, 1.8f, IM_COL32(114, 226, 210, 255));
        blk(-0.7f, -0.4f, 1.4f, 1.4f, IM_COL32(246, 212, 72, 255));
    } else if (shortId == "ocean_ruins") {
        blk(-3.2f, -2.4f, 2.0f, 5.8f, IM_COL32(112, 162, 168, 255));
        blk( 1.0f, -0.6f, 2.0f, 4.0f, IM_COL32(92, 142, 152, 255));
        blk(-1.2f,  1.6f, 2.4f, 1.8f, IM_COL32(132, 184, 188, 255));
    } else if (shortId == "shipwrecks") {
        blk(-0.5f, -3.6f, 1.0f, 4.8f, IM_COL32(118, 82, 48, 255));
        blk(-2.8f, -3.0f, 2.3f, 2.6f, IM_COL32(236, 228, 212, 255));
        blk(-3.6f,  1.0f, 7.2f, 1.4f, IM_COL32(154, 108, 64, 255));
        blk(-2.4f,  2.4f, 4.8f, 1.2f, IM_COL32(126, 86, 48, 255));
    } else if (shortId == "buried_treasures") {
        // Classic Minecraft treasure map red pixel X
        for (int k = -3; k <= 3; ++k) {
            blk(float(k) - 0.5f, float(k) - 0.5f, 1.2f, 1.2f, IM_COL32(232, 58, 52, 255));
            blk(float(k) - 0.5f, float(-k) - 0.5f, 1.2f, 1.2f, IM_COL32(232, 58, 52, 255));
        }
    } else if (shortId == "woodland_mansions") {
        blk(-3.8f, -1.8f, 7.6f, 1.4f, IM_COL32(72, 48, 32, 255));
        blk(-2.4f, -3.2f, 4.8f, 1.4f, IM_COL32(88, 60, 40, 255));
        blk(-3.6f, -0.4f, 7.2f, 3.8f, IM_COL32(106, 74, 48, 255));
        blk(-2.4f,  0.4f, 1.2f, 1.2f, IM_COL32(242, 206, 114, 255));
        blk( 1.2f,  0.4f, 1.2f, 1.2f, IM_COL32(242, 206, 114, 255));
        blk(-0.6f,  1.4f, 1.2f, 2.0f, IM_COL32(146, 148, 152, 255));
    } else if (shortId == "trail_ruins") {
        blk(-3.2f, -1.4f, 2.8f, 4.8f, IM_COL32(194, 108, 72, 255));
        blk( 0.2f, -2.6f, 2.8f, 6.0f, IM_COL32(166, 92, 62, 255));
        blk(-1.4f,  0.8f, 3.2f, 2.6f, IM_COL32(126, 128, 134, 255));
    } else if (shortId == "ruined_portals") {
        blk(-2.8f, -3.4f, 1.4f, 6.8f, IM_COL32(42, 28, 62, 255));
        blk( 1.4f, -3.4f, 1.4f, 6.8f, IM_COL32(42, 28, 62, 255));
        blk(-2.8f, -3.4f, 5.6f, 1.4f, IM_COL32(42, 28, 62, 255));
        blk(-1.4f, -2.0f, 2.8f, 4.4f, IM_COL32(178, 72, 255, 255));
        blk(-3.2f,  2.2f, 6.4f, 1.2f, IM_COL32(146, 56, 46, 255));
        blk( 1.2f,  1.0f, 1.4f, 1.4f, IM_COL32(246, 206, 62, 255));
    } else if (shortId == "strongholds") {
        blk(-2.8f, -2.8f, 5.6f, 5.6f, IM_COL32(72, 92, 76, 255));
        blk(-1.8f, -1.8f, 3.6f, 3.6f, IM_COL32(36, 152, 132, 255));
        blk(-0.8f, -0.8f, 1.6f, 1.6f, IM_COL32(196, 255, 224, 255));
    } else if (shortId == "ancient_cities") {
        blk(-3.4f, -2.8f, 6.8f, 5.6f, IM_COL32(36, 44, 54, 255));
        blk(-1.6f, -1.4f, 3.2f, 3.2f, IM_COL32(42, 206, 224, 255));
    } else if (shortId == "trial_chambers") {
        blk(-3.2f, -3.2f, 6.4f, 6.4f, IM_COL32(74, 156, 134, 255));
        blk(-1.6f, -1.6f, 3.2f, 3.2f, IM_COL32(216, 124, 62, 255));
    } else {
        blk(-2.6f, -2.6f, 5.2f, 5.2f, shadeRgb(rgb, 1.0f, 255));
    }
}

namespace detail3d {

template <class ProjectFn>
inline void drawQuad3D(ImDrawList* dl, ProjectFn&& proj, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, ImU32 fill, ImU32 edge) {
    ImVec2 pa, pb, pc, pd;
    if (!proj(a, pa) || !proj(b, pb) || !proj(c, pc) || !proj(d, pd)) return;
    dl->AddQuadFilled(pa, pb, pc, pd, fill);
    if ((edge >> 24) > 0) dl->AddQuad(pa, pb, pc, pd, edge, 1.0f);
}

// Draws an axis-aligned Minecraft voxel block with shaded top & side faces (no sketchy lines).
template <class ProjectFn>
inline void drawBox3D(ImDrawList* dl, ProjectFn&& proj, glm::vec3 camWorld, glm::vec3 baseCenter, glm::vec3 size, ImU32 topCol, ImU32 sideACol, ImU32 sideBCol, ImU32 edgeCol) {
    float hx = size.x * 0.5f, hz = size.z * 0.5f, h = size.y;
    glm::vec3 p000 = baseCenter + glm::vec3(-hx, 0, -hz);
    glm::vec3 p100 = baseCenter + glm::vec3( hx, 0, -hz);
    glm::vec3 p101 = baseCenter + glm::vec3( hx, 0,  hz);
    glm::vec3 p001 = baseCenter + glm::vec3(-hx, 0,  hz);
    glm::vec3 p010 = p000 + glm::vec3(0, h, 0);
    glm::vec3 p110 = p100 + glm::vec3(0, h, 0);
    glm::vec3 p111 = p101 + glm::vec3(0, h, 0);
    glm::vec3 p011 = p001 + glm::vec3(0, h, 0);

    if (camWorld.z < baseCenter.z - hz) drawQuad3D(dl, proj, p000, p100, p110, p010, sideACol, edgeCol);
    if (camWorld.z > baseCenter.z + hz) drawQuad3D(dl, proj, p101, p001, p011, p111, sideACol, edgeCol);
    if (camWorld.x < baseCenter.x - hx) drawQuad3D(dl, proj, p001, p000, p010, p011, sideBCol, edgeCol);
    if (camWorld.x > baseCenter.x + hx) drawQuad3D(dl, proj, p100, p101, p111, p110, sideBCol, edgeCol);
    if (camWorld.y >= baseCenter.y + h * 0.5f) drawQuad3D(dl, proj, p010, p110, p111, p011, topCol, edgeCol);
}

// Helper to draw a voxel block from a single base RGB color using Minecraft-style directional face shading.
template <class ProjectFn>
inline void drawBlock3D(ImDrawList* dl, ProjectFn&& proj, glm::vec3 camWorld, glm::vec3 baseCenter, glm::vec3 size, uint32_t rgb, ImU32 edgeCol) {
    drawBox3D(dl, proj, camWorld, baseCenter, size, shadeRgb(rgb, 1.12f, 248), shadeRgb(rgb, 0.90f, 248), shadeRgb(rgb, 0.74f, 248), edgeCol);
}

} // namespace detail3d

// Block-built simplified 3D structure models at realistic Minecraft scale & biome palette.
template <class ProjectFn, class SampleGroundFn>
inline void drawStructureModel3D(ImDrawList* dl, ProjectFn&& proj, glm::vec3 camWorld, glm::vec3 basePos, const std::string& shortId, uint32_t colorRgb, const std::string& variantName, SampleGroundFn&& sampleGround) {
    float dist = glm::length(camWorld - basePos);
    if (dist > 3400.0f) {
        ImVec2 sp;
        if (proj(basePos, sp)) {
            dl->AddRectFilled({sp.x - 2.6f, sp.y - 2.6f}, {sp.x + 2.6f, sp.y + 2.6f}, IM_COL32(12, 16, 20, 180));
            dl->AddRectFilled({sp.x - 1.8f, sp.y - 1.8f}, {sp.x + 1.8f, sp.y + 1.8f}, shadeRgb(colorRgb, 1.1f, 245));
        }
        return;
    }

    float s = std::clamp(dist / 900.0f, 1.0f, 1.85f);
    ImU32 edge = dist > 520.0f ? IM_COL32(0, 0, 0, 0) : IM_COL32(12, 16, 20, 46);
    if (dist > 1750.0f) {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {7.0f * s, 4.5f * s, 7.0f * s}, colorRgb, edge);
        return;
    }

    constexpr uint32_t kOak = 0xB48652u;
    constexpr uint32_t kDarkOak = 0x4E3622u;
    constexpr uint32_t kSpruce = 0x7A5636u;
    constexpr uint32_t kAcacia = 0xBA683Cu;
    constexpr uint32_t kCobble = 0x888C90u;
    constexpr uint32_t kSandstone = 0xDECBA0u;
    constexpr uint32_t kCutSand = 0xD2BE92u;
    constexpr uint32_t kTerracotta = 0xCC6E46u;
    constexpr uint32_t kWhiteWall = 0xEAE2CEu;
    constexpr uint32_t kSnow = 0xF0F6FAu;
    constexpr uint32_t kIce = 0x9AD2F6u;
    constexpr uint32_t kWater = 0x3A7AD8u;
    constexpr uint32_t kMossy = 0x6E8E5Cu;
    constexpr uint32_t kPrismarine = 0x52BBAEu;
    constexpr uint32_t kGold = 0xF4D046u;
    constexpr uint32_t kObsidian = 0x261A38u;
    constexpr uint32_t kPortal = 0xAD46FAu;
    constexpr uint32_t kRed = 0xDC3E3Eu;
    constexpr uint32_t kCyan = 0x38A2B8u;
    constexpr uint32_t kWhite = 0xEAE4D6u;
    constexpr uint32_t kDirtPath = 0x9A7D4Au;
    constexpr uint32_t kFarmland = 0x5E4026u;
    constexpr uint32_t kWheat = 0xC8B048u;
    constexpr uint32_t kCrops = 0x649E38u;
    constexpr uint32_t kLava = 0xF46A1Cu;

    auto groundAt = [&](float wx, float wz) -> float {
        float gy = basePos.y;
        sampleGround(glm::vec2(wx, wz), gy);
        return gy;
    };

    // Pitched gabled roof (stepped stairs tapering along either X or Z)
    auto drawGabledRoof = [&](glm::vec3 roofBase, float w, float d, float rh, uint32_t roofCol, bool ridgeAlongZ) {
        if (ridgeAlongZ) {
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase, {w * 1.18f, rh * 0.34f, d * 1.12f}, roofCol, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase + glm::vec3(0, rh * 0.34f, 0), {w * 0.78f, rh * 0.34f, d * 1.08f}, roofCol, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase + glm::vec3(0, rh * 0.68f, 0), {w * 0.38f, rh * 0.32f, d * 1.04f}, roofCol, edge);
        } else {
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase, {w * 1.12f, rh * 0.34f, d * 1.18f}, roofCol, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase + glm::vec3(0, rh * 0.34f, 0), {w * 1.08f, rh * 0.34f, d * 0.78f}, roofCol, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, roofBase + glm::vec3(0, rh * 0.68f, 0), {w * 1.04f, rh * 0.32f, d * 0.38f}, roofCol, edge);
        }
    };

    if (shortId == "villages") {
        bool isDesert = (variantName.find("desert") != std::string::npos);
        bool isSavanna = (variantName.find("savanna") != std::string::npos);
        bool isSnowy = (variantName.find("snowy") != std::string::npos);
        bool isTaiga = (variantName.find("taiga") != std::string::npos);

        uint32_t vBase = kCobble, vWall = kOak, vWallAlt = kWhiteWall, vRoof = kOak, vTrim = kOak, vPath = kDirtPath;
        if (isDesert) {
            vBase = kCutSand; vWall = kSandstone; vWallAlt = kTerracotta; vRoof = kCutSand; vTrim = kTerracotta; vPath = kSandstone;
        } else if (isSavanna) {
            vBase = kCobble; vWall = kAcacia; vWallAlt = kTerracotta; vRoof = kAcacia; vTrim = kSpruce; vPath = kDirtPath;
        } else if (isSnowy) {
            vBase = kCobble; vWall = kSpruce; vWallAlt = kSnow; vRoof = kSnow; vTrim = kIce; vPath = kSnow;
        } else if (isTaiga) {
            vBase = kCobble; vWall = kSpruce; vWallAlt = kMossy; vRoof = kDarkOak; vTrim = kSpruce; vPath = kDirtPath;
        }

        uint32_t seed = uint32_t(int(std::floor(basePos.x)) * 73856093) ^ uint32_t(int(std::floor(basePos.z)) * 19349663);
        auto nextRand = [&]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return float((seed >> 8) & 0xFFFFu) / 65535.0f;
        };

        auto drawPlinthHouse = [&](float ox, float oz, float w, float h, float d, uint32_t wallCol, uint32_t roofCol, bool ridgeZ) {
            float wx = basePos.x + ox * s, wz = basePos.z + oz * s;
            float gy = groundAt(wx, wz);
            float foundH = 3.5f * s;
            detail3d::drawBlock3D(dl, proj, camWorld, {wx, gy - foundH + 0.5f * s, wz}, {w * s, foundH, d * s}, vBase, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {wx, gy + 0.5f * s, wz}, {w * s, h * s, d * s}, wallCol, edge);
            if (isDesert) {
                // Flat sandstone roof with terracotta band & stepped parapet
                detail3d::drawBlock3D(dl, proj, camWorld, {wx, gy + (0.5f + h) * s, wz}, {w * 1.04f * s, 0.7f * s, d * 1.04f * s}, vTrim, edge);
                detail3d::drawBlock3D(dl, proj, camWorld, {wx, gy + (1.2f + h) * s, wz}, {w * 0.88f * s, 0.8f * s, d * 0.88f * s}, roofCol, edge);
            } else {
                drawGabledRoof({wx, gy + (0.5f + h) * s, wz}, w * s, d * s, 2.8f * s, roofCol, ridgeZ);
            }
        };

        // 1. Organic terrain-draped village paths (4 varied arms, 3 segments each)
        float baseAngle = nextRand() * 1.57f;
        for (int arm = 0; arm < 4; ++arm) {
            float ang = baseAngle + float(arm) * 1.570796f + (nextRand() - 0.5f) * 0.45f;
            float armLen = (24.0f + nextRand() * 12.0f);
            float dx = std::cos(ang), dz = std::sin(ang);
            for (int seg = 0; seg < 4; ++seg) {
                float rMid = (float(seg) + 0.5f) * (armLen * 0.25f);
                float bend = (seg >= 2 ? (nextRand() - 0.5f) * 3.5f : 0.0f);
                float px = basePos.x + (dx * rMid - dz * bend) * s;
                float pz = basePos.z + (dz * rMid + dx * bend) * s;
                float py = groundAt(px, pz);
                float pw = (std::abs(dx) > std::abs(dz)) ? (armLen * 0.27f * s) : (3.2f * s);
                float pd = (std::abs(dx) > std::abs(dz)) ? (3.2f * s) : (armLen * 0.27f * s);
                detail3d::drawBlock3D(dl, proj, camWorld, {px, py - 0.15f * s, pz}, {pw, 0.45f * s, pd}, vPath, edge);
            }
        }

        // 2. Town Center Well / Meeting Plaza
        float cy = groundAt(basePos.x, basePos.z);
        detail3d::drawBlock3D(dl, proj, camWorld, {basePos.x, cy - 1.0f * s, basePos.z}, {5.0f * s, 2.0f * s, 5.0f * s}, vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {basePos.x, cy + 1.0f * s, basePos.z}, {3.0f * s, 0.25f * s, 3.0f * s}, kWater, edge);
        for (int cx : {-1, 1}) for (int cz : {-1, 1})
            detail3d::drawBlock3D(dl, proj, camWorld, {basePos.x + cx * 1.7f * s, cy + 1.0f * s, basePos.z + cz * 1.7f * s}, {0.45f * s, 2.6f * s, 0.45f * s}, vTrim, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {basePos.x, cy + 3.6f * s, basePos.z}, {4.4f * s, 0.9f * s, 4.4f * s}, vRoof, edge);

        // 3. Landmark Church / Desert Temple along first arm
        float chAng = baseAngle + 0.42f;
        float chX = basePos.x + std::cos(chAng) * 13.5f * s;
        float chZ = basePos.z + std::sin(chAng) * 13.5f * s;
        float chY = groundAt(chX, chZ);
        detail3d::drawBlock3D(dl, proj, camWorld, {chX, chY - 3.0f * s, chZ}, {6.0f * s, 3.5f * s, 8.0f * s}, vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {chX, chY + 0.5f * s, chZ}, {5.5f * s, 5.0f * s, 7.5f * s}, isDesert ? kSandstone : vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {chX, chY + 5.5f * s, chZ - 1.6f * s}, {4.2f * s, 5.5f * s, 4.2f * s}, isDesert ? kCutSand : vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {chX, chY + 11.0f * s, chZ - 1.6f * s}, {4.6f * s, 1.2f * s, 4.6f * s}, isDesert ? kTerracotta : vRoof, edge);

        // 4. Blacksmith / Forge with chimney & lava hearth
        float smAng = baseAngle + 2.05f;
        float smX = basePos.x + std::cos(smAng) * 12.5f * s;
        float smZ = basePos.z + std::sin(smAng) * 12.5f * s;
        float smY = groundAt(smX, smZ);
        detail3d::drawBlock3D(dl, proj, camWorld, {smX, smY - 3.0f * s, smZ}, {8.5f * s, 3.5f * s, 6.5f * s}, vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {smX - 1.2f * s, smY + 0.5f * s, smZ}, {5.2f * s, 3.8f * s, 5.5f * s}, vWall, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {smX + 2.2f * s, smY + 0.5f * s, smZ}, {2.6f * s, 1.4f * s, 2.6f * s}, kLava, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {smX + 2.5f * s, smY + 0.5f * s, smZ - 1.5f * s}, {1.6f * s, 5.2f * s, 1.6f * s}, vBase, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, {smX, smY + 4.3f * s, smZ}, {8.6f * s, 0.9f * s, 6.6f * s}, vRoof, edge);

        // 5. 4 Varied Villager Houses + Library along arms
        for (int hi = 0; hi < 4; ++hi) {
            float ha = baseAngle + float(hi) * 1.55f + (nextRand() - 0.5f) * 0.45f;
            float hr = 14.0f + nextRand() * 11.0f;
            float hw = 5.0f + nextRand() * 2.5f;
            float hd = 5.0f + nextRand() * 2.5f;
            float hh = 3.8f + nextRand() * 1.6f;
            uint32_t wCol = (hi % 2 == 0) ? vWall : vWallAlt;
            drawPlinthHouse(std::cos(ha) * hr, std::sin(ha) * hr, hw, hh, hd, wCol, vRoof, (hi & 1) == 0);
        }

        // 6. Farm Plot / Desert Market Stall
        float fmAng = baseAngle + 3.65f;
        float fmX = basePos.x + std::cos(fmAng) * 17.0f * s;
        float fmZ = basePos.z + std::sin(fmAng) * 17.0f * s;
        float fmY = groundAt(fmX, fmZ);
        if (isDesert) {
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX, fmY - 1.0f * s, fmZ}, {7.0f * s, 1.5f * s, 5.0f * s}, kSandstone, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX - 1.5f * s, fmY + 2.6f * s, fmZ}, {3.5f * s, 0.6f * s, 4.5f * s}, kTerracotta, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX + 1.8f * s, fmY + 2.6f * s, fmZ}, {3.0f * s, 0.6f * s, 4.5f * s}, kCyan, edge);
        } else {
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX, fmY - 1.0f * s, fmZ}, {8.0f * s, 1.4f * s, 6.0f * s}, vTrim, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX, fmY + 0.4f * s, fmZ}, {7.0f * s, 0.35f * s, 5.0f * s}, kFarmland, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX - 1.6f * s, fmY + 0.75f * s, fmZ}, {2.2f * s, 0.55f * s, 4.4f * s}, kWheat, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX + 1.6f * s, fmY + 0.75f * s, fmZ}, {2.2f * s, 0.55f * s, 4.4f * s}, kCrops, edge);
            detail3d::drawBlock3D(dl, proj, camWorld, {fmX, fmY + 0.5f * s, fmZ}, {0.9f * s, 0.3f * s, 4.6f * s}, kWater, edge);
        }
    } else if (shortId == "pillager_outposts") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {9.0f * s, 5.0f * s, 9.0f * s}, kCobble, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 5.0f * s, 0), {7.0f * s, 10.0f * s, 7.0f * s}, kDarkOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 15.0f * s, 0), {11.0f * s, 3.5f * s, 11.0f * s}, kOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 18.5f * s, 0), {8.0f * s, 3.0f * s, 8.0f * s}, kDarkOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(3.5f * s, 18.5f * s, 0), {1.6f * s, 5.5f * s, 1.6f * s}, kRed, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(-10.0f * s, 0, 5.0f * s), {5.0f * s, 4.0f * s, 5.0f * s}, kWhite, edge);
    } else if (shortId == "desert_pyramids") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {21.0f * s, 4.0f * s, 21.0f * s}, kSandstone, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 4.0f * s, 0), {15.0f * s, 3.5f * s, 15.0f * s}, kSandstone, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 7.5f * s, 0), {9.0f * s, 3.0f * s, 9.0f * s}, kTerracotta, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(-8.5f * s, 0, 8.5f * s), {4.5f * s, 9.5f * s, 4.5f * s}, kSandstone, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3( 8.5f * s, 0, 8.5f * s), {4.5f * s, 9.5f * s, 4.5f * s}, kSandstone, edge);
    } else if (shortId == "igloos") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {9.0f * s, 3.0f * s, 9.0f * s}, kSnow, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 3.0f * s, 0), {6.0f * s, 2.5f * s, 6.0f * s}, kSnow, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 0, 5.5f * s), {4.0f * s, 2.8f * s, 3.8f * s}, kSnow, edge);
    } else if (shortId == "jungle_temples") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {14.0f * s, 5.0f * s, 14.0f * s}, kMossy, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 5.0f * s, 0), {10.0f * s, 4.0f * s, 10.0f * s}, kCobble, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 9.0f * s, 0), {6.0f * s, 3.0f * s, 6.0f * s}, kMossy, edge);
    } else if (shortId == "swamp_huts") {
        for (int sx : {-1, 1}) for (int sz : {-1, 1})
            detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(sx * 3.0f * s, 0, sz * 3.0f * s), {1.2f * s, 3.5f * s, 1.2f * s}, kOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 3.5f * s, 0), {7.5f * s, 4.0f * s, 7.5f * s}, kSpruce, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 7.5f * s, 0), {8.8f * s, 1.8f * s, 8.8f * s}, kDarkOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 9.3f * s, 0), {5.5f * s, 1.5f * s, 5.5f * s}, kDarkOak, edge);
    } else if (shortId == "ocean_monuments") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {26.0f * s, 5.0f * s, 26.0f * s}, kPrismarine, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 5.0f * s, 0), {18.0f * s, 4.5f * s, 18.0f * s}, kPrismarine, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 9.5f * s, 0), {10.0f * s, 4.0f * s, 10.0f * s}, kPrismarine, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 13.5f * s, 0), {4.0f * s, 3.5f * s, 4.0f * s}, kGold, edge);
    } else if (shortId == "ocean_ruins" || shortId == "trail_ruins") {
        uint32_t c1 = (shortId == "trail_ruins") ? kTerracotta : kPrismarine;
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(-3.0f * s, 0, -2.0f * s), {3.0f * s, 6.0f * s, 3.0f * s}, c1, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3( 3.0f * s, 0,  1.5f * s), {3.0f * s, 4.0f * s, 3.0f * s}, kCobble, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 0, 3.5f * s), {7.0f * s, 2.2f * s, 2.5f * s}, c1, edge);
    } else if (shortId == "shipwrecks") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {7.0f * s, 4.0f * s, 18.0f * s}, kOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 4.0f * s, 0), {1.4f * s, 11.0f * s, 1.4f * s}, kDarkOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 8.0f * s, 0.8f * s), {8.0f * s, 5.5f * s, 1.2f * s}, kWhite, edge);
    } else if (shortId == "woodland_mansions") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {32.0f * s, 3.5f * s, 24.0f * s}, kCobble, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 3.5f * s, 0), {30.0f * s, 11.0f * s, 22.0f * s}, kDarkOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 14.5f * s, 0), {32.0f * s, 3.5f * s, 24.0f * s}, kSpruce, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 18.0f * s, 0), {22.0f * s, 3.5f * s, 16.0f * s}, kSpruce, edge);
    } else if (shortId == "ruined_portals") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {10.0f * s, 1.5f * s, 7.0f * s}, kTerracotta, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(-3.2f * s, 1.5f * s, 0), {2.0f * s, 8.0f * s, 2.0f * s}, kObsidian, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3( 3.2f * s, 1.5f * s, 0), {2.0f * s, 8.0f * s, 2.0f * s}, kObsidian, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 9.0f * s, 0), {8.4f * s, 1.8f * s, 2.0f * s}, kObsidian, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 1.5f * s, 0), {4.4f * s, 7.5f * s, 0.8f * s}, kPortal, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(5.0f * s, 1.5f * s, 2.0f * s), {2.0f * s, 2.0f * s, 2.0f * s}, kGold, edge);
    } else if (shortId == "buried_treasures") {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {3.0f * s, 2.0f * s, 2.2f * s}, kOak, edge);
        detail3d::drawBlock3D(dl, proj, camWorld, basePos + glm::vec3(0, 1.1f * s, 1.2f * s), {0.7f * s, 0.7f * s, 0.35f * s}, kGold, edge);
    } else {
        detail3d::drawBlock3D(dl, proj, camWorld, basePos, {6.0f * s, 4.5f * s, 6.0f * s}, colorRgb, edge);
    }
}

} // namespace mcstruct
