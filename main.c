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
    uintptr_t arg;
} HeapItem;

static char names[DICT_SIZE][NAME_LEN] = {0};
static unsigned int heap_indexes[DICT_SIZE] = {0};
static int namecount = 0;
static HeapItem heap[HEAP_SIZE] = {0};
static int heapptr = 0;
static int stack[STACK_SIZE] = {0};
static int stackptr = 0;
static char *curline, *lineptr;
// Whether we should continue running the program
static int running = 1;
// Wether the parsing of the current line has been aborted
static int aborted = 0;

// Foward declarations
static void execute();

// Internal
static int newname(char *name)
{
    strncpy(names[namecount], name, NAME_LEN);;
    heap_indexes[namecount] = heapptr;
    namecount++;
    return namecount-1;
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

static char* readword()
{
    char *s = lineptr;
    while (1) {
        if (*lineptr == '\0') {
            return s;
        }
        if (*lineptr <= ' ') {
            *lineptr = 0;
            lineptr++;
            return s;
        }
        lineptr++;
    }
}

// Returns a heap index
static int find(char *word)
{
    for (int i=0; i<namecount; i++) {
        if (strncmp(word, names[i], NAME_LEN) == 0) {
            return heap_indexes[i];
        }
    }
    return -1;
}

static void compile(HeapItem *hi, char *word)
{
    hi->type = TYPE_STOP;
    if (*word == '\0') { // EOL
        return;
    }
    int index = find(word);
    if (index >= 0) {
        hi->type = TYPE_WORD;
        hi->arg = index;
    } else {
        // not in dict, maybe a number?
        char *endptr;
        int num = strtol(word, &endptr, 10);
        if (endptr == lineptr) {
            // whole word read, this means it was a number, we're good.
            hi->type = TYPE_NUM;
            hi->arg = num;
        } else {
            // not a number
            printf("What is %s?\n", word);
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

static void u()
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
    newname(word);
    word = readword();
    while ((*word) && (*word != ';')) {
        compile(&heap[heapptr++], word);
        word = readword();
    }
    heap[heapptr].type = TYPE_STOP;
    heapptr++;
}

static void init_dict()
{
    newname("hello");
    heapput(hello);
    heapput(NULL);
    newname("bye");
    heapput(bye);
    heapput(NULL);
    newname("u");
    heapput(u);
    heapput(NULL);
    newname("execute");
    heapput(execute);
    heapput(NULL);
    newname(":");
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
