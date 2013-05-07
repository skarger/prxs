#
#
# makefile for webserver
#

CC = gcc -Wall -g -pthread

prxs: prxs.o prxs.h socklib.o socklib.h table.o flexstr.o flexstr.h util.o util.h
	$(CC) -o prxs prxs.o socklib.o util.o flexstr.o

table.o: table.c
	$(CC) -c table.c

flexstr.o: flexstr.c util.c
	$(CC) -c flexstr.c util.c

util.o: util.c util.h
	$(CC) -c util.c

clean:
	rm -f prxs.o socklib.o table.o flexstr.o util.o
