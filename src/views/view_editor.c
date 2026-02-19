#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../editor.h"
#include "../render.h"
#include "../state.h"
#include "../util.h"
#include "../views.h"

static bool is_ed_selected(int y, int x) {
	if (!g_app_state.ed_selecting) return false;
	int s_y = g_app_state.ed_sel_start_y, s_x = g_app_state.ed_sel_start_x;
	int e_y = g_app_state.ed_sel_end_y, e_x = g_app_state.ed_sel_end_x;
	if (s_y > e_y || (s_y == e_y && s_x > e_x)) {
		int t = s_y;
		s_y = e_y;
		e_y = t;
		t = s_x;
		s_x = e_x;
		e_x = t;
	}
	if (y < s_y || y > e_y) return false;
	if (y == s_y && x < s_x) return false;
	if (y == e_y && x > e_x) return false;
	return true;
}

void draw_browser(int top, int h) {
	int w = g_app_state.layout_width;
	int sx = g_app_state.sidebar_w + 1;
	bool act = (g_app_state.focus == FOCUS_BROWSER);
	box_top(top, sx, w, "Files", act, NULL);
	box_sides(top, sx, w, h, act);
	box_fill(top, sx, w, h, TH->bg_panel);
	int row = top + 1, lim = top + h - 1, vis = lim - row;
	if (g_app_state.needs_sync) {
		if (g_app_state.browser_sel < g_app_state.browser_scroll)
			g_app_state.browser_scroll = g_app_state.browser_sel;
		if (g_app_state.browser_sel >= g_app_state.browser_scroll + vis)
			g_app_state.browser_scroll = g_app_state.browser_sel - vis + 1;
	}
	int maxsc = imax(0, g_app_state.browser_count - vis);
	g_app_state.browser_scroll = iclamp(g_app_state.browser_scroll, 0, maxsc);

	for (int i = g_app_state.browser_scroll; i < g_app_state.browser_count && row < lim;
		 i++, row++) {
		bool sel = (i == g_app_state.browser_sel && act);
		at(row, sx + 1);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
			ppad(" »", 2);
		} else {
			cbg(TH->bg_panel);
			ppad("  ", 2);
		}

		if (g_app_state.browser_files[i].is_dir) {
			cfg(sel ? TH->fg_sel : TH->fg_accent1);
			ppad("📁 ", 3);
		} else {
			cfg(sel ? TH->fg_sel : TH->fg_normal);
			ppad("📄 ", 3);
		}
		ppad(g_app_state.browser_files[i].path, w - 7);
		rst();
	}
	box_bot(top + h - 1, sx, w, act);
}

void draw_editor(int top, int render_x, int render_width, int h) {
	if (g_app_state.tab_count == 0) {
		box_top(top, render_x, render_width, "Editor", (g_app_state.focus == FOCUS_EDITOR), NULL);
		box_sides(top, render_x, render_width, h, (g_app_state.focus == FOCUS_EDITOR));
		box_fill(top, render_x, render_width, h, TH->bg_base);
		at(top + h / 2, render_x + render_width / 2 - 10);
		cfg(TH->fg_dim);
		ppad("(no files open)", 15);
		box_bot(top + h - 1, render_x, render_width, (g_app_state.focus == FOCUS_EDITOR));
		return;
	}
	Tab *t = &g_app_state.tabs[g_app_state.tab_current];
	Editor *ed = &t->ed;
	bool act = (g_app_state.focus == FOCUS_EDITOR);

	char title[512];
	snprintf(title, sizeof(title), " %s ", t->path);
	box_top(top, render_x, render_width, "Editor", act, " [Save] ");
	box_sides(top, render_x, render_width, h, act);
	box_fill(top, render_x, render_width, h, TH->bg_base);

	at(top + 1, render_x + 1);
	cbg(TH->bg_panel);
	for (int i = 0; i < render_width - 2; i++) put_cell(top + 1, render_x + 1 + i, " ");

	int cur_tab_x = render_x + 1;
	for (int i = 0; i < g_app_state.tab_count; i++) {
		bool cur = (i == g_app_state.tab_current);
		at(top + 1, cur_tab_x);
		if (cur) {
			cbg(TH->bg_base);
			cfg(TH->fg_bright);
			g_app_state.cur_bold = true;
		} else {
			cbg(TH->bg_panel);
			cfg(TH->fg_dim);
		}

		char *fname = strrchr(g_app_state.tabs[i].path, '/');
		fname = fname ? fname + 1 : g_app_state.tabs[i].path;

		char tbuf[128];
		int tab_w = snprintf(tbuf, sizeof(tbuf), "  %s%s  ", fname,
							 g_app_state.tabs[i].ed.modified ? "*" : "");
		ppad(tbuf, tab_w);

		g_app_state.ed_tab_x[i] = cur_tab_x;
		if (i == g_app_state.tab_count - 1) g_app_state.ed_tab_x[i + 1] = cur_tab_x + tab_w;

		cur_tab_x += tab_w;
		if (!cur) {
			cfg(TH->fg_dim);
			put_cell(top + 1, cur_tab_x - 1, "│");
		}
		rst();
	}

	int row = top + 2, lim = top + h - 1, vis = lim - row;
	if (ed->needs_sync) {
		if (ed->cursor_row < ed->scroll_row) ed->scroll_row = ed->cursor_row;
		if (ed->cursor_row >= ed->scroll_row + vis) ed->scroll_row = ed->cursor_row - vis + 1;

		int vis_w = render_width - 10;
		if (ed->cursor_col < ed->scroll_col) ed->scroll_col = ed->cursor_col;
		if (ed->cursor_col >= ed->scroll_col + vis_w) ed->scroll_col = ed->cursor_col - vis_w + 1;
		ed->needs_sync = false;
	}

	int vis_w = render_width - 10;
	for (int i = ed->scroll_row; i < ed->line_count && row < lim - 1; i++, row++) {
		at(row, render_x + 1);
		cfg(TH->fg_linenum);
		char lno[16];
		snprintf(lno, sizeof(lno), "%4d ", i + 1);
		ppad(lno, 5);
		cfg(TH->fg_dim);
		ppad("│", 1);

		char *full_line = ed->lines[i];
		int full_len = (int)strlen(full_line);

		char line_buf[1024];
		int start = ed->scroll_col;
		int len = 0;
		if (start < full_len) {
			len = imin(full_len - start, 1023);
			len = imin(len, vis_w);
			memcpy(line_buf, full_line + start, len);
		}
		line_buf[len] = '\0';

		const char *ext = strrchr(t->path, '.');
		int cur_c = render_x + 7;
		int j = 0;
		while (j < len && cur_c < render_x + render_width - 3) {
			// int real_j = j + start;
			char c = line_buf[j];

			if ((c == '/' && j + 1 < len && line_buf[j + 1] == '/') ||
				(c == '#' && (ext && strcmp(ext, ".py") == 0))) {
				cfg(TH->fg_accent3);
				while (j < len && cur_c < render_x + render_width - 3) {
					if (is_ed_selected(i, j + start))
						cbg(TH->bg_sel);
					else
						cbg(TH->bg_base);
					char cs[2] = {line_buf[j++], 0};
					put_cell(row, cur_c++, cs);
				}
				break;
			}

			if (c == '"' || c == '\'') {
				char quote = c;
				cfg(TH->fg_unstaged);
				if (is_ed_selected(i, j + start))
					cbg(TH->bg_sel);
				else
					cbg(TH->bg_base);
				char cs[2] = {line_buf[j++], 0};
				put_cell(row, cur_c++, cs);
				while (j < len && cur_c < render_x + render_width - 3) {
					if (is_ed_selected(i, j + start))
						cbg(TH->bg_sel);
					else
						cbg(TH->bg_base);
					char sc = line_buf[j++];
					char scs[2] = {sc, 0};
					put_cell(row, cur_c++, scs);
					if (sc == quote && (j < 2 || line_buf[j - 2] != '\\')) break;
				}
				continue;
			}

			if (isalnum((unsigned char)c) || c == '_' || c == '#') {
				char buf[256];
				int bi = 0;
				int tok_start = j;
				while (j < len &&
					   (isalnum((unsigned char)line_buf[j]) || line_buf[j] == '_' ||
						line_buf[j] == '#') &&
					   bi < 255) {
					buf[bi++] = line_buf[j++];
				}
				buf[bi] = '\0';
				Color tcol = get_token_color(buf, false, ext);
				for (int k = 0; k < bi; k++) {
					if (cur_c >= render_x + render_width - 3) break;
					if (is_ed_selected(i, tok_start + k + start))
						cbg(TH->bg_sel);
					else
						cbg(TH->bg_base);
					cfg(tcol);
					char tcs[2] = {buf[k], 0};
					put_cell(row, cur_c++, tcs);
				}
				continue;
			}

			if (is_ed_selected(i, j + start))
				cbg(TH->bg_sel);
			else
				cbg(TH->bg_base);
			cfg(ispunct((unsigned char)c) ? TH->fg_dim : TH->fg_normal);
			char pcs[2] = {line_buf[j++], 0};
			put_cell(row, cur_c++, pcs);
		}

		if (i == ed->cursor_row) {
			int cursor_screen_x = render_x + 7 + ed->cursor_col - ed->scroll_col;
			if (cursor_screen_x >= render_x + 7 && cursor_screen_x < render_x + render_width - 3) {
				at(row, cursor_screen_x);
				char c = (ed->lines[i] && ed->lines[i][ed->cursor_col])
							 ? ed->lines[i][ed->cursor_col]
							 : ' ';
				char cs[2] = {c, 0};
				if (act) {
					cbg(TH->fg_accent1);
					cfg(TH->bg_base);
					g_app_state.cur_bold = true;
				} else {
					g_app_state.cur_under = true;
					cfg(TH->fg_dim);
				}
				put_cell(row, cursor_screen_x, cs);
				g_app_state.cur_bold = false;
				g_app_state.cur_under = false;
			}
		}
		rst();
	}
	while (row < lim - 1) {
		at(row, render_x + 1);
		cbg(TH->bg_base);
		for (int i = 0; i < render_width - 2; i++) put_cell(row, render_x + 1 + i, " ");
		row++;
	}

	/* Minimap */
	int vis_rows = h - 4;
	if (ed->line_count > vis_rows && vis_rows > 2) {
		int bh = imax(1, (vis_rows * vis_rows) / ed->line_count);
		int maxsc_ed = imax(1, ed->line_count - vis_rows);
		int bpos = (ed->scroll_row * (vis_rows - bh)) / maxsc_ed;
		g_app_state.editor_scrollbar_x = render_x + render_width - 3;
		g_app_state.editor_scrollbar_y = top + 2;
		g_app_state.editor_scrollbar_height = vis_rows;
		g_app_state.editor_scrollbar_total = ed->line_count;
		g_app_state.editor_scrollbar_visible = vis_rows;

		for (int r = 0; r < vis_rows; r++) {
			bool thumb = (r >= bpos && r < bpos + bh);
			for (int sw = 0; sw < 3; sw++) {
				at(top + 2 + r, render_x + render_width - 1 - sw);
				if (thumb) {
					if (g_app_state.dragging_ed_sc)
						cfg(TH->fg_sel);
					else if (g_app_state.last_mx >= render_x + render_width - 3)
						cfg(TH->fg_accent1);
					else
						cfg(TH->fg_accent2);
					put_cell(top + 2 + r, render_x + render_width - 1 - sw, "█");
				} else {
					cfg(TH->fg_dim);
					put_cell(top + 2 + r, render_x + render_width - 1 - sw, sw == 0 ? "│" : " ");
				}
			}
		}
		rst();
	} else {
		g_app_state.editor_scrollbar_height = 0;
	}

	at(top + h - 2, render_x + 1);
	cbg(TH->bg_panel);
	for (int i = 0; i < render_width - 2; i++) put_cell(top + h - 2, render_x + 1 + i, " ");

	at(top + h - 2, render_x + 1);
	if (act) {
		cfg(TH->fg_bright);
		cbg(TH->bg_panel);
		g_app_state.cur_bold = true;
	} else
		cfg(TH->fg_dim);
	char sbuf[256];
	snprintf(sbuf, sizeof(sbuf), " Ln %d, Col %d  ", ed->cursor_row + 1, ed->cursor_col + 1);
	ppad(sbuf, (int)strlen(sbuf));
	at(top + h - 2, render_x + render_width - 20);
	if (!act)
		cfg(TH->fg_dim);
	else {
		cfg(TH->fg_dim);
		cbg(TH->bg_panel);
		g_app_state.cur_bold = false;
	}
	snprintf(sbuf, sizeof(sbuf), " [UTF-8]  %s ", get_lang_name(t->path));
	ppad(sbuf, (int)strlen(sbuf));
	rst();

	box_bot(top + h - 1, render_x, render_width, act);
}
