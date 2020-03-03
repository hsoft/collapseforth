#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "libz80/z80.h"

typedef byte (*IORD) ();
typedef void (*IOWR) (byte data);

typedef struct {
    Z80Context cpu;
    byte mem[0x10000];
    // Set to non-zero to specify where ROM ends. Any memory write attempt
    // below ramstart will trigger a warning.
    ushort ramstart;
    // The minimum value reached by SP at any point during execution.
    ushort minsp;
    // Array of 0x100 function pointers to IO read and write routines. Leave to
    // NULL when IO port is unhandled.
    IORD iord[0x100];
    IOWR iowr[0x100];
} Machine;

typedef enum {
    TRI_HIGH,
    TRI_LOW,
    TRI_HIGHZ
} Tristate;

Machine* emul_init();
bool emul_step();
bool emul_steps(unsigned int steps);
void emul_loop();
void emul_printdebug();
