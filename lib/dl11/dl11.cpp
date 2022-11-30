#include <Arduino.h>
#include <pdp11.h>
#include "dl11.h"
#include "cpu.h"
#include "console.h"

namespace dl11 {

  uint32_t RCSR; // 777560
  uint32_t RBUF; // 777562
  uint32_t XCSR; // 777564
  uint32_t XBUF; // 777566
  
  uint32_t count;

  void reset() {
    RCSR = 0;
    RBUF = 0;    
    XCSR = 1 << 7; // xmit ready
    XBUF = 0;
  }

  static void addchar(const char c) {
    RCSR |= 0x80;
	  RBUF = c;
    if (RCSR & (1 << 6)) { // int enabled
      cpu::interrupt(INTTTYIN, 4);
    }
  }

  void poll() {
    if (Serial.available()) {
      char c = Serial.read();
      switch (c) {
        case 0x10: // ctrl-p
          console::loop(true);
          //print_state();
          break;
        case 0x14: // ctrl-t
          toggle_trace();
          break;
        default:
          addchar(c);
      }
    }
    if ((XCSR & 0x80) == 0) {
      if (++count > 64) { // 32: change this and unibus errors happen .oO(?)
        Serial.write(XBUF & 0x7f);        
        XCSR |= 0x80;
        if (XCSR & (1 << 6)) {
          cpu::interrupt(INTTTYOUT, 4);
        }
      }
    }    
  }

  // TODO(dfc) this could be rewritten to translate to the native AVR UART registers
  // http://www.appelsiini.net/2011/simple-usart-with-avr-libc

  uint16_t read16(const uint32_t a) {
    //Serial.printf("cons: read16: %06o, RCSR: %06o, RBUF: %06o, XBUF: %06o, XCSR: %06o\r\n", a, RCSR, RBUF, XBUF, XCSR);
    switch (a) {
      case 0777560:
        return RCSR;
      case 0777562:
        if (RCSR & 0x80) {
          RCSR &= 0xff7e;
          return RBUF;
        }
        return 0;
      case 0777564:  
        //Serial.printf("cons: read16: PC: %06o, XCSR: %06o, XBUF: %06o\r\n", cpu::PC, XCSR, XBUF);
        return XCSR;  // xmit status 
      case 0777566:
        return 0;     // write only
      default:
        Serial.printf("dl11: read16 from invalid address: %06o\r\n", a);
        panic();
    }
    return 0; // not reached
  }

  void write16(const uint32_t a, const uint16_t v) {
    //Serial.printf("cons: write16: %06o %06o, RCSR: %06o, RBUF: %06o, XBUF: %06o, XCSR: %06o\r\n", a, v, RCSR, RBUF, XBUF, XCSR);
    switch (a) {
      case 0777560:
        if (v & (1 << 6)) {
          RCSR |= 1 << 6;
        } else {
          RCSR &= ~(1 << 6);
        }
        break;
      case 0777564:
        if (v & (1 << 6)) {
          XCSR |= 1 << 6;
        } else {
          XCSR &= ~(1 << 6);
        }
        break;
      case 0777566:
        XBUF = v & 0xff;
        XCSR &= 0xff7f;
        count = 0;
        break;
      default:
        Serial.printf("dl11: write16 to invalid address: %06o\r\n", a); // " + ostr(a, 6))
        panic();
    }
  }
};
