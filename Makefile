# Makefile for gzip/gunzip

CC = cc
CFLAGS = -O
PROGRAMS = gunzip gzip

all: $(PROGRAMS)

gunzip: gunzip.c
	$(CC) $(CFLAGS) -o gunzip gunzip.c;

gzip: gzip.c
	$(CC) $(CFLAGS) -o gzip gzip.c;

clean:
	rm -f $(PROGRAMS) *.o

.PHONY: all clean



