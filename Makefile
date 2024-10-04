CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lX11 -lm

gifw: gifw.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

gifw.o: gifw.c
	$(CC) $(CFLAGS) -c $<

.PHONY: clean install

clean:
	rm -f gifw gifw.o

install: gifw
	install -D -m 755 gifw $(DESTDIR)/usr/local/bin/gifw