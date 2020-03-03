/* Dictionary

hello           ( -- )      Print "Hello Forth!".
bye             ( -- )      Quits interpreter.
.               ( n -- )    Print n in decimal representation.
execute         ( hi -- )   Execute from heap starting at index hi.
; w t1 t2 ... ; ( -- )      Define word w and associate with code compiled from
                            tokens.

*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <readline/readline.h>

#define NAME_LEN 8
#define STACK_SIZE 500
#define DICT_SIZE 500
#define HEAP_SIZE 0x1000

typedef void (*Callable) ();

typedef enum {
    TYPE_WORD,
    TYPE_NUM,
    TYPE_CALLABLE,
    TYPE_STOP
} HeapItemType;

typedef struct {
    HeapItemType type;
    // When TYPE_WORD, arg is index in heap_indexes
    // When TYPE_NUM, arg is the parsed number
    // When TYPE_CALLABLE, arg is pointer to function
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

// Foward declarations
static void execute();

// Internal
static DictionaryEntry* newentry(char *name)
{
    DictionaryEntry *de = &dictionary[entrycount++];
    strncpy(de->name, name, NAME_LEN);;
    de->heap_index = heapptr;
    return de;
}

static void heapput(Callable c) {
    if (c != NULL) {
        heap[heapptr].type = TYPE_CALLABLE;
        heap[heapptr].arg = (uintptr_t)c;
    } else {
        heap[heapptr].type = TYPE_STOP;
    }
    heapptr++;
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

static DictionaryEntry* find(char *word)
{
    for (int i=0; i<entrycount; i++) {
        if (strncmp(word, dictionary[i].name, NAME_LEN) == 0) {
            return &dictionary[i];
        }
    }
    return NULL;
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
    }
    return hi->type;
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
    // Maybe entry already exists?
    DictionaryEntry *de = find(word);
    if (de != NULL) {
        // We already have a new entry, let's update heap index
        de->heap_index = heapptr;
    } else {
        // nope, new entry
        de = newentry(word);
    }
    word = readword();
    while ((*word) && (*word != ';')) {
        compile(&heap[heapptr++], word);
        if (aborted) {
            // Something went wrong, let's rollback on new entry
            entrycount--;
            heapptr = de->heap_index;
            return;
        }
        word = readword();
    }
    heap[heapptr].type = TYPE_STOP;
    heapptr++;
}

static void init_dict()
{
    newentry("hello");
    heapput(hello);
    heapput(NULL);
    newentry("bye");
    heapput(bye);
    heapput(NULL);
    newentry(".");
    heapput(dot);
    heapput(NULL);
    newentry("execute");
    heapput(execute);
    heapput(NULL);
    newentry(":");
    heapput(define);
    heapput(NULL);
}

int main()
{
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
