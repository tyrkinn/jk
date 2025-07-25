#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
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

size_t current_line_len() {
	return strlen(ctx.lines[ctx.pos.line]);
}

void adjust_col() {
	size_t curlen = current_line_len();
	if (ctx.pos.col >= curlen) {
		ctx.pos.col = curlen-1;
	}
}



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
	for (size_t i = 0; i < ws.ws_row - 2; ++i) {
		if (i < ctx.lines_count) {
			print_line(i, ctx.lines[i], &current_pos);
		} else {
			printf("%zu: \n", i);
		}
	}
	printf("LINE: %d, COL: %d\n", ctx.pos.line, ctx.pos.col);
}

// TEXT OPERATIONS

void word_next() {
	bool word_ended = false;
	char *current_line = ctx.lines[ctx.pos.line];
	size_t i = ctx.pos.col;
	for (;i < strlen(current_line); ++i) {
		if (word_ended) {
			ctx.pos.col = i;
			return;
		}
		char cur = current_line[i];  
		if (cur == ' ' || cur == '\n') {
			word_ended = true;
		}
	}
	if (i >= strlen(current_line)) {
		if (ctx.pos.line + 1 < ctx.lines_count) {
			ctx.pos.line++;
			ctx.pos.col = 0;
			adjust_col();
		}
	}
}

void word_end() {
	char *current_line = ctx.lines[ctx.pos.line];
	if (ctx.pos.col + 1 >= strlen(current_line)) {
		if (ctx.pos.line + 1 < ctx.lines_count) {
			ctx.pos.line++;
			ctx.pos.col = 0;
			adjust_col();
		}
	}
	size_t i = ctx.pos.col + 1;
	bool word_ended = current_line[i] != ' ';
	for (; i <= strlen(current_line); ++i) {
		char cur = current_line[i];  
		if (cur == ' ' || i == strlen(current_line)) {
			if (word_ended) {
				ctx.pos.col = i - 1;
				return;
			}
			word_ended = true;
			i++;
		}
	}
}

void word_back() {
	if (ctx.pos.col == 0 && ctx.pos.line - 1 >= 0) {
		ctx.pos.line--;
		ctx.pos.col = strlen(ctx.lines[ctx.pos.line]) - 1;
	}
	char *current_line = ctx.lines[ctx.pos.line];
	int i = ctx.pos.col - 1; 
	bool in_word = current_line[i] != ' ';
	for (;i >= 0; i--) {
		char cur = current_line[i];
		if (in_word) {
			if (cur == ' ' || i == 0) {
				ctx.pos.col = i == 0 ? i : i + 1;
				return;
			}
			continue;
		}
		i--;
		in_word = true;
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
	while ((bytes = read(STDIN_FILENO, buf, 1)) > 0) {
		char key = buf[0];
		switch (key) {
			case 'q':
				goto shutdown;
			case 'l':
				if (ctx.pos.col + 1 < current_line_len()) {
					ctx.pos.col++;
				}
				print_contents();
				break;
			case 'h':
				if (ctx.pos.col - 1 >= 0) {
					ctx.pos.col--;
				}
				print_contents();
				break;
			case 'j':
				if (ctx.pos.line + 1 < ctx.lines_count) {
					ctx.pos.line++;
				}
				adjust_col();
				print_contents();
				break;
			case 'k':
				if (ctx.pos.line - 1 >= 0) {
					ctx.pos.line--;
				}
				adjust_col();
				print_contents();
				break;
			case 'w':
				word_next();
				print_contents();
				break;
			case 'e':
				word_end();
				print_contents();
				break;
			case 'b':
				word_back();
				print_contents();
				break;
			case 'd':
				if((bytes = read(STDIN_FILENO, buf, 1)) > 0) {
					if (buf[0] == 'd') {
						ctx.lines[ctx.pos.line] = NULL;
						ctx.lines_count--;
						if (ctx.pos.line - 1 >= 0) {
							ctx.pos.line--;
							adjust_col();
						}
						print_contents();
					}
				};
				break;
			default:
				break;
		}
	}

shutdown: 
	free_context();
	return 0;

}
