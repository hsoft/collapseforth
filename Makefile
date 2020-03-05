TARGET = forth
OBJS = main.o core_forth.o emul.o libz80/libz80.o
ASMWORDS = plus swap emit dup here current
ASMWORDSRC = ${ASMWORDS:%=words/%.fth}

.PHONY: all
all: $(TARGET)

.PHONY: bootstrap
bootstrap: | $(ASMWORDSRC)
	./fth2c.sh $(ASMWORDSRC) > words-bin.h

$(TARGET): $(OBJS) words-bin.h
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

libz80/libz80.o: libz80/z80.c
	$(MAKE) -C libz80/codegen opcodes
	$(CC) -ansi -g -c -o libz80/libz80.o libz80/z80.c

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
