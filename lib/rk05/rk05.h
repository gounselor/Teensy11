#include "SdFat.h"

#define RK_NUM_DRV 8

// enable rtc time superblock patch (v6 only?)
extern bool patch_super;

namespace rk11 {

  struct disk {
    FsFile file;
    bool attached = false;
    bool write_lock = false;
  };

  // V6 struct filsys
  struct filsys {
        uint16_t s_isize;        /* size in blocks of I list */
        uint16_t s_fsize;        /* size in blocks of entire volume */
        uint16_t s_nfree;        /* number of in core free blocks (0-100) */
        uint16_t s_free[100];    /* in core free blocks */
        uint16_t s_ninode;       /* number of in core I nodes (0-100) */
        uint16_t s_inode[100];   /* in core free I nodes */
        uint8_t  s_flock;        /* lock during free list manipulation */
        uint8_t  s_ilock;        /* lock during I list manipulation */
        uint8_t  s_fmod;         /* super block modified flag */
        uint8_t  s_ronly;        /* mounted read-only flag */
        uint16_t s_time[2];      /* current date of last update */
        uint16_t pad[50];        
  };  

  extern struct disk rkdata[RK_NUM_DRV];
  
  void reset();
  void write16(uint32_t a, uint16_t v);
  uint16_t read16(uint32_t a);

  extern uint32_t drive;
  extern uint32_t sector;
  extern uint32_t surface; 
  extern uint32_t cylinder;
};

enum {
  RKOVR = (1 << 14),
  RKNXD = (1 << 7),
  RKNXC = (1 << 6),
  RKNXS = (1 << 5)
};


