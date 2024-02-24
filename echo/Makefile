CC = gcc
CFLAGS = -O2 -Wall -I .
TARGET = echoclient echoserver

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: echoclient echoserver

echoclient: echoclient.c csapp.o
	$(CC) $(CFLAGS) -o echoclient echoclient.c csapp.o $(LIB)

echoserver: echoserveri.c csapp.o echo.o
	$(CC) $(CFLAGS) -o echoserver echoserveri.c csapp.o echo.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

echo.o: echo.c
	$(CC) $(CFLAGS) -c echo.c

clean:
	rm -f *.o
	rm -f $(TARGET)