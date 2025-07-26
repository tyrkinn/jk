#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
#define CLEAR_SCREEN() printf("\033[2J");

#define SELECTION_BG "15"
#define SELECTION_FG "0"
#define FOCUS_FG "160"

#define ESCAPE 27

enum editor_mode {
	NORMAL,
	INSERT,
	VISUAL,
	COMMAND
};

char* mode_to_string(enum editor_mode mode) {
	switch (mode) {
		case NORMAL:
			return "NORMAL";
		case INSERT:
			return "INSERT";
		case VISUAL:
			return "VISUAL";
		case COMMAND:
			return "COMMAND";
	}
}

int cmp(int a, int b) {
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

struct position { int line, col, bufpos; };
int pos_cmp(struct position pos1, struct position pos2) {
	if (pos1.line == pos2.line) {
		return cmp(pos1.col, pos2.col);
	}
	if (pos1.line > pos2.line) {
		return 1;
	}
	return -1;
}

struct selection { struct position from; struct position to;};

void normalize_selection(struct selection *sel) {
	if (pos_cmp(sel->from, sel->to) == 1) {
		struct position temp = sel->from;
		sel->from = sel->to;
		sel->to = temp;
	}
}

struct context {
	FILE* f;
	char* contents;
	char** lines;
	size_t lines_count;
	struct position pos;
	struct selection sel;
	enum editor_mode mode;
};
struct winsize ws;
struct context ctx;

bool in_range(struct position pos, struct position from, struct position to) {
	if (pos.line == from.line && pos.line == to.line) {
		return pos.col >= from.col && pos.col <= to.col;
	} 
	if (pos.line == from.line) {
		return pos.col >= from.col;
	} 
	if (pos.line == to.line) {
		return pos.col <= to.col;
	} 
	return pos.line >= from.line && pos.line <= to.line;
}

bool in_selection(struct position pos) {
	if (pos_cmp(ctx.sel.from, ctx.sel.to) >= 0) {
		return in_range(pos, ctx.sel.to, ctx.sel.from);
	}
	return in_range(pos, ctx.sel.from, ctx.sel.to);
}

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

// SELECTION

void start_selection() {
	ctx.sel.from = ctx.pos;
	ctx.sel.to = ctx.pos;
}

void expand_selection() {
	if (ctx.mode != VISUAL) {
		return;
	}
	ctx.sel.to = ctx.pos;
}

void clear_selection() {
	struct selection sel = {{-1,-1,-1}, {-1,-1,-1}};
	ctx.sel = sel;
}

// CONTEXT

void init_context(FILE* file) {
	ctx.f = file;
	ctx.contents = read_all_file(file);
	ctx.lines = read_all_lines(ctx.contents);
	struct position pos = {0,0,0};
	ctx.pos = pos;
	struct selection sel = {{-1,-1,-1}, {-1,-1,-1}};
	ctx.sel = sel;
	ctx.mode = NORMAL;
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

void ccolored(char c, char* fgColor, char* bgColor) {
	if (fgColor != NULL && bgColor != NULL) {
		printf("\033[38;5;%s;48;5;%sm%c\033[0m", fgColor, bgColor, c);
	} else if (fgColor != NULL) {
		printf("\033[38;5;%sm%c\033[0m", fgColor, c);
	} else if (bgColor != NULL) {
		printf("\033[48;5;%sm%c\033[0m", bgColor, c);
	}
}


void print_line(size_t ln, char* line, struct position *cpos) {
	size_t line_len = strlen(line);
	printf("%zu: ", ln);
	for (size_t i = 0; i < line_len; ++i) {
		if (in_selection(*cpos)) {
			ccolored(line[i], SELECTION_FG, SELECTION_BG);
			goto loopend;
		}
		if (cpos->line == ctx.pos.line && cpos->col == ctx.pos.col) {
			ccolored(line[i], FOCUS_FG, NULL);
		} else {
			putchar(line[i]);
		}
	loopend:
		cpos->bufpos++;
		cpos->col++;
	}
	putchar('\n');
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
	printf("LINE: %d, COL: %d, BPOS: %d, MODE: %s, SELECTION f:%d:%d t:%d:%d\n", 
				ctx.pos.line, ctx.pos.col, ctx.pos.bufpos, mode_to_string(ctx.mode), 
				ctx.sel.from.line, ctx.sel.from.col,
				ctx.sel.to.line, ctx.sel.to.col);
}

// TEXT OPERATIONS

void word_next() {
	bool word_ended = false;
	char *current_line = ctx.lines[ctx.pos.line];
	size_t initial_col = ctx.pos.col;
	size_t i = initial_col;
	for (;i < strlen(current_line); ++i) {
		if (word_ended) {
			ctx.pos.col = i;
			ctx.pos.bufpos += i - initial_col;
			return;
		}
		char cur = current_line[i];  
		if (cur == ' ' || cur == '\n') {
			word_ended = true;
		}
	}
	if (i >= strlen(current_line)) {
		if (ctx.pos.line + 1 < ctx.lines_count) {
			ctx.pos.bufpos += current_line_len() - ctx.pos.col + 1;
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
			ctx.pos.bufpos += current_line_len() - ctx.pos.col + 1;
			ctx.pos.line++;
			ctx.pos.col = 0;
			adjust_col();
		}
	}
	size_t initial_col = ctx.pos.col;
	size_t i = initial_col + 1;
	bool word_ended = current_line[i] != ' ';
	for (; i <= strlen(current_line); ++i) {
		char cur = current_line[i];  
		if (cur == ' ' || i == strlen(current_line)) {
			if (word_ended) {
				ctx.pos.col = i - 1;
				ctx.pos.bufpos += i - 1 - initial_col;
				return;
			}
			word_ended = true;
			i++;
		}
	}
}

void word_back() {
	if (ctx.pos.col == 0 && ctx.pos.line - 1 >= 0) {
		int initial_col = ctx.pos.col;
		ctx.pos.line--;
		ctx.pos.col = strlen(ctx.lines[ctx.pos.line]) - 1;
		ctx.pos.bufpos -= initial_col + current_line_len() - ctx.pos.col + 1;
	}
	char *current_line = ctx.lines[ctx.pos.line];
	int initial_col = ctx.pos.col;
	int i = initial_col - 1; 
	bool in_word = current_line[i] != ' ';
	for (;i >= 0; i--) {
		char cur = current_line[i];
		if (in_word) {
			if (cur == ' ' || i == 0) {
				ctx.pos.col = i == 0 ? i : i + 1;
				ctx.pos.bufpos -= initial_col - (i == 0 ? i : i + 1); 
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

	char buf[1];
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
					ctx.pos.bufpos++;
				}
				expand_selection();
				print_contents();
				break;
			case 'h':
				if (ctx.pos.col - 1 >= 0) {
					ctx.pos.col--;
					ctx.pos.bufpos--;
				}
				expand_selection();
				print_contents();
				break;
			case 'j':
				if (ctx.pos.line + 1 >= ctx.lines_count) break;
				int line_est_chars = current_line_len() - ctx.pos.col;
				ctx.pos.line++;
				adjust_col();
				ctx.pos.bufpos += line_est_chars + ctx.pos.col + 1;
				expand_selection();
				print_contents();
				break;
			case 'k':
				if (ctx.pos.line - 1 < 0) break;
				int initial_col = ctx.pos.col;
				ctx.pos.line--;
				adjust_col();
				line_est_chars = current_line_len() - ctx.pos.col;
				ctx.pos.bufpos -= initial_col + line_est_chars + 1;
				expand_selection();
				print_contents();
				break;
			case 'w':
				word_next();
				expand_selection();
				print_contents();
				break;
			case 'e':
				word_end();
				expand_selection();
				print_contents();
				break;
			case 'b':
				word_back();
				expand_selection();
				print_contents();
				break;
			case 'v':
				ctx.mode = VISUAL;
				start_selection();
				print_contents();
				break;
			case 'x':

				break;
			case 'd':
				switch (ctx.mode) {
					case NORMAL:
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
					case INSERT: break;
					case VISUAL: 
						// normalize_selection(&ctx.sel);
						// char* slice = ctx.contents + ctx.sel.from.bufpos;
						// int delete_size = ctx.sel.to.bufpos - ctx.sel.from.bufpos;
						// memmove(slice, slice+delete_size+1, 1+strlen(slice + delete_size));
						// clear_selection();
						// ctx.mode = NORMAL;
						// ctx.lines = read_all_lines(ctx.contents);
						// print_contents();
						break;
					case COMMAND: break;
				}
				break;
			case ESCAPE:
				ctx.mode = NORMAL;
				clear_selection();
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
