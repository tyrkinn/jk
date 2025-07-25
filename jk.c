#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#define CLEAR_SCREEN() printf("\033[2J");

struct position { int line, col, bufpos; };

struct context {
	FILE* f;
	char* contents;
	char** lines;
	size_t lines_count;
	struct position pos;
};

struct winsize ws;
struct context ctx;


// FILES

FILE* get_file(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "USAGE: jk <FILE>\n");
		exit(1);
	}
	char* fname = argv[1];
	if (access(fname, F_OK) != 0) {
		fprintf(stderr, "You should provide existing file\n");
		exit(1);
	}
	return fopen(fname, "r");
}

char* read_all_file(FILE* file) {
	size_t size;
	fseek(file, 0, SEEK_END); 
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* dest = malloc(sizeof(char) * size);
	fread(dest, 1, size, file);
	return dest;
}

char** read_all_lines(char* source) {
	char** lines = malloc(2056 * sizeof(char));
	ctx.lines_count = 0;
	char* line = strtok(source, "\n");
	do {
		lines[ctx.lines_count++] = line;
	}
	while ((line=strtok(NULL, "\n")) != NULL);
	return lines;
};

// CONTEXT

void init_context(FILE* file) {
	ctx.f = file;
	ctx.contents = read_all_file(file);
	ctx.lines = read_all_lines(ctx.contents);
	struct position pos = {0,0,0};
	ctx.pos = pos;
}

void free_context() {
	fclose(ctx.f);
	free(ctx.contents);
	free(ctx.lines);
}

// TERMINAL SETUP

void enable_non_canonical() {
	struct termios attr;
	tcgetattr(STDIN_FILENO, &attr);
	attr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &attr);
}

// DISPLAYING
//

void ccolored(char c) {
	printf("\033[31;1;4m%c\033[0m", c);
}

void print_line(size_t ln, char* line, struct position *cpos) {
	size_t line_len = strlen(line);
	printf("%zu: ", ln);
	for (size_t i = 0; i < line_len; ++i) {
		if (cpos->line == ctx.pos.line && cpos->col == ctx.pos.col) {
			ccolored(line[i]);
		} else {
			printf("%c", line[i]);
		}
		cpos->bufpos++;
		cpos->col++;
	}
	printf("\n");
	cpos->line++;
	cpos->col = 0;
}

void print_contents() {
	CLEAR_SCREEN();
	struct position current_pos = {0,0,0};
	for (size_t i = 0; i < ws.ws_row - 1; ++i) {
		if (i < ctx.lines_count) {
			print_line(i, ctx.lines[i], &current_pos);
		} else {
			printf("%zu: \n", i);
		}
	}
}

int main(int argc, char** argv) {
	ioctl(0, TIOCGWINSZ, &ws);
	FILE* file = get_file(argc, argv);
	init_context(file);
	enable_non_canonical();

	uint8_t buf[1];
	ssize_t bytes;
	print_contents();
	size_t current_line_len;
	while ((bytes = read(STDIN_FILENO, buf, 1)) > 0) {
		char key = buf[0];
		switch (key) {
			case 'q':
				goto shutdown;
			case 'l':
				current_line_len = strlen(ctx.lines[ctx.pos.line]);
				if (ctx.pos.col + 1 < current_line_len) {
					ctx.pos.col++;
				}
				print_contents();
				break;
			case 'h':
				current_line_len = strlen(ctx.lines[ctx.pos.line]);
				if (ctx.pos.col - 1 >= 0) {
					ctx.pos.col--;
				}
				print_contents();
				break;
			case 'j':
				if (ctx.pos.line + 1 < ctx.lines_count) {
					ctx.pos.line++;
				}
				print_contents();
				break;
			case 'k':
				if (ctx.pos.line - 1 >= 0) {
					ctx.pos.line--;
				}
				print_contents();
				break;
			default:
				break;
		}
	}

shutdown: 
	free_context();
	return 0;

}
