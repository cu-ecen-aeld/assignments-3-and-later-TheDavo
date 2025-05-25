CROSS_COMPILE=
CC=$(CROSS_COMPILE)gcc
CFLAGS= -Wall -Werror -g
DEPS=
LIBS=-lpthread

SRCS=aesdsocket.c
OBJS=$(SRCS:.c=.o)

# executable file
MAIN=aesdsocket

all: $(SRCS)
	$(CC) $(CFLAGS) $^ -o $(MAIN) $(LIBS)

$(MAIN): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f $(OBJS) $(MAIN)
