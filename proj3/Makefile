CC      = gcc
FLEX    = flex
BISON   = bison
CFLAGS  = -std=gnu11 -Wall -Wno-unused-function

TARGET  = cc
OBJS    = main.o syntax.tab.o lex.yy.o ast.o ir.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lfl

syntax.tab.c syntax.tab.h: syntax.y
	$(BISON) -d syntax.y

lex.yy.c: lexical.l syntax.tab.h
	$(FLEX) -o lex.yy.c lexical.l

main.o: main.c ast.h ir.h
	$(CC) $(CFLAGS) -c main.c

ast.o: ast.c ast.h
	$(CC) $(CFLAGS) -c ast.c

ir.o: ir.c ir.h ast.h
	$(CC) $(CFLAGS) -c ir.c

syntax.tab.o: syntax.tab.c ast.h
	$(CC) $(CFLAGS) -c syntax.tab.c

lex.yy.o: lex.yy.c ast.h syntax.tab.h
	$(CC) $(CFLAGS) -c lex.yy.c

clean:
	rm -f $(TARGET) $(OBJS) lex.yy.c syntax.tab.c syntax.tab.h
