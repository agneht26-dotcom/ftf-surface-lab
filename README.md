# FTF Surface Lab

> **⚠️ Made with AI.** This program was written almost entirely by AI assistants (Anthropic's Claude, OpenAI's ChatGPT, and Google's Gemini). A human directed and tested it, but no human has reviewed the code line by line. Expect bugs and use it at your own risk. Keep backups of the presets you edit.
>
> Not affiliated with FreeTerraForged, Mojang or Microsoft.

A realtime terrain-surface editor for [FreeTerraForged](https://www.curseforge.com/minecraft/mc-mods/freeterraforged) (FTF) presets for Minecraft 1.21.1. Move a slider and the terrain, biomes and surface blocks of the whole world update live, with no need to create a world in the game.

This edition covers FTF with **vanilla Minecraft 1.21.1** biomes and blocks plus open-licence integrations (**Regions Unexplored 0.6.3** MIT, **RoadWeaver 2.3.1** MIT, **Dynamic Waters 13.1.1** MPL-2.0 / EUPL-1.2 / MIT, and vanilla Minecraft structures). Terrain generation follows FreeTerraForged (MIT licence). This is an unofficial tool, not made or endorsed by the FreeTerraForged developers.

## Download and run

1. Get the latest zip from **Releases** and unpack it anywhere.
2. Run `Launch Surface Lab.cmd` (or the `.exe` next to it).

Requirements: Windows 10 or 11 (64-bit) and a GPU with Vulkan 1.1 or newer. It was developed and tested on an NVIDIA RTX 3080; other GPUs are untested.

**The first start takes several minutes.** The graphics driver compiles the shaders once; later starts take about a second.

## What you can do

- Open a FreeTerraForged preset, or start from FTF's built-in JAR-backed presets via the **FTF version -> starting preset** setup (`1.0.0032`, `1.0.0013`, `1.0.0010`), with version mismatch warnings and **`File -> New...`** (`Ctrl+N`) to start fresh.
- Edit every setting live: continents (`PlateUpliftBridge`, multi/single), terrain families, mountains, rivers, lakes, climate, and filters, with Undo/Redo and World Seed Copy/Randomise buttons.
- Inspect in 3D straight away: orbit, fly (`F`), or walk on the surface Minecraft-style (`G`), with adaptive finer detail and optional TAA (`Settings`).
- Switch between block colours, biome tints, grass/foliage tints, and use **Biomes generated** to check biome coverage inside the preview domain.
- Open-licence worldgen previews (experimental / approximate warnings included): **Regions Unexplored 0.6.3** (MIT), **RoadWeaver 2.3.1** (MIT) structure roads, **Dynamic Waters 13.1.1** (MPL-2.0 / EUPL-1.2 / MIT) rivers and lakes, and **Vanilla 1.21.1 structures**.
- Measuring (`M`), pinned points (`B`), recentre (`C`), spawn (`P`), copy coordinates (`K`), project save/load, autosave, and FTF preset/datapack export.

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
| **Exact** | Terrain height with FTF `1.0.0032`, `1.0.0013`, and `1.0.0010`: compared against real `r=1000` Minecraft `1.21.1` NeoForge worlds at matching seeds and presets, 98.4-99.5% of columns match height cleanly. Vanilla Minecraft biome table lookup (99.9%+ quart-biome match) and surface blocks (checked bit for bit against Minecraft's noise/strata/scree/snow rules). |
| **Approximate** | One representative colour per block with biome tints; Regions Unexplored replacements/surface rules, Dynamic Waters carving/lakes, RoadWeaver structure road networks, and Vanilla structure pin positions (in-game chunk/placement rules may differ). |
| **Not shown** | Caves, underground ores, the Nether and the End. Trees, flowers, grass and other small surface features. |

## Limitations

- Windows only, Vulkan only.
- Includes vanilla Minecraft 1.21.1 and open-licence mod support only (**Regions Unexplored 0.6.3** MIT, **RoadWeaver 2.3.1** MIT, **Dynamic Waters 13.1.1** MPL-2.0 / EUPL-1.2 / MIT). Non-MIT / ARR biome mods and their assets are excluded from this edition.
- FTF `1.0.0032` (default), `1.0.0013`, and `1.0.0010` are supported. FTF `1.0.0` (stable) is not: its droplet erosion cannot be reproduced.
- **Biomes generated**, **Vanilla structures (1.21.1)**, **RoadWeaver**, and **Dynamic Waters** are marked experimental/approximate in the UI and can differ from full in-game chunk generation.
- Cold first launch compiles Vulkan shader pipelines once (~60-80 s / ~3.3 GiB peak RAM); subsequent cached starts take under 1 second.
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
- Regions Unexplored 0.6.3: Biome replacement rules and surface preview metadata (`LICENSES/RegionsUnexplored-MIT.txt` or `notices/RegionsUnexplored-MIT.txt`).
- RoadWeaver 2.3.1: Structure road network, bridge/tunnel planning, and config export (`LICENSES/RoadWeaver-MIT.txt` or `notices/RoadWeaver-MIT.txt`).
- Dynamic Waters 13.1.1: CPU hydrology carving, mountain rivers, watershed lakes, and `dynamicwaters-common.toml` export (`LICENSES/DynamicWaters-MPL-2.0.txt`, `LICENSES/DynamicWaters-EUPL-1.2.txt`, and `LICENSES/DynamicWaters-THIRD-PARTY-NOTICES.txt` — covering Mozilla Public License 2.0, TerraFirmaCraft EUPL-1.2 components, and MIT async rivers).
- Minecraft biome, block and surface-rule data are derived from Minecraft 1.21.1. Minecraft is a trademark of Mojang/Microsoft.
- Libraries: GLFW, Dear ImGui, volk, Vulkan-Headers, GLM, nlohmann/json and miniz, each under its own licence (see `third_party/*/LICENSE*` after bootstrapping, or `notices/` in the release zip).
