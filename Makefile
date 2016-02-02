# Declaration of variables
CC = gcc
CC_FLAGS = -w


all: parta

parta: parta.c
	gcc $< tsc.o -o parta 

# To obtain object files
tsc.o: tsc.c
	$(CC) -c $(CC_FLAGS) $< -o $@

# To remove generated files
clean:
	rm -f parta tsc.o