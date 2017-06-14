# STC DIY Watch Kit firmware

**note this project in development and a work-in-progress**

This is a firmware replacement for the [STC15F204EA](http://www.stcmicro.com/datasheet/STC15F204EA-en.pdf) based DIY LED watch kit (available through Bangood and eBay). It uses [sdcc](http://sdcc.sf.net) to build and [stcgal](https://github.com/grigorig/stcgal) to flash the firmware.

![Image of Banggood SKU 206204](https://img3.banggood.com/thumb/view/2014/xiemeijuan/05/SKU206204/SKU206204a.jpg)

[link to Banggood product page for SKU 206204](https://www.banggood.com/LED-Digital-Watch-Electronic-Clock-Kit-With-Transparent-Cover-p-976634.html)

## Features
* Nothing yet. 

## Features To Add
* Set and read date and time from RTC chip.
* Display time for N seconds when button 1 is pressed, then power down to conserve battery power.
* Set and display time in 12 or 24 hour format
* Set and display the day and month

## Hardware

* DIY LED Digital Watch Kit, based on STC15F204EA and DS1302, e.g. [Banggood SKU 206204](https://www.banggood.com/LED-Digital-Watch-Electronic-Clock-Kit-With-Transparent-Cover-p-976634.html)
* Connected to PC via cheap USB-UART adapter, e.g. CP2102, CH340G. [Banggood: CP2102 USB-UART adapter](http://www.banggood.com/CJMCU-CP2102-USB-To-TTLSerial-Module-UART-STC-Downloader-p-970993.html?p=WX0407753399201409DA)

## Connection
| P1 header | UART adapter |
|-----------|--------------|
| P3.1      | RXD          |
| P3.0      | TXD          |
| GND       | GND          |
| 5V        | 5V           |

## Requirements
* Windows, Linux, or Mac (untested on Linux or Mac; please comment with results on Linux or Mac)
* [sdcc](http://sdcc.sf.net) installed and in the path (recommend sdcc >= 3.5.0)
* [sdcc](http://sdcc.sf.net) (or optionally stc-isp). Note you can either do "git clone --recursive ..." when you check this repo out, or do "git submodule update --init --recursive" in order to fetch stcgal.

## usage
```
make clean
make
make flash
```

## Options
* Override default serial port:
`STCGALPORT=/dev/ttyUSB0 make flash`

* Add other options:
`STCGALOPTS="-l 9600 -b 9600" make flash`

## Use STC-ISP flash tool
Instead of stcgal, you could alternatively use the official stc-isp tool, e.g stc-isp-15xx-v6.85I.exe, to flash.
A windows app, but also works fine for me under mac and linux with wine.

**note** due to optimizations that make use of "eeprom" section for holding lookup tables, if you are using 4k flash model mcu AND if using stc-isp tool, you must flash main.hex (as code file) and eeprom.hex (as eeprom file). (Ignore stc-isp warning about exceeding space when loading code file.)
To generate eeprom.hex, run:
```
make eeprom
```

## Clock assumptions
Some of the code assumes 11.0592 MHz internal RC system clock (set by stc-isp or stcgal).
For example, delay routines would need to be adjusted if this is different.

## Differences from Original Work
This project is based on the [stc_diyclock](https://github.com/zerog2k/stc_diyclock) project by Jens Jensen. Without his original work this project would not exist. The modifications from Jens' original work include
* Some of the content of this README was altered to reflect the differences with this project.
* Code for features not supported by this kit (temperature and light sensors) was removed.
* LED display routines were modified to reflect this kit's hardware.
* Other code changes to reflect the different PIN configuration of this kit.
* Code formatting changes to fit my personal tastes.
* New code to support putting the MCU into power down mode to save battery life; similar to how the original firmware operates.

## Disclaimers
This code is provided as-is, with NO guarantees or liabilities.
As the original firmware loaded on an STC MCU cannot be downloaded or backed up, it cannot be restored. If you are not comfortable with experimenting, I suggest obtaining another blank STC MCU and using this to test, so that you can move back to original firmware, if desired.

### References
http://www.stcmcu.com (mostly in Chinese)

STC15F204EA english datasheet:
http://www.stcmcu.com/datasheet/stc/stc-ad-pdf/stc15f204ea-series-english.pdf

sdcc user guide:
http://sdcc.sourceforge.net/doc/sdccman.pdf

Maxim DS1302 datasheet:
http://datasheets.maximintegrated.com/en/ds/DS1302.pdf
