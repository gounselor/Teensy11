The MCU should start with a small cmd shell, you can type help to get a list of 
commands. It should look like this on boot:

I2C bus addr:
CPU  : 600 MHz
PSRAM: 16Mb
ITCM : 128kB, DTCM: 384kB, OCRAM: 0(+512)kB [DDDDDDDDDDDDIIII]
ITCM : 107960 82.37% of  128kB (  23112 Bytes free) (RAM1) FASTRUN
OCRAM:  12384  2.36% of  512kB ( 511904 Bytes free) (RAM2) DMAMEM, Heap
FLASH: 138240  1.70% of 7936kB (7988224 Bytes free) FLASHMEM, PROGMEM
$ <- you can type help now. The monitor commands (below) are not in the help.

### Howto install v6

- put empty.rkx on sd card
- using the small "shell" do:
  - cp empty.rkx r.rk0
  - cp empty.rkx s.rk1
  - cp empty.rkx d.rk2
  - # attach disks and tape:
  - rk 0 r.rk0
    # not needed now, but later, if disks are not mounted inside v6 you can 
    # always ctrl-p and adjust disks / tapes
  - (rk 1 s.rk1, rk 2 d.rk2)
  - tm 0 wellsch.tap 
    (https://www.tuhs.org/Archive/Distributions/Research/Ken_Wellsch_v6/v6.tape.gz)
    might need deblock from: 
    https://www.tuhs.org/Archive/Distributions/Research/Bug_Fixes/V6enb/v6enb.tar.gz
  - enter tape loader into mem (type into shell)
    > 100000 012700 172526 010040 012740 060003 000777
    G 100000
    
	It should load the tape's boot block and will loop then, so press ctrl-p.
    If back in shell, type G 0

    It should look like this:

    $ rk 0 r.rk0
    attached r.rk0 on rk0
    $ tm 0 wellsch.tap
    attached wellsch.tap on tm0
    $  
    $ > 100000 012700 172526 010040 012740 060003 000777
    $ G 100000
    $ 
    
    2593906 instr/s, mmu: off
    R0 172522 R1 000000 R2 000000 R3 000000
    R4 000000 R5 000000 R6 000000 R7 100012 PS [kK    ]
    
    $ G 0
    $ 
    tm11: rewind
    tm11: rewind done, MTS: 000041, MTC: 060217
    =
 
    Now basically follow https://gunkies.org/wiki/Installing_UNIX_v6_(PDP-11)_on_SIMH
    - After the last copy (count 3999) press ctrl-p again.
    - Start fresh by typing "reset"
    - Attach rk0 again:
      rk 0 r.rk0
    - Type "boot", at the @ prompt, type "rkunix". This should give you a login: prompt.
      It's important to use "rkunix" here, the other kernels do not work.
  
    - If you have a boot prompt, and are able to login, type 'sync' 3 times, and go back
      to the emulator. Make a backup of your disk now, to save your work so far:
      
      $ cp r.rk0 r.bak

    - If you want /usr/source and /usr/doc follow again the gunkies link.
    - Note: you have to create the device files using /etc/mknod, which is a bit painful.
    - Note: backspace/delete is '#', kill (ignore) a whole mistyped line: just type '@' 

    If you follow https://gunkies.org/wiki/Installing_UNIX_v6_(PDP-11)_on_SIMH 
    carefully you should end up with a working V6 with sources and docs.

    To enhance it a bit you could follow this:

    http://mercury.lcs.mit.edu/~jnc/tech/V6Unix.html, starting at 
    "First things to do on Unix". 

    Later you could also do: http://mercury.lcs.mit.edu/~jnc/tech/ImprovingV6.html.

    I tried most of the stuff and it seems to work.

    It's a bit hard to get Noel's sources to V6, i have a small gotp program for tape 
    creation which unfortunately is not finished yet. 

    IIRC it should be possible to just attach the files as tapes and 
    use dd to get them to V6 
    (if you created /dev/mt0 manually using: /etc/mknod /dev/mt0 b 3 0)
    But i think the files need to be padded on 512 byte boundaries with zeros.

    So something like this:
    
    Ctrl-P to emulator.
    tm 0 mkdev.sh
    cont
    # cat /dev/mt0 > /tmp/mkdevs.sh
    # sh /tmp/mkdevs.sh

    WARNING: don't type 'df' without arguments in V6 until you patched/recompiled it, 
    the emulator can panic if unattached disks are accessed. Using it with a device as
    argument: # df /dev/rk0
    should work.
       
### Monitor commands

D [addr] disassemble, if no address is given continue
M [addr] memory dump, if no address is given continue  
E [addr] dump i/o space
G addr   continue at address
W [addr] words..., write into memory
> [addr] same as W
  Tape bootloader example:
  > 100000 012700 172526 010040 012740 060003 000777
  G 100000 (break (^p)
  G 0  
  
### Howto run a paper tape

pt "filename", e.g. pt ZKAAA0.BIN
G 200

### Notes

- CPU is KD11A
