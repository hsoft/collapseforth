/* Dictionary

hello           ( -- )      Print "Hello Forth!".
bye             ( -- )      Quits interpreter.
.               ( n -- )    Print n in decimal representation.
execute         ( hi -- )   Execute from heap starting at index hi.
; w t1 t2 ... ; ( -- )      Define word w and associate with code compiled from
                            tokens.
loadf fname     ( -- )      Reads file fname and interprets its contents as if
                            it was typed directly in the interpreter.
variable name   ( -- )      Creates a new word pointing to a cell.
!               ( x a -- )  store value x in cell at address (heap index) a.
@               ( a -- x )  fetch value x from cell at address (heap index) a.
?               ( -- )      Same as "@ ."

INSIDE Z80

regr r          ( -- n)     Put value of register r in n. r can be a single
                            register name (A, B, C) or a pair (BC, DE).
regw r          ( n -- )    Put n in register r.

*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <readline/readline.h>
#include "emul.h"

#define NAME_LEN 8
#define STACK_SIZE 500
#define DICT_SIZE 500
#define HEAP_SIZE 0x1000

typedef void (*Callable) ();

typedef enum {
    TYPE_WORD,
    TYPE_NUM,
    TYPE_CALLABLE,
    TYPE_CELL,
    TYPE_STOP
} HeapItemType;

typedef struct {
    int index;
    HeapItemType type;
    // When TYPE_WORD, arg is index in heap_indexes
    // When TYPE_NUM, arg is the parsed number
    // When TYPE_CALLABLE, arg is pointer to function
    // When TYPE_CELL, arg is the cell's value.
    uintptr_t arg;
} HeapItem;

typedef struct {
    char name[NAME_LEN];
    // index at which compiled code that correspond to word starts
    unsigned int heap_index;
} DictionaryEntry;

static DictionaryEntry dictionary[DICT_SIZE] = {0};
// Number of entries in dictionary
static int entrycount = 0;
static HeapItem heap[HEAP_SIZE] = {0};
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
        // We already have a new entry, let's update heap index
        de->heap_index = heapptr;
        return de;
    }
    // nope, new entry
    de = &dictionary[entrycount++];
    strncpy(de->name, name, NAME_LEN);
    de->heap_index = heapptr;
    return de;
}

static void heapput(Callable c)
{
    heap[heapptr].index = heapptr;
    heap[heapptr].type = TYPE_CALLABLE;
    heap[heapptr].arg = (uintptr_t)c;
    heapptr++;
}

static void heapstop()
{
    heap[heapptr++].type = TYPE_STOP;
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
        hi->arg = de->heap_index;
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
        case TYPE_CALLABLE:
            ((Callable)hi->arg)();
            break;
        case TYPE_NUM:
            push((int)hi->arg);
            break;
        case TYPE_WORD:
            push((int)hi->arg);
            execute();
            break;
        case TYPE_CELL:
            // Executing a cell pushes the cell's *address* onto the stack
            // rather than its value.
            push(hi->index);
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
    HeapItem hi = heap[index++];
    while (execstep(&hi) != TYPE_STOP) {
        hi = heap[index++];
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
    word = readword();
    while ((*word) && (*word != ';')) {
        HeapItem *hi = &heap[heapptr];
        hi->index = heapptr;
        heapptr++;
        compile(hi, word);
        if (aborted) {
            // Something went wrong, let's rollback on new entry
            entrycount--;
            heapptr = de->heap_index;
            return;
        }
        word = readword();
    }
    heapstop();
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
    HeapItem *hi = &heap[heapptr++];
    hi->index = de->heap_index;
    hi->type = TYPE_CELL;
    hi->arg = 0;
    heapstop();
}

static void store()
{
    int addr = pop();
    int val = pop();
    HeapItem *hi = &heap[addr];
    if (hi->type != TYPE_CELL) {
        error("Not a cell address");
        return;
    }
    hi->arg = val;
}

static void fetch()
{
    int addr = pop();
    HeapItem *hi = &heap[addr];
    if (hi->type != TYPE_CELL) {
        error("Not a cell address");
        return;
    }
    push(hi->arg);
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
    newentry("hello");
    heapput(hello);
    heapstop();
    newentry("bye");
    heapput(bye);
    heapstop();
    newentry(".");
    heapput(dot);
    heapstop();
    newentry("execute");
    heapput(execute);
    heapstop();
    newentry(":");
    heapput(define);
    heapstop();
    newentry("loadf");
    heapput(loadf);
    heapstop();
    newentry("variable");
    heapput(variable);
    heapstop();
    newentry("!");
    heapput(store);
    heapstop();
    newentry("@");
    heapput(fetch);
    heapstop();
    newentry("?");
    heapput(fetch);
    heapput(dot);
    heapstop();
    // Inside z80
    newentry("regr");
    heapput(regr);
    heapstop();
    newentry("regw");
    heapput(regw);
    heapstop();

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
