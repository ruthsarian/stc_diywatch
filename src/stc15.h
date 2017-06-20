#ifndef _STC15_H_
#define _STC15_H_

#include <8051.h>
#include <compiler.h>

#ifdef REG8051_H
#undef REG8051_H
#endif

// BYTE Register
__sfr __at 0x94 P0M0;
__sfr __at 0x93 P0M1;
__sfr __at 0x92 P1M0; 
__sfr __at 0x91 P1M1; 
__sfr __at 0x96 P2M0;
__sfr __at 0x95 P2M1;
__sfr __at 0xB2 P3M0;
__sfr __at 0xB1 P3M1;
__sfr __at 0xC1 WDT_CONTR;
__sfr __at 0x97 CLK_DIV;

#endif
