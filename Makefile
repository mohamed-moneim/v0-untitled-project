CC = gcc
CFLAGS = -Wall -g -lm

all: process_generator clk scheduler process testgenerator

process_generator: process_generator.c headers.h
	$(CC) process_generator.c -o process_generator $(CFLAGS)

clk: clk.c headers.h
	$(CC) clk.c -o clk $(CFLAGS)

scheduler: scheduler.c headers.h
	$(CC) scheduler.c -o scheduler $(CFLAGS)

process: process.c headers.h
	$(CC) process.c -o process $(CFLAGS)

testgenerator: testgenerator.c
	$(CC) testgenerator.c -o testgenerator $(CFLAGS)

clean:
	rm -f process_generator clk scheduler process testgenerator *.log *.perf

run: all
	./process_generator

.PHONY: all clean run
