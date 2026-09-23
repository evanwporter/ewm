CC ?= cc

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS ?= -O2
CFLAGS += -std=c99 \
          -D_POSIX_C_SOURCE=200809L \
          -Wall -Wextra -Wpedantic \
          -Wno-unused-parameter \
          -I. \
          $(shell pkg-config --cflags wayland-client xkbcommon)

LDLIBS += $(shell pkg-config --libs wayland-client xkbcommon)

WAYLAND_SCANNER ?= wayland-scanner

PROTOCOL_DIR = protocol

PROTOCOLS = \
	river-window-management-v1 \
	river-xkb-bindings-v1

GENERATED_HEADERS = $(addsuffix -client-protocol.h,$(PROTOCOLS))
GENERATED_SOURCES = $(addsuffix -protocol.c,$(PROTOCOLS))

SRC = tinyrwm.c $(GENERATED_SOURCES)
OBJ = $(SRC:.c=.o)

TARGET = tinyrwm


.PHONY: all clean install uninstall compile_commands

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

tinyrwm.o: tinyrwm.c $(GENERATED_HEADERS)

%-client-protocol.h: $(PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) client-header $< $@

%-protocol.c: $(PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) private-code $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ) $(GENERATED_HEADERS) $(GENERATED_SOURCES)

compile_commands:
	$(MAKE) clean
	bear -- $(MAKE)
