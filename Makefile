# compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra   # turn on warnings

# default target
all: server client

# build server
server: src/server.c src/common.h
	$(CC) $(CFLAGS) src/server.c -o src/server

# build client
client: src/client.c src/common.h
	$(CC) $(CFLAGS) src/client.c -o src/client -lpthread

# remove compiled binaries
clean:
	rm -f src/server src/client
