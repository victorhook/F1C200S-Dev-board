ifndef BUILD
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
