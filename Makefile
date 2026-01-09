# Makefile for sudo

CC = cc
CFLAGS = -O
PROGRAM = gunzip
SRCS = gunzip.c

all: $(PROGRAM)

$(PROGRAM): $(SRCS)
	$(CC) $(CFLAGS) -o $(PROGRAM) $(SRCS);

clean:
	rm -f $(PROGRAM) *.o

.PHONY: all clean



