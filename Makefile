CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk4) -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pthread
LIBS = $(shell pkg-config --libs gtk4) -pthread -lm

SRC = src/main.c src/package.c src/fuzzy.c src/pacman_source.c src/aur_source.c src/wallpaper.c src/palette.c src/theme.c

lazy-store: $(SRC)
	$(CC) $(CFLAGS) -o lazy-store $(SRC) $(LIBS)

clean:
	rm -f lazy-store
