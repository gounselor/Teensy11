#include "SdFat.h"

#define TM_NUM_DRV 8


namespace tm11 {

    struct tape {
        FsFile file;
        off_t pos;
        bool attached = false;
    };

    extern struct tape tmdata[TM_NUM_DRV];
    extern uint16_t MTBRC; // 772524 Byte Record Counter
    extern uint16_t MTCMA; 

    void reset();
    void go();
    uint16_t read16(uint32_t a);
    void write16(uint32_t a, uint16_t v);
 
};