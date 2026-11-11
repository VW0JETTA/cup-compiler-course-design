CC      = gcc
CFLAGS  = -std=c11 -Wall
FLEX    = flex
BISON   = bison

TARGET  = parser
OBJS    = main.c tree.c semantic.c syntax.tab.c lex.yy.c

.PHONY: all clean test

all: $(TARGET)

syntax.tab.c syntax.tab.h: syntax.y tree.h
	$(BISON) -d -o syntax.tab.c syntax.y

lex.yy.c: lexical.l syntax.tab.h tree.h
	$(FLEX) -o lex.yy.c lexical.l

$(TARGET): $(OBJS) tree.h semantic.h
	$(CC) $(CFLAGS) main.c tree.c semantic.c syntax.tab.c lex.yy.c -lfl -o $(TARGET)

clean:
	rm -f $(TARGET) lex.yy.c syntax.tab.c syntax.tab.h syntax.output

test: $(TARGET)
	@for f in test*.cmm; do \
		echo "===== $$f ====="; \
		./$(TARGET) $$f; \
		echo; \
	done
