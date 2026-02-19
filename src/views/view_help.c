#include <stdio.h>
#include <string.h>

#include "../render.h"
#include "../state.h"
#include "../util.h"
#include "../views.h"

void draw_help(int top, int h) {
	int w = g_app_state.cols;
	box_top(top, 1, w, "Help & Keybindings", true, NULL);
	box_sides(top, 1, w, h, true);
	box_fill(top, 1, w, h, TH->bg_panel);
	box_bot(top + h - 1, 1, w, true);

	static const char *E[][2] = {{"NAVIGATION", ""},
								 {"  Tab / Shift+Tab", "Cycle through all VISIBLE panes"},
								 {"  1-4 / ?", "Jump to specific Git view"},
								 {"  ↑/↓", "Move selection"},
								 {"  ←/→", "Switch focus (or Browser back/enter)"},
								 {"  Home / End", "Top / bottom"},
								 {"  PgUp/PgDn", "Page scroll"},
								 {"", ""},
								 {"CHANGES PANE", ""},
								 {"  e", "Open selected file in Editor"},
								 {"  Space / Ctrl+S", "Stage / unstage selected file"},
								 {"  a / u", "Stage all / Unstage all"},
								 {"  d", "Discard changes"},
								 {"  Enter / =", "Toggle Diff pane"},
								 {"", ""},
								 {"EDITOR", ""},
								 {"  e", "Toggle Editor visibility (right pane)"},
								 {"  Arrows", "Move cursor"},
								 {"  Enter", "Insert newline"},
								 {"  BS", "Delete character"},
								 {"  Ctrl+Z", "Undo"},
								 {"  Ctrl+Y", "Redo"},
								 {"  Ctrl+S", "Save file"},
								 {"  Ctrl+X", "Cut selection"},
								 {"  Ctrl+C / y", "Copy selection"},
								 {"  Ctrl+V", "Paste from clipboard"},
								 {"  Shift+Arrows", "Extend text selection"},
								 {"", ""},
								 {"FILE BROWSER", ""},
								 {"  b", "Toggle Browser visibility (left pane)"},
								 {"  ↑/↓", "Move selection"},
								 {"  Enter / →", "Open file in Editor / Enter directory"},
								 {"  ←", "Go back to parent directory"},
								 {"", ""},
								 {"DIFF SELECTION", ""},
								 {"  Mouse Drag", "Select text in diff view"},
								 {"  y / Ctrl+C", "Copy selection to clipboard"},
								 {"", ""},
								 {"GLOBAL", ""},
								 {"  c / Ctrl+C", "Commit staged changes"},
								 {"  A", "Amend last commit"},
								 {"  P / Ctrl+P", "Push to remote"},
								 {"  f / Ctrl+F", "Fetch + pull"},
								 {"  s", "Stash working changes"},
								 {"  R / Ctrl+R / Ctrl+L", "Full refresh"},
								 {"  T", "Cycle theme"},
								 {"  ?", "Toggle this help"},
								 {"  q / Esc / Ctrl+Q", "Go back / quit"},
								 {"", ""},
								 {"MOUSE", ""},
								 {"  Click", "Focus pane, select item"},
								 {"  Scroll wheel", "Scroll pane under cursor"},
								 {NULL, NULL}};
	int row = top + 1, lim = top + h - 1;
	int split = g_app_state.cols / 2;
	for (int i = 0; E[i][0] && row < lim; i++, row++) {
		at(row, 2);
		cbg(TH->bg_panel);
		if (E[i][1][0] == '\0' && strlen(E[i][0]) > 1) {
			cfg(TH->fg_accent1);
			g_app_state.cur_bold = true;
			ppad(E[i][0], split - 3);
		} else if (!E[i][0][0]) {
			for (int k = 0; k < g_app_state.cols - 3; k++) put_cell(row, 2 + k, " ");
		} else {
			cfg(TH->fg_accent2);
			g_app_state.cur_bold = true;
			ppad(E[i][0], split - 3);
			at(row, split);
			cbg(TH->bg_panel);
			cfg(TH->fg_normal);
			ppad(E[i][1], g_app_state.cols - split - 2);
		}
		rst();
	}
	while (row < lim) {
		at(row, 2);
		cbg(TH->bg_panel);
		for (int i = 0; i < g_app_state.cols - 3; i++) putchar(' ');
		rst();
		row++;
	}
}
