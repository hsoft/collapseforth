#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "emul.h"
#include "core.h"

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

/* About heap item (misnomer for now...)
The heap is where compiled code lives. It's in z80 memory at a precise offset
and is refered to, in DictionaryEntry, as a memory offset.

Items in the heap are of variable length depending on their type:

- If it's a word reference, the heap item is 2 bytes long: it's a straight
  dictionary index.
- If it's a number, it's 3 bytes long: the first byte is 0xfe, followed by the
  2 bytes of the number.
- The stop indicator is one byte and is 0xff.

For this reason, the maximum theoretical number of dictionary entries is 0xfdff,
but it's anyway impossible to fit that many entries in a 64k memory space.
*/


typedef void (*Callable) ();

typedef enum {
    // Entry is a compile list of words. arg points to address in heap.
    TYPE_COMPILED = 0,
    // Entry links to native code. arg is an index of func to call in
    // native_funcs array.
    TYPE_NATIVE = 1,
    // Entry is a cell, arg holds cell value.
    TYPE_CELL = 2
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

static char *curline, *lineptr;
// Whether we should continue running the program
static int running = 1;
// Whether the parsing of the current line has been aborted
static int aborted = 0;

static Machine *m;

// Foward declarations
static void execute();
static int interpret();
static void call_native(int index);

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

static void nativeentry(char *name, int index)
{
    DictionaryEntry de = _create(name, TYPE_NATIVE, 2);
    writew(de.offset+ENTRY_FIELD_DATA, index);
}

static HeapItem readheap(int offset)
{
    HeapItem r;
    byte val = m->mem[offset];
    if (val == 0xff) {
        r.type = TYPE_STOP;
    } else if (val == 0xfe) {
        r.type = TYPE_NUM;
        r.arg = m->mem[offset+1];
        r.arg |= m->mem[offset+2] << 8;
        r.next = offset+3;
    } else {
        r.type = TYPE_WORD;
        r.arg = val;
        r.arg |= m->mem[offset+1] << 8;
        r.next = offset+2;
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
            m->mem[nextoffset++] = hi->arg & 0xff;
            m->mem[nextoffset++] = (hi->arg >> 8) & 0xff;
            break;
        case TYPE_WORD:
            m->mem[nextoffset++] = hi->arg & 0xff;
            m->mem[nextoffset++] = (hi->arg >> 8) & 0xff;
            break;
    }
    writew(HERE_ADDR, nextoffset);
}

static void push(uint16_t x)
{
    m->cpu.R1.wr.SP -= 2;
    writew(m->cpu.R1.wr.SP, x);
}

static uint16_t pop()
{
    if (m->cpu.R1.wr.SP == 0xffff) {
        aborted = 1;
        printf("Stack underflow\n");
        return 0;
    }
    uint16_t r = readw(m->cpu.R1.wr.SP);
    m->cpu.R1.wr.SP += 2;
    return r;
}

static char* readws()
{
    // skip extra whitespace.
    while ((*lineptr > 0) && (*lineptr <= ' ')) {
        lineptr++;
    }
    return lineptr;
}

static char* readword()
{
    char *s = readws();
    while (1) {
        if (*lineptr == '\0') {
            break;
        }
        if (*lineptr <= ' ') {
            *lineptr = 0;
            lineptr++;
            break;
        }
        lineptr++;
    }
    return s;
}

static void compile(HeapItem *hi, char *word)
{
    hi->type = TYPE_STOP;
    if (*word == '\0') { // EOL
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
        if ((endptr > word) && ((endptr == lineptr) || (endptr == lineptr-1))) {
            // whole word read, this means it was a number, we're good.
            hi->type = TYPE_NUM;
            hi->arg = num;
        } else {
            // not a number
            printf("What is %s?\n", word);
            aborted = 1;
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

static void error(char *msg)
{
    printf("%s\n", msg);
    aborted = 1;
    return;
}

// Not static because it's used in core_forth.c
void interpret_line(const char *line)
{
    char buf[0x200];
    char *oldline = curline;
    char *oldptr = lineptr;
    strcpy(buf, line);
    curline = buf;
    lineptr = curline;
    aborted = 0;
    while (interpret());
    curline = oldline;
    lineptr = oldptr;
}

// Callable
static void execute() {
    int offset = pop();
    if (aborted) return;
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
            call_native(de.arg);
            break;
        case TYPE_CELL:
            push(offset+ENTRY_FIELD_DATA);
            break;
    }
}

// Returns true if we still have words to interpret.
static int interpret() {
    char *word = readword();
    HeapItem hi;
    compile(&hi, word);
    return running && !aborted && execstep(&hi) != TYPE_STOP;
}

static void emit()
{
    putchar(pop() & 0xff);
}

static void bye()
{
    running = 0;
    aborted = 1;
}

static void dot()
{
    uint16_t num = pop();
    if (aborted) return;
    printf("%d", num);
}

static void dotx()
{
    uint16_t num = pop();
    if (aborted) return;
    printf("%02x", num);
}

static void define()
{
    char *word = readword();
    if (!*word) {
        printf("No define name");
        aborted = 1;
        return;
    }
    // we start writing the heap right after the entry's header
    DictionaryEntry de = _create(word, TYPE_COMPILED, 0);
    word = readword();
    HeapItem hi;
    while ((*word) && (*word != ';')) {
        compile(&hi, word);
        writeheap(&hi);
        if (aborted) {
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
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        error("Can't open file");
        return;
    }

    while ((read = getline(&line, &len, fp)) != -1) {
        interpret_line(line);
    }
    free(line);
    fclose(fp);
}

static void store()
{
    int addr = pop();
    int val = pop();
    writew(addr, val);
}

static void fetch()
{
    int addr = pop();
    push(readw(addr));
}

static void storec()
{
    int addr = pop();
    int val = pop();
    m->mem[addr] = val & 0xff;
}

static void fetchc()
{
    int addr = pop();
    push(m->mem[addr]);
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

static void here()
{
    push(HERE_ADDR);
}

static void current()
{
    push(CURRENT_ADDR);
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

static void plus()
{
    uint16_t n2 = pop();
    uint16_t n1 = pop();
    push(n1 + n2);
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
    emul_loop();
}

// Main loop
static Callable native_funcs[] = {
    emit, bye, dot, execute, define, loadf, store, fetch, storec, fetchc,
    forget, create, here, current, regr, regw, plus, minus, mult, div_,
    and_, or_, lshift, rshift, call, dotx};

static void call_native(int index)
{
    native_funcs[index]();
}

static void init_dict()
{
    int i = 0;
    // same order as in native_funcs
    nativeentry("emit", i++);
    nativeentry("bye", i++);
    nativeentry(".", i++);
    nativeentry("execute", i++);
    nativeentry(":", i++);
    nativeentry("loadf", i++);
    nativeentry("!", i++);
    nativeentry("@", i++);
    nativeentry("C!", i++);
    nativeentry("C@", i++);
    nativeentry("forget", i++);
    nativeentry("create", i++);
    nativeentry("here", i++);
    nativeentry("current", i++);
    nativeentry("regr", i++);
    nativeentry("regw", i++);
    nativeentry("+", i++);
    nativeentry("-", i++);
    nativeentry("*", i++);
    nativeentry("/", i++);
    nativeentry("and", i++);
    nativeentry("or", i++);
    nativeentry("lshift", i++);
    nativeentry("rshift", i++);
    nativeentry("call", i++);
    nativeentry(".x", i++);
}

int main(int argc, char *argv[])
{
    m = emul_init();
    m->cpu.R1.wr.SP = 0xffff;
    writew(HERE_ADDR, DICT_ADDR);
    init_dict();
    running = 1;
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
        aborted = 0;
        curline = fgets(inputbuf, 0x200, stdin);
        if (curline == NULL) break;
        lineptr = curline;
        while (interpret());
        if (!aborted) {
            printf(" ok\n");
        }
    }
    return 0;
}
