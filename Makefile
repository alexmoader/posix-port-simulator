CC = gcc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -pthread -lm -lrt

SOURCES = config.c resources.c output.c dispatcher.c runtime.c workers.c ipc.c process.c
HEADERS = port_types.h config.h resources.h output.h dispatcher.h runtime.h ipc.h process.h
TARGETS = port_sim_unnamed port_sim_named port_sim_independent port_worker

.PHONY: all test check clean

all: $(TARGETS)

port_sim_unnamed: main.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDEFAULT_MODE=IPC_UNNAMED main.c $(SOURCES) -o $@ $(LDLIBS)

port_sim_named: main.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDEFAULT_MODE=IPC_NAMED main.c $(SOURCES) -o $@ $(LDLIBS)

port_sim_independent: main.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDEFAULT_MODE=IPC_INDEPENDENT main.c $(SOURCES) -o $@ $(LDLIBS)

port_worker: worker_main.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) worker_main.c $(SOURCES) -o $@ $(LDLIBS)

test_dispatcher: tests/test_dispatcher.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. tests/test_dispatcher.c $(SOURCES) -o $@ $(LDLIBS)

test: all test_dispatcher
	@directory=$$(mktemp -d); binary="$(CURDIR)/test_dispatcher"; \
	    (cd "$$directory" && "$$binary" > checks.log) || { cat "$$directory/checks.log"; rm -rf "$$directory"; exit 1; }; \
	    tail -n 1 "$$directory/checks.log"; rm -rf "$$directory"
	bash tests/test_processes.sh

check: test

clean:
	rm -f $(TARGETS) test_dispatcher
