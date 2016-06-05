CC = gcc
CFLAGS += -std=c99 -Wall -pedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS =

OBJECTS = vt.o \
		  main.o

%.o: %.cr
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

default: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o simplylock $(OBJECTS)

clean:
	rm *.o simplylock
