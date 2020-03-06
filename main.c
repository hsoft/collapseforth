#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "emul.h"
#include "core.h"
#include "z80-bin.h"

#define NAME_LEN 8
/* About dictionary

Structure

- 1b EntryType
- 8b name
- 2b prev entry offset, 0 for none
- 2b+ data

*/
#define DICT_ADDR 0x3000
#define DICT_SIZE 0x1000
// offsets for each field
#define ENTRY_FIELD_TYPE 0
#define ENTRY_FIELD_NAME 1
#define ENTRY_FIELD_PREV 9
#define ENTRY_FIELD_DATA 11

// System variables
// See *variables* section in dictionary.txt for meaning.
#define HERE_ADDR 0x2ffe
#define CURRENT_ADDR 0x2ffc
// Offset where we place our bitwise flags
#define FLAGS_ADDR 0x2ffb
// When reading a word, we place the last read WS in this address so that we
// can properly detect newlines
#define LASTWS_ADDR 0x2ffa

// Offset where we place currently read word
#define CURWORD_ADDR 0x2f00

// Whether the parsing of the current line has been aborted and that we need to
// return to the interpreter
#define FLAG_QUITTING 0

// Z80 Ports
#define STDIO_PORT 0x00

typedef void (*Callable) ();

typedef enum {
    // Entry is a compile list of words. arg points to address in heap.
    TYPE_COMPILED = 0,
    // Entry links to native code. If arg < 0x20, it's an index in native_funcs
    // Array. Otherwise, it's a code offset to call in z80.
    TYPE_NATIVE = 1,
    // Entry is a cell, arg holds cell value.
    TYPE_CELL = 2,
} EntryType;

typedef enum {
    TYPE_WORD,
    TYPE_NUM,
    TYPE_STOP
} HeapItemType;

typedef struct {
    HeapItemType type;
    int arg;
    int next; // offset of next item
} HeapItem;

typedef struct {
    uint16_t offset; // offset where it lives.
    uint16_t next; // set by find() to have a an easy link to next.
    char *name;
    uint16_t prev;
    EntryType type;
    uint16_t arg; // see EntryType comments.
} DictionaryEntry;

// Whether we should continue running the program
static bool running = true;
// Current stream being read.
FILE *curstream;

static Machine *m;

// Foward declarations
static void execute();
static bool interpret();
static void call_native(int index);
static void call();

// Internal

static uint16_t readw(uint16_t offset)
{
    uint16_t r;
    r = m->mem[offset];
    r |= m->mem[offset+1] << 8;
    return r;
}

static void writew(uint16_t offset, uint16_t dest)
{
    m->mem[offset] = dest & 0xff;
    m->mem[offset+1] = dest >> 8;
}

static bool _aborted()
{
    return m->mem[FLAGS_ADDR] & (1 << FLAG_QUITTING);
}

static void _unquit()
{
    m->mem[FLAGS_ADDR] &= ~(1 << FLAG_QUITTING);
}

static void readentry(DictionaryEntry *de, uint16_t offset)
{
    de->offset = offset;
    de->type = (EntryType)m->mem[offset+ENTRY_FIELD_TYPE];
    de->name = &m->mem[offset+ENTRY_FIELD_NAME];
    de->prev = readw(offset+ENTRY_FIELD_PREV);
    de->arg = readw(offset+ENTRY_FIELD_DATA);
}

static DictionaryEntry find(char *word)
{
    DictionaryEntry de;
    // We purposefully omit the "no entry" case: never happens.
    de.prev = readw(CURRENT_ADDR);
    de.offset = readw(HERE_ADDR);
    de.next = 0;
    while (de.prev > 0) {
        de.next = de.offset; // useful for forget()
        readentry(&de, de.prev);
        if (strncmp(word, de.name, NAME_LEN) == 0) {
            return de;
        }
    }
    de.offset = 0;
    return de;
}

// Creates and returns a new dictionary entry. That entry has its header written
// to memory.
static DictionaryEntry _create(char *name, EntryType type, uint16_t extra_allot)
{
    DictionaryEntry de;
    de.type = type;
    de.name = name;
    de.arg = 0;
    de.prev = readw(CURRENT_ADDR);
    de.offset = readw(HERE_ADDR);
    m->mem[de.offset+ENTRY_FIELD_TYPE] = de.type;
    strncpy(&m->mem[de.offset+ENTRY_FIELD_NAME], de.name, NAME_LEN);
    writew(de.offset+ENTRY_FIELD_PREV, de.prev);
    writew(CURRENT_ADDR, de.offset);
    writew(HERE_ADDR, de.offset + ENTRY_FIELD_DATA + extra_allot);
    return de;
}

static HeapItem readheap(int offset)
{
    HeapItem r;
    byte val = m->mem[offset];
    if (val == 0xff) {
        r.type = TYPE_STOP;
    } else if (val == 0xfe) {
        r.type = TYPE_NUM;
        r.arg = readw(offset+1);
        r.next = offset+3;
    } else {
        r.type = TYPE_WORD;
        r.arg = readw(offset+1);
        r.next = offset+3;
    }
    return r;
}

static void writeheap(HeapItem *hi)
{
    uint16_t nextoffset = readw(HERE_ADDR);
    switch (hi->type) {
        case TYPE_STOP:
            m->mem[nextoffset++] = 0xff;
            break;
        case TYPE_NUM:
            m->mem[nextoffset++] = 0xfe;
            writew(nextoffset, hi->arg);
            nextoffset += 2;
            break;
        case TYPE_WORD:
            m->mem[nextoffset++] = 0xfd;
            writew(nextoffset, hi->arg);
            nextoffset += 2;
            break;
    }
    writew(HERE_ADDR, nextoffset);
}

static void error(char *msg)
{
    if (msg != NULL) {
        fprintf(stderr, "%s\n", msg);
    }
    m->mem[FLAGS_ADDR] |= (1 << FLAG_QUITTING);
    return;
}

static void push(uint16_t x)
{
    m->cpu.R1.wr.SP -= 2;
    writew(m->cpu.R1.wr.SP, x);
}

static uint16_t pop()
{
    if (m->cpu.R1.wr.SP == 0xffff) {
        error("Stack underflow");
        return 0;
    }
    uint16_t r = readw(m->cpu.R1.wr.SP);
    m->cpu.R1.wr.SP += 2;
    return r;
}

static int readc()
{
    return fgetc(curstream);
}

static char* readword()
{
    int c;
    char *s = &m->mem[CURWORD_ADDR];
    *s = '\0';
    while (1) {
        c = readc();
        if ((c == EOF) || (c == '\n')) {
            return NULL;
        }
        if (c > ' ') break;
    }
    while (1) {
        *s = c;
        s++;
        c = readc();
        if ((c == EOF) || (c <= ' ')) break;
    }
    m->mem[LASTWS_ADDR] = c;
    *s = '\0';
    return &m->mem[CURWORD_ADDR];
}

static void compile(HeapItem *hi, char *word)
{
    hi->type = TYPE_STOP;
    if ((word == NULL) || (*word == '\0')) { // EOL
        return;
    }
    DictionaryEntry de = find(word);
    if (de.offset > 0) {
        hi->type = TYPE_WORD;
        hi->arg = de.offset;
    } else {
        // not in dict, maybe a number?
        char *endptr;
        int num;
        if (strncmp(word, "0x", 2) == 0) {
            // Hex literal
            word += 2;
            num = strtol(word, &endptr, 16);
        } else {
            // Try decimal
            num = strtol(word, &endptr, 10);
        }
        if ((endptr > word) && (*endptr < ' ')) {
            // whole word read, this means it was a number, we're good.
            hi->type = TYPE_NUM;
            hi->arg = num;
        } else {
            // not a number
            fprintf(stderr, "What is %s?\n", word);
            error(NULL);
        }
    }
}

static HeapItemType execstep(HeapItem *hi)
{
    switch (hi->type) {
        case TYPE_NUM:
            push(hi->arg);
            break;
        case TYPE_WORD:
            push(hi->arg);
            execute();
            break;
    }
    return hi->type;
}

// Not static because it's used in core_forth.c
void interpret_line(char *line)
{
    FILE *oldstream = curstream;
    curstream = fmemopen(line, strlen(line), "r");
    _unquit();
    while (interpret());
    fclose(curstream);
    curstream = oldstream;
}

// Callable
static void execute() {
    int offset = pop();
    if (_aborted()) return;
    DictionaryEntry de;
    readentry(&de, offset);
    switch (de.type) {
        case TYPE_COMPILED:
            offset = offset + ENTRY_FIELD_DATA;
            HeapItem hi = readheap(offset);
            while (execstep(&hi) != TYPE_STOP) {
                hi = readheap(hi.next);
            }
            break;
        case TYPE_NATIVE:
            if (de.arg < 0x20) {
                call_native(de.arg);
            } else {
                push(offset+ENTRY_FIELD_DATA);
                call();
            }
            break;
        case TYPE_CELL:
            push(offset+ENTRY_FIELD_DATA);
            break;
    }
}

// Returns true if we still have words to interpret.
static bool interpret() {
    char *word = readword();
    if (word == NULL) {
        return false;
    }
    HeapItem hi;
    compile(&hi, word);
    return !_aborted() && (execstep(&hi) != TYPE_STOP);
}

static void bye()
{
    running = false;
}

static void dot()
{
    uint16_t num = pop();
    if (_aborted()) return;
    printf("%d", num);
}

static void dotx()
{
    uint16_t num = pop();
    if (_aborted()) return;
    printf("%02x", num);
}

static void define()
{
    char *word = readword();
    if (!*word) {
        error("No define name");
        return;
    }
    // we start writing the heap right after the entry's header
    DictionaryEntry de = _create(word, TYPE_COMPILED, 0);
    word = readword();
    HeapItem hi;
    while ((*word) && (*word != ';')) {
        compile(&hi, word);
        writeheap(&hi);
        if (_aborted()) {
            // Something went wrong, let's rollback on new entry
            writew(CURRENT_ADDR, de.prev);
            writew(HERE_ADDR, de.offset);
            return;
        }
        word = readword();
    }
    hi.type = TYPE_STOP;
    writeheap(&hi);
}

static void loadf()
{
    char *line = NULL;
    ssize_t read;
    ssize_t len = 0;
    char *fname = readword();

    if (!fname) {
        error("Missing filename");
        return;
    }
    FILE *oldstream = curstream;
    curstream = fopen(fname, "r");
    if (!curstream) {
        error("Can't open file");
        curstream = oldstream;
        return;
    }
    _unquit();
    while (interpret());
    fclose(curstream);
    curstream = oldstream;
}

static void forget()
{
    char *word = readword();
    if (!*word) {
        error("No specified name");
        return;
    }
    DictionaryEntry de = find(word);
    if (de.offset == 0) {
        error("Name not found");
        return;
    }
    if (de.offset == readw(CURRENT_ADDR)) {
        // We're the last of the chain
        writew(CURRENT_ADDR, de.prev);
        writew(HERE_ADDR, de.offset);
    } else {
        // not the last, we have to hook stuff.
        // de.next is the offset of the next entry. We need to write "de.prev"
        // in that entry's "prev" offset, which is offset+9
        writew(de.next+9, de.prev);
    }
}

static void create()
{
    char *word = readword();
    if (!*word) {
        error("Name needed");
        return;
    }
    // The create word doesn't allot any data.
    _create(word, TYPE_CELL, 0);
}

// get pointer to word reg
static ushort* _getwreg(char *name)
{
    if (strcmp(name, "AF") == 0) {
        return &m->cpu.R1.wr.AF;
    } else if (strcmp(name, "BC") == 0) {
        return &m->cpu.R1.wr.BC;
    } else if (strcmp(name, "DE") == 0) {
        return &m->cpu.R1.wr.DE;
    } else if (strcmp(name, "HL") == 0) {
        return &m->cpu.R1.wr.HL;
    } else if (strcmp(name, "IX") == 0) {
        return &m->cpu.R1.wr.IX;
    } else if (strcmp(name, "IY") == 0) {
        return &m->cpu.R1.wr.IY;
    } else if (strcmp(name, "SP") == 0) {
        return &m->cpu.R1.wr.SP;
    }
    return NULL;
}

static byte* _getbreg(char *name)
{
    if (name[1] != '\0') {
        return NULL;
    }
    switch (name[0]) {
        case 'A': return &m->cpu.R1.br.A;
        case 'F': return &m->cpu.R1.br.F;
        case 'B': return &m->cpu.R1.br.B;
        case 'C': return &m->cpu.R1.br.C;
        case 'D': return &m->cpu.R1.br.D;
        case 'E': return &m->cpu.R1.br.E;
        case 'H': return &m->cpu.R1.br.H;
        case 'L': return &m->cpu.R1.br.L;
        default: return NULL;
    }
}

static void regr()
{
    char *name = readword();
    ushort *w = _getwreg(name);
    if (w != NULL) {
        push(*w);
    } else {
        byte *b = _getbreg(name);
        if (b != NULL) {
            push(*b);
        } else {
            error("Invalid register\n");
        }
    }
}

static void regw()
{
    char *name = readword();
    ushort *w = _getwreg(name);
    if (w != NULL) {
        *w = pop();
    } else {
        byte *b = _getbreg(name);
        if (b != NULL) {
            *b = pop();
        } else {
            error("Invalid register\n");
        }
    }
}

static void minus()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 - n2);
}

static void mult()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 * n2);
}

static void div_()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 / n2);
}

static void and_()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 & n2);
}

static void or_()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 | n2);
}

static void lshift()
{
    uint16_t x = pop();
    uint16_t n = pop();
    push(n << x);
}

static void rshift()
{
    uint16_t x = pop();
    uint16_t n = pop();
    push(n >> x);
}

static void call()
{
    m->cpu.PC = pop();
    // Run until we encounter a RET (0xc9)
    m->cpu.halted = 0;
    while ((m->mem[m->cpu.PC] != 0xc9) && emul_step());
}

static void apos()
{
    char *word = readword();
    DictionaryEntry de = find(word);
    if (de.offset == 0) {
        error("Name not found");
        return;
    }
    push(de.offset);
}

static void see()
{
    uint16_t addr = pop();
    char buf[NAME_LEN+1] = {0};
    strncpy(buf, &m->mem[addr+ENTRY_FIELD_NAME], NAME_LEN);
    printf("Addr: %04x Type: %x Name: %s Prev: %04x Dump:\n",
        addr, m->mem[addr], buf, readw(addr+ENTRY_FIELD_PREV));
    for (int i=0; i<32; i++) {
        printf("%02x", m->mem[addr+ENTRY_FIELD_DATA+i]);
    }
    printf("\n");
}

// Z80 I/Os
static uint8_t iord_stdio()
{
    int c = getchar();
    if (c != EOF) {
        return c & 0xff;
    } else {
        // EOF
        return 0;
    }
}

static void iowr_stdio(uint8_t val)
{
    putchar(val);
}

// Main loop
static Callable native_funcs[] = {
    bye, dot, execute, define, loadf,
    forget, create, regr, regw, minus, mult, div_,
    and_, or_, lshift, rshift, call, dotx};

static void call_native(int index)
{
    native_funcs[index]();
}

static void nativeentry(char *name, int index)
{
    DictionaryEntry de = _create(name, TYPE_NATIVE, 2);
    writew(de.offset+ENTRY_FIELD_DATA, index);
}

static void z80entry(char *name, unsigned char* bin, uint16_t binlen)
{
    DictionaryEntry de = _create(name, TYPE_NATIVE, binlen+1);
    for (int i=0; i<binlen; i++) {
        m->mem[de.offset+ENTRY_FIELD_DATA+i] = bin[i];
    }
    // End with a RET (0xc9)
    m->mem[de.offset+ENTRY_FIELD_DATA+binlen] = 0xc9;
}

static void init_dict()
{
    int i = 0;
    // same order as in native_funcs
    nativeentry("bye", i++);
    nativeentry(".", i++);
    nativeentry("execute", i++);
    nativeentry(":", i++);
    nativeentry("loadf", i++);
    nativeentry("forget", i++);
    nativeentry("create", i++);
    nativeentry("regr", i++);
    nativeentry("regw", i++);
    nativeentry("-", i++);
    nativeentry("*", i++);
    nativeentry("/", i++);
    nativeentry("and", i++);
    nativeentry("or", i++);
    nativeentry("lshift", i++);
    nativeentry("rshift", i++);
    nativeentry("call", i++);
    nativeentry(".x", i++);
    nativeentry("'", i++);
    nativeentry("see", i++);
    z80entry("+", plus_bin, sizeof(plus_bin));
    z80entry("swap", swap_bin, sizeof(swap_bin));
    z80entry("emit", emit_bin, sizeof(emit_bin));
    z80entry("dup", dup_bin, sizeof(dup_bin));
    z80entry("here", here_bin, sizeof(here_bin));
    z80entry("current", current_bin, sizeof(current_bin));
    z80entry("C!", storec_bin, sizeof(storec_bin));
    z80entry("C@", fetchc_bin, sizeof(fetchc_bin));
    z80entry("!", store_bin, sizeof(store_bin));
    z80entry("@", fetch_bin, sizeof(fetch_bin));
    z80entry("over", over_bin, sizeof(over_bin));
    z80entry("rot", rot_bin, sizeof(rot_bin));
}

int main(int argc, char *argv[])
{
    curstream = stdin;
    m = emul_init();
    m->iord[STDIO_PORT] = iord_stdio;
    m->iowr[STDIO_PORT] = iowr_stdio;
    m->cpu.R1.wr.SP = 0xffff;
    writew(HERE_ADDR, DICT_ADDR);
    writew(CURRENT_ADDR, 0);
    init_dict();
    running = true;
    init_core_defs();
    if (argc > 1) {
        // We have arguments. Interpret then and exit
        for (int i=1; i<argc; i++) {
            interpret_line(argv[i]);
        }
        return 0;
    }
    char inputbuf[0x200];
    while (running) {
        _unquit();
        while (interpret() && running && m->mem[LASTWS_ADDR] != '\n');
        if (running && !_aborted()) {
            printf(" ok\n");
        }
    }
    return 0;
}
