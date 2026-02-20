#include <stdio.h>
#include <string.h>

#include "../render.h"
#include "../state.h"
#include "../ui_strings.h"
#include "../util.h"
#include "../views.h"

void draw_help(int top, int h) {
	int w = g_app_state.cols;
	box_top(top, 1, w, UI->title_help, true, NULL);
	box_sides(top, 1, w, h, true);
	box_fill(top, 1, w, h, TH->bg_panel);
	box_bot(top + h - 1, 1, w, true);

	const UiHelpEntry *E = UI->help_entries;
	int row = top + 1, lim = top + h - 1;
	int split = g_app_state.cols / 2;
	for (int i = 0; E[i].left && row < lim; i++, row++) {
		at(row, 2);
		cbg(TH->bg_panel);
		if (E[i].right[0] == '\0' && strlen(E[i].left) > 1) {
			cfg(TH->fg_accent1);
			g_app_state.cur_bold = true;
			ppad(E[i].left, split - 3);
		} else if (!E[i].left[0]) {
			for (int k = 0; k < g_app_state.cols - 3; k++) put_cell(row, 2 + k, " ");
		} else {
			cfg(TH->fg_accent2);
			g_app_state.cur_bold = true;
			ppad(E[i].left, split - 3);
			at(row, split);
			cbg(TH->bg_panel);
			cfg(TH->fg_normal);
			ppad(E[i].right, g_app_state.cols - split - 2);
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
