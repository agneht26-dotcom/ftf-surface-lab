# FTF Surface Lab

> **⚠️ Made with AI.** This program was written almost entirely by AI assistants (Anthropic's Claude and OpenAI's ChatGPT). A human directed and tested it, but no human has reviewed the code line by line. Expect bugs and use it at your own risk. Keep backups of the presets you edit.
>
> Not affiliated with FreeTerraForged, Mojang or Microsoft.

A realtime terrain-surface editor for [FreeTerraForged](https://www.curseforge.com/minecraft/mc-mods/freeterraforged) (FTF) presets for Minecraft 1.21.1. Move a slider and the terrain, biomes and surface blocks of the whole world update live, with no need to create a world in the game.

This edition covers FTF with **vanilla Minecraft 1.21.1** biomes and blocks. Terrain generation follows FreeTerraForged (MIT licence). This is an unofficial tool, not made or endorsed by the FreeTerraForged developers.

## Download and run

1. Get the latest zip from **Releases** and unpack it anywhere.
2. Run `Launch Surface Lab.cmd` (or the `.exe` next to it).

Requirements: Windows 10 or 11 (64-bit) and a GPU with Vulkan 1.1 or newer. It was developed and tested on an NVIDIA RTX 3080; other GPUs are untested.

**The first start takes several minutes.** The graphics driver compiles the shaders once; later starts take about a second.

## What you can do

- Open an FTF preset (or start from FTF's built-in presets) and edit every setting: continents, terrain, mountains, rivers, lakes, climate and filters.
- See the result in 3D straight away: orbit, fly (`F`) or walk on the surface Minecraft-style (`G`).
- Biome colours, block colours or grass and foliage tints, and a **Biomes generated** count that shows which biomes appear within the preview world size.
- Measuring (`M`), points (`B`), recentre (`C`), spawn (`P`), undo/redo, projects and autosave.
- Export the edited preset, or a datapack, for use in the game.

## How it works

The whole pipeline runs on the GPU as Vulkan compute shaders, one terrain sample per column:

1. **Terrain:** FTF's terrain graphs (continents, mountains, volcanoes, rivers, lakes, wetlands, climate and world filters such as erosion), ported from FTF's code with the same seeds, noise and order of operations.
2. **Biomes:** Minecraft 1.21.1's own biome table and climate lookup, run on FTF's climate values.
3. **Surface blocks:** Minecraft's surface rules with its seeded noises, surface depth, temperature and preliminary surface, plus FTF's strata, scree, snow and river water.
4. **Display:** a level-of-detail mesh (finer near the camera), block or biome colours, water, fog and clouds.

The ported parts were checked against the real FTF and Minecraft classes, value for value.

## Exact, approximate, not shown

| | |
|---|---|
| **Exact** | Terrain height with FTF 1.0.0010: compared with a real world from the same seed and preset, 94-99% of columns have the exact height in the test areas. Biome selection (Minecraft's table on FTF's climate, without the game's 4-block biome blending). Vanilla surface blocks, checked bit for bit against the game's classes. |
| **Exact port, not yet compared with a world** | Terrain height with FTF 1.0.0013 and 1.0.0014 (their filter settings). |
| **Approximate** | One colour per block, with biome tints. Between samples the mesh is interpolated: a coarser grid shows the same terrain at lower resolution. |
| **Not shown** | Caves, underground ores, the Nether and the End. Trees, structures, flowers, grass and other small decorations. |

## Limitations

- Windows only, Vulkan only.
- Vanilla Minecraft biomes only. Biome mods are not included in this edition.
- FTF 1.0.0014, 1.0.0013 and 1.0.0010 are supported. FTF 1.0.0 (stable) is not: its droplet erosion cannot be reproduced.
- The **Biomes generated** count samples the world on a grid, so very small biomes can fall between samples. Use more samples to catch them.
- Large preview sizes at high resolution need a lot of video memory and can take seconds to build.
- It previews the surface only. The game's world is the final word.

## Build from source

Needs Windows, the Visual Studio 2022 (or newer) C++ build tools and Python 3.

```
powershell -File build.ps1 -Bootstrap
```

`-Bootstrap` downloads CMake, glslang and the libraries listed below into `.tools` and `third_party` (first time only). The program is `build/FTFSurface.exe`; run it from `build` (it reads `assets` and `shaders` next to it).

## Credits and licences

- Terrain generation: [FreeTerraForged](https://github.com/etcodehome/freeterraforged) (MIT licence, copyright ReTerraForged; full text in `LICENSES/FreeTerraForged-MIT.txt`, or `notices/` in the release zip). The terrain code here is a port of FTF's, and FTF's presets in `assets/ftf-presets` are FTF's own. Unofficial: not made or endorsed by the FreeTerraForged developers.
- Erosion filter: `shaders/ftf_erosion.glsl` is a derived work of FTF's `Erosion.java` (its erosion filter re-implemented in GLSL for the GPU), which is based on Rune Skovbo Johansen's (runevision) Phacelle Noise / Fast & Gorgeous Erosion Filter and is under the Mozilla Public License 2.0. That one file is therefore also under MPL-2.0 (notice, what was changed and the original acknowledgement in `LICENSES/Erosion-MPL-2.0.txt`). It holds only the erosion filter; FTF's other filters (cliff relaxation, beach detection, steepness, terrain ceiling) are in other files under FTF's MIT licence.
- Minecraft biome, block and surface-rule data are derived from Minecraft 1.21.1. Minecraft is a trademark of Mojang/Microsoft.
- Libraries: GLFW, Dear ImGui, volk, Vulkan-Headers, GLM, nlohmann/json and miniz, each under its own licence (see `third_party/*/LICENSE*` after bootstrapping, or `notices/` in the release zip).
