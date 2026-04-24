ifndef BUILD
# Invoked directly in this subdir — hand off to the top-level Makefile
# so all the cross-compile vars (CC, CFLAGS, BUILD, ...) are set correctly.
.PHONY: all clean
all:
	@$(MAKE) -C .. $(notdir $(CURDIR)) --no-print-directory
clean:
	@$(MAKE) -C .. clean --no-print-directory
else
TARGET := $(notdir $(CURDIR))
SRCS   := $(wildcard *.c)

$(BUILD)/$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
endif
