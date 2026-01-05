CC      ?= cc
CFLAGS  ?= -Os -Wall -Wextra
LDFLAGS ?= -s
LDLIBS  ?= -lsystemd

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin
UNITDIR ?= $(PREFIX)/lib/systemd/user

TARGET  = mpd-inhibit

all: $(TARGET)

$(TARGET): mpd-inhibit.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 mpd-inhibit.service $(DESTDIR)$(UNITDIR)/mpd-inhibit.service

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(UNITDIR)/mpd-inhibit.service

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
