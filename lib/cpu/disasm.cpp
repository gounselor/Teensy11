#include <Arduino.h>
#include <pdp11.h>
#include "cpu.h"
#include "mmu.h"
#include "unibus.h"

typedef struct {
  uint16_t mask;
  const char *mnemonic;
  uint8_t sp;
  uint8_t flags;
} instr;

enum {
  DF = 1 << 1,
  SF = 1 << 2,
  NN = 1 << 3,
  RR = 1 << 4,
  O = 1 << 5,
};

const char *Reg[] = {
  "R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"
};

instr table[] = {
    { 0160000, "SUB",   2, SF|DF },
    { 0150000, "BISB",  1, SF|DF },
    { 0140000, "BICB",  1, SF|DF },
    { 0130000, "BITB",  1, SF|DF },
    { 0120000, "CMPB",  1, SF|DF },
    { 0110000, "MOVB",  1, SF|DF },

    { 0106600, "MTPD",  1, SF },
    { 0106500, "MFPD",  1, DF },
    { 0106300, "ASLB",  1, DF },
    { 0106200, "ASRB",  1, DF },
    { 0106100, "ROLB",  1, DF },
    { 0106000, "RORB",  1, DF },
    { 0105700, "TSTB",  1, DF },
    { 0105600, "SBCB",  1, DF },
    { 0105500, "ADCB",  1, DF },
    { 0105400, "NEGB",  1, DF },
    { 0105300, "DECB",  1, DF },
    { 0105200, "INCB",  1, DF },
    { 0105100, "COMB",  1, DF },
    { 0105000, "CLRB",  1, DF },
    { 0104400, "TRAP",  1, NN },
    { 0104000, "EMT",   2, NN },
    { 0103400, "BCS",   2, O },
    { 0103000, "BCC",   2, O },
    { 0102400, "BVS",   2, O },
    { 0102000, "BVC",   2, O },
    { 0101400, "BLOS",  1, O },
    { 0101000, "BHI",   2, O },
    { 0100400, "BMI",   2, O },
    { 0100000, "BPL",   2, O },

    { 0077000, "SOB",   2, RR|O },
    { 0075000, "FADD",  1, RR },
    { 0075010, "FSUB",  1, RR },
    { 0075020, "FMUL",  1, RR },
    { 0075030, "FDIV",  1, RR },
    { 0074000, "XOR",   2, RR|DF },
    { 0073000, "ASHC",  1, RR|SF },
    { 0072000, "ASH",   2, RR|SF },
    { 0071000, "DIV",   2, RR|SF },
    { 0070000, "MUL",   2, RR|SF },
    { 0060000, "ADD",   2, SF|DF },
    { 0050000, "BIS",   2, SF|DF },
    { 0040000, "BIC",   2, SF|DF },
    { 0030000, "BIT",   2, SF|DF },
    { 0020000, "CMP",   2, SF|DF },
    { 0010000, "MOV",   2, SF|DF },

    { 0006700, "SXT",   2, DF },
    { 0006600, "MTPI",  1, DF },
    { 0006500, "MFPI",  1, DF }, // was SF
    { 0006400, "MARK",  1, NN },
    { 0006300, "ASL",   2, DF },
    { 0006200, "ASR",   2, DF },
    { 0006100, "ROL",   2, DF },
    { 0006000, "ROR",   2, DF },
    { 0005700, "TST",   2, DF },
    { 0005600, "SBC",   2, DF },
    { 0005500, "ADC",   2, DF },
    { 0005400, "NEG",   2, DF },
    { 0005300, "DEC",   2, DF },
    { 0005200, "INC",   2, DF },
    { 0005100, "COM",   2, DF },
    { 0005000, "CLR",   2, DF },

    { 0004000, "JSR",   2, RR|DF },
    { 0003400, "BLE",   2, O },
    { 0003000, "BGT",   2, O },
    { 0002400, "BLT",   2, O },
    { 0002000, "BGE",   2, O },
    { 0001400, "BEQ",   2, O },
    { 0001000, "BEQ",   2, O },
    { 0000400, "BR",    3, O },

    { 0000300, "SWAB",  1, DF },
    { 0000277, "SCC",   2, 0 },
    { 0000270, "SEN",   2, 0 },
    { 0000264, "SEZ",   2, 0 },
    { 0000262, "SEV",   2, 0 },
    { 0000261, "SEC",   2, 0 },
    { 0000257, "CCC",   2, 0 },
    { 0000250, "CLN",   2, 0 },
    { 0000244, "CLZ",   2, 0 },
    { 0000242, "CLV",   2, 0 },
    { 0000241, "CLC",   2, 0 },
    { 0000240, "NOP",   2, 0 },

    { 0000200, "RTS",   2, RR },

    { 0000100, "JMP",   2, DF },

    { 0000006, "RTT",   2, 0 },
    { 0000005, "RESET", 0, 0 },
    { 0000004, "IOT",   2, 0 },
    { 0000003, "BPT",   2, 0 },
    { 0000002, "RTI",   2, 0 },
    { 0000001, "WAIT",  0, 0 },
    { 0000000, "HALT",  0, 0 },
    { 0177777, "ILL?",  0, 0 },
};

const char *space(uint8_t n) {
  switch (n) {
    case 1:
    return " ";
    break;
    case 2:
    return "  ";
    break;
    case 3:
    return "   ";
    break;
    default:
    return "";    
  }
}

void print_state() {
  Serial.println();
  Serial.printf("R0 %06o ", uint16_t(cpu::R[0]));
  Serial.printf("R1 %06o ", uint16_t(cpu::R[1]));
  Serial.printf("R2 %06o ", uint16_t(cpu::R[2]));
  Serial.printf("R3 %06o ", uint16_t(cpu::R[3]));
  Serial.printf("PS [%s%s%s%s%s%s] ",
    cpu::prevuser ? "u" : "k",
    cpu::curuser ? "U" : "K",
    cpu::PS.Flags.N ? "N" : " ",
    cpu::PS.Flags.Z ? "Z" : " ",
    cpu::PS.Flags.V ? "V" : " ",
    cpu::PS.Flags.C ? "C" : " ");
  Serial.println();
  Serial.printf("R4 %06o ", uint16_t(cpu::R[4]));
  Serial.printf("R5 %06o ", uint16_t(cpu::R[5]));
  Serial.printf("R6 %06o ", uint16_t(cpu::R[6]));
  Serial.printf("R7 %06o ", uint16_t(cpu::R[7]));
  disasm(mmu::decode(cpu::PC, false, cpu::curuser));  
}

uint32_t disasmaddr(uint16_t m, uint32_t a) {
  uint32_t offs = 0;
  //Serial.printf("[ m: %03o ]", m);
  if (m & 7) { // PC adressing
    switch (m) {
      case 027: // immediate        
        a += 2;
        Serial.printf("#%06o", unibus::read16(a));        
        return 2;
      case 037:
        a += 2;
        Serial.printf("@#%06o", unibus::read16(a));
        return 2;
      case 067:
        a += 2;
        Serial.printf("%06o", (a + 2 + (unibus::read16(a))) & 0xFFFF);
        return 2;
      case 077: // check
        a += 2;
        Serial.printf("@%06o", (a + 2 + (unibus::read16(a))) & 0xFFFF);
        return 0;
    }
  }

  switch (m & 070) {
    case 000: // register
      Serial.printf("%s", Reg[m & 7]);
      break;
    case 010: // register deferred
      Serial.printf("(%s)", Reg[m & 7]);
      break;
    case 020: // auto increment
      Serial.printf("(%s)+", Reg[m & 7]);
      break;
    case 030: // auto increment deferred XXX
      Serial.printf("@(%s)+", Reg[m & 7]);
      break;
    case 040: // auto decrement
      Serial.printf("-(%s)", Reg[m & 7]);
      break;
    case 050: // auto decrement deferred XXX
      Serial.printf("@-(%s)", Reg[m & 7]);
      break;
    case 060: // indexed
      offs = 2;
      a += 2;
      Serial.printf("%06o (%s)", unibus::read16(a), Reg[m & 7]);
      break;
    case 070: // indexed deferred
      offs = 2;
      a += 2;
      Serial.printf("@%06o (%s)", unibus::read16(a), Reg[m & 7]);
      break;
  }
  return offs;
}

uint32_t disasm(uint32_t a) {
  uint8_t ss = 0;
  uint8_t dd = 0;  
  uint8_t nn = 0;
  uint8_t offs;
  uint16_t opcode = unibus::read16(a);
  uint32_t next = a;
  instr *p;

  for (p = &table[0]; p->mask != 0177777; p++) {
    if ((opcode & p->mask) == p->mask) {
      Serial.printf("%06o: %06o %s%s", a, opcode, p->mnemonic, space(p->sp));
      switch(p->flags) {
      case SF|DF:
        ss = (opcode >> 6) & 077;
        Serial.print(' ');
        offs = disasmaddr(ss, a);
        next += offs;
        Serial.print(',');
        a+=offs;
      case DF:
        dd = opcode & 077;
        Serial.print(' ');
        next += disasmaddr(dd, a);
        break;
      case RR|O:        
        Serial.print(' ');
        Serial.print(Reg[(opcode & 0700) >> 6]);
        Serial.print(',');
      case O:
        offs = opcode & 077;
        if (opcode & 0200) {
          Serial.printf(" -%03o / %06o", (2 * (077 & (uint8_t) ~offs)), a - (2 * (077 & (uint8_t) ~offs)));
        } else {
          Serial.printf(" +%03o / %06o", (2 * offs), a + 2 + (2 * offs));
        };
        break;
      case RR|DF:
        dd = opcode & 077;
        Serial.print(' ');
        next += disasmaddr(dd, a);
        Serial.print(", ");
        Serial.print(Reg[(opcode & 0700) >> 6]);
        break;
      case RR|SF:
        ss = opcode & 077;
        Serial.print(' ');
        next += disasmaddr(ss, a);
        Serial.print(", ");
        Serial.print(Reg[(opcode & 0700) >> 6]);
        break;
      case RR:
        Serial.print(' ');
        Serial.print(Reg[opcode & 7]);
        break;
      case NN:
        nn = opcode & 0377;
        Serial.printf(" %03o", nn);
      }
      Serial.println();
      break;
    } // if    
  } // for 
  // print illegal here?
  return next + 2;
}
