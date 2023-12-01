#ifndef INPUT_H
#define INPUT_H

#include <stdlib.h>

struct position {
	const char *path;
	int line, column;
};

struct input {
	const char *path;
	char *contents;
};

void input_add_include_path(const char *path);
void input_disable_path(const char *path);

struct input *input_open(const char *parent_path, const char *path, int system);

void input_free(struct input *input);

void input_reset(void);

#endif
