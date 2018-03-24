# Development mode (make clean before recompiling with debug)
#CFLAGS = -DDEBUG -g -Wall
CFLAGS = -O2 -Wall

TARGET = nocrazydots
LIBS = -lm -lasound
CC = gcc
PREFIX  = /usr
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
INSTALL = /usr/bin/install
INSTALLDATA = /usr/bin/install -m 644

.PHONY: default all clean install

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o
	#-rm -f $(TARGET)

install: $(TARGET)
	$(INSTALL) $(TARGET) $(BINDIR)
	$(INSTALLDATA) -D README.md $(DATADIR)/$(TARGET)/README.md
	$(INSTALL) -d $(DATADIR)/$(TARGET)/{data,sample_scores}/
	$(INSTALLDATA) sample_scores/* $(DATADIR)/$(TARGET)/sample_scores/
	$(INSTALLDATA) data/* $(DATADIR)/$(TARGET)/data/
