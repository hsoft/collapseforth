TARGET = forth
OBJS = main.o emul.o libz80/libz80.o

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lreadline

libz80/libz80.o: libz80/z80.c
	$(MAKE) -C libz80/codegen opcodes
	$(CC) -ansi -g -c -o libz80/libz80.o libz80/z80.c

