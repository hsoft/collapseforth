TARGET = forth
OBJS = main.o core_forth.o emul.o libz80/libz80.o
ASMWORDS = plus
ASMWORDSRC = ${ASMWORDS:%=words/%.fth}

.PHONY: all
all: $(TARGET) words-bin.h

words-bin.h: $(ASMWORDSRC) $(TARGET)
	./fth2c.sh $(ASMWORDSRC) > $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

libz80/libz80.o: libz80/z80.c
	$(MAKE) -C libz80/codegen opcodes
	$(CC) -ansi -g -c -o libz80/libz80.o libz80/z80.c

