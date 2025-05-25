CROSS_COMPILE ?=
CC ?=$(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
DEPS ?=
LDFLAGS ?=-lpthread

SRCS=aesdsocket.c
OBJS=$(SRCS:.c=.o)

# executable file
MAIN=aesdsocket

all: $(SRCS)
	$(CC) $(CFLAGS) $^ -o $(MAIN) $(LDFLAGS)

$(MAIN): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(MAIN)
