#include <stdio.h>
#include <string.h>

#include "../git.h"
#include "../render.h"
#include "../state.h"
#include "../util.h"
#include "../views.h"

static void draw_commit_bar(int row, int w, int sx) {
	bool focused = g_app_state.commit_bar_focused;
	int iw = w - 2;

	at(row, sx + 1);
	cbg(TH->bg_header);
	cfg(TH->fg_staged);
	g_app_state.cur_bold = true;
	ppad(" \xe2\x9c\x8d ", 3);
	rst();

	int field_w = iw - 3 - 14;
	if (field_w < 4) field_w = 4;

	int len = (int)strlen(g_app_state.commit_msg_buf);
	int disp_start = 0;
	if (g_app_state.commit_msg_cursor >= field_w)
		disp_start = g_app_state.commit_msg_cursor - field_w + 1;

	at(row, sx + 4);
	cbg(focused ? TH->bg_tab_act : TH->bg_panel);
	cfg(focused ? TH->fg_bright : TH->fg_dim);
	for (int i = 0; i < field_w; i++) put_cell(row, sx + 4 + i, " ");

	for (int i = disp_start; i < len && (i - disp_start) < field_w; i++) {
		int col = sx + 4 + (i - disp_start);
		bool is_cursor = (focused && i == g_app_state.commit_msg_cursor);
		if (is_cursor) {
			cbg(TH->fg_accent1);
			cfg(TH->bg_base);
			g_app_state.cur_bold = true;
		} else {
			cbg(focused ? TH->bg_tab_act : TH->bg_panel);
			cfg(focused ? TH->fg_bright : TH->fg_dim);
			g_app_state.cur_bold = false;
		}
		char cs[2] = {g_app_state.commit_msg_buf[i], 0};
		put_cell(row, col, cs);
	}
	if (focused && g_app_state.commit_msg_cursor >= len) {
		int col = sx + 4 + (len - disp_start);
		if (col >= sx + 4 && col < sx + 4 + field_w) {
			cbg(TH->fg_accent1);
			cfg(TH->bg_base);
			g_app_state.cur_bold = true;
			put_cell(row, col, " ");
		}
	}
	rst();

	if (!focused && len == 0) {
		at(row, sx + 4);
		cbg(TH->bg_panel);
		cfg(TH->fg_dim);
		g_app_state.cur_italic = true;
		ppad("commit message...", field_w);
		g_app_state.cur_italic = false;
		rst();
	}

	int btn_x = sx + 4 + field_w;
	at(row, btn_x);
	cbg(TH->fg_staged);
	cfg(TH->bg_base);
	g_app_state.cur_bold = true;
	ppad(" Commit", 7);
	rst();
	at(row, btn_x + 7);
	cbg(TH->bg_panel);
	cfg(TH->fg_accent2);
	g_app_state.cur_bold = true;
	ppad(" Amend ", 7);
	rst();
}

void draw_changes(int top, int h) {
	if (h <= 2) return;
	int w = g_app_state.layout_width;
	int sx = g_app_state.sidebar_w + 1;
	bool act = (g_app_state.focus == FOCUS_CHANGES && g_app_state.current_view == VIEW_STATUS);
	box_top(top, sx, w, "Changes", act, NULL);
	box_sides(top, sx, w, h, act);
	box_fill(top, sx, w, h, TH->bg_panel);

	int staged_n = 0, unstaged_n = 0;
	for (int i = 0; i < g_app_state.file_count; i++)
		g_app_state.files[i].staged ? staged_n++ : unstaged_n++;

	int row = top + 1, lim = top + h - 1, iw = w - 2;

	if (row < lim) {
		draw_commit_bar(row, w, sx);
		row++;
	}
	if (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_panel);
		cfg(TH->fg_dim);
		for (int i = 0; i < iw; i++) put_cell(row, sx + 1 + i, "─");
		rst();
		row++;
	}

	if (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_header);
		cfg(TH->fg_staged);
		g_app_state.cur_bold = true;
		char hdr[64];
		snprintf(hdr, sizeof(hdr), " ✓ Staged (%d) ", staged_n);
		ppad(hdr, iw);
		rst();
		row++;
	}
	for (int i = 0; i < g_app_state.file_count && row < lim; i++) {
		if (!g_app_state.files[i].staged) continue;
		bool sel = (g_app_state.file_sel == i && act);
		at(row, sx + 1);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
			ppad(" »", 2);
		} else {
			cbg(TH->bg_panel);
			cfg(TH->fg_staged);
			ppad("  ", 2);
		}

		const char *ic = "M";
		switch (g_app_state.files[i].status) {
			case FS_STAGED_NEW:
				ic = "A";
				break;
			case FS_STAGED_DEL:
				ic = "D";
				break;
			case FS_RENAMED:
				ic = "R";
				break;
			case FS_COPIED:
				ic = "C";
				break;
			default:
				ic = "M";
				break;
		}
		ppad(ic, 1);
		ppad(" ", 1);
		if (sel) {
			cfg(TH->fg_sel);
			cbg(TH->bg_sel);
		} else {
			cfg(TH->fg_normal);
			cbg(TH->bg_panel);
		}
		char disp[512];
		if (g_app_state.files[i].original_path[0])
			snprintf(disp, sizeof(disp), "%s→%s", g_app_state.files[i].original_path,
					 g_app_state.files[i].path);
		else
			snprintf(disp, sizeof(disp), "%s", g_app_state.files[i].path);
		ppad(disp, iw - 4);
		rst();
		row++;
	}
	if (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_panel);
		for (int i = 0; i < iw; i++) put_cell(row, sx + 1 + i, " ");
		rst();
		row++;
	}

	if (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_header);
		cfg(TH->fg_unstaged);
		g_app_state.cur_bold = true;
		char hdr[64];
		snprintf(hdr, sizeof(hdr), " ✗ Unstaged (%d) ", unstaged_n);
		ppad(hdr, iw);
		rst();
		row++;
	}
	for (int i = 0; i < g_app_state.file_count && row < lim; i++) {
		if (g_app_state.files[i].staged) continue;
		bool sel = (g_app_state.file_sel == i && act);
		at(row, sx + 1);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
			ppad(" »", 2);
		} else {
			cbg(TH->bg_panel);
			cfg(TH->fg_unstaged);
			ppad("  ", 2);
		}

		Color ic_col = TH->fg_unstaged;
		const char *ic = "M";
		switch (g_app_state.files[i].status) {
			case FS_UNTRACKED:
				ic = "?";
				ic_col = TH->fg_untracked;
				break;
			case FS_DELETED:
				ic = "D";
				break;
			case FS_CONFLICT:
				ic = "!";
				ic_col = TH->fg_conflict;
				break;
			default:
				ic = "M";
				break;
		}
		cfg(sel ? TH->fg_sel : ic_col);
		ppad(ic, 1);
		ppad(" ", 1);
		if (sel) {
			cfg(TH->fg_sel);
			cbg(TH->bg_sel);
		} else {
			cfg(TH->fg_normal);
			cbg(TH->bg_panel);
		}
		ppad(g_app_state.files[i].path, iw - 4);
		rst();
		row++;
	}
	while (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_panel);
		for (int i = 0; i < iw; i++) put_cell(row, sx + 1 + i, " ");
		rst();
		row++;
	}
	box_bot(top + h - 1, sx, w, act);
}

void draw_graph(int top, int h) {
	if (h <= 2) return;
	int w = g_app_state.layout_width;
	int sx = g_app_state.sidebar_w + 1;
	bool act = (g_app_state.focus == FOCUS_GRAPH && g_app_state.current_view == VIEW_STATUS);
	box_top(top, sx, w, "Graph", act, NULL);
	box_sides(top, sx, w, h, act);
	box_fill(top, sx, w, h, TH->bg_base);

	int row = top + 1, lim = top + h - 1, iw = w - 2;
	int vis = lim - row;
	if (vis <= 0) {
		box_bot(top + h - 1, sx, w, act);
		return;
	}

	if (g_app_state.commit_sel < g_app_state.commit_scroll)
		g_app_state.commit_scroll = g_app_state.commit_sel;
	if (g_app_state.commit_sel >= g_app_state.commit_scroll + vis)
		g_app_state.commit_scroll = g_app_state.commit_sel - vis + 1;

	g_app_state.graph_rows_count = 0;
	for (int i = g_app_state.commit_scroll; i < g_app_state.commit_count && row < lim; i++, row++) {
		GitCommit *c = &g_app_state.commits[i];
		bool sel = (g_app_state.commit_sel == i);

		if (g_app_state.graph_rows_count < MAX_COMMITS * 17) {
			g_app_state.graph_rows[g_app_state.graph_rows_count].commit_idx = i;
			g_app_state.graph_rows[g_app_state.graph_rows_count].file_idx = -1;
			g_app_state.graph_rows_count++;
		}

		at(row, sx + 1);
		if (sel) {
			cbg(TH->bg_sel);
			g_app_state.cur_bold = true;
		} else
			cbg(TH->bg_base);

		cfg(sel ? TH->fg_sel : TH->fg_dim);
		ppad(c->expanded ? "▾ " : "▸ ", 2);

		char *gp = c->graph;
		int gc = 0;
		int gx = sx + 3;
		while (*gp && gc < GRAPH_COLS) {
			int ci_ = (c->graph_col / 2) % 6;
			at(row, gx + gc);
			if (*gp == '*') {
				cfg(TH->fg_graph[ci_]);
				g_app_state.cur_bold = true;
				put_cell(row, gx + gc, "●");
				rst();
				if (sel) {
					cbg(TH->bg_sel);
					g_app_state.cur_bold = true;
				} else
					cbg(TH->bg_base);
			} else if (*gp == '|') {
				cfg(TH->fg_graph[gc / 2 % 6]);
				put_cell(row, gx + gc, "|");
			} else if (*gp == '/') {
				cfg(TH->fg_graph[1]);
				put_cell(row, gx + gc, "/");
			} else if (*gp == '\\') {
				cfg(TH->fg_graph[2]);
				put_cell(row, gx + gc, "\\");
			} else if (*gp == '-') {
				cfg(TH->fg_graph[2]);
				put_cell(row, gx + gc, "-");
			} else {
				char tmp[2] = {*gp, 0};
				put_cell(row, gx + gc, tmp);
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
			at(row, gx + gc);
			put_cell(row, gx + gc, " ");
			gc++;
		}

		at(row, gx + GRAPH_COLS);
		cfg(sel ? TH->fg_sel : TH->fg_accent1);
		g_app_state.cur_bold = true;
		char hbuf[16];
		snprintf(hbuf, sizeof(hbuf), "%.8s ", c->hash);
		ppad(hbuf, 9);
		rst();
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
		} else
			cbg(TH->bg_base);

		int used = GRAPH_COLS + 9 + 2;
		if (c->refs[0] && iw > used + 8) {
			cfg(sel ? TH->fg_sel : TH->fg_ref_local);
			char rf[32];
			snprintf(rf, sizeof(rf), "(%.14s) ", c->refs);
			ppad(rf, (int)strlen(rf));
			used += (int)strlen(rf);
			if (sel) {
				cfg(TH->fg_sel);
				cbg(TH->bg_sel);
			} else {
				rst();
				cbg(TH->bg_base);
			}
		}
		int sw = iw - used;
		if (sw > 0) {
			cfg(sel ? TH->fg_sel : TH->fg_normal);
			ppad(c->subject, sw);
		}
		rst();

		if (c->expanded && row + 1 < lim) {
			for (int fi = 0; fi < c->file_count && row + 1 < lim; fi++) {
				row++;
				bool fsel = (sel && g_app_state.graph_file_sel == fi);
				if (g_app_state.graph_rows_count < MAX_COMMITS * 17) {
					g_app_state.graph_rows[g_app_state.graph_rows_count].commit_idx = i;
					g_app_state.graph_rows[g_app_state.graph_rows_count].file_idx = fi;
					g_app_state.graph_rows_count++;
				}
				at(row, sx + 1);
				if (fsel) {
					cbg(TH->bg_sel);
					g_app_state.cur_bold = true;
				} else
					cbg(TH->bg_base);

				cfg(TH->fg_dim);
				ppad(fi == c->file_count - 1 ? "  └─ " : "  ├─ ", 5);

				cfg(fsel ? TH->fg_sel : TH->fg_accent2);
				ppad(c->files[fi], iw - 7);
				rst();
			}
		}
	}
	while (row < lim) {
		at(row, sx + 1);
		cbg(TH->bg_base);
		for (int i = 0; i < iw; i++) put_cell(row, sx + 1 + i, " ");
		rst();
		row++;
	}
	box_bot(top + h - 1, sx, w, act);
}
