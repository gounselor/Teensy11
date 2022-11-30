#include <Arduino.h>
#include <pdp11.h>
#include "cpu.h"
#include "mmu.h"

#define DEBUG_MMU 0

namespace mmu {

struct page {
    uint16_t par;
    uint16_t pdr;
};

page pages[16];
uint16_t SR0, SR1, SR2;

void reset() {
  SR0 = 0;  
  for (uint8_t i = 0; i < 16; i++) {
    pages[i].par = 0;
    pages[i].pdr = 0;
  }  
}
// crashes fs a is changed to 32bit
uint32_t decode(const uint16_t a, const bool w, const bool user) {
  if (SR0 & 1) {
    // mmu enabled
    const uint8_t i = user ? ((a >> 13) + 8) : (a >> 13);
    if (w && !(pages[i].pdr & 6)) {
      SR0 = (1 << 13) | 1;
      SR0 |= (a >> 12) & ~1;
      if (user) {
        SR0 |= (1 << 5) | (1 << 6);
      }
      SR2 = cpu::PC;
      Serial.print(F("mmu::decode write to read-only page ")); Serial.println(a, OCT);
      longjmp(trapbuf, INTFAULT);
    }
    if (!(pages[i].pdr & 2)) {
      SR0 = (1 << 15) | 1;
      SR0 |= (a >> 12) & ~1;
      if (user) {
        SR0 |= (1 << 5) | (1 << 6);
      }
      SR2 = cpu::PC;
      Serial.print(F("mmu::decode read from no-access page ")); Serial.println(a, OCT);
      longjmp(trapbuf, INTFAULT);
    }
    const uint8_t block = (a >> 6) & 0177;
    const uint8_t disp = a & 077;
    // if ((p.ed() && (block < p.len())) || (!p.ed() && (block > p.len()))) {
    if ((pages[i].pdr & 8) ? (block < ((pages[i].pdr >> 8) & 0x7f)) : (block > ((pages[i].pdr >>8) & 0x7f))) {
      SR0 = (1 << 14) | 1;
      SR0 |= (a >> 12) & ~1;
      if (user) {
        SR0 |= (1 << 5) | (1 << 6);
      }
      SR2 = cpu::PC;
      //Serial.printf("page %d length exceeded, address %06o (block %03o) is beyond length %03o\r\n", 
      //i, a, block, ((pages[i].pdr >> 8) & 0x7f));
      longjmp(trapbuf, INTFAULT);
    }
    if (w) {
      pages[i].pdr |= 1 << 6;
    }
    uint32_t addr = ((block + (pages[i].par & 07777)) << 6) + disp;
    if (DEBUG_MMU) {
      Serial.print("decode: slow "); Serial.print(a, OCT); Serial.print(" -> "); Serial.println(addr, OCT);
    }
    return addr;
  }
  // mmu disabled, fast path
  return a > 0167777 ? ((uint32_t)a) + 0600000 : a;                                      
}

uint16_t read16(const uint32_t a) {
  uint8_t i = (a & 017) >> 1;
  if ((a >= 0772300) && (a < 0772320)) {
    return pages[i].pdr;
  }
  if ((a >= 0772340) && (a < 0772360)) {
    return pages[i].par;
  }
  if ((a >= 0777600) && (a < 0777620)) {
    return pages[i + 8].pdr;
  }
  if ((a >= 0777640) && (a < 0777660)) {
    return pages[i + 8].par;
  }
  Serial.print(F("mmu::read16 invalid address: ")); Serial.println(a, OCT);
  longjmp(trapbuf, INTBUS);
}

void write16(const uint32_t a, const uint16_t v) {
  uint8_t i = ((a & 017) >> 1);
  if ((a >= 0772300) && (a < 0772320)) {
    pages[i].pdr = v;
    return;
  }
  if ((a >= 0772340) && (a < 0772360)) {
    pages[i].par = v;
    return;
  }
  if ((a >= 0777600) && (a < 0777620)) {
    pages[i + 8].pdr = v;
    return;
  }
  if ((a >= 0777640) && (a < 0777660)) {
    pages[i + 8].par = v;
    return;
  }
  Serial.print(F("mmu::write16 invalid address: ")); Serial.println(a, OCT);
  longjmp(trapbuf, INTBUS);
}

};
