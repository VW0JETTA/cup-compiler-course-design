CC      = gcc
CFLAGS  = -std=c11 -Wall
FLEX    = flex
BISON   = bison

TARGET  = parser
OBJS    = main.c tree.c syntax.tab.c lex.yy.c

.PHONY: all clean test

all: $(TARGET)

# Bison：生成语法分析器 + 头文件（含 Token 定义，供 Flex 引用）
syntax.tab.c syntax.tab.h: syntax.y tree.h
	$(BISON) -d -o syntax.tab.c syntax.y

# Flex：生成词法分析器，依赖 Bison 头文件
lex.yy.c: lexical.l syntax.tab.h tree.h
	$(FLEX) -o lex.yy.c lexical.l

# 链接为可执行文件
$(TARGET): $(OBJS) tree.h
	$(CC) $(CFLAGS) main.c tree.c syntax.tab.c lex.yy.c -lfl -o $(TARGET)

clean:
	rm -f $(TARGET) lex.yy.c syntax.tab.c syntax.tab.h syntax.output

# 依次运行所有测试用例
test: $(TARGET)
	@for f in test*.cmm; do \
		echo "===== $$f ====="; \
		./$(TARGET) $$f; \
		echo; \
	done
