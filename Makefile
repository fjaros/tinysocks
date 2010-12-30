EXEC = ts
CC = gcc
CFLAGS = -c -Wall

$(EXEC)	:	tinysocks.o
	$(CC) -o $(EXEC) tinysocks.o
	
tinysocks.o:	tinysocks.c
	$(CC) $(CFLAGS) tinysocks.c
	
clean	:
	rm -f *.o *~ ts
