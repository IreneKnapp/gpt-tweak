gpt-tweak: $(wildcard *.c)
	gcc -O3 -g -o $@ $^
