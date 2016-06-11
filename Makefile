CC = gcc
CFLAGS += -std=c99 -Wall -pedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpam -lpam_misc

OBJECTS = vt.o \
		  options.o \
		  auth.o \
		  main.o

%.o: %.cr
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

default: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o simplylock $(OBJECTS)

clean:
	rm *.o simplylock
