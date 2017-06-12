CFLAGS=-std=gnu99
LIBS=-pthread

all: server

server: server.c communication.h
	gcc $(CFLAGS) $(LIBS) -o server server.c

clean:
	rm -f server

.PHONY: all server clean
