#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <readline/readline.h>
#include "emul.h"

#define NAME_LEN 8
#define STACK_SIZE 500
/* About dictionary

Structure

- 1b EntryType
- 8b name
- 2b prev entry offset, 0 for none
- 2b arg

*/
#define DICT_ADDR 0x3000
#define DICT_SIZE 0x1000

/* About heap
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

#define HEAP_ADDR 0x4000
#define HEAP_SIZE 0x1000

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
    long arg; // see EntryType comments.
} DictionaryEntry;

// Offset of last entry in dictionary
static uint16_t lastentryoffset = 0;
static uint16_t nextoffset = DICT_ADDR;
static int heapptr = 0;
static int stack[STACK_SIZE] = {0};
static int stackptr = 0;
static char *curline, *lineptr;
// Whether we should continue running the program
static int running = 1;
// Whether the parsing of the current line has been aborted
static int aborted = 0;

static Machine *m;

// Foward declarations
static void execute();
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
    de->type = (EntryType)m->mem[offset];
    de->name = &m->mem[offset+1];
    de->prev = readw(offset+9);
    de->arg = readw(offset+11);
}

static void writeentry(DictionaryEntry *de)
{
    int offset = de->offset;
    m->mem[offset] = de->type;
    strncpy(&m->mem[offset+1], de->name, NAME_LEN);
    writew(offset+9, de->prev);
    writew(offset+11, de->arg);
    if (offset == nextoffset) {
        // We're writing at the end of the dict
        lastentryoffset = offset;
        nextoffset = offset + 13;
    }
}

static DictionaryEntry find(char *word)
{
    DictionaryEntry de;
    // We purposefully omit the "no entry" case: never happens.
    de.prev = lastentryoffset;
    de.offset = nextoffset;
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

// Creates and returns a new dictionary entry.
static DictionaryEntry newentry(char *name, EntryType type)
{
    DictionaryEntry de;
    de.type = type;
    de.name = name; // will be copied in writeentry
    de.arg = 0;
    de.prev = lastentryoffset;
    de.offset = nextoffset;
    return de;
}

static void nativeentry(char *name, int index)
{
    DictionaryEntry de = newentry(name, TYPE_NATIVE);
    de.arg = index;
    writeentry(&de);
}

static HeapItem readheap(int offset)
{
    HeapItem r;
    byte val = m->mem[HEAP_ADDR+offset];
    if (val == 0xff) {
        r.type = TYPE_STOP;
    } else if (val == 0xfe) {
        r.type = TYPE_NUM;
        r.arg = m->mem[HEAP_ADDR+offset+1];
        r.arg |= m->mem[HEAP_ADDR+offset+2] << 8;
        r.next = offset+3;
    } else {
        r.type = TYPE_WORD;
        r.arg = val;
        r.arg |= m->mem[HEAP_ADDR+offset+1] << 8;
        r.next = offset+2;
    }
    return r;
}

// write to heapptr and updates it
static void writeheap(HeapItem *hi)
{
    switch (hi->type) {
        case TYPE_STOP:
            m->mem[HEAP_ADDR+heapptr++] = 0xff;
            heapptr++;
            break;
        case TYPE_NUM:
            m->mem[HEAP_ADDR+heapptr++] = 0xfe;
            m->mem[HEAP_ADDR+heapptr++] = hi->arg & 0xff;
            m->mem[HEAP_ADDR+heapptr++] = (hi->arg >> 8) & 0xff;
            break;
        case TYPE_WORD:
            m->mem[HEAP_ADDR+heapptr++] = hi->arg & 0xff;
            m->mem[HEAP_ADDR+heapptr++] = (hi->arg >> 8) & 0xff;
            break;
    }
}

static void push(int x)
{
    if (stackptr == STACK_SIZE) {
        aborted = 1;
        printf("Stack overflow\n");
        return;
    }
    stack[stackptr++] = x;
}

static int pop()
{
    if (!stackptr) {
        aborted = 1;
        printf("Stack underflow\n");
        return -1;
    }
    return stack[--stackptr];
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
        int num = strtol(word, &endptr, 10);
        if ((endptr == lineptr) || (endptr == lineptr-1)) {
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

// Callable
static void execute() {
    int offset = pop();
    if (aborted) return;
    DictionaryEntry de;
    readentry(&de, offset);
    switch (de.type) {
        case TYPE_COMPILED:
            offset = de.arg;
            HeapItem hi = readheap(offset);
            while (execstep(&hi) != TYPE_STOP) {
                hi = readheap(hi.next);
            }
            break;
        case TYPE_NATIVE:
            call_native(de.arg);
            break;
        case TYPE_CELL:
            push(offset+11); // +11 == arg field
            break;
    }
}

static void interpret() {
    char *word = readword();
    HeapItem hi;
    compile(&hi, word);
    if (execstep(&hi) == TYPE_STOP) {
        aborted = 1;
    }
}

static void hello()
{
    printf("Hello Forth!\n");
}

static void bye()
{
    running = 0;
    aborted = 1;
}

static void dot()
{
    int num = pop();
    if (aborted) return;
    printf("%d\n", num);
}

static void define()
{
    char *word = readword();
    if (!*word) {
        printf("No define name");
        aborted = 1;
        return;
    }
    DictionaryEntry de = newentry(word, TYPE_COMPILED);
    de.arg = heapptr;
    writeentry(&de);
    word = readword();
    HeapItem hi;
    while ((*word) && (*word != ';')) {
        compile(&hi, word);
        writeheap(&hi);
        if (aborted) {
            // Something went wrong, let's rollback on new entry
            heapptr = de.arg;
            return;
        }
        word = readword();
    }
    hi.type = TYPE_STOP;
    writeheap(&hi);
}

static void loadf()
{
    char *oldline = curline;
    char *oldptr = lineptr;
    char *fname = readword();
    ssize_t read;
    ssize_t len = 0;

    if (!fname) {
        error("Missing filename");
        return;
    }
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        error("Can't open file");
        return;
    }

    curline = NULL;
    while ((read = getline(&curline, &len, fp)) != -1) {
        lineptr = curline;
        while (running && (!aborted)) interpret();
    }
    free(curline);
    fclose(fp);
    // restore old curline/lineptr
    curline = oldline;
    lineptr = oldptr;
}

static void variable()
{
    char *word = readword();
    if (!*word) {
        error("No variable name");
        return;
    }
    DictionaryEntry de = newentry(word, TYPE_CELL);
    writeentry(&de);
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
    if (de.next == 0) {
        // We're the last of the chain
        lastentryoffset = de.prev;
        nextoffset = de.offset;
    } else {
        // not the last, we have to hook stuff.
        uint16_t newprev = de.prev;
        readentry(&de, de.next);
        de.prev = newprev;
        writeentry(&de);
    }
}
// Inside Z80

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

// Main loop
static Callable native_funcs[] = {
    hello, bye, dot, execute, define, loadf, variable, store, fetch, forget,
    regr, regw};

static void call_native(int index)
{
    native_funcs[index]();
}

static void init_dict()
{
    nativeentry("hello", 0);
    nativeentry("bye", 1);
    nativeentry(".", 2);
    nativeentry("execute", 3);
    nativeentry(":", 4);
    nativeentry("loadf", 5);
    nativeentry("variable", 6);
    nativeentry("!", 7);
    nativeentry("@", 8);
    nativeentry("forget", 9);
    nativeentry("regr", 10);
    nativeentry("regw", 11);
}

int main()
{
    m = emul_init();
    init_dict();
    running = 1;
    while (running) {
        aborted = 0;
        curline = readline("? ");
        if (curline == NULL) break;
        lineptr = curline;
        while (running && (!aborted)) interpret();
        free(curline);
    }
    return 0;
}
