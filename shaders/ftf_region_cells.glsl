#ifndef FTF_REGION_CELLS
#define FTF_REGION_CELLS
// FTF biome-region cells (Cell.biomeRegionX/Z) per sample, relative to the cell under the domain centre with 16 signed bits
// per axis (a 65536-block view at a 4-block biome size would still fit). climate.comp writes them, biomes.comp reads them.
ivec2 regionCellBase(vec2 centre){return ivec2(worldFloor(worldDiv(centre.x,graph.climateShape.x)),worldFloor(worldDiv(centre.y,graph.climateShape.x)));}
uint regionCellPack(ivec2 cell,vec2 centre){ivec2 d=cell-regionCellBase(centre);return (uint(d.x)&65535u)|(uint(d.y)<<16u);}
ivec2 regionCellUnpack(uint packed,vec2 centre){return regionCellBase(centre)+ivec2(int(packed<<16u)>>16,int(packed)>>16);}
#endif
