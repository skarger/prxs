# makefile for proxy server

CC = gcc -Wall -pthread

prxs: prxs.o prxs.h socklib.o socklib.h flexstr.o flexstr.h util.o util.h
	$(CC) -o prxs prxs.o socklib.o util.o flexstr.o

socklib.o: socklib.c socklib.h
	$(CC) -c socklib.c

flexstr.o: flexstr.c flexstr.h util.c
	$(CC) -c flexstr.c util.c

util.o: util.c util.h
	$(CC) -c util.c

clean:
	rm -f prxs.o socklib.o table.o flexstr.o util.o
