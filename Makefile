CC = gcc
CFLAGS += -std=c99 -Wall -pedantic -D_POSIX_C_SOURCE=200809L $(shell MagickWand-config --cflags)
INCLUDES = -I./src
LDFLAGS += -lpam -lpam_misc $(shell MagickWand-config --ldflags --libs)

SRC = src
OUT = out

OBJECTS = $(OUT)/vt.o \
		  $(OUT)/bg.o \
		  $(OUT)/options.o \
		  $(OUT)/auth.o \
		  $(OUT)/lock.o \
		  $(OUT)/main.o

$(OUT)/%.o: $(SRC)/%.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

default: $(OBJECTS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(OUT)/simplylock $(OBJECTS) $(LDFLAGS)

clean:
	rm -rf $(OUT)

install: default
	cp $(OUT)/simplylock /usr/bin/simplylock
	chown root:root /usr/bin/simplylock
	chmod 4755 /usr/bin/simplylock
