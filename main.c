#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>

#define STACK_SIZE 500
#define DICT_SIZE 500
#define HEAP_SIZE 0x1000

typedef void (*Callable) ();
static char *names[DICT_SIZE] = {0};
static unsigned int heap_indexes[DICT_SIZE] = {0};
static Callable heap[HEAP_SIZE] = {0};
static int heapptr = 0;
static int stack[STACK_SIZE] = {0};
static int stackptr = 0;
static char *curline, *lineptr;
// Whether we should continue running the program
static int running = 1;
// Wether the parsing of the current line has been aborted
static int aborted = 0;

// Internal
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

static void heapput(Callable c) {
    heap[heapptr++] = c;
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
    for (int i=0; i<DICT_SIZE; i++) {
        if (names[i] == NULL) break;
        if (strcmp(word, names[i]) == 0) {
            return heap_indexes[i];
        }
    }
    return -1;
}

// Callable
static void execute() {
    int index = pop();
    if (aborted) return;
    Callable c = heap[index++];
    while (c != NULL) {
        c();
        c = heap[index++];
    }
}

static void interpret() {
    char *word = readword();
    if (*word == '\0') { // EOL
        aborted = 1;
        return;
    }
    int index = find(word);
    if (index >= 0) {
        push(index);
        execute();
    } else {
        // not in dict, maybe a number?
        char *endptr;
        int num = strtol(word, &endptr, 10);
        if (endptr == lineptr) {
            // whole word read, this means it was a number, we're good.
            push(num);
        } else {
            // not a number
            aborted = 1;
            printf("What is %s?\n", word);
        }
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

static void init_dict()
{
    names[0] = "hello";
    heap_indexes[0] = heapptr;
    heapput(hello);
    heapput(NULL);
    names[1] = "bye";
    heap_indexes[1] = heapptr;
    heapput(bye);
    heapput(NULL);
    names[2] = "u";
    heap_indexes[2] = heapptr;
    heapput(u);
    heapput(NULL);
    names[3] = "execute";
    heap_indexes[3] = heapptr;
    heapput(execute);
    heapput(NULL);
    names[4] = NULL;
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
