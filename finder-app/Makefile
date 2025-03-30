# learned from makefiletutorial.com
CC=aarch64-none-linux-gnu-gcc

all: writer
.PHONY: all clean
	
writer: writer.o
	$(CC) writer.o -o writer

writer.o: writer.c
	$(CC) -c writer.c -o writer.o

clean:
	rm -f ./writer ./writer.o
