// SPDX-License-Identifier: GPL-2.0-or-later
// Prints the 64x64 seed map of the game's own generator (src/MapSeed.cpp), one row a line.
#include <cstdio>
#include <cstdlib>
#include <SDL.h>
void createMapWithSeed(Uint32 Para_Seed,Uint16 *pResultMap);
// s=sand r=rock d=dunes m=mountain p=spice P=thick spice
int main(int argc,char**argv){
  Uint16 m[64*64]; createMapWithSeed((Uint32)strtoul(argv[1],0,10),m);
  for(int y=0;y<64;y++){for(int x=0;x<64;x++){unsigned t=m[y*64+x]>>4;char c='s';
    switch(t){case 0x7:c='s';break;case 0x2:case 0x8:c='r';break;case 0x9:c='d';break;case 0xa:c='m';break;case 0xb:c='p';break;case 0xc:c='P';break;default:c='?';}
    putchar(c);} putchar('\n');}
}
