// This module is a pure forth implementation of the "core words", wrapped
// around a C module so that we don't need to keep a file next to the forth
// executable for it to work properly.

extern void interpret_line(const char *line);

void init_core_defs()
{
    char *lines[] = {
        ": allot here @ + here ! ;",
        ": variable create 2 allot ;",
        ": ? @ . ;",
        ": , here @ ! 2 allot ;",
        ": C, here @ C! 1 allot ;",
        ": splitb dup 8 rshift swap 0xff and ;",
        ": +! dup rot rot @ + swap ! ;",
        ": +1! 1 swap +! ;",
    };
    for (int i=0; i<(sizeof(lines)/sizeof(char*)); i++) {
        interpret_line(lines[i]);
    }
}
