#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <readline/readline.h>
#include "emul.h"

#define NAME_LEN 8
#define STACK_SIZE 500
#define DICT_SIZE 500

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
    // Entry links to native code. arg is function pointer.
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
    char name[NAME_LEN];
    EntryType type;
    int index;
    long arg; // see EntryType comments.
} DictionaryEntry;

static DictionaryEntry dictionary[DICT_SIZE] = {0};
// Number of entries in dictionary
static int entrycount = 0;
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

// Internal

static DictionaryEntry* find(char *word)
{
    for (int i=0; i<entrycount; i++) {
        if (strncmp(word, dictionary[i].name, NAME_LEN) == 0) {
            return &dictionary[i];
        }
    }
    return NULL;
}

// Creates and returns a new dictionary entry. If "name" already exists, return
// this entry with a heap_index pointing to the end of the heap.
static DictionaryEntry* newentry(char *name)
{
    // Maybe entry already exists?
    DictionaryEntry *de = find(name);
    if (de != NULL) {
        return de;
    }
    // nope, new entry
    de = &dictionary[entrycount++];
    de->index = entrycount-1;
    strncpy(de->name, name, NAME_LEN);
    de->arg = 0;
    return de;
}

static void nativeentry(char *name, Callable c)
{
    DictionaryEntry *de = newentry(name);
    de->type = TYPE_NATIVE;
    de->arg = (long)c;
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

static void writeheap(HeapItem *hi, int offset)
{
    switch (hi->type) {
        case TYPE_STOP:
            m->mem[HEAP_ADDR+offset] = 0xff;
            break;
        case TYPE_NUM:
            m->mem[HEAP_ADDR+offset] = 0xfe;
            m->mem[HEAP_ADDR+offset+1] = hi->arg & 0xff;
            m->mem[HEAP_ADDR+offset+2] = (hi->arg >> 8) & 0xff;
            hi->next = offset + 3;
            break;
        case TYPE_WORD:
            m->mem[HEAP_ADDR+offset] = hi->arg & 0xff;
            m->mem[HEAP_ADDR+offset+1] = (hi->arg >> 8) & 0xff;
            hi->next = offset + 2;
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
    DictionaryEntry *de = find(word);
    if (de != NULL) {
        hi->type = TYPE_WORD;
        hi->arg = de->index;
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
    int index = pop();
    if (aborted) return;
    DictionaryEntry *de = &dictionary[index];
    switch (de->type) {
        case TYPE_COMPILED:
            index = de->arg;
            HeapItem hi = readheap(index);
            while (execstep(&hi) != TYPE_STOP) {
                hi = readheap(hi.next);
            }
            break;
        case TYPE_NATIVE:
            ((Callable)de->arg)();
            break;
        case TYPE_CELL:
            push(index);
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
    DictionaryEntry *de = newentry(word);
    de->type = TYPE_COMPILED;
    word = readword();
    int oldptr = heapptr; // in case we abort
    HeapItem hi;
    while ((*word) && (*word != ';')) {
        compile(&hi, word);
        writeheap(&hi, heapptr);
        heapptr = hi.next;
        if (aborted) {
            // Something went wrong, let's rollback on new entry
            entrycount--;
            heapptr = oldptr;
            return;
        }
        word = readword();
    }
    hi.type = TYPE_STOP;
    writeheap(&hi, heapptr);
    heapptr = hi.next;
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
    DictionaryEntry *de = newentry(word);
    de->type = TYPE_CELL;
}

static void store()
{
    int addr = pop();
    int val = pop();
    DictionaryEntry *de = &dictionary[addr];
    if (de->type != TYPE_CELL) {
        error("Not a cell address");
        return;
    }
    de->arg = val;
}

static void fetch()
{
    int addr = pop();
    DictionaryEntry *de = &dictionary[addr];
    if (de->type != TYPE_CELL) {
        error("Not a cell address");
        return;
    }
    push(de->arg);
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
static void init_dict()
{
    nativeentry("hello", hello);
    nativeentry("bye", bye);
    nativeentry(".", dot);
    nativeentry("execute", execute);
    nativeentry(":", define);
    nativeentry("loadf", loadf);
    nativeentry("variable", variable);
    nativeentry("!", store);
    nativeentry("@", fetch);
    nativeentry("regr", regr);
    nativeentry("regw", regw);
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
