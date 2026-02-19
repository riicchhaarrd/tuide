#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int imin(int a, int b) { return a < b ? a : b; }

int imax(int a, int b) { return a > b ? a : b; }

void strtrim(char *s) {
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
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
