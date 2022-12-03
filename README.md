# Teensy11

A PDP11/40 emulator for the [Teensy 4.1](https://www.pjrc.com/store/teensy41.html) development board.

## Features

In the basic hardware version the emulator is able to run V6 including the installation from tape files.  
It has a RK05 disk drive emulation and a TM11 magtape emulation which is good enough to do the initial V6 install.
Papertapes like DEC's maindec can be loaded, it has a simple shell to manipulate the sd card, 
attach drives and tapes, set the rtc clock if installed, dump first 64k of "core" to the sdcard,
and even an tftp server if you install an airlift featherwing, see Hardware used.

If you add some feather wings then you can get an RTC which can set the V6 time if patched into the rk05 drive 0 
superblock while reading it. (And 2k patches are installed). TFTP server support if you have an AirLift Feather Wing.

## Acknowledgements, Authors and Thanks

This project is based on the work of Dave Cheney's [AVR11](https://github.com/davecheney/avr11) which 
was derived from Julius Schmidt's pdp11 Javascript simulator.

It also uses code from Frank B. for displaying Teensy FlexRam information. 
It's available [here](https://forum.pjrc.com/threads/57326-T4-0-Memory-trying-to-make-sense-of-the-different-regions?p=227539&viewfull=1#post227539).

The project uses some libraries from other people, please see platformio.ini which ones are used.

I would like to thank [Dave Cheney](https://github.com/davecheney/avr11) for the initial version, 
[Noel Chiappa](http://mercury.lcs.mit.edu/~jnc/) for his 
[Bringing up V6 Unix on the Ersatz-11 PDP-11 Emulator](http://mercury.lcs.mit.edu/~jnc/tech/V6Unix.html) and
[Improving V6 Unix](http://mercury.lcs.mit.edu/~jnc/tech/ImprovingV6.html) pages.

Thanks to [Paul Stoffregen](https://www.pjrc.com) for creating the teensy boards.

And finally thanks to [Chloe Lunn](https://gitlab.com/ChloeLunn) for getting me interested in that project again.

## Hardware used

This should run on a stock Teensy 4.1, you just need a (small) micro sd card for the tape and disk files.
You can add 8 or 16Mb of PSRAM to it, this should be used automatically but can also be adjusted in unibus.c.

If you want a more sophisticated blinkenlights version you can build one using the following parts:

[Teensy 3.x Feather Adapter](https://www.adafruit.com/product/3200)

[Adafruit Quad 2x2 FeatherWing](https://www.adafruit.com/product/4253)

[Adafruit AirLift FeatherWing](https://www.adafruit.com/product/4264)

[DS3231 Precision RTC FeatherWing](https://www.adafruit.com/product/3028)

2x [Adafruit 0.8" 8x16 LED Matrix FeatherWing](https://www.adafruit.com/product/3152)

2x [Adafruit 0.56" 4-Digit 7-Segment FeatherWing](https://www.adafruit.com/product/3108)

Please see the pictures directory for examples. The feather wings should be configured 
for the following i2c addresses:

0x68: DS3231 RTC
0x70: bottom left matrix display 
0x71: bottom right matrix display
0x72: top left 7-Segment display
0x73: top right 7-Segment display

The top/bottom location does not really matter but left right should be correct.

If everything works, you get the rk0 Cylinder/Sector displayed on the 7-Segment displays and
the registers in binary on the matrix displays. Register order would be:

```
[ R0 ] [ R1 ]
[ R2 ] [ R2 ]
...
```
The last line shows the processor status word on the left and a clock tick counter on the right matrix.

## Software used

I use [PlatformIO](https://platformio.org) as development platform which is a plugin to VSCode.

You need to install the Teensy platform and the Teensy 4.1 board in PlatformIO.

The software itself is using Arduino/Teensyduino.

The following libraries are used:

https://github.com/greiman/SdFat-beta/#2.0.0-beta.9 

https://github.com/luni64/TeensyTimerTool

https://github.com/adafruit/RTClib

https://github.com/adafruit/WiFiNINA

https://github.com/adafruit/Adafruit-GFX-Library

https://github.com/adafruit/Adafruit_LED_Backpack

https://github.com/adafruit/Adafruit_BusIO

https://github.com/MajenkoLibraries/CLI

Please see the repositories for the licenses.

## Howto use

Get PlatformIO up and running, install the platform and board. 
Import the project, build and flash to the teensy 4.1. 
Connect the teensy to your development machine.

An usb serial port (/dev/cu.usbmodemPDP1140*) should appear.
The device might be differently named on your machine.
Use some terminal program to connect to it using 115200 8N1.

For example: 

```
minicom -D /dev/cu.usbmodemPDP11401
```

The MCU should start with a small command shell, you can type **help** to get a list of 
commands. It should look like this on connect:

```
I2C bus addr:
CPU  : 600 MHz
PSRAM: 16Mb
ITCM : 128kB, DTCM: 384kB, OCRAM: 0(+512)kB [DDDDDDDDDDDDIIII]
ITCM : 107960 82.37% of  128kB (  23112 Bytes free) (RAM1) FASTRUN
OCRAM:  12384  2.36% of  512kB ( 511904 Bytes free) (RAM2) DMAMEM, Heap
FLASH: 138240  1.70% of 7936kB (7988224 Bytes free) FLASHMEM, PROGMEM
$ <- you can type help now. The monitor commands (below) are not in the help.
```

The commands should be more or less self explaining. Some Notes:

- the **pt** command loads the papertape to 0200, start it with G 200, values are octal!
- if you break the running emulation with **^P**, boot works like cont, so for a
  real boot you should **reset** before.
- **date** just makes sense if you have the [DS3231 Precision RTC FeatherWing](https://www.adafruit.com/product/3028)
  installed.
- for **tftp** you need an [Adafruit AirLift FeatherWing](https://www.adafruit.com/product/4264) installed.
- the **trace** command sets the number of instructions which will be printed to the serial port if you press **^T**
- You can attach disk image files with **rk "number" filename**, tape files with **tm "number" filename**.
- At least 4 of these devices might be supported in parallel. Might be 8, just don't remember right now.
- You can detach these images by using a **-** as the filename. rk/tm without argument shows the current configuration.

The monitor commands are not yet in the help, they are:

### Monitor commands

Note: they are upper-case!

```
D [addr] disassemble, if no address is given continue on next addr
M [addr] memory dump, if no address is given continue on next addr
E [addr] dump i/o space
G addr   continue at address
W [addr] words..., write into memory
> [addr] same as W
  Tape bootloader example:
  > 100000 012700 172526 010040 012740 060003 000777
  G 100000 (break (^p)
  G 0  
```

### Howto run a paper tape

pt "filename", e.g.: 
```
pt ZKAAA0.BIN
G 200
```

You can use the paper tape feature to run diagnostic (xxdp?) software.
Some of them are available [here](https://github.com/aap/pdp11/tree/master/maindec).

In the file [1140.c](https://github.com/aap/pdp11/blob/master/1140.c) there are comments
what the files are about. On error they should HALT the machine, some seem to run forever.

### Howto install V6

#### Preparing the sd card

The sd card should be formatted using fat(32).

Prepare some files:

- an empty disk file
- the v6 tape file
- mkdevs.tap from the V6 directory of this repository

On a Unix system, create an empty disk file as model for new disk files:

```
dd if=/dev/zero of=empty.rkx bs=512 count=4872
```

Get the [v6.tape.gz](https://www.tuhs.org/Archive/Distributions/Research/Ken_Wellsch_v6/v6.tape.gz) file.
Uncompress it. Look at a hex dump of the first lines of the tape, it should start with:

```
00000000: 0701 ac01 0000 0000 0000 0000 0000 0100  ................
```

If not, the tape file must be converted using deblock from [this archive](https://www.tuhs.org/Archive/Distributions/Research/Bug_Fixes/V6enb/). This archive also contains some fixes which can be applied to V6 later.

Put all of these three files on the sd card and put it into the teensy, connect to your dev machine and start your terminal
program using the correct serial device. (See above).

#### Installing V6

You can basically follow [this](http://mercury.lcs.mit.edu/~jnc/tech/V6Unix.html) or [that](https://gunkies.org/wiki/Installing_UNIX_v6_(PDP-11)_on_SIMH). The tape loader program is entered differently, but once V6 booted, the info from the
web pages should apply.

Using the small "shell" do: (don't type # comments)
```
  cp empty.rkx r.rk0
  cp empty.rkx s.rk1
  cp empty.rkx d.rk2
  # attach disks and tape:
  rk 0 r.rk0
    # not needed now, but later, if disks are not mounted inside v6 you can 
    # always ctrl-p and adjust disks / tapes
    # (rk 1 s.rk1, rk 2 d.rk2)
  tm 0 v6.tape
    # (https://www.tuhs.org/Archive/Distributions/Research/Ken_Wellsch_v6/v6.tape.gz)
    # might need deblock from: 
    # https://www.tuhs.org/Archive/Distributions/Research/Bug_Fixes/V6enb/v6enb.tar.gz
    # enter tape loader into mem (type into shell)
  > 100000 012700 172526 010040 012740 060003 000777
  G 100000
```
    
	It should load the tape's boot block and will loop then, so press *CTRL-P*.
  If back in shell, type *G 0*. If you get the *=* prompt, follow the instructions from the links above.

  Example install session:

```
I2C bus addr:
CPU  : 600 MHz
PSRAM: 16Mb
ITCM : 128kB, DTCM: 384kB, OCRAM: 0(+512)kB [DDDDDDDDDDDDIIII]
ITCM : 108088 82.46% of  128kB (  22984 Bytes free) (RAM1) FASTRUN
OCRAM:  12384  2.36% of  512kB ( 511904 Bytes free) (RAM2) DMAMEM, Heap
FLASH: 139264  1.71% of 7936kB (7987200 Bytes free) FLASHMEM, PROGMEM
EXTMEM not available, using OCRAM.
Core is at OCRAM: 0x20203190

$ tm 0 v6.tape
attached v6.tape on tm0
$ cp empty.rkx r.rk0
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
.........................................................................
..................................................
$ rk 0 r.rk0
attached r.rk0 on rk0
$ > 100000 012700 172526 010040 012740 060003 000777
$ M 100000
100000: 012700 172526 010040 012740 060003 000777 000000 000000
100020: 000000 000000 000000 000000 000000 000000 000000 000000
100040: 000000 000000 000000 000000 000000 000000 000000 000000
100060: 000000 000000 000000 000000 000000 000000 000000 000000
100100: 000000 000000 000000 000000 000000 000000 000000 000000
100120: 000000 000000 000000 000000 000000 000000 000000 000000
100140: 000000 000000 000000 000000 000000 000000 000000 000000
100160: 000000 000000 000000 000000 000000 000000 000000 000000
$ G 100000
$ ^P here

2825355 instr/s, mmu: off
R0 172522 R1 000000 R2 000000 R3 000000
R4 000000 R5 000000 R6 000000 R7 100012 PS [kK    ]

$ G 0
$
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
=tmrk
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
disk offset
0
tape offset
100
count
1
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
=tmrk
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
disk offset
1
tape offset
101
count
3999
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
tm11: rewind
tm11: rewind done, MTS: 000041, MTC: 060217
=
1600938 instr/s, mmu: off
R0 000075 R1 136740 R2 000000 R3 000000
R4 136740 R5 137454 R6 136776 R7 137274 PS [kK Z  ]

$ reset
I2C bus addr:
CPU  : 600 MHz
PSRAM: 16Mb
ITCM : 128kB, DTCM: 384kB, OCRAM: 0(+512)kB [DDDDDDDDDDDDIIII]
ITCM : 108088 82.46% of  128kB (  22984 Bytes free) (RAM1) FASTRUN
OCRAM:  12384  2.36% of  512kB ( 511904 Bytes free) (RAM2) DMAMEM, Heap
FLASH: 139264  1.71% of 7936kB (7987200 Bytes free) FLASHMEM, PROGMEM
EXTMEM not available, using OCRAM.
Core is at OCRAM: 0x20203190

$ rk 0 r.rk0
attached r.rk0 on rk0
$ > 777570 173030
$ boot

@rkunix
mem = 1035
RESTRICTED RIGHTS

Use, duplication or disclosure is subject to
restrictions stated in Contract with Western
Electric Company, Inc.
# STTY -LCASE
# stty 9600 nl0 cr0
# pwd
/
# ls -la
total 246
drwxrwxr-x  9 bin       256 Jul 18 11:36 .
drwxrwxr-x  9 bin       256 Jul 18 11:36 ..
drwxrwxr-x  2 bin      1104 May 14 00:47 bin
drwxrwxr-x  2 bin      1824 Aug 14 22:04 dev
drwxrwxr-x  2 bin       496 Jul 18 09:17 etc
-rwxrwxrwx  1 root    29074 Oct 10 12:28 hpunix
drwxrwxr-x  2 bin       464 May 13 23:35 lib
drwxrwxr-x  2 bin        32 May 13 20:01 mnt
-rwxrwxrwx  1 root    28836 Oct 10 12:22 rkunix
-rwxrwxrwx  1 root    29020 Oct 10 12:25 rpunix
drwxrwxrwx  2 bin       272 Jul 18 09:19 tmp
-rw-rw-rw-  1 root    28684 Jul 18 09:18 unix
drwxrwxr-x 14 bin       224 May 13 20:16 usr
#
```
      
It's important to use "rkunix" here at the *@*, the other kernels do not work.
  
If you have a boot prompt, and are able to login, type 'sync' 3 times, and go back (*^p*)
to the emulator. Make a backup of your disk now, to save your work so far:
      
$ cp r.rk0 r.bak

- If you want /usr/source and /usr/doc follow again the gunkies link.
- Note: you have to create the device files using /etc/mknod, which is a bit painful.
  You can actually attach the file V6/mkdevs.tap to a tape and cp it over:


- Note: In V6: backspace/delete is '#', kill (ignore) a whole mistyped line: just type '@' 

  If you follow https://gunkies.org/wiki/Installing_UNIX_v6_(PDP-11)_on_SIMH 
  carefully you should end up with a working V6 with sources and docs.

  To enhance it a bit you could follow this:

  http://mercury.lcs.mit.edu/~jnc/tech/V6Unix.html, starting at 
  "First things to do on Unix". 

  Later you could also do: http://mercury.lcs.mit.edu/~jnc/tech/ImprovingV6.html.

  I tried most of the stuff and it seems to work.

  It's a bit hard to get Noel's sources to V6, i have a small gotp program for tape 
  creation which unfortunately is not finished yet. You can always use pad a file
  to 512 byte blocks, attach it as tape and cat it in V6. You need to make /dev/mt0 first.

  In V6:

  ```
  /etc/mknod /dev/mt0 b 3 0
  ```

  On some unix box:

  ```
  dd if=mkdevs.sh of=mkdevs.tap conv=noerror,sync
  ```
  - copy mkdevs.tap to the sd card. If you have airlift you can tftp it. 
  - attach it in the "shell": (*^p*, type command, type *cont*)
    - tm 0 mkdevs.tap
  - in V6 chdir /tmp; cat /dev/mt0 > mkdevs.sh
    run sh mkdevs.sh

  *WARNING*: don't type 'df' without arguments in V6 until you patched/recompiled it, 
  the emulator can panic if unattached disks are accessed. Using it with a device as
  argument: # df /dev/rk0 should work.

## Bugs

V6 runs quite ok, you can compile you own kernel, compile the c compiler with patches, bc works, chess works.
You can even dump / restore using /dev/mt0 disks. icheck etc works. 

### Bugs and caveats

- Yes there a bugs. Likely some instructions are not correct, tape emulation is lacking etc.

- If you access an unknown disk (even by accident, e.g. typing 'df' without arguments it will panic. DF can be patched
  to the actual existing disks, see the gunkies link for that. I even have a 'ndf' with with some mkmtab command can detect
  the actual mounted disks and work correctly.

- You should sync;sync;sync before detaching disk or resetting the emulater.

- You better backup your disk files from time to time. You can icheck /dev/rrk0 them inside V6.

- The MMU implementation is so bare that it just can run V6 and some DEC programs. It cannot unfortunately run 2.9BSD.

- The disassembler in the monitor might not be 100% correct.

- The tape (tm11) emulation cannot skip over files. It has no idea of tape marks but works on flat files.
  You can always detach- reattach single files of a longer tape. Using this kludge it's possible to 
  run the mkfs and restor programs from the 2.9BSD tape. 

- If you reconfigure and recompile the kernel, which is possible and you should, the following devices should be sufficient:
```
rk
tm
8dc
```

- For kernel recompilation please follow the mentioned links.

## License

MIT License

Copyright (c) Michael Stiller

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.