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
#define COMMIT_BTN_W 9
#define AMEND_BTN_W 8

/* Clamping and min/max helpers */
int iclamp(int v, int lo, int hi);
int imin(int a, int b);
int imax(int a, int b);

/* String helpers */
void strtrim(char *s);
int str_display_width(const char *s);

/* Convert screen column position to character index, accounting for tabs.
 * screen_col: 0-based screen column position
 * line: the line of text
 * tab_width: visual width of tab characters (typically 8)
 * Returns: character index corresponding to the screen column
 */
int screen_col_to_char_idx(const char *line, int screen_col, int tab_width);

/* Convert character index to screen column position, accounting for tabs.
 * char_idx: 0-based character index
 * line: the line of text
 * tab_width: visual width of tab characters (typically 8)
 * Returns: screen column position corresponding to the character index
 */
int char_idx_to_screen_col(const char *line, int char_idx, int tab_width);

/* Clipboard helpers */
void copy_to_sys_clipboard(const char *text);

#endif
