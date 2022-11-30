#pragma once

#include <Arduino.h>

#ifdef LITTLE_ENDIAN
#define htonl(_x) bswap32(_x)
#define htons(_x) bswap16(_x)
#define ntohl(_x) bswap32(_x)
#define ntohs(_x) bswap16(_x)
#else 
#define htonl(_x) (_x)
#define htons(_x) (_x)
#define ntohl(_x) (_x)
#define ntohs(_x) (_x)
#endif

namespace tftp {

    /* tftp opcode mnemonic */
    enum opcode {
        RRQ=1,
        WRQ,
        DATA,
        ACK,
        ERROR
    };

    enum error {
        UNDEF = 0,     // undef
        ENOENT,        // File not found.
        EACCESS,       // Access violation.
        ENOSPC,        // Disk full or allocation exceeded.
        EINVAL,        // Illegal TFTP operation.
        EPROTO,        // Unknown Transfer ID.
        EEXISTS,       // File already exists.
        ENOUSR         // No such user.     
    };

    /* tftp transfer mode */
    enum mode {
        NETASCII = 1,
        OCTET
    };

    /* tftp message structure */
    typedef union {

        uint16_t opcode;

        struct request {
            uint16_t opcode; /* RRQ or WRQ */             
            uint8_t filename_and_mode[514];
        } request;     

        struct data {
            uint16_t opcode; /* DATA */
            uint16_t block_number;
            uint8_t data[512];
        } data;

        struct ack {
            uint16_t opcode; /* ACK */             
            uint16_t block_number;
        } ack;

        struct error {
            uint16_t opcode; /* ERROR */     
            uint16_t error_code;
            uint8_t error_string[512];
        } error;

    } tftp_message;

    static __inline uint16_t
    bswap16(uint16_t _x) {
        return ((uint16_t)((_x >> 8) | ((_x << 8) & 0xff00)));
    }

    static __inline uint32_t
    bswap32(uint32_t _x) { 
        return ((uint32_t)((_x >> 24) | ((_x >> 8) & 0xff00) |
            ((_x << 8) & 0xff0000) | ((_x << 24) & 0xff000000)));
    }
};
