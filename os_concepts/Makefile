CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2
LDLIBS = -pthread
BIN    = bin
PROGS  = scheduler prodcons deadlock paging disk process_demo

all: $(addprefix $(BIN)/,$(PROGS))

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%: src/%.c | $(BIN)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -rf $(BIN)

.PHONY: all clean
