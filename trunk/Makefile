CC=gcc
CFLAGS=-O2
LIBS=-lm -lcurl -lcrypto -lconfig

mlb = mlb

all: mlb

mlb:mlb.c utils.c output.c $(LIBS)

clean:
	rm -f *.o mlb

