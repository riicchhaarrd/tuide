#include <stdio.h>
#include <string.h>

#include "../git.h"
#include "../render.h"
#include "../state.h"
#include "../strings.h"
#include "../util.h"
#include "../views.h"

static void draw_commit_bar(int row, int w, int sx) {
	bool focused = g_app_state.commit_bar_focused;
	int iw = w - 2;
	const char *commit_label = UI->commit_button_label;
	const char *amend_label = UI->amend_button_label;
	int icon_w = str_display_width(UI->commit_bar_icon);
	if (icon_w < 1) icon_w = 1;
	int commit_w = imax(COMMIT_BTN_W, str_display_width(commit_label));
	int amend_w = imax(AMEND_BTN_W, str_display_width(amend_label));

	at(row, sx + 1);
	cbg(TH->bg_header);
	cfg(TH->fg_staged);
	g_app_state.cur_bold = true;
	ppad(UI->commit_bar_icon, icon_w);
	rst();

	int btn_total_w = commit_w + amend_w;
	int field_w = iw - icon_w - btn_total_w;
	if (field_w < 4) field_w = 4;

	int len = (int)strlen(g_app_state.commit_msg_buf);
	int disp_start = 0;
	if (g_app_state.commit_msg_cursor >= field_w)
		disp_start = g_app_state.commit_msg_cursor - field_w + 1;

	int field_x = sx + 1 + icon_w;
	at(row, field_x);
	cbg(focused ? TH->bg_tab_act : TH->bg_panel);
	cfg(focused ? TH->fg_bright : TH->fg_dim);
	for (int i = 0; i < field_w; i++) put_cell(row, field_x + i, " ");

	for (int i = disp_start; i < len && (i - disp_start) < field_w; i++) {
		int col = field_x + (i - disp_start);
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
		int col = field_x + (len - disp_start);
		if (col >= field_x && col < field_x + field_w) {
			cbg(TH->fg_accent1);
			cfg(TH->bg_base);
			g_app_state.cur_bold = true;
			put_cell(row, col, " ");
		}
	}
	rst();

	if (!focused && len == 0) {
		at(row, field_x);
		cbg(TH->bg_panel);
		cfg(TH->fg_dim);
		g_app_state.cur_italic = true;
		ppad(UI->commit_placeholder, field_w);
		g_app_state.cur_italic = false;
		rst();
	}

	int btn_x = field_x + field_w;
	at(row, btn_x);
	cbg(TH->fg_staged);
	cfg(TH->bg_base);
	g_app_state.cur_bold = true;
	ppad(commit_label, commit_w);
	rst();
	at(row, btn_x + commit_w);
	cbg(TH->bg_header);
	cfg(TH->fg_accent2);
	g_app_state.cur_bold = true;
	ppad(amend_label, amend_w);
	rst();
}

void draw_changes(int top, int h) {
	if (h <= 2) return;
	int w = g_app_state.layout_width;
	int sx = g_app_state.sidebar_w + 1;
	bool act = (g_app_state.focus == FOCUS_CHANGES && g_app_state.current_view == VIEW_STATUS);
	box_top(top, sx, w, UI->title_changes, act, NULL);
	box_sides(top, sx, w, h, act);
	box_fill(top, sx, w, h, TH->bg_panel);

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

	int vis = lim - row;
	if (vis <= 0) {
		box_bot(top + h - 1, sx, w, act);
		return;
	}

	struct Item {
		bool is_header;
		int file_idx;
		char label[64];
	} items[MAX_FILES + 4];
	int item_count = 0;

	int staged_n = 0, unstaged_n = 0;
	for (int i = 0; i < g_app_state.file_count; i++)
		g_app_state.files[i].staged ? staged_n++ : unstaged_n++;

	if (staged_n > 0) {
		items[item_count].is_header = true;
		snprintf(items[item_count].label, 64, UI->header_staged_fmt, staged_n);
		items[item_count].file_idx = -1;
		item_count++;
		for (int i = 0; i < g_app_state.file_count; i++) {
			if (g_app_state.files[i].staged) {
				items[item_count].is_header = false;
				items[item_count].file_idx = i;
				item_count++;
			}
		}
	}
	if (unstaged_n > 0) {
		if (staged_n > 0) {
			items[item_count].is_header = true;
			items[item_count].label[0] = '\0';
			items[item_count].file_idx = -1;
			item_count++;
		}
		items[item_count].is_header = true;
		snprintf(items[item_count].label, 64, UI->header_unstaged_fmt, unstaged_n);
		items[item_count].file_idx = -1;
		item_count++;
		for (int i = 0; i < g_app_state.file_count; i++) {
			if (!g_app_state.files[i].staged) {
				items[item_count].is_header = false;
				items[item_count].file_idx = i;
				item_count++;
			}
		}
	}

	if (g_app_state.needs_sync) {
		int sel_idx = -1;
		for (int i = 0; i < item_count; i++) {
			if (!items[i].is_header && items[i].file_idx == g_app_state.file_sel) {
				sel_idx = i;
				break;
			}
		}
		if (sel_idx != -1) {
			if (sel_idx < g_app_state.file_scroll) g_app_state.file_scroll = sel_idx;
			if (sel_idx >= g_app_state.file_scroll + vis)
				g_app_state.file_scroll = sel_idx - vis + 1;
		}
	}
	g_app_state.file_scroll = iclamp(g_app_state.file_scroll, 0, imax(0, item_count - vis));

	for (int i = g_app_state.file_scroll; i < item_count && row < lim; i++, row++) {
		if (items[i].is_header) {
			at(row, sx + 1);
			if (items[i].label[0]) {
				cbg(TH->bg_header);
				cfg(items[i].label[1] == 'v' || items[i].label[2] == 'S' ? TH->fg_staged
																		 : TH->fg_unstaged);
				g_app_state.cur_bold = true;
				ppad(items[i].label, iw);
			} else {
				cbg(TH->bg_panel);
				for (int k = 0; k < iw; k++) put_cell(row, sx + 1 + k, " ");
			}
			rst();
			continue;
		}

		int fi = items[i].file_idx;
		GitFile *f = &g_app_state.files[fi];
		bool sel = (g_app_state.file_sel == fi && act);
		at(row, sx + 1);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
			ppad(" »", 2);
		} else {
			cbg(TH->bg_panel);
			cfg(f->staged ? TH->fg_staged : TH->fg_unstaged);
			ppad("  ", 2);
		}

		Color ic_col = f->staged ? TH->fg_staged : TH->fg_unstaged;
		const char *ic = "M";
		switch (f->status) {
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
		char disp[512];
		if (f->original_path[0])
			snprintf(disp, sizeof(disp), "%s→%s", f->original_path, f->path);
		else
			snprintf(disp, sizeof(disp), "%s", f->path);
		ppad(disp, iw - 4);
		rst();
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
	box_top(top, sx, w, UI->title_graph, act, NULL);
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
	int maxsc = imax(0, g_app_state.commit_count - vis);
	g_app_state.commit_scroll = iclamp(g_app_state.commit_scroll, 0, maxsc);

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
