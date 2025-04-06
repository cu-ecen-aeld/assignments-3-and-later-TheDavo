CROSS_COMPILE=
CC=$(CROSS_COMPILE)gcc
CFLAGS= -Wall -Werror
DEPS=

SRCS=aesdsocket.c
OBJS=$(SRCS:.c=.o)

# executable file
MAIN=aesdsocket

all: $(SRCS)
	$(CC) $(CFLAGS) $^ -o $(MAIN)

$(MAIN): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(OBJS) $(MAIN)
