#include <SdFat.h>
namespace unibus {
  
    // operations on uint32_t types are insanely expensive
    union addr {
     uint8_t  bytes[4];
     uint32_t value;
    };

    extern uint16_t SWR;
    extern uint16_t SLR;
    extern uint16_t PIRQ;

    uint16_t read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void write8(uint32_t a, uint16_t v);
    void write16(uint32_t a, uint16_t v);

    void reset(void);
    bool dump(void);
};

