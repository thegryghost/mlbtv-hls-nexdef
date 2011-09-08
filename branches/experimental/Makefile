CC=gcc
CFLAGS=-O2
LIBS=-lm -lcrypto -lpthread -lconfig -lcurl -lhttp_fetcher

mlb = mlb

all: mlb

mlb:
	 $(CC) $(CFLAGS) mlb.c utils.c output.c $(LIBS) -o mlbhls

clean:
	rm -f *.o mlbhls

