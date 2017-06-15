#include <stdint.h>
#include <stdio.h>
#include "stc15.h"
#include "led.h"
#include "ds1302.h"

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
#define display_refresh_rate 20

// counter used to maintain which LED to update
volatile uint8_t display_refresh_counter = 0;

// count main program loops; used
uint16_t display_show_counter = 0;

// how many seconds to the display before the MCU goes into power down mode
#define display_show_seconds 50

// flag to determine when to display the colon
volatile __bit  display_colon = 0;

// keyboard mode states
enum keyboard_mode {
	K_NORMAL,
	K_WAIT_S1,
	K_WAIT_S2,
	K_SET_HOUR,
	K_SET_MINUTE,
	K_SET_HOUR_12_24,
	K_SEC_DISP,
	K_DATE_DISP,
	K_DATE_SWDISP,
	K_SET_MONTH,
	K_SET_DAY,
	K_WEEKDAY_DISP,
	K_DEBUG
};

// display mode states
enum display_mode {
	M_NORMAL,
	M_SET_HOUR_12_24,
	M_SEC_DISP,
	M_DATE_DISP,
	M_WEEKDAY_DISP,
	M_DEBUG
};

// variables to manage state of the watch
uint8_t	dmode = M_NORMAL;
uint8_t	kmode = K_NORMAL;

// button aliases
#define SW1 P3_3
#define SW2 P3_1

// button debounce
volatile uint8_t debounce[2] = {0, 0};

// long button press detection
volatile uint8_t switchcount[2] = {0, 0};
#define SW_CNTMAX 100

// button states
volatile __bit  S1_LONG;
volatile __bit  S1_PRESSED;
volatile __bit  S2_LONG;
volatile __bit  S2_PRESSED;

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
	ds_init();
	ds_ram_config_init();

	// setup INT1 to pull system out of power down mode
	IT1 = 0;	// set INT1 (SW1) to trigger interrupt when pulled low
	EX1 = 0;	// begin with external interrupt disabled; it will be enabled as MCU goes to sleep

	// setup display refresh timer
	TL0 = 0xA4;		// Initial timer value
	TH0 = 0xFF;		// Initial timer value
	TF0 = 0;		// Clear TF0 flag
	TR0 = 1;		// Timer0 start run
	ET0 = 1;		// enable timer0 interrupt

	// enable interrupts
	EA  = 1;
}

// timer to manage display refresh and button press detection
void timer0_isr() __interrupt (1) __using (1)
{
	//
	// DISPLAY REFRESH
	//

	// which digit to update
	uint8_t digit = display_refresh_counter % 4;

	// 4 digit 7 segment LED display is common anode
	// current into common anode of the digit (source current), out through the segment pins (sink current)

	// turn off all digits (logic low)
	P3 &= 0x0F;

	// test whether or not it's time to update the display
	if (display_refresh_counter % display_refresh_rate < 4 ) {

		// reset segments to all off
		//P1 = 0xFF;

		// enable appropriate segment PINs (logic low)
		P1 = dbuf[digit];

		// enable the digit (logic high)
		P3 |= (0x10 << digit);
	}

	display_refresh_counter++;

	//
	// BUTTON PRESS DETECTION
	//

	// is the button down?
	S1_PRESSED = debounce[0] == 0x00 ? 1 : 0;
	S2_PRESSED = debounce[1] == 0x00 ? 1 : 0;

	// keep track of how long the button has been pressed
	switchcount[0] = S1_PRESSED ? (switchcount[0] > SW_CNTMAX ? SW_CNTMAX : switchcount[0]+1) : 0;
	switchcount[1] = S1_PRESSED ? (switchcount[1] > SW_CNTMAX ? SW_CNTMAX : switchcount[1]+1) : 0;

	// flag that the button has been held down a long time
	S1_LONG = switchcount[0] == SW_CNTMAX ? 1 : 0;
	S2_LONG = switchcount[1] == SW_CNTMAX ? 1 : 0;

	// read button states into sliding 8-bit window
	// buttons are active low
	debounce[0] = (debounce[0] << 1) | SW1;
	debounce[1] = (debounce[1] << 1) | SW2;
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
	uint8_t tens_hour;

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

		// read clock data
		ds_readburst();

		// control when the colon should blink: ever other second
		display_colon = rtc_table[DS_ADDR_SECONDS]&DS_MASK_SECONDS_UNITS % 2;

		// input detection




		// manage actions based on button input, if any
		switch (kmode) {
			case K_DEBUG:
				dmode = M_DEBUG;
				if (S1_PRESSED) {
					kmode = K_NORMAL;
				}
				break;

			case K_NORMAL:
			default:
				dmode = M_NORMAL;
				if (S2_PRESSED) {
					kmode = K_DEBUG;
				}
				break;
		}

		// clear temporary display buffer
		clearTmpDisplay();

		// based on current display state of watch, update temporary buffer
		switch (dmode) {

			case M_DEBUG:
				filldisplay(0, (display_show_counter / 10000) % 10, 0);
				filldisplay(1, (display_show_counter / 1000)  % 10, 0);
				filldisplay(2, (display_show_counter / 100)   % 10, 0);
				filldisplay(3, (display_show_counter / 10)    % 10, 0);
				break;

			case M_NORMAL:
			default:
				tens_hour = (rtc_table[DS_ADDR_HOUR]>>4)&(DS_MASK_HOUR24_TENS>>4);
				filldisplay( 0, (tens_hour<1?LED_BLANK:tens_hour), 0);
				filldisplay( 1, rtc_table[DS_ADDR_HOUR]&DS_MASK_HOUR_UNITS, display_colon);
				filldisplay( 2, (rtc_table[DS_ADDR_MINUTES]>>4)&(DS_MASK_MINUTES_TENS>>4), 0);
				filldisplay( 3, rtc_table[DS_ADDR_MINUTES]&DS_MASK_MINUTES_UNITS, 0);
				break;
		}

		// copy temporary display buffer to display buffer
		__critical { updateTmpDisplay(); }

		display_show_counter++;

		// reset WDT
		WDT_CLEAR();
	}
}
