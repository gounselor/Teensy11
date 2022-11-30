#include <setjmp.h>

extern jmp_buf trapbuf;

namespace pdp11 {
struct intr {
  uint32_t vec;
  uint32_t pri;
};
};

#define ITABN 32

extern pdp11::intr itab[ITABN];

typedef union {
  struct {
    uint32_t C:1;
    uint32_t V:1;
    uint32_t Z:1;
    uint32_t N:1;
    uint32_t T:1;
    uint32_t SLP:3;
    uint32_t _reserved:4;
    uint32_t PM:2;
    uint32_t CM:2;
  } Flags;
  uint32_t Word;
} PSW;


namespace cpu {

extern uint32_t R[8];

extern PSW PS;
extern uint32_t PC;
extern uint32_t USP;
extern uint32_t KSP;
extern uint32_t LKS;
extern bool curuser;
extern bool prevuser;
extern bool g_cmd;

void print_stats();
void step();
void reset(void);
void switchmode(bool newm);

void trapat(uint16_t vec);
void interrupt(uint8_t vec, uint8_t pri);
void handleinterrupt();

};
