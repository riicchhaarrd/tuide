#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_FILES 512
#define MAX_COMMITS 512
#define MAX_BRANCHES 256
#define MAX_STASHES 64
#define MAX_DIFF_LINES 16384
#define MAX_TABS 16
#define LINE_MAX_LEN 512
#define INPUT_MAX 512
#define GRAPH_COLS 8
#define MAX_UNDO 80

/* Clamping and min/max helpers */
int iclamp(int v, int lo, int hi);
int imin(int a, int b);
int imax(int a, int b);

/* String helpers */
void strtrim(char *s);

/* Clipboard helpers */
void copy_to_sys_clipboard(const char *text);

#endif
