#ifndef FTF_SIN_TABLE
#define FTF_SIN_TABLE
// Exact float bits from the supplied FTF NoiseUtil sine lookup, uploaded from src/ftf_sin_table.hpp. A storage buffer, not a
// const array: a large const array indexed at run time is copied into slow per-thread memory (the river pass spent ~95% there).
layout(std430,set=0,binding=15) readonly buffer FtfSinTable {uint FTF_SIN_BITS[];};
#endif
