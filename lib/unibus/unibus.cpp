#include "SdFat.h"
#include <pdp11.h>
#include "cpu.h"
#include "dl11.h"
#include "mmu.h"
#include "unibus.h"
#include "rk05.h"
#include "tm11.h"
#include "console.h"

extern void displayWordExternal(uint32_t val);

namespace unibus {

// memory as words
// int *intptr = reinterpret_cast<int *>(0x2200);
// memory as bytes
// char *charptr = reinterpret_cast<char *>(0x2200);
// 248 Kbytes of memory is the maximum allowed by the UNIBUS
// http://gunkies.org/wiki/KT11-B_Paging_Option
// 0760000 // 248

uint16_t SWR;
uint16_t SLR;
uint16_t PIRQ;

extern "C" uint8_t external_psram_size;

// start of io space
#define MEM 0760000

//uint16_t core16[MEM >> 1];
//uint16_t *core16 = (uint16_t *) (0x70000000);
//uint16_t *core16 = (uint16_t *) (0x20200000+0x10000); 512k RAM2 + 64k because of heap
uint16_t *core16;
uint8_t  *core8 = (uint8_t *) core16; 

bool dump(void) {
  FsFile core;
  if (!core.open("core", O_CREAT|O_TRUNC|O_WRITE)) {
    return false;
  }
  Serial.println("dumping core...");
  size_t n = core.write(core16, 0177777);
  core.sync();
  core.close();
  Serial.printf("%d (%06o) bytes dumped\r\n", n, n);
  if (n == 0177777) {
    return true;
  }
  return false;
}

#define IGNORE_EXTMEM 1

void reset() {
  if (IGNORE_EXTMEM || external_psram_size == 0) {
    Serial.println("EXTMEM not available, using OCRAM.");
    core16 = (uint16_t *) malloc(MEM);
    if (core16 == NULL) {
      Serial.println("Malloc failed.");
      panic();
    }    
    core8 = (uint8_t *) core16;
    Serial.printf("Core is at OCRAM: 0x%08x\r\n", core16);    
  } else {
    Serial.printf("Using EXTMEM: %dMb\r\n", external_psram_size);
    core16 = (uint16_t *) (0x70000000);
  }
  memset(&core16[0], 0, MEM);
  SLR = 0;
}

uint16_t read16(const uint32_t a) {
  /*
  if (a == 0777775) { // SLR special
    return SLR & 0177400;
  }
  */
  if (a & 1) {
    Serial.printf("unibus: read16 from odd address: %06o\r\n", a);
    longjmp(trapbuf, INTBUS);
    return 0xFFFF; // -1
  }
  if (a < 0760000 ) {
    return core16[a >> 1];
  }

  if (a == 0777774) {
    return SLR;
  }
  if (a == 0777546) { // KW11-L clock status LKS
    return cpu::LKS;
  }

  if (a == 0777570) { // Read only Panel Switch Register, Single user value for init is 0173030
    return SWR; //0173030;
  }

  if (a == 0777572) { // SSR0
    return mmu::SR0;
  }

  if (a == 0777574) { // SSR1
    return mmu::SR1;
  }

  if (a == 0777576) { // SSR2
    return mmu::SR2;
  }

  if (a == 0777516) { // SSR3
    return 0; // mmu::SSR3;
  }

  if (a == 0777746) { // CCR
    return 0; // BSD verarschen
  }

  if (a == 0777776) { // PS
    return cpu::PS.Word;
  }

  if ((a & 0777770) == 0777560) { // 
    return dl11::read16(a);
  }

  if ((a & 0777760) == 0777400) { // RK11
    return rk11::read16(a);
  }

  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) { // 
    return mmu::read16(a);
  }

  if ((a & 0777720) == 0772520) {
    return tm11::read16(a);
  }
  if (a == 0760000) { // fuibyte, gword
    longjmp(trapbuf, INTBUS);
    return 0xFFFF;
  }
  Serial.printf("unibus: read16 invalid address: %06o\r\n", a);
  longjmp(trapbuf, INTBUS);
  return 0xFFFF;
}


void write16(const uint32_t a, const uint16_t v) {
  if (a & 1) {
    Serial.printf("unibus: write16 to odd address: %06o\r\n", a);
    longjmp(trapbuf, INTBUS);
  }
  if (a < 0760000) {
    core16[a >> 1] = v;
    return;
  }
  switch (a) {
    case 0777776:
      switch (v >> 14) {
        case 0:
          cpu::switchmode(false);
          break;
        case 3:
          cpu::switchmode(true);
          break;
        default:
          Serial.printf("invalid mode: %06o, switch 1\r\n", v >> 14);
          panic();
      }
      switch ((v >> 12) & 3) {
        case 0:
          cpu::prevuser = false;
          break;
        case 3:
          cpu::prevuser = true;
          break;
        default:
          Serial.printf("invalid mode: %06o, switch 2\r\n", (v >> 12) & 3);
          panic();
      }
      cpu::PS.Word = v;
      return;
    /*  
    case 0777774:
      SLR = v & 0177400;
      return;
    */
    case 0777546:
      cpu::LKS = v;
      return;
    case 0777572:
      mmu::SR0 = v;
      return;
    case 0777574:
      mmu::SR1 = v;
      return;
    case 0777576:
      mmu::SR2 = v;
      return;
    case 0777516:
      //mmu::SR3 = v;
      return;
    case 0777570: // switch register
    if (console::active) {
      displayWordExternal(v);
    }
      SWR = v;
      return;
  }
  if ((a & 0777770) == 0777560) {
    dl11::write16(a, v);
    return;
  }
  if ((a & 0777700) == 0777400) {
    rk11::write16(a, v);
    return;
  }
  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
    mmu::write16(a, v);
    return;
  }
  if ((a & 0777720) == 0772520) {
    tm11::write16(a, v);
    return;
  }
  Serial.printf("unibus: write16 invalid address: %06o\r\n", a);
  longjmp(trapbuf, INTBUS);
  return;
}


uint16_t read8(const uint32_t a) {
  /*
  if (a > 0760000 && a != 0777560) {
    Serial.printf("%06o: read8 from %06o\r\n", cpu::PC, a);
  }
  */
  if (a & 1) {
    return read16(a & ~1) >> 8;
  }
  return read16(a & ~1) & 0xFF;
}


void write8(const uint32_t a, const uint16_t v) {
  /*
  if (a > 0760000) {
    Serial.printf("%06o: write8 %06o to %06o\r\n", cpu::PC, v, a);
  }
  */
  if (a < 0760000) {
    core8[a] = v & 0xFF; // bootloader does things
    return;
  }
  if (a & 1) {
    write16(a&~1, (read16(a) & 0xFF) | (v & 0xFF) << 8);
    return;
  } 
  write16(a&~1, (read16(a) & 0xFF00) | (v & 0xFF));
}

};