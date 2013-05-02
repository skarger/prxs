#
#
# makefile for webserver
#

CC = gcc -Wall

prxs: prxs.o prxs.h socklib.o socklib.h table.o flexstr.o flexstr.h util.h
	$(CC) -o prxs prxs.o socklib.o

table.o: table.c
	$(CC) -c table.c

flexstr.o: flexstr.c util.c
	$(CC) -c flexstr.c util.c

clean:
	rm -f prxs.o socklib.o table.o flexstr.o util.o
