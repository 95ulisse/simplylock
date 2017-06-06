CC = gcc
CFLAGS += -std=c99 -Wall -pedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS += -lpam -lpam_misc

OBJECTS = vt.o \
		  options.o \
		  auth.o \
		  lock.o \
		  main.o

%.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

default: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(LDFLAGS) -o simplylock

clean:
	rm -f *.o simplylock || true

install: default
	cp ./simplylock /bin/simplylock
	chown root:root /bin/simplylock
	chmod 4755 /bin/simplylock
