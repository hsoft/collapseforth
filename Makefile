TARGET = forth
OBJS = main.o core_forth.o emul.o libz80/libz80.o
ASMPARTS = plus swap emit dup here current storec fetchc store fetch over rot
ASMPARTSSRC = ${ASMPARTS:%=z80/%.fth}

.PHONY: all
all: $(TARGET)

.PHONY: bootstrap
bootstrap: | $(ASMPARTSSRC)
	./fth2c.sh $(ASMPARTSSRC) > z80-bin.h

$(TARGET): $(OBJS) z80-bin.h
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

libz80/libz80.o: libz80/z80.c
	$(MAKE) -C libz80/codegen opcodes
	$(CC) -ansi -g -c -o libz80/libz80.o libz80/z80.c

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
