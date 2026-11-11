CC = gcc
CFLAGS = -Wall -g
TARGET = cc

SRCS = main.c ast.c ir.c codegen.c syntax.tab.c lex.yy.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

syntax.tab.c syntax.tab.h: syntax.y
	bison -d syntax.y

lex.yy.c: lexical.l syntax.tab.h
	flex lexical.l

$(TARGET): syntax.tab.c lex.yy.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) *.o syntax.tab.c syntax.tab.h lex.yy.c

.PHONY: all clean
