#include <stdint.h>

// index into ledtable[]
#define LED_A		0x0A
#define LED_B		0x0B
#define LED_C		0x0C
#define LED_D		0x0D
#define LED_E		0x0E
#define LED_F		0x0F
#define LED_H		0x12
#define LED_BLANK	0x10
#define LED_DASH	0x11
#define LED_DP		0x13

const uint8_t __at (0x1000) ledtable[] 
= {
	// digit to led digit lookup table
	// dp,g,f,e,d,c,b,a
	0b11000000, // 0
	0b11111001, // 1
	0b10100100, // 2
	0b10110000, // 3
	0b10011001, // 4
	0b10010010, // 5
	0b10000010, // 6
	0b11111000, // 7
	0b10000000, // 8
	0b10011000, // 9
	0b10001000, // A
	0b10000011, // b
	0b11000110, // C
	0b10100001, // d
	0b10000110, // E
	0b10001110, // F
	0b11111111, // 0x10 - ' '
	0b10111111, // 0x11 - '-'
	0b10001011, // 0x12 - 'h'
	0b01111111, // 0x13 - '.'
};

uint8_t	dbuf[4];
uint8_t	tmpbuf[4];
__bit	dot0;
__bit	dot1;
__bit	dot2;
__bit	dot3;

#define clearTmpDisplay() { dot0=0; dot1=0; dot2=0; dot3=0; tmpbuf[0]=tmpbuf[1]=tmpbuf[2]=tmpbuf[3]=LED_BLANK; }
#define filldisplay(pos,val,dp) { tmpbuf[pos]=(uint8_t)(val); if (dp) dot##pos=1;}
#define dotdisplay(pos,dp) { if (dp) dot##pos=1;}
#define updateTmpDisplay() { uint8_t tmp; \
	tmp=ledtable[tmpbuf[0]]; if (dot0) tmp&=0x7F; dbuf[0]=tmp; \
	tmp=ledtable[tmpbuf[1]]; if (dot1) tmp&=0x7F; dbuf[1]=tmp; \
	tmp=ledtable[tmpbuf[3]]; if (dot3) tmp&=0x7F; dbuf[3]=tmp; \
	tmp=ledtable[tmpbuf[2]]; if (dot2) tmp&=0x7F; dbuf[2]=tmp; \
}