CC=gcc
CFLAGS=-g -Wall -Werror


main: main.c
	$(CC) $(CFLAGS) -o main main.c -ljson-c -lcurl
