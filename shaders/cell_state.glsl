// Surface metadata retained for climate, hydrology and tile filters.
// terrain low byte: land0,badlands1,chain2,volcano3,deep4,shallow5,coast6,island7,beach8,islandMountain9,river10,wetland11,lake12.
// bit256 reserves the source erosion mask. Normalized river mask/sediment retain float precision.
struct CellState {float riverMask;float sediment;uint terrain;float waterHeight;float gateMin;float erosion;float weirdness;uint biome;};
#ifdef FTF_NO_GRAPH_CELL_HELPERS
layout(std430,set=0,binding=5) readonly buffer CellStates {CellState cells[];};
#else
layout(std430,set=0,binding=5) buffer CellStates {CellState cells[];};
#endif
// Vertex shaders read metadata without the terrain input block.
#ifndef FTF_NO_GRAPH_CELL_HELPERS
uint terrainFromChoice(uint choice){
    if(choice==64u)return 2u;if(choice>=65u)return choice-61u;
    uvec4 node=graph.choices[choice];if(node.y==12u)return 3u;
    return node.y==6u||node.y==8u||node.z==6u||node.z==8u?1u:0u;
}

#endif
