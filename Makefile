.PHONY: all
all:
	gcc -Wall -Wno-error -o code main.c buddy.c

test:
	gcc -Wall -Wno-error -o test main.c buddy.c

clean:
	rm -f code test