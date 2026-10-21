CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -Werror -O2
LDFLAGS =

# Test harness
TEST_SRC = main.c tal_lex.c tal_parse.c tal_guard.c
TEST_OBJ = $(TEST_SRC:.c=.o)

# LSP server
LSP_SRC = tal_lsp.c tal_json.c tal_lex.c tal_parse.c tal_sema.c tal_guard.c \
          tacl_lex.c tacl_sema.c tacl_tabs.c
LSP_OBJ = $(LSP_SRC:.c=.o)

all: tal-test tal-lspd

tal-test: $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ) $(LDFLAGS)

tal-lspd: $(LSP_OBJ)
	$(CC) $(CFLAGS) -o $@ $(LSP_OBJ) $(LDFLAGS)

%.o: %.c tal.h tacl.h tal_lsp.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f tal-test tal-lspd *.o

.PHONY: all clean
