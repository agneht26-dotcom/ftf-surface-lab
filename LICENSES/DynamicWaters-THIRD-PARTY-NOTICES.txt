# Third-party notices

Dynamic Waters is licensed under the Mozilla Public License 2.0 (see [LICENSE](LICENSE)). The files below are
adapted from other projects and keep those projects' licences. Each one also says so in a notice at its top.

Paths are relative to `src/main/java/com/AdamNew/dynamicwaters/`.

## TerraFirmaCraft

- Source: https://github.com/TerraFirmaCraft/TerraFirmaCraft
- Copyright (c) TerraFirmaCraft team
- Licence: European Union Public Licence v1.2, see [LICENSES/EUPL-1.2.txt](LICENSES/EUPL-1.2.txt)

| File | Adapted from |
|---|---|
| `FastConcurrentCache.java` | `net.dries007.tfc.world.FastConcurrentCache` |
| `river/RiverContext.java` | `net.dries007.tfc.world.river.RiverInfo` |
| `river/RiverFlow.java` | `net.dries007.tfc.world.river.Flow` |
| `river/RiverGraphEdge.java` | `net.dries007.tfc.world.region.RiverEdge` |
| `river/RiverInstance.java` | `net.dries007.tfc.world.river.River` |
| `river/RiverMathUtils.java` | `net.dries007.tfc.world.river.RiverHelpers` |
| `river/RiverNetworkBuilder.java` | `net.dries007.tfc.world.region.AddRiversAndLakes` |
| `river/RiverPartition.java` | `net.dries007.tfc.world.region.RegionPartition` |
| `river/RiverPathFractal.java` | `net.dries007.tfc.world.river.MidpointFractal` |

These files remain available under the EUPL v1.2. `MainRivers.java` uses them but is not adapted from
TerraFirmaCraft.

## Dynamic Waters Async Rivers

- Source: https://github.com/erutobusiness/dynamicwaters-asyncrivers
- Licence: MIT

| File | Adapted from |
|---|---|
| `async/MountainRegionStore.java` | `net.erutobusiness.asyncrivers.MountainRegionStore` |
| `async/MountainRiverScheduler.java` | `net.erutobusiness.asyncrivers.MountainRiverScheduler` |

```
MIT License

Copyright (c) 2026 erutobusiness

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Stonecutter mod template

- Source: https://github.com/rotgruengelb/stonecutter-mod-template
- Copyright (c) 2025 murder_spagurder, rotgruengelb and stonecutter-mod-template contributors
- Licence: MIT, see [build-logic/LICENSE](build-logic/LICENSE)

The build setup comes from this template: `build-logic/`, `build.*.gradle.kts`, `settings.gradle.kts`,
`stonecutter.gradle.kts` and `gradle/`, at the root of the repository.

## Minecraft

`src/main/resources/assets/dynamicwaters/sounds/river_flow.ogg` is a sound from Minecraft (Mojang Studios). It is
not covered by the Mozilla Public License.
