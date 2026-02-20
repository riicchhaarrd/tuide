#include <stdio.h>
#include <string.h>

#include "../render.h"
#include "../state.h"
#include "../strings.h"
#include "../util.h"
#include "../views.h"

void draw_stash(int top, int h) {
	int w = g_app_state.cols;
	char title[64];
	snprintf(title, sizeof(title), UI->title_stash_fmt, g_app_state.stash_count);
	box_top(top, 1, w, title, true, NULL);
	box_sides(top, 1, w, h, true);
	box_fill(top, 1, w, h, TH->bg_panel);
	box_bot(top + h - 1, 1, w, true);
	int row = top + 1, lim = top + h - 1;
	if (!g_app_state.stash_count) {
		int msg_w = str_display_width(UI->stash_empty_msg);
		int max_w = w - 2;
		if (max_w < 1) max_w = 1;
		int draw_w = iclamp(msg_w, 1, max_w);
		int msg_x = 1 + (w - draw_w) / 2;
		at(row + 2, msg_x);
		cfg(TH->fg_dim);
		ppad(UI->stash_empty_msg, draw_w);
		rst();
		return;
	}
	for (int i = 0; i < g_app_state.stash_count && row < lim; i++, row++) {
		bool sel = (g_app_state.stash_sel == i);
		at(row, 2);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
		} else
			cbg(TH->bg_panel);
		cfg(sel ? TH->fg_sel : TH->fg_accent1);
		g_app_state.cur_bold = true;
		char sibuf[16];
		snprintf(sibuf, sizeof(sibuf), " stash@{%d} ", g_app_state.stashes[i].index);
		ppad(sibuf, (int)strlen(sibuf));
		cfg(sel ? TH->fg_sel : TH->fg_accent3);
		char hbuf[16];
		snprintf(hbuf, sizeof(hbuf), "%.8s  ", g_app_state.stashes[i].hash);
		ppad(hbuf, 10);
		cfg(sel ? TH->fg_sel : TH->fg_normal);
		ppad(g_app_state.stashes[i].message, w - 28);
		rst();
	}
	while (row < lim) {
		at(row, 2);
		cbg(TH->bg_panel);
		for (int i = 0; i < w - 3; i++) put_cell(row, 2 + i, " ");
		rst();
		row++;
	}
}
