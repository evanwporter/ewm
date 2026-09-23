CC ?= cc

MAIN := anvl

SRC_DIR := src
BUILD_DIR := build
PROTO_DIR := protocol

SRC := \
	$(SRC_DIR)/anvl.c \
	$(SRC_DIR)/river.c \
	$(SRC_DIR)/management.c \
	$(SRC_DIR)/bar.c \
	$(SRC_DIR)/status.c

OBJ := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

PROTO_XML := $(shell fd -e xml . $(PROTO_DIR))
PROTO_SRC := $(patsubst $(PROTO_DIR)/%.xml,$(BUILD_DIR)/%-protocol.c,$(PROTO_XML))
PROTO_HDR := $(patsubst $(PROTO_DIR)/%.xml,$(BUILD_DIR)/%-client-protocol.h,$(PROTO_XML))
PROTO_OBJ := $(PROTO_SRC:.c=.o)

DEP := $(OBJ:.o=.d) $(PROTO_OBJ:.o=.d)

PKGS := xkbcommon wayland-client pixman-1 fcft

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I$(BUILD_DIR) \
	$(shell pkg-config --cflags $(PKGS))

CFLAGS := \
	-std=c23 \
	-MMD \
	-MP

LDLIBS := $(shell pkg-config --libs $(PKGS))

all: $(BUILD_DIR)/$(MAIN)

compile_commands:
	bear --output compile_commands.json -- make clean all

$(BUILD_DIR)/$(MAIN): $(OBJ) $(PROTO_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# Normal source files may include generated Wayland protocol headers,
# so ensure those headers exist before compiling any normal object.
$(OBJ): $(PROTO_HDR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%-protocol.o: $(BUILD_DIR)/%-protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%-client-protocol.h: $(PROTO_DIR)/%.xml | $(BUILD_DIR)
	wayland-scanner client-header $< $@

$(BUILD_DIR)/%-protocol.c: $(PROTO_DIR)/%.xml | $(BUILD_DIR)
	wayland-scanner private-code $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEP)

.PHONY: all clean compile_commands
