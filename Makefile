CC := gcc
override __CFLAGS := -std=gnu99 -O2 -I ./src $(CFLAGS)

LD := gcc
override __LDFLAGS := $(LDFLAGS)

.PHONY: all
all: egps

egps: src/main.o
	$(LD) $(__LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(__CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	find -name '*.o' -delete
	rm -f egps
