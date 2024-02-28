CC = gcc
CFLAGS = -O2 -Wall -I .
TARGET = hostinfo 

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: hostinfo

hostinfo: hostinfo.c csapp.o
	$(CC) $(CFLAGS) -o hostinfo hostinfo.c csapp.o $(LIB)

csapp.o: ../csapp.c
	$(CC) $(CFLAGS) -c ../csapp.c
	
clean:
	rm -f *.o
	rm -f $(TARGET)