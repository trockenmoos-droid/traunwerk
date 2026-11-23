#include "Presets.h"

using namespace tw;

namespace
{
    enum { K = 0, S, C, H, O, T, W /* HOLZ */, G };
}

const std::vector<PresetDef>& Presets::all()
{
    static const std::vector<PresetDef> list
    {
        {
            "Init", 120.0f,
            { "x...x...x...x...", "....x.......x...", "................", "x.x.x.x.x.x.x.x.",
              "................", "................", "................", "................" },
            {}, { { id::feed, 0.0f } }, {}
        },
        {
            "Moos Minimal", 124.0f,
            { "x...x...x...x...", "................", "....x.......x...", "..x...x...x.x.x.",
              "......x.........", ".......x......x.", "...x..x...x..x",   "........x......." },
            { { H, pDensity, 320.0f }, { H, pGrain, 7.0f }, { H, pWear, 0.35f },
              { W, pSpray, 0.32f }, { W, pPitch, -2.0f },
              { G, pPitch, -5.0f }, { G, pDecay, 1.6f }, { G, pLevel, 0.34f }, { G, pSpray, 0.25f },
              { T, pPitch, -4.0f }, { C, pSpray, 0.18f } },
            { { id::feed, 0.32f }, { id::ghost, 0.5f }, { id::myzel, 0.45f }, { id::swing, 0.06f } },
            { { K, W, 0.3f }, { C, H, 0.25f }, { W, T, 0.2f } }
        },
        {
            "Trommelweiber", 108.0f,
            { "X.....x.X...x...", "x.xxX.x.x.xxX.xx", "................", "x.x.x.x.x.x.x.x.",
              "................", "X.......X..x....", "x...x...x...x...", "X..............." },
            { { S, pSpray, 0.2f }, { S, pDensity, 230.0f }, { S, pWear, 0.55f }, { S, pDecay, 0.3f },
              { T, pPitch, -3.0f }, { T, pDecay, 0.65f }, { T, pLevel, 0.8f },
              { H, pLevel, 0.32f }, { W, pPitch, 3.0f }, { W, pLevel, 0.45f },
              { G, pPitch, -9.0f }, { G, pDecay, 2.2f }, { G, pSpray, 0.45f }, { G, pLevel, 0.42f },
              { K, pDecay, 0.7f }, { K, pPitch, -2.0f } },
            { { id::swing, 0.12f }, { id::feed, 0.28f }, { id::myzel, 0.5f }, { id::recover, 1.0f } },
            { { S, T, 0.3f }, { K, S, 0.2f }, { T, W, 0.25f } }
        },
        {
            "Saegewerk Steeg", 96.0f,
            { "X..x..x...X..x..", "....X.......X...", "................", "xxxxxxxxxxxxxxxx",
              "................", "..........x..x..", "x.xx.x.xx.x.x.xx", "x.............." },
            { { W, pPitch, -7.0f }, { W, pScan, 0.5f }, { W, pGrain, 60.0f }, { W, pWear, 0.6f }, { W, pLevel, 0.8f },
              { H, pDensity, 70.0f }, { H, pGrain, 4.0f }, { H, pSpray, 0.65f }, { H, pLevel, 0.4f }, { H, pDecay, 0.12f },
              { K, pTone, -0.25f }, { K, pDecay, 0.45f },
              { S, pScan, 0.4f }, { S, pGrain, 45.0f }, { S, pSpray, 0.3f },
              { G, pPitch, -14.0f }, { G, pScan, 0.25f }, { G, pDecay, 2.5f }, { G, pLevel, 0.35f } },
            { { id::kompost, 0.35f }, { id::drive, 0.4f }, { id::feed, 0.2f }, { id::recover, 3.0f } },
            { { W, H, 0.3f }, { K, T, 0.2f } }
        },
        {
            "Gloeckler", 116.0f,
            { "x.......x.......", "................", "....x.......x..x", "x.x.x.x.x.x.x.x.",
              "..x...x...x...x.", "................", "x..x..x..x..x",    "x..x..x.x..x..x." },
            { { G, pDecay, 1.4f }, { G, pLevel, 0.42f }, { G, pSpray, 0.12f },
              { O, pDecay, 0.5f }, { O, pSpray, 0.2f }, { W, pLevel, 0.4f }, { W, pPitch, 5.0f } },
            { { id::myzel, 0.55f }, { id::myzelDiv, 1.0f }, { id::feed, 0.2f } },
            { { G, H, 0.5f }, { G, O, 0.3f }, { K, G, 0.25f } }
        },
        {
            "Lawinenhang", 122.0f,
            { "x.......x.......", "....x.......x...", "................", "x...x...x...x...",
              "................", "................", "................", "................" },
            {},
            { { id::feed, 0.75f }, { id::thresh, 4.0f }, { id::spur, 0.08f }, { id::ghost, 0.75f } },
            {}
        },
        {
            "Myzelgeflecht", 110.0f,
            { "x...........x...", "........x.......", "................", "................",
              "................", "................", "...x............", "................" },
            { { H, pLevel, 0.45f }, { O, pLevel, 0.4f }, { G, pLevel, 0.35f }, { G, pPitch, -2.0f } },
            { { id::myzel, 0.85f }, { id::myzelDiv, 2.0f }, { id::feed, 0.1f } },
            { { K, H, 0.85f }, { H, W, 0.55f }, { W, T, 0.55f }, { T, S, 0.3f },
              { S, O, 0.55f }, { O, G, 0.3f }, { K, C, 0.3f } }
        },
        {
            "Kompost", 100.0f,
            { "x..x....x..x....", "....x.......x...", "............x...", "x.xxx.xxx.xxx.xx",
              "................", ".......x........", "................", "................" },
            { { K, pWear, 0.6f }, { S, pWear, 0.6f }, { S, pSpray, 0.25f }, { H, pWear, 0.6f },
              { H, pSpray, 0.25f }, { C, pWear, 0.6f }, { T, pWear, 0.6f }, { S, pScan, 0.7f } },
            { { id::kompost, 0.7f }, { id::recover, 4.0f }, { id::feed, 0.2f } },
            { { K, H, 0.2f } }
        },
        {
            "Krippenstein Breaks", 172.0f,
            { "x.x.......xx....", "....x..x.x..x..x", "................", "x.x.x.x.x.x.x.x.",
              "..............x.", "................", "......x.......", "................" },
            { { S, pDecay, 0.2f }, { S, pBite, 1.2f }, { H, pLevel, 0.4f }, { K, pDecay, 0.35f },
              { W, pLevel, 0.4f } },
            { { id::swing, 0.18f }, { id::feed, 0.3f }, { id::ghost, 0.55f } },
            { { S, W, 0.2f } }
        },
    };
    return list;
}
