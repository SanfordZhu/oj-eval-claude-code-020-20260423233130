.PHONY: all
all:
	gcc -o code main.c buddy.c

test:
	gcc -o test main.c buddy.c