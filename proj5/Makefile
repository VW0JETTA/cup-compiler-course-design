CC     = gcc
CFLAGS = -std=gnu11 -Wall -Wno-unused-function

TARGET = cc
OBJS   = main.o ir_reader.o ir_writer.o opt_passes.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o: main.c ir_opt.h
	$(CC) $(CFLAGS) -c main.c

ir_reader.o: ir_reader.c ir_opt.h
	$(CC) $(CFLAGS) -c ir_reader.c

ir_writer.o: ir_writer.c ir_opt.h
	$(CC) $(CFLAGS) -c ir_writer.c

opt_passes.o: opt_passes.c ir_opt.h
	$(CC) $(CFLAGS) -c opt_passes.c

clean:
	rm -f $(TARGET) $(OBJS)
