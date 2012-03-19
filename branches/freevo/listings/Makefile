CC=gcc
CFLAGS=-O2 -march=native -I/usr/include/libxml2
LIBS=-lm

mlblist = mlblist
target = mlblist

sources = mlblist.c

all: mlblist

mlblist: mlblist.c $(LIBS) -lxml2


clean:
	rm -f *.o mlblist
