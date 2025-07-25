all: build run

build: jk.c
	cc -o jk jk.c

run: 
	./jk sample.txt
