#include <stdint.h>
#include <Arduino.h>
#include <SdFat.h>
#include <RTClib.h>
#include <pdp11.h>
#include "unibus.h"
#include "rk05.h"
#include "cpu.h"

#define DEBUG_RK05 0

extern RTC_DS3231 rtc;

bool patch_super = false;

namespace rk11 {

uint32_t RKBA, RKDS, RKER, RKCS, RKWC;
uint32_t drive, sector, surface, cylinder;

struct disk rkdata[RK_NUM_DRV];

uint16_t read16(const uint32_t a) {
  if (DEBUG_RK05) {
    Serial.printf("rk11: read16: %06o\r\n", a);
  }
  switch (a) {
    case 0777400: // RKDS
    if (rkdata[drive].write_lock) {
        return (RKDS | (1 << 5));
    }
    case 0777402: // RKER
      return RKER;
    case 0777404: // RKCS
      return RKCS | (RKBA & 0x30000) >> 12;
    case 0777406: // RKWC
      return RKWC;
    case 0777410: // RKBA
      return RKBA & 0xFFFF;
    case 0777412: // RKDA
      return (sector) | (surface << 4) | (cylinder << 5) | (drive << 13);
    default:
      Serial.printf("rk11: invalid read16: %06o\r\n", a);
      panic();
  }
  return 0; // unreached
}

static void rknotready() {
  RKDS &= ~(1 << 6);
  RKCS &= ~(1 << 7);
}

static void rkready() {
  RKDS |= 1 << 6;
  RKCS |= 1 << 7;
}

void rkerror(const uint32_t e) {
  rkready();
  RKER |= e;
  RKCS |= (1<<15) | (1<<14);
  switch(e) {
    case RKOVR: 
    Serial.println("rk11: overflow"); 
    break;
    case RKNXD: 
    Serial.println("rk11: invalid disk accessed"); 
    break;
    case RKNXC: 
    Serial.println("rk11: invalid cylinder accessed"); 
    break;
    case RKNXS: 
    Serial.println("rk11: invalid sector accessed"); 
    break;
    default:
    Serial.printf("rk11: unknown error %06o\r\n", e);
  }
  if ((RKCS >> 5) & 1) {
    cpu::interrupt(INTRK, 5);  
  }  
  longjmp(trapbuf, INTBUS);
}

static void step() {
  again:
  yield();
  bool w = false;
  uint32_t cmd = (RKCS & 017) >> 1;
  switch (cmd) {
    case 0: //
      rk11::reset();
      rkready();
      return;
      break;
    case 1: // write
      w = true;
      break;
    case 2: // read
      w = false;
      break;
    case 6:
      rk11::reset();
      return;
      break;
    case 7: // write lock        
      rkready();
      if (RKCS & (1 << 6)) {
        cpu::interrupt(INTRK, 5);
      }
      return;
      break;
    default:
      Serial.printf("rk11: unimplemented operation: %06o\r\n", cmd);
      rknotready();
      return; // was panic
  }
  
  if (DEBUG_RK05) {
    Serial.printf("rk11: RKBA: %06o RKWC: %06o cyl: %d, sec: %d, write: ", RKBA, RKWC, cylinder, sector);
    Serial.println(w ? "true" : "false");
    //printstate();
  }

  if (drive > RK_NUM_DRV) {
    //Serial.printf("rk11: drive > 4: %d\r\n", drive);
    rkerror(RKNXD);
    rkready();
    //rknotready();
    return;
  }
  if (!rkdata[drive].attached) {
    rkerror(RKNXD);
    rkready();
    //rknotready();
    return;
  }
  if (cylinder > 0312) {
    Serial.printf("rk11: cylinder > 0312: %06o\r\n", cylinder);
    rkerror(RKNXC);
    rkready();
    return;
  }
  if (sector > 013) {
    Serial.printf("rk11: sector > 013: %06o\r\n", sector);
    rkerror(RKNXS);
    rkready();
    return;
  }

  const uint32_t pos = (cylinder * 24 + surface * 12 + sector) * 512;
  if (!rkdata[drive].file.seekSet(pos)) {
    Serial.printf("rk11: failed to seek: drive: %d, pos: %d, cyl: %d, sur: %d, sec: %d\r\n",
      drive, pos, cylinder, surface, sector);
    panic();
  }
  __disable_irq();
  if (w) { // write
    for (int i = 0; i < 256 && RKWC != 0; i++) {
      const uint16_t val = unibus::read16(RKBA);
      rkdata[drive].file.write(val & 0xFF);
      rkdata[drive].file.write((val >> 8) & 0xFF);
      RKBA += 2;
      RKWC = (RKWC + 1) & 0xFFFF;
    }
  } else {
    uint32_t patch_time = 0;
    if (patch_super && drive == 0 && pos == 512) { // superblock
      patch_time = rtc.now().unixtime();
    }
    for (int i = 0; i < 256 && RKWC != 0; i++) {
        uint16_t dv = rkdata[drive].file.read() | (rkdata[drive].file.read() << 8);
        if (patch_time) {
          if (i == 206) {
            dv = patch_time >> 16;
          }
          if (i == 207) {
            dv = patch_time & 0xFFFF;
          }
        }
        //Serial.printf("rkdata: %06o: %04x\r\n", RKBA, dv);
        unibus::write16(RKBA, dv);        
        RKBA += 2;
        RKWC = (RKWC + 1) & 0xFFFF;
    }
    patch_time = 0;
  }
  __enable_irq();
  yield();
  //digitalWriteFast(13,0);
  sector++;
  if (sector > 013) {
    sector = 0;
    surface++;
    if (surface > 1) {
      surface = 0;
      cylinder++;
      if (cylinder > 0312) {
        //rkerror(RKOVR);
        rkready();
      }
    }
  }
  if (RKWC == 0) {
    rkready();
    if (RKCS & (1 << 6)) {
      cpu::interrupt(INTRK, 5);
    }
  } else {
    goto again;
  }
}

void write16(const uint32_t a, uint16_t v) {
  //printf("rkwrite: %06o\n",a);
  switch (a) {
    case 0777400:
      break;
    case 0777402:
      break;
    case 0777404:
      RKBA = (RKBA & 0xFFFF) | ((v & 060) << 12);
      v &= 017517; // writable bits
      RKCS &= ~017517;
      RKCS |= v & ~1; // don't set GO bit
      if (v & 1) {
        switch ((RKCS & 017) >> 1) {
          case 0:
            reset();
            break;
          case 1:
          case 2:
            rknotready();
            step();
            break;
          case 6:
            rknotready();
            step();
            break;
          case 7:
            rkdata[drive].write_lock = true;
            rknotready();;
            step();
            break;
          default:
            Serial.printf("rk11: write16: unimplemented RK05 operation: %06o\r\n", (RKCS & 017)>>1);
            //panic();
        }
      }
      break;
    case 0777406:
      RKWC = v;
      break;
    case 0777410:
      RKBA = (RKBA & 0x30000) | (v);
      break;
    case 0777412:
      drive = v >> 13;
      cylinder = (v >> 5) & 0377;
      surface = (v >> 4) & 1;
      sector = v & 15;
      break;
    default:
      Serial.printf("rk11: invalid write16: %06o\r\n", a);
      panic();
  }
}

void reset() {
  RKDS = (1 << 11) | (1 << 7) | (1 << 6);
  RKCS = 1 << 7;
  RKER = 0;
  RKWC = 0;
  RKBA = 0;
}

};
