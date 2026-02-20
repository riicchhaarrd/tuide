#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "ui.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "git.h"
#include "render.h"
#include "strings.h"
#include "views.h"

void set_status(bool err, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_app_state.status_msg, sizeof(g_app_state.status_msg), fmt, ap);
	va_end(ap);
	g_app_state.status_msg_time = time(NULL);
	g_app_state.status_is_err = err;
}

void layout(void) {
	g_app_state.sidebar_w = 10;
	if (g_app_state.lw_custom > 0)
		g_app_state.layout_width = iclamp(g_app_state.lw_custom, 20, g_app_state.cols - 20);
	else
		g_app_state.layout_width = imax(26, imin(48, g_app_state.cols * 32 / 100));

	g_app_state.render_x = g_app_state.sidebar_w + g_app_state.layout_width + 1;
	g_app_state.render_width = g_app_state.cols - g_app_state.render_x + 1;
	int ch = g_app_state.rows - 2;

	if (g_app_state.lh_chg_custom > 0)
		g_app_state.layout_height_changes = iclamp(g_app_state.lh_chg_custom, 4, ch - 4);
	else
		g_app_state.layout_height_changes = imax(5, ch * 58 / 100);

	g_app_state.layout_height_graph = ch - g_app_state.layout_height_changes + 1;
	if (g_app_state.layout_height_graph < 4) {
		g_app_state.layout_height_graph = 4;
		g_app_state.layout_height_changes = ch - g_app_state.layout_height_graph + 1;
	}

	int drw = (g_app_state.current_view == VIEW_LOG) ? g_app_state.cols : g_app_state.render_width;
	if (g_app_state.diff_split_custom > 0)
		g_app_state.diff_split = iclamp(g_app_state.diff_split_custom, 10, drw - 10);
	else
		g_app_state.diff_split = drw / 2;

	if (g_app_state.lh_log_custom > 0)
		g_app_state.layout_height_log = iclamp(g_app_state.lh_log_custom, 4, ch - 4);
	else
		g_app_state.layout_height_log = ch * 55 / 100;
	g_app_state.diff_height_log = ch - g_app_state.layout_height_log + 1;
	if (g_app_state.diff_height_log < 4) {
		g_app_state.diff_height_log = 4;
		g_app_state.layout_height_log = ch - g_app_state.diff_height_log + 1;
	}
}

void draw(void) {
	layout();
	buf_clear(&g_app_state.back);
	rst();
	draw_tabbar();
	draw_sidebar();

	int ct = 2, ch = g_app_state.rows - 2;
	switch (g_app_state.current_view) {
		case VIEW_STATUS:
			if (g_app_state.browser_active) {
				draw_browser(ct, ch);
			} else {
				if (g_app_state.layout_height_changes > 2)
					draw_changes(ct, g_app_state.layout_height_changes);
				if (g_app_state.layout_height_graph > 2)
					draw_graph(ct + g_app_state.layout_height_changes,
							   g_app_state.layout_height_graph);
			}

			if (g_app_state.editor_active) {
				draw_editor(ct, g_app_state.render_x, g_app_state.render_width, ch);
			} else {
				draw_diff(ct, g_app_state.render_x, g_app_state.render_width, ch);
			}
			break;
		case VIEW_LOG: {
			draw_log(ct, g_app_state.layout_height_log);
			if (g_app_state.editor_active)
				draw_editor(ct + g_app_state.layout_height_log, 1, g_app_state.cols,
							g_app_state.diff_height_log);
			else
				draw_diff(ct + g_app_state.layout_height_log, 1, g_app_state.cols,
						  g_app_state.diff_height_log);
			break;
		}
		case VIEW_BRANCHES:
			draw_branches(ct, ch);
			break;
		case VIEW_STASH:
			draw_stash(ct, ch);
			break;
		case VIEW_EDITOR:
			draw_browser(ct, ch);
			draw_editor(ct, g_app_state.render_x, g_app_state.render_width, ch);
			break;
		case VIEW_HELP:
			draw_help(ct, ch);
			break;
		default:
			break;
	}
	draw_statusbar();
	draw_cli();
	draw_prompt_overlay();
	draw_dividers();
	draw_menu();

	g_app_state.needs_sync = false;
	draw_flush();
}

void draw_tabbar(void) {
	at(1, 1);
	cbg(TH->bg_tab_inact);
	cfg(TH->fg_accent2);
	g_app_state.cur_bold = true;
	int title_w = str_display_width(UI->tabbar_title);
	int title_max = g_app_state.cols - 1;
	if (title_max < 1) title_max = 1;
	title_w = iclamp(title_w, 1, title_max);
	ppad(UI->tabbar_title, title_w);
	rst();
	struct {
		const char *n, *k;
		View v;
	} tabs[] = {{UI->tab_changes, "1", VIEW_STATUS},
				{UI->tab_log, "2", VIEW_LOG},
				{UI->tab_branches, "3", VIEW_BRANCHES},
				{UI->tab_stash, "4", VIEW_STASH},
				{UI->tab_help, "?", VIEW_HELP}};
	int cur_c = title_w + 1;
	for (int i = 0; i < 5; i++) {
		g_app_state.tab_x[i] = cur_c;
		bool act = (tabs[i].v == g_app_state.current_view);
		at(1, cur_c);
		if (act) {
			cbg(TH->bg_tab_act);
			cfg(TH->fg_bright);
			g_app_state.cur_bold = true;
		} else {
			cbg(TH->bg_tab_inact);
			cfg(TH->fg_dim);
		}
		char tbuf[64];
		snprintf(tbuf, sizeof(tbuf), " %s[%s] ", tabs[i].n, tabs[i].k);
		int tab_w = str_display_width(tbuf);
		ppad(tbuf, tab_w);
		rst();
		cur_c += tab_w;
		at(1, cur_c);
		cbg(TH->bg_tab_inact);
		cfg(TH->fg_dim);
		put_cell(1, cur_c, "│");
		cur_c++;
	}
	g_app_state.tab_x[5] = cur_c;

	cbg(TH->bg_tab_inact);
	cfg(TH->fg_accent3);
	char rbuf[160];
	snprintf(rbuf, sizeof(rbuf), " ◈ %s  ⎇ %s ", TH->name, g_app_state.branch_name);
	int rlen = str_display_width(rbuf);
	int pad = g_app_state.cols - cur_c - rlen;
	if (pad < 0) pad = 0;
	for (int i = 0; i < pad; i++) put_cell(1, cur_c + i, " ");
	at(1, g_app_state.cols - rlen + 1);
	cfg(TH->fg_accent3);
	ppad("◈ ", 2);
	ppad(TH->name, (int)strlen(TH->name));
	ppad("  ", 2);
	cfg(TH->fg_ref_local);
	g_app_state.cur_bold = true;
	ppad("⎇ ", 2);
	ppad(g_app_state.branch_name, (int)strlen(g_app_state.branch_name));
	rst();
}

void draw_statusbar(void) {
	at(g_app_state.rows, 1);
	cbg(TH->bg_panel);

	cfg(TH->fg_bright);
	cbg(TH->fg_accent1);
	g_app_state.cur_bold = true;
	const char *fstr = UI->focus_unknown;
	switch (g_app_state.focus) {
		case FOCUS_CHANGES:
			fstr = UI->focus_changes;
			break;
		case FOCUS_GRAPH:
			fstr = UI->focus_graph;
			break;
		case FOCUS_DIFF:
			fstr = UI->focus_diff;
			break;
		case FOCUS_BROWSER:
			fstr = UI->focus_browser;
			break;
		case FOCUS_EDITOR:
			fstr = UI->focus_editor;
			break;
		case FOCUS_CLI:
			fstr = UI->focus_cli;
			break;
		default:
			fstr = UI->focus_unknown;
			break;
	}
	ppad(fstr, 10);
	rst();
	cbg(TH->bg_panel);

	const char *hint = UI->hint_empty;
	if (g_app_state.current_view == VIEW_STATUS) {
		if (g_app_state.focus == FOCUS_CHANGES)
			hint = UI->hint_status_changes;
		else if (g_app_state.focus == FOCUS_GRAPH)
			hint = UI->hint_status_graph;
		else if (g_app_state.focus == FOCUS_BROWSER)
			hint = UI->hint_status_browser;
		else if (g_app_state.focus == FOCUS_EDITOR)
			hint = UI->hint_status_editor;
		else
			hint = UI->hint_status_diff;
	} else if (g_app_state.current_view == VIEW_LOG)
		hint = UI->hint_log;
	else if (g_app_state.current_view == VIEW_BRANCHES)
		hint = UI->hint_branches;
	else if (g_app_state.current_view == VIEW_STASH)
		hint = UI->hint_stash;
	else if (g_app_state.current_view == VIEW_EDITOR) {
		if (g_app_state.focus == FOCUS_BROWSER)
			hint = UI->hint_editor_browser;
		else
			hint = UI->hint_editor_editor;
	} else if (g_app_state.current_view == VIEW_HELP)
		hint = UI->hint_help;

	cfg(TH->fg_dim);
	ppad(" ", 1);
	ppad(hint, g_app_state.cols - 2);

	if (g_app_state.status_msg[0] && (time(NULL) - g_app_state.status_msg_time) < 5) {
		int mlen = (int)strlen(g_app_state.status_msg) + 2;
		at(g_app_state.rows, g_app_state.cols - mlen);
		if (g_app_state.status_is_err)
			cfg(TH->fg_err);
		else
			cfg(TH->fg_ok);
		g_app_state.cur_bold = true;
		char sbuf[258];
		snprintf(sbuf, sizeof(sbuf), " %s ", g_app_state.status_msg);
		ppad(sbuf, mlen);
	}
	rst();
}

void draw_cli(void) {
	int row = g_app_state.rows - 1;
	at(row, 1);
	bool focused = (g_app_state.focus == FOCUS_CLI);
	if (focused) {
		cbg(TH->bg_sel);
		cfg(TH->fg_sel);
		g_app_state.cur_bold = true;
	} else {
		cbg(TH->bg_panel);
		cfg(TH->fg_dim);
	}
	int prompt_w = str_display_width(UI->cli_prompt);
	if (prompt_w < 1) prompt_w = 1;
	ppad(UI->cli_prompt, prompt_w);
	if (focused) {
		cfg(TH->fg_bright);
	} else {
		cfg(TH->fg_normal);
	}

	int len = (int)strlen(g_app_state.cli_buf);
	int input_x = 1 + prompt_w;
	at(row, input_x);
	ppad(g_app_state.cli_buf, g_app_state.cli_cursor);

	if (focused) {
		at(row, input_x + g_app_state.cli_cursor);
		cbg(TH->fg_accent1);
		cfg(TH->bg_base);
		char c = g_app_state.cli_buf[g_app_state.cli_cursor];
		char cs[2] = {c ? c : ' ', 0};
		put_cell(row, input_x + g_app_state.cli_cursor, cs);
		rst();
		if (focused)
			cbg(TH->bg_sel);
		else
			cbg(TH->bg_panel);
		cfg(TH->fg_bright);
	}

	at(row, input_x + g_app_state.cli_cursor +
				 (g_app_state.cli_buf[g_app_state.cli_cursor] ? 1 : 0));
	ppad(g_app_state.cli_buf + g_app_state.cli_cursor +
			 (g_app_state.cli_buf[g_app_state.cli_cursor] ? 1 : 0),
		 len - g_app_state.cli_cursor - (g_app_state.cli_buf[g_app_state.cli_cursor] ? 1 : 0));

	int used = prompt_w + len;
	at(row, 1 + used);
	for (int i = used; i < g_app_state.cols; i++) put_cell(row, 1 + i, " ");
	rst();
}

void draw_prompt_overlay(void) {
	if (!g_app_state.in_prompt) return;
	at(g_app_state.rows - 1, 1);
	cbg(TH->bg_tab_act);
	cfg(TH->fg_accent2);
	g_app_state.cur_bold = true;
	char lbuf[132];
	snprintf(lbuf, sizeof(lbuf), " %s ", g_app_state.prompt_label);
	ppad(lbuf, (int)strlen(lbuf));
	rst();
	cbg(TH->bg_panel);
	cfg(TH->fg_bright);
	ppad(" ", 1);

	int label_len = (int)strlen(g_app_state.prompt_label) + 3;
	at(g_app_state.rows - 1, label_len);
	if (g_app_state.prompt_obscure) {
		for (int i = 0; i < g_app_state.prompt_cursor; i++)
			put_cell(g_app_state.rows - 1, label_len + i, "*");
	} else {
		ppad(g_app_state.prompt_buf, g_app_state.prompt_cursor);
	}

	at(g_app_state.rows - 1, label_len + g_app_state.prompt_cursor);
	cbg(TH->fg_accent1);
	cfg(TH->bg_base);
	char nc = g_app_state.prompt_buf[g_app_state.prompt_cursor];
	char ncs[2] = {nc ? nc : ' ', 0};
	put_cell(g_app_state.rows - 1, label_len + g_app_state.prompt_cursor, ncs);
	rst();

	cbg(TH->bg_panel);
	cfg(TH->fg_bright);
	if (!g_app_state.prompt_obscure) {
		at(g_app_state.rows - 1, label_len + g_app_state.prompt_cursor + 1);
		ppad(g_app_state.prompt_buf + g_app_state.prompt_cursor + (nc ? 1 : 0),
			 (int)strlen(g_app_state.prompt_buf) - g_app_state.prompt_cursor - (nc ? 1 : 0));
	}

	int used = (int)strlen(g_app_state.prompt_label) + 3 + (int)strlen(g_app_state.prompt_buf) + 2;
	at(g_app_state.rows - 1, used);
	for (int i = used; i < g_app_state.cols; i++) put_cell(g_app_state.rows - 1, 1 + i, " ");
	rst();
}

void draw_sidebar(void) {
	int h = g_app_state.rows - 2;
	int top = 2;
	cfg(TH->fg_dim);
	cbg(TH->bg_panel);
	for (int r = top; r < top + h; r++) {
		at(r, 1);
		ppad(" ", g_app_state.sidebar_w);
	}

	at(top + 1, 1);
	bool explorer_act = g_app_state.browser_active;
	if (explorer_act) {
		cfg(TH->bg_panel);
		cbg(TH->fg_accent1);
		g_app_state.cur_bold = true;
		ppad(UI->sidebar_browser_active, g_app_state.sidebar_w);
	} else {
		cfg(TH->fg_dim);
		cbg(TH->bg_panel);
		ppad(UI->sidebar_browser_inactive, g_app_state.sidebar_w);
	}

	at(top + 3, 1);
	bool git_act = !g_app_state.browser_active;
	if (git_act) {
		cfg(TH->bg_panel);
		cbg(TH->fg_accent1);
		g_app_state.cur_bold = true;
		ppad(UI->sidebar_git_active, g_app_state.sidebar_w);
	} else {
		cfg(TH->fg_dim);
		cbg(TH->bg_panel);
		ppad(UI->sidebar_git_inactive, g_app_state.sidebar_w);
	}

	rst();
	cfg(TH->fg_dim);
	for (int r = top; r < top + h; r++) {
		at(r, g_app_state.sidebar_w);
		put_cell(r, g_app_state.sidebar_w, "│");
	}
	rst();
}

void draw_dividers(void) {
	int ct = 2;
	if (g_app_state.current_view == VIEW_STATUS) {
		int vx = g_app_state.sidebar_w + g_app_state.layout_width;
		if (vx >= 1 && vx <= g_app_state.cols) {
			for (int r = ct; r < g_app_state.rows - 1; r++) {
				at(r, vx);
				bool hover = (g_app_state.last_mx == vx && g_app_state.last_my == r);
				if (g_app_state.dragging_v || hover) {
					cfg(TH->fg_accent1);
					g_app_state.cur_bold = true;
					put_cell(r, vx, "┃");
				} else {
					cfg(TH->fg_dim);
					put_cell(r, vx, "│");
				}
				rst();
			}
		}
		if (g_app_state.browser_active) return;
		int hr = ct + g_app_state.layout_height_changes - 1;
		int hstart = g_app_state.sidebar_w + 1;
		if (hr >= 1 && hr < g_app_state.rows) {
			for (int c = hstart; c < vx; c++) {
				at(hr, c);
				bool hover = (g_app_state.last_my == hr && g_app_state.last_mx == c);
				if (g_app_state.dragging_h || hover) {
					cfg(TH->fg_accent1);
					g_app_state.cur_bold = true;
					put_cell(hr, c, "━");
				} else {
					cfg(TH->fg_dim);
					put_cell(hr, c, "─");
				}
				rst();
			}
		}
	} else if (g_app_state.current_view == VIEW_LOG) {
		int hr = ct + g_app_state.layout_height_log - 1;
		if (hr >= 1 && hr < g_app_state.rows) {
			for (int c = 1; c < g_app_state.cols; c++) {
				at(hr, c);
				bool hover = (g_app_state.last_my == hr && g_app_state.last_mx == c);
				if (g_app_state.dragging_h_log || hover) {
					cfg(TH->fg_accent1);
					g_app_state.cur_bold = true;
					put_cell(hr, c, "━");
				} else {
					cfg(TH->fg_dim);
					put_cell(hr, c, "─");
				}
				rst();
			}
		}
	}
}

void menu_reset(int x, int y) {
	g_app_state.menu_active = true;
	g_app_state.menu_x = x;
	g_app_state.menu_y = y;
	g_app_state.menu_item_count = 0;
	g_app_state.menu_w = 20;
	g_app_state.menu_h = 2;
}
void menu_add_item(const char *label, void (*action)(void)) {
	if (g_app_state.menu_item_count >= 12) return;
	snprintf(g_app_state.menu_items[g_app_state.menu_item_count], 32, "%s", label);
	g_app_state.menu_actions[g_app_state.menu_item_count] = action;
	g_app_state.menu_item_count++;
	g_app_state.menu_h++;
	int l = (int)strlen(label) + 4;
	if (l > g_app_state.menu_w) g_app_state.menu_w = l;
}

void draw_menu(void) {
	if (!g_app_state.menu_active) return;
	int x = g_app_state.menu_x, y = g_app_state.menu_y, w = g_app_state.menu_w,
		h = g_app_state.menu_h;
	if (x + w > g_app_state.cols) x = g_app_state.cols - w;
	if (y + h > g_app_state.rows) y = g_app_state.rows - h;
	if (x < 1) x = 1;
	if (y < 1) y = 1;
	g_app_state.menu_x = x;
	g_app_state.menu_y = y;

	box_top(y, x, w, UI->menu_title, true, NULL);
	box_sides(y, x, w, h, true);
	box_fill(y, x, w, h, TH->bg_panel);
	box_bot(y + h - 1, x, w, true);
	for (int i = 0; i < g_app_state.menu_item_count; i++) {
		at(y + 1 + i, x + 1);
		bool hover = (g_app_state.last_my == y + 1 + i && g_app_state.last_mx > x &&
					  g_app_state.last_mx < x + w - 1);
		if (hover) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
		} else {
			cfg(TH->fg_normal);
		}
		char mitem[64];
		snprintf(mitem, sizeof(mitem), " %-*s ", w - 4, g_app_state.menu_items[i]);
		ppad(mitem, w - 2);
		rst();
	}
}

void prompt_start(const char *label, void (*cb)(const char *), bool obs) {
	g_app_state.in_prompt = true;
	snprintf(g_app_state.prompt_label, sizeof(g_app_state.prompt_label), "%s", label);
	g_app_state.prompt_buf[0] = '\0';
	g_app_state.prompt_cursor = 0;
	g_app_state.prompt_cb = cb;
	g_app_state.prompt_obscure = obs;
}

static void prompt_ins(char c) {
	int len = (int)strlen(g_app_state.prompt_buf);
	if (len + 1 < INPUT_MAX) {
		memmove(&g_app_state.prompt_buf[g_app_state.prompt_cursor + 1],
				&g_app_state.prompt_buf[g_app_state.prompt_cursor],
				len - g_app_state.prompt_cursor + 1);
		g_app_state.prompt_buf[g_app_state.prompt_cursor++] = c;
	}
}
static void prompt_bksp(void) {
	if (!g_app_state.prompt_cursor) return;
	int len = (int)strlen(g_app_state.prompt_buf);
	memmove(&g_app_state.prompt_buf[g_app_state.prompt_cursor - 1],
			&g_app_state.prompt_buf[g_app_state.prompt_cursor],
			len - g_app_state.prompt_cursor + 1);
	g_app_state.prompt_cursor--;
}
static void prompt_del(void) {
	int len = (int)strlen(g_app_state.prompt_buf);
	if (g_app_state.prompt_cursor >= len) return;
	memmove(&g_app_state.prompt_buf[g_app_state.prompt_cursor],
			&g_app_state.prompt_buf[g_app_state.prompt_cursor + 1],
			len - g_app_state.prompt_cursor);
}
static void prompt_confirm(void) {
	g_app_state.in_prompt = false;
	void (*cb)(const char *) = g_app_state.prompt_cb;
	char copy[INPUT_MAX];
	snprintf(copy, sizeof(copy), "%s", g_app_state.prompt_buf);
	g_app_state.prompt_buf[0] = '\0';
	g_app_state.prompt_cursor = 0;
	g_app_state.prompt_cb = NULL;
	if (cb) cb(copy);
}
static void prompt_cancel(void) {
	g_app_state.in_prompt = false;
	g_app_state.prompt_buf[0] = '\0';
	g_app_state.prompt_cursor = 0;
	g_app_state.prompt_cb = NULL;
	OK("%s", UI->msg_cancelled);
}

void handle_prompt_key(Key k) {
	switch (k.type) {
		case KEY_ENTER:
			prompt_confirm();
			break;
		case KEY_ESC:
			prompt_cancel();
			break;
		case KEY_BACKSPACE:
			prompt_bksp();
			break;
		case KEY_DEL:
			prompt_del();
			break;
		case KEY_LEFT:
			if (g_app_state.prompt_cursor > 0) g_app_state.prompt_cursor--;
			break;
		case KEY_RIGHT:
			if (g_app_state.prompt_buf[g_app_state.prompt_cursor]) g_app_state.prompt_cursor++;
			break;
		case KEY_HOME:
		case KEY_CTRL_A:
			g_app_state.prompt_cursor = 0;
			break;
		case KEY_END:
			g_app_state.prompt_cursor = (int)strlen(g_app_state.prompt_buf);
			break;
		case KEY_CTRL_U:
			g_app_state.prompt_buf[0] = '\0';
			g_app_state.prompt_cursor = 0;
			break;
		case KEY_CTRL_W: {
			while (g_app_state.prompt_cursor > 0 &&
				   g_app_state.prompt_buf[g_app_state.prompt_cursor - 1] == ' ')
				prompt_bksp();
			while (g_app_state.prompt_cursor > 0 &&
				   g_app_state.prompt_buf[g_app_state.prompt_cursor - 1] != ' ')
				prompt_bksp();
			break;
		}
		case KEY_CHAR:
			prompt_ins(k.ch);
			break;
		default:
			break;
	}
}

void handle_cli_key(Key k) {
	int len = (int)strlen(g_app_state.cli_buf);
	if (k.type == KEY_ESC) {
		g_app_state.focus = FOCUS_CHANGES;
		return;
	}
	if (k.type == KEY_ENTER) {
		if (g_app_state.cli_buf[0]) {
			char cmd[INPUT_MAX];
			snprintf(cmd, sizeof(cmd), "%s", g_app_state.cli_buf);
			OK(UI->msg_cli_executing_fmt, cmd);
			/* Draw not strictly needed if loop continues, but OK */
			int r = system(cmd);
			if (r == 0)
				OK(UI->msg_cli_success_fmt, cmd);
			else
				ERR(UI->msg_cli_failed_fmt, r, cmd);
			g_app_state.cli_buf[0] = '\0';
			g_app_state.cli_cursor = 0;
			reload_all();
		}
		g_app_state.focus = FOCUS_CHANGES;
		return;
	}
	if (k.type == KEY_BACKSPACE) {
		if (g_app_state.cli_cursor > 0) {
			memmove(&g_app_state.cli_buf[g_app_state.cli_cursor - 1],
					&g_app_state.cli_buf[g_app_state.cli_cursor], len - g_app_state.cli_cursor + 1);
			g_app_state.cli_cursor--;
		}
	} else if (k.type == KEY_DEL) {
		if (g_app_state.cli_cursor < len) {
			memmove(&g_app_state.cli_buf[g_app_state.cli_cursor],
					&g_app_state.cli_buf[g_app_state.cli_cursor + 1], len - g_app_state.cli_cursor);
		}
	} else if (k.type == KEY_LEFT) {
		if (g_app_state.cli_cursor > 0) g_app_state.cli_cursor--;
	} else if (k.type == KEY_RIGHT) {
		if (g_app_state.cli_cursor < len) g_app_state.cli_cursor++;
	} else if (k.type == KEY_HOME || k.type == KEY_CTRL_A) {
		g_app_state.cli_cursor = 0;
	} else if (k.type == KEY_END || k.type == KEY_CTRL_E) {
		g_app_state.cli_cursor = len;
	} else if (k.type == KEY_CTRL_U) {
		memmove(g_app_state.cli_buf, &g_app_state.cli_buf[g_app_state.cli_cursor],
				len - g_app_state.cli_cursor + 1);
		g_app_state.cli_cursor = 0;
	} else if (k.type == KEY_CTRL_K) {
		g_app_state.cli_buf[g_app_state.cli_cursor] = '\0';
	} else if (k.type == KEY_CTRL_W) {
		if (g_app_state.cli_cursor > 0) {
			int pos = g_app_state.cli_cursor - 1;
			while (pos > 0 && g_app_state.cli_buf[pos - 1] == ' ') pos--;
			while (pos > 0 && g_app_state.cli_buf[pos - 1] != ' ') pos--;
			memmove(&g_app_state.cli_buf[pos], &g_app_state.cli_buf[g_app_state.cli_cursor],
					len - g_app_state.cli_cursor + 1);
			g_app_state.cli_cursor = pos;
		}
	} else if (k.type == KEY_CHAR) {
		if (len + 1 < INPUT_MAX) {
			memmove(&g_app_state.cli_buf[g_app_state.cli_cursor + 1],
					&g_app_state.cli_buf[g_app_state.cli_cursor], len - g_app_state.cli_cursor + 1);
			g_app_state.cli_buf[g_app_state.cli_cursor++] = k.ch;
			g_app_state.cli_buf[len + 1] = '\0';
		}
	}
}
