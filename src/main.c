#include <stdint.h>
#include <stdio.h>
#include "stc15.h"
#include "led.h"
#include "ds1302.h"

// so said EVELYN the modified DOG
#pragma less_pedantic

// reference to clock speed (not used ??)
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
#define display_refresh_rate 50

// counter used to maintain which LED to update
volatile uint8_t display_refresh_counter = 0;

// count main program loops; used to time how long display is on before powering off
uint16_t display_show_counter = 0;

// how many seconds to the display before the MCU goes into power down mode
#define display_show_seconds 5

// flag to determine when to display the colon
volatile __bit  display_colon = 0;

// flags to control flashing pairs of digits
__bit  flash_01 = 0;
__bit  flash_23 = 0;

// keyboard mode states
typedef enum {
	K_NORMAL,
	K_SET_HOUR,
	K_SET_MINUTE,
	K_SET_HOUR_12_24,
	K_DATE_DISP,
	K_SET_MONTH,
	K_SET_DAY,
	K_YEAR_DISP,
	K_SET_YEAR,
	K_WEEKDAY_DISP,
	K_DEBUG
} keyboard_mode_t;

// display mode states
typedef enum {
	M_NORMAL,
	M_SET_HOUR_12_24,
	M_DATE_DISP,
	M_YEAR_DISP,
	M_WEEKDAY_DISP,
	M_DEBUG
} display_mode_t;

// variables to manage state of the watch
keyboard_mode_t kmode = K_NORMAL;
display_mode_t dmode = M_NORMAL;

// button aliases
#define SW1 P3_3
#define SW2 P3_1

// button debounce
volatile uint8_t debounce[2] = {0, 0};
#define SW_CHECK 10 // * 100uS = how often button state is tested

// long button press detection
volatile uint16_t switchcount[2] = {0, 0};
#define SW_CNTMAX 1500	// * SW_CHECK * 100uS = time before long button press is registered

// button states/flags
volatile __bit  S1_PRESSED = 0;
volatile __bit  S1_LONG = 0;
volatile __bit	S1_READY = 0;
volatile __bit	S1_READY_PRESSED = 0;
volatile __bit  S2_PRESSED = 0;
volatile __bit  S2_LONG = 0;
volatile __bit	S2_READY = 0;
volatile __bit	S2_READY_PRESSED = 0;

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

	// reset the clock if it has an invalid (00) month value
	ds_readburst();
	if (rtc_table[DS_ADDR_MONTH] == 0x00) {
		ds_reset_clock();
	}

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

	// slow down how often the button states are checked
	if (display_refresh_counter % SW_CHECK == 0) {

		// is the button down?
		S1_PRESSED = debounce[0] == 0x00 ? 1 : 0;
		S2_PRESSED = debounce[1] == 0x00 ? 1 : 0;

		// set Sx_LONG flag if button was held down for a long time. 
		// this flag must be cleared by the main loop.
		if (S1_PRESSED && switchcount[0]++ == SW_CNTMAX) {
			S1_LONG = 1;
		} else if (!S1_PRESSED) {
			switchcount[0] = 0;
		}
		if (S2_PRESSED && switchcount[1]++ == SW_CNTMAX) {
			S2_LONG = 1;
		} else if (!S2_PRESSED) {
			switchcount[1] = 0;
		}

		// read button states into sliding 8-bit window
		// buttons are active low
		debounce[0] = (debounce[0] << 1) | SW1;
		debounce[1] = (debounce[1] << 1) | SW2;
	}
}

// INT0 = interrupt 0; Timer0 = interrupt 1; INT1 = interrupt 2;
void INT1_routine(void) __interrupt (2) 
{
	if (is_power_down) {
		is_power_down = 0;
		// some kind of debounce is needed here
		// while ( P3_3 == 0) {}
	}
}

// when entering new mode need to wait for previous button press to be released. 
// at that point the button is READY to be used by the new button state
//
// Sx_READY indicates that the button state is relative to the current mode.
//          it protects against a button held down through transition of modes
//          so the new mode does not trigger off the old mode's button press.
//
// Sx_READY_PRESSED indicates the button has been pressed since entering the current mode.
//                  this, combined with !Sx_PRESSED, will indicate a button press and release.
//
// Scenario: a button is pressed and held down. trigger ONCE off the button press then when
//           Sx_LONG triggers, repeat the action.
//
// Solution: create a variable to note that the button has been triggered on so we don't do it again
//           that's probably the correct way to do this.
//
//           however, we can abuse the Sx_READY flag instead because of how it's set. if it's unset
//           after the short trigger, we can test Sx_READY as it will be reset to 1 after a button
//           release. changes in how Sx_READY is set (handled in the function below) may affect
//           this hack, so be aware.
//			
//           NOTE: if button_ready_check() were to reset Sx_READY_PRESSED when Sx_READY is unset,
//                 then we'd also reset Sx_LONG and modes won't register LONG button presses.
//
//                 i think the correct, true to variable name, function would be to reset Sx_READY_PRESSED
//                 when Sx_READY is reset. however this would break the above hack/solution.
void button_ready_check(void) {

	if (!S1_READY && !S1_PRESSED) {
		S1_READY = 1;
		S1_LONG = 0;
	} else if (S1_READY && S1_PRESSED) {
		S1_READY_PRESSED = 1;
	}

	if (!S2_READY && !S2_PRESSED) {
		S2_READY = 1;
		S2_LONG = 0;
	} else if (S2_READY && S2_PRESSED) {
		S2_READY_PRESSED = 1;
	}
}

// call this function to change the keyboard mode.
// this function will reset all appropriate variables before entering the new mode
void change_kmode(keyboard_mode_t new_kmode) {

	// reset display power off counter
	display_show_counter = 0;

	// reset button 1 flags
	S1_READY = 0;
	S1_LONG = 0;
	S1_READY_PRESSED = 0;

	// reset button 2 flags
	S2_READY = 0;
	S2_LONG = 0;
	S2_READY_PRESSED = 0;

	// reset flashing digit flags
	flash_01 = 0;
	flash_23 = 0;

	// switch to new keyboard mode
	kmode = new_kmode;
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
			// clear display
			clearTmpDisplay();
			updateTmpDisplay();
			_delay_ms(10);

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

			// i find without this, wakeup button immediately changes modes. 
			_delay_ms(100);

			// start back up in time mode
			change_kmode( K_NORMAL );

			// reset counter (timer) until next power down
			display_show_counter = 0;
		}

		// read clock data
		ds_readburst();

		// control when the colon should blink: ever other second
		display_colon = rtc_table[DS_ADDR_SECONDS]&DS_MASK_SECONDS_UNITS % 2;

		// manage actions based on button input, if any
		switch (kmode) {

			case K_SET_HOUR:

				// flash the hours digit every other loop
				flash_01 = !flash_01;

				// check button ready state
				button_ready_check();

				// Detect request to change mode (button 1 press and release)
				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode( K_SET_MINUTE );
				} else

				// only change values when only button 2 is pressed
				if (S2_READY_PRESSED && S2_PRESSED && !S1_PRESSED) {

					// keep incrementing if button is held down for a long time
					if (S2_LONG && flash_01) {
						ds_hours_incr();
	
					// register a single button press and reset the ready state
					// so we can detect more button presses
					} else if (S2_READY) {
						ds_hours_incr();
						S2_READY = 0;		// reset S2_READY to prevent triggering multiple times
						             		// during short button press.
					}
				}
				break;

			case K_SET_MINUTE:
				flash_23 = !flash_23;
				button_ready_check();
				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode( K_SET_HOUR_12_24 ); 
				} else if (S2_READY_PRESSED && S2_PRESSED && !S1_PRESSED) {
					if (S2_LONG && flash_23) {
						ds_minutes_incr();
					} else if (S2_READY) {
						ds_minutes_incr();
						S2_READY = 0;
					}
				} 
				break;

			case K_SET_HOUR_12_24:
				dmode = M_SET_HOUR_12_24;

				button_ready_check();

				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode(K_NORMAL);
				} 

				if (S2_READY && S2_PRESSED && !S1_PRESSED) {
					ds_hours_12_24_toggle();
					S2_READY = 0;
				} 
				break;

			case K_DATE_DISP:
				dmode = M_DATE_DISP;

				button_ready_check();

				if (S1_READY_PRESSED && !S2_PRESSED) {
					if (S1_LONG) {
						change_kmode(K_SET_MONTH);
					} else if (!S1_PRESSED) {
						change_kmode(K_YEAR_DISP);
					}
				}

				// Button 2 does nothing in this mode
//				if (S2_READY_PRESSED && !S1_PRESSED) {						
//				}

				break;

			case K_SET_MONTH:
				flash_01 = !flash_01;
				button_ready_check();
				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode(K_SET_DAY);
				} else if (S2_READY_PRESSED && S2_PRESSED && !S1_PRESSED) {
					if (S2_LONG && flash_01) {
						ds_month_incr();
					} else if (S2_READY) {
						ds_month_incr();
						S2_READY = 0;
					}
				}
				break;

			case K_SET_DAY:
				flash_23 = !flash_23;
				button_ready_check();
				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode(K_DATE_DISP); 
				} else if (S2_READY_PRESSED && S2_PRESSED && !S1_PRESSED) {
					if (S2_LONG && flash_23) {
						ds_day_incr();
					} else if (S2_READY) {
						ds_day_incr();
						S2_READY = 0;
					}
				} 
				break;

			case K_YEAR_DISP:
				dmode = M_YEAR_DISP;
				button_ready_check();
				if (S1_READY_PRESSED && !S2_PRESSED) {
					if (S1_LONG) {
						change_kmode(K_SET_YEAR);
					} else if (!S1_PRESSED) {
						change_kmode(K_WEEKDAY_DISP);
					}
				}
				break;

			case K_SET_YEAR:
				flash_23 = !flash_23;
				button_ready_check();
				if (S1_READY_PRESSED && (S1_LONG || !S1_PRESSED) && !S2_PRESSED) {
					change_kmode(K_YEAR_DISP); 
				} else if (S2_READY_PRESSED && S2_PRESSED && !S1_PRESSED) {
					if (S2_LONG && flash_23) {
						ds_year_incr();
					} else if (S2_READY) {
						ds_year_incr();
						S2_READY = 0;
					}
				} 
				break;

			case K_WEEKDAY_DISP:
				dmode = M_WEEKDAY_DISP;
				button_ready_check();
				if (S1_READY_PRESSED && !S1_PRESSED && !S2_PRESSED) {
					change_kmode(K_NORMAL);
				}
				break;

			case K_DEBUG:
				dmode = M_DEBUG;

				button_ready_check();

				if (S2_READY_PRESSED && !S1_PRESSED) {
					if (S2_LONG && !S2_PRESSED) {
						change_kmode(K_NORMAL);
					}
				}

				if (S1_READY_PRESSED && !S2_PRESSED) {
					if (S1_LONG && !S1_PRESSED) {
						change_kmode(K_NORMAL);
					}
				}

				break;

			case K_NORMAL:
			default:
				dmode = M_NORMAL;

				button_ready_check();

				// Below is a basic button handler template.
				// It could perhaps be turned into its own function
				// but that might get messy.

				// ** BUTTON ONE **
				if (S1_READY_PRESSED && !S2_PRESSED) {

					// change mode after long button press is released
//					if (S1_LONG && !S1_PRESSED) {
//						change_kmode( K_SET_HOUR );
//					} else

					// change mode immediately on long button press
					if (S1_LONG) {
						change_kmode( K_SET_HOUR );
					} else

					// change mode on short button press
					if (!S1_PRESSED) {
						change_kmode( K_DATE_DISP );
					}
				} else

				// ** BUTTON TWO ***
				if (S2_READY_PRESSED && !S1_PRESSED) {

					// change mode after long button press is released
					if (S2_LONG && !S2_PRESSED) { 
						change_kmode( K_DEBUG );	// if i make this K_SET_HOUR it goes a little nutty
													// because of assumptions (button 2 never changes mode)
													// but could i protect against that with Sx_READY_PRESSED ?
					} else 

					// change mode immediately on long button press
//					if (S2_LONG) {
//						change_kmode( K_SET_HOUR );
//					} else

					// change mode on short button press
					if (!S2_PRESSED) {
						change_kmode( K_DEBUG );
					}
				}

				break;
		}

		// clear temporary display buffer
		clearTmpDisplay();

		// based on current display state of watch, update temporary buffer
		switch (dmode) {

			case M_WEEKDAY_DISP:
				filldisplay( 1, LED_DASH, 0);
				filldisplay( 2, rtc_table[DS_ADDR_WEEKDAY], 0);		//weekday ( &MASK_UNITS useless, all MSBs are '0')
				filldisplay( 3, LED_DASH, 0);
				break;

			case M_YEAR_DISP:
				// the DS1302 only maintains a 2 digit year (2000 - 2100); so 20 of 20xx is hard-coded
				filldisplay( 0, 2, 0);
				filldisplay( 1, 0, 0);
				if (!flash_23) {
					filldisplay( 2, rtc_table[DS_ADDR_YEAR]>>4, 0);
					filldisplay( 3, rtc_table[DS_ADDR_YEAR]&DS_MASK_YEAR_UNITS, 0);
				}
				break;

			case M_DATE_DISP:
				// month
				if (!flash_01) {
					filldisplay( 0, rtc_table[DS_ADDR_MONTH]>>4, 0);					// tenmonth ( &MASK_TENS useless, as MSB bits are read as '0')
					filldisplay( 1, rtc_table[DS_ADDR_MONTH]&DS_MASK_MONTH_UNITS, 1);
				}

				// day 
				if (!flash_23) {
					filldisplay( 2, rtc_table[DS_ADDR_DAY]>>4, 0);						// tenday   ( &MASK_TENS useless)
					filldisplay( 3, rtc_table[DS_ADDR_DAY]&DS_MASK_DAY_UNITS, 0);		// day       
				}
				break;

			case M_SET_HOUR_12_24:
				if (H12_24) {
					filldisplay(0, 1, 0);
					filldisplay(1, 2, 0);
				} else {
					filldisplay(0, 2, 0);
					filldisplay(1, 4, 0);
				}
				filldisplay(2, LED_h, 0);
				filldisplay(3, LED_r, 0);
				break;

			case M_DEBUG:
				/*
				filldisplay(0, (display_show_counter / 10000) % 10, 0);
				filldisplay(1, (display_show_counter / 1000)  % 10, 0);
				filldisplay(2, (display_show_counter / 100)   % 10, 0);
				filldisplay(3, (display_show_counter / 10)    % 10, 0);
				*/
				filldisplay(1, S1_PRESSED, 0);
				filldisplay(0, S1_LONG, 0);
				filldisplay(3, S2_PRESSED, 0);
				filldisplay(2, S2_LONG, 0);
				break;

			case M_NORMAL:
			default:
				if (!flash_01) {
					if (!H12_24) {
						tens_hour = (rtc_table[DS_ADDR_HOUR]>>4)&(DS_MASK_HOUR24_TENS>>4);
						filldisplay( 0, (tens_hour<1?LED_BLANK:tens_hour), 0);
					} else if (H12_TH) {
						filldisplay( 0, 1, 0);						
					}
					filldisplay( 1, rtc_table[DS_ADDR_HOUR]&DS_MASK_HOUR_UNITS, display_colon);
				}
				if (!flash_23) {
					filldisplay( 2, (rtc_table[DS_ADDR_MINUTES]>>4)&(DS_MASK_MINUTES_TENS>>4), 0);
					filldisplay( 3, rtc_table[DS_ADDR_MINUTES]&DS_MASK_MINUTES_UNITS, H12_24 & H12_PM);
				}
				break;
		}

		// copy temporary display buffer to display buffer
		__critical { updateTmpDisplay(); }

		// reset the display timer during user interaction
		// otherwise increment it
		display_show_counter = S1_PRESSED || S2_PRESSED ? 0 : display_show_counter + 1;

		// reset WDT
		WDT_CLEAR();
	}
}
