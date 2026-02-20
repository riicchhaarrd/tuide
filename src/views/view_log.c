#include <stdio.h>
#include <string.h>

#include "../render.h"
#include "../state.h"
#include "../strings.h"
#include "../util.h"
#include "../views.h"

void draw_log(int top, int h) {
	int w = g_app_state.cols;
	box_top(top, 1, w, UI->title_commit_log, true, NULL);
	box_sides(top, 1, w, h, true);
	box_fill(top, 1, w, h, TH->bg_base);
	box_bot(top + h - 1, 1, w, true);

	int row = top + 1, lim = top + h - 1, vis = lim - row;
	if (g_app_state.commit_sel < g_app_state.commit_scroll)
		g_app_state.commit_scroll = g_app_state.commit_sel;
	if (g_app_state.commit_sel >= g_app_state.commit_scroll + vis)
		g_app_state.commit_scroll = g_app_state.commit_sel - vis + 1;
	int maxsc = imax(0, g_app_state.commit_count - (vis - 1)); /* -1 for header */
	g_app_state.commit_scroll = iclamp(g_app_state.commit_scroll, 0, maxsc);

	at(row, 2);
	cbg(TH->bg_header);
	cfg(TH->fg_accent3);
	g_app_state.cur_bold = true;
	int cursor_col = 2;
	ppad(UI->header_commit_graph, GRAPH_COLS);
	cursor_col += GRAPH_COLS;

	int hx = cursor_col + g_app_state.col_hash_w;
	ppad(UI->header_commit_hash, g_app_state.col_hash_w);
	at(row, hx);
	if (g_app_state.dragging_col_hash)
		cfg(TH->fg_accent1);
	else
		cfg(TH->fg_dim);
	put_cell(row, hx, "│");
	cursor_col = hx + 1;

	cfg(TH->fg_accent3);
	ppad(UI->header_commit_refs, 21);
	cursor_col += 21;

	int ax = cursor_col + g_app_state.col_author_w;
	ppad(UI->header_commit_author, g_app_state.col_author_w);
	at(row, ax);
	if (g_app_state.dragging_col_author)
		cfg(TH->fg_accent1);
	else
		cfg(TH->fg_dim);
	put_cell(row, ax, "│");
	cursor_col = ax + 1;

	cfg(TH->fg_accent3);
	int dx = cursor_col + g_app_state.col_date_w;
	ppad(UI->header_commit_date, g_app_state.col_date_w);
	at(row, dx);
	if (g_app_state.dragging_col_date)
		cfg(TH->fg_accent1);
	else
		cfg(TH->fg_dim);
	put_cell(row, dx, "│");
	cursor_col = dx + 1;

	cfg(TH->fg_accent3);
	ppad(UI->header_commit_subject, w - cursor_col - 1);
	rst();
	row++;
	vis--;

	for (int i = g_app_state.commit_scroll; i < g_app_state.commit_count && row < lim; i++, row++) {
		GitCommit *c = &g_app_state.commits[i];
		bool sel = (g_app_state.commit_sel == i);
		at(row, 2);
		if (sel) {
			cbg(TH->bg_sel);
			g_app_state.cur_bold = true;
		} else
			cbg(TH->bg_base);

		char *gp = c->graph;
		int gc = 0;
		while (*gp && gc < GRAPH_COLS) {
			int ci_ = (c->graph_col / 2) % 6;
			at(row, 2 + gc);
			if (*gp == '*') {
				cfg(TH->fg_graph[ci_]);
				g_app_state.cur_bold = true;
				put_cell(row, 2 + gc, "●");
			} else if (*gp == '|') {
				cfg(TH->fg_graph[gc / 2 % 6]);
				put_cell(row, 2 + gc, "|");
			} else {
				char tmp[2] = {*gp, 0};
				put_cell(row, 2 + gc, tmp);
			}
			rst();
			if (sel) {
				cbg(TH->bg_sel);
				g_app_state.cur_bold = true;
			} else
				cbg(TH->bg_base);
			gp++;
			gc++;
		}
		while (gc < GRAPH_COLS) {
			at(row, 2 + gc);
			put_cell(row, 2 + gc, " ");
			gc++;
		}

		cursor_col = 2 + GRAPH_COLS;
		at(row, cursor_col);
		cfg(sel ? TH->fg_sel : TH->fg_accent1);
		g_app_state.cur_bold = true;
		char hbuf[64];
		snprintf(hbuf, sizeof(hbuf), "%.*s", g_app_state.col_hash_w - 1, c->hash);
		ppad(hbuf, g_app_state.col_hash_w);
		rst();
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
		} else
			cbg(TH->bg_base);
		at(row, cursor_col + g_app_state.col_hash_w);
		put_cell(row, cursor_col + g_app_state.col_hash_w, "│");
		cursor_col += g_app_state.col_hash_w + 1;

		if (c->refs[0]) {
			cfg(sel ? TH->fg_sel : TH->fg_ref_local);
			char rf[32];
			snprintf(rf, sizeof(rf), "(%.18s) ", c->refs);
			ppad(rf, 21);
		} else {
			ppad("", 21);
		}
		cursor_col += 21;

		cfg(sel ? TH->fg_sel : TH->fg_accent2);
		ppad(c->author, g_app_state.col_author_w);
		at(row, cursor_col + g_app_state.col_author_w);
		put_cell(row, cursor_col + g_app_state.col_author_w, "│");
		cursor_col += g_app_state.col_author_w + 1;

		cfg(sel ? TH->fg_sel : TH->fg_accent3);
		ppad(c->date, g_app_state.col_date_w);
		at(row, cursor_col + g_app_state.col_date_w);
		put_cell(row, cursor_col + g_app_state.col_date_w, "│");
		cursor_col += g_app_state.col_date_w + 1;

		int sw = w - cursor_col - 1;
		if (sw > 0) {
			cfg(sel ? TH->fg_sel : TH->fg_normal);
			ppad(c->subject, sw);
		}
		rst();
	}
	while (row < lim) {
		at(row, 2);
		cbg(TH->bg_base);
		for (int i = 0; i < w - 3; i++) put_cell(row, 2 + i, " ");
		rst();
		row++;
	}
}
