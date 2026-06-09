CC := gcc
CFLAGS := -Wall -Wextra -std=c99 -O2 -fPIC
LDFLAGS := -lsqlite3 -lcurl -lncurses -lcjson -lm

# Detect OS and add platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -ldl
endif

# Debian/Ubuntu package dependencies
PKG_DEPS := libsqlite3-dev libcurl4-openssl-dev libncurses-dev libcjson-dev ffmpeg

TARGETS := jfm-ui jfm-sync jfm-populate
SOURCES := jfm.h jfm.c db.c jf_api.c jfm-ui.c jfm-sync.c jfm-populate.c
COMMON_OBJS := db.o jf_api.o

.PHONY: all clean install install-deps init-db

all: $(TARGETS)

jfm-ui: jfm-ui.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

jfm-sync: jfm-sync.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

jfm-populate: jfm-populate.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c jfm.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGETS) jfm.db

install: all
	mkdir -p /usr/local/bin
	install -m 755 jfm-ui /usr/local/bin/
	install -m 755 jfm-sync /usr/local/bin/
	install -m 755 jfm-populate /usr/local/bin/

install-deps:
	@echo "Installing build and runtime dependencies..."
	sudo apt-get update
	sudo apt-get install -y $(PKG_DEPS)

init-db:
	@echo "Initializing database..."
	sqlite3 $(HOME)/.local/share/jfm/jfm.db < init.sql
	@echo "Database initialized at $(HOME)/.local/share/jfm/jfm.db"
	@echo "Configure servers with: sqlite3 $(HOME)/.local/share/jfm/jfm.db"
	@echo "  INSERT INTO servers (name, url, api_key) VALUES ('server1', 'http://jellyfin.local:8096', 'your_key_here');"
	@echo "  INSERT INTO exclude_paths (path) VALUES ('/data/video/youtube');"
