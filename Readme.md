

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