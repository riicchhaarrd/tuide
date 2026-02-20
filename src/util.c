#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int imin(int a, int b) { return a < b ? a : b; }

int imax(int a, int b) { return a > b ? a : b; }

void strtrim(char *s) {
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}

int str_display_width(const char *s) {
	if (!s) return 0;
	int w = 0;
	mbstate_t state = {0};
	while (*s) {
		if (*s == 0x1b) {
			s++;
			if (*s == '[') {
				s++;
				while (*s && (*s < '@' || *s > '~')) s++;
				if (*s) s++;
			}
			continue;
		}
		wchar_t wc;
		size_t len = mbrtowc(&wc, s, MB_CUR_MAX, &state);
		if (len == (size_t)-1 || len == (size_t)-2) {
			// Invalid sequence, count as 1 and advance
			s++;
			w++;
		} else if (len == 0) {
			break; // null character
		} else {
			int width = wcwidth(wc);
			if (width < 0) width = 1; // non-printable or control char
			w += width;
			s += len;
		}
	}
	return w;
}

int screen_col_to_char_idx(const char *line, int screen_col, int tab_width) {
	if (!line) return 0;
	int visual_col = 0;
	int char_idx = 0;
	while (line[char_idx] && visual_col < screen_col) {
		if (line[char_idx] == '\t') {
			/* Tab advances to next multiple of tab_width */
			int next_tab_stop = ((visual_col / tab_width) + 1) * tab_width;
			visual_col = next_tab_stop;
		} else {
			visual_col++;
		}
		char_idx++;
	}
	/* If we're past the target, find the closest character */
	if (visual_col > screen_col && char_idx > 0) {
		/* Back up to see if previous character is closer */
		int prev_visual = 0;
		int prev_idx = 0;
		for (int i = 0; i < char_idx - 1; i++) {
			if (line[i] == '\t') {
				int next_tab = ((prev_visual / tab_width) + 1) * tab_width;
				prev_visual = next_tab;
			} else {
				prev_visual++;
			}
			prev_idx++;
		}
		/* Use the closer position */
		if (screen_col - prev_visual < visual_col - screen_col) {
			return prev_idx;
		}
	}
	return char_idx;
}

int char_idx_to_screen_col(const char *line, int char_idx, int tab_width) {
	if (!line) return 0;
	int visual_col = 0;
	for (int i = 0; i < char_idx && line[i]; i++) {
		if (line[i] == '\t') {
			/* Tab advances to next multiple of tab_width */
			visual_col = ((visual_col / tab_width) + 1) * tab_width;
		} else {
			visual_col++;
		}
	}
	return visual_col;
}

void copy_to_sys_clipboard(const char *text) {
	if (!text || !text[0]) return;
	const char *cmds[] = {"xclip -selection clipboard", "xsel --clipboard --input", "wl-copy",
						  "pbcopy", "clip.exe"};
	for (int i = 0; i < 5; i++) {
		char check[256];
		snprintf(check, sizeof(check), "command -v %s >/dev/null 2>&1",
				 (i == 0)	? "xclip"
				 : (i == 1) ? "xsel"
				 : (i == 2) ? "wl-copy"
				 : (i == 3) ? "pbcopy"
							: "clip.exe");
		if (system(check) == 0) {
			FILE *fp = popen(cmds[i], "w");
			if (fp) {
				fputs(text, fp);
				pclose(fp);
				return;
			}
		}
	}
}
