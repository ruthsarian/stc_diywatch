#include <stdint.h>
#include <stdio.h>
#include "stc15.h"
#include "led.h"

// so said EVELYN the modified DOG
#pragma less_pedantic

// reference to clock speed
#define FOSC    11059200

// create a nop 'function' consistent with STC15F204EA datasheet examples.
#define _nop_(); 		__asm nop __endasm

// create a 'function' to clear the watchdog timer
#define WDT_CLEAR();	(WDT_CONTR |= 1 << 4)

// flag to let us know whether or not the MCU is powered down
// needed in case there are INT events while the MCU is not powered down
// ?? given that we're enabling/disabling the interrupt with every sleep/wakeup cycle, is this variable really needed ??
unsigned char is_power_down = 0;

// control how often the display is updated
// HOW THIS WILL WORK:
//		Every N loops of the display update routine, the display will be updated over the next 4 loops.
//		The higher this value, the dimmer the LED display will be.
//		The display routine will execute about every 100 microseconds. 
uint8_t display_refresh_rate = 250;

// counter used to maintain which LED to update
volatile uint8_t display_refresh_counter = 0;

// count main program loops; used
uint16_t display_show_counter = 0;

// how many seconds to the display before the MCU goes into power down mode
uint8_t display_show_seconds = 5;

// delay by milliseconds
void _delay_ms(uint8_t ms)
{
	// delay function, tuned for 11.092 MHz clock
	// optimized to assembler
	ms; // keep compiler from complaining?
	__asm;
		; dpl contains ms param value
		delay$:
			mov	b, #8	; i
		outer$:
			mov	a, #243	; j
		inner$:
			djnz acc, inner$
			djnz b, outer$
			djnz dpl, delay$
	__endasm;
}

void sys_init(void)
{
	// setup LED display 
	// Set IO pins for LED common anodes to push-pull output to provide more current
	P3M0 |= 0xF0;
	// P3M1 &= 0x0F;	// default value after power-on or reset

	// LED segments should be set to quasi-bidirectional to sink the current
	// P1M0 = 0x00;		// default value after power-on or reset
	// P1M1 = 0x00;		// default value after power-on or reset

	// clock initialization

	// setup INT1 to pull system out of power down mode
	IT1 = 0;	// set INT1 (SW1) to trigger interrupt when pulled low
	EX1 = 0;	// begin with external interrupt disabled; it will be enabled as MCU goes to sleep

	// setup display refresh timer
	TL0 = 0xA4;		// Initial timer value
	TH0 = 0xFF;		// Initial timer value
    TF0 = 0;		// Clear TF0 flag
    TR0 = 1;		// Timer0 start run
    ET0 = 1;        // enable timer0 interrupt

	// enable interrupts
	EA  = 1;
}

// Display refresh (Timer0) routine
void timer0_isr() __interrupt (1) __using (1)
{
	// which digit to update
	uint8_t digit = display_refresh_counter % 4;

	// 4 digit 7 segment LED display is common anode
	// current into common anode of the digit (source current), out through the segment pins (sink current)

	// turn off all digits (logic low)
	P3 &= 0x0F;

	// turn off all segments (logic high)
	P1 = 0xFF;

	// test whether or not it's time to update the display
	if (display_refresh_counter % display_refresh_rate < 4 ) {
		// enable appropriate segment PINs (logic low)
		P1 = dbuf[digit];

		// enable the digit (logic high)
		P3 |= (0x10 << digit);
	}

	display_refresh_counter++;
}

void INT1_routine(void) __interrupt (2) // INT0 = interrupt 0; Timer0 = interrupt 1; INT1 = interrupt 2;
{
	if (is_power_down) {
		is_power_down = 0;
		while ( P3_3 == 0) {
			// wait for the switch press to end
			// ?? is this really necessary ??
			// ?? _nop_(); // is a nop needed for an empty while {} ??
		}
	}
}

void main(void)
{
	// setup the system
	sys_init();

	// main program loop
	while(1)
	{

		// delay the loop to execute only once every 100ms
		_delay_ms(100);

		// check power down counter
		if (display_show_counter / 10 > display_show_seconds)
		{
			// enable external interrupt
			EX1 = 1;

			// go to sleep
			is_power_down = 1;
			PCON = 0x02;

			// wakeup; NOPs required per MCU datasheet for returning from power down mode
			_nop_();
			_nop_();
			_nop_();
			_nop_();

			// disable external interrupt; so we can use it as a regular button
			EX1 = 0;

			// reset counter (timer) until next power down
			display_show_counter = 0;
		}

		// update clock data
		// does the DS1302 have some kind of lowe power mode as well?  

		// input detection

		// action (based on detected input; if any)

		// clear temporary display buffer
		clearTmpDisplay();

		// update content of temporary display buffer
		filldisplay(0, (display_show_counter / 10000) % 10, 0);
		filldisplay(1, (display_show_counter / 1000)  % 10, 0);
		filldisplay(2, (display_show_counter / 100)   % 10, 0);
		filldisplay(3, (display_show_counter / 10)    % 10, 0);

		// copy temporary display buffer to display buffer
		__critical { updateTmpDisplay(); }

		display_show_counter++;

		// reset WDT
		WDT_CLEAR();
	}
}
