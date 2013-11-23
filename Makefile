CC=gcc
CFLAGS=
LDFLAGS=-lpthread -lusb-1.0

all: lightmanager

lightmanager: lightmanager.c
	$(CC) lightmanager.c $(CFLAGS) $(LDFLAGS) -olightmanager

clean:
	rm -f *.o *~ *.so *.out lightmanager

install:
	cp ./lightmanager /usr/local/bin/
