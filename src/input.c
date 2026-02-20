#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"
#include "git.h"
#include "render.h" /* for draw_flush in action_push/pull if needed, but actions are in git.c */
#include "state.h"
#include "strings.h"
#include "ui.h"
#include "util.h"
#include "views.h"

/* Helper for selection movement */
static void msel(int *sel, int *scr, int cnt, int d, int vis, bool is_graph) {
	*sel = iclamp(*sel + d, 0, cnt > 0 ? cnt - 1 : 0);
	if (*sel < *scr) *scr = *sel;
	if (*sel >= *scr + vis) *scr = *sel - vis + 1;
	if (is_graph && cnt > 0) {
		if (g_app_state.commits[*sel].expanded) fetch_commit_files(*sel);
		sync_graph_preview();
	}
}

static bool right_pane_is_editor_content(void) {
	bool editor_visible = (g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR);
	return editor_visible && !editor_current_tab_is_diff() && g_app_state.tab_count > 0;
}

static FocusPane right_pane_focus_pane(void) {
	if (right_pane_is_editor_content()) return FOCUS_EDITOR;
	if (g_app_state.current_view == VIEW_EDITOR && !editor_current_tab_is_diff())
		return FOCUS_EDITOR;
	return FOCUS_DIFF;
}

static void focus_diff_pane(void) {
	if (g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR)
		editor_select_visible_tab(0);
	else
		g_app_state.focus = FOCUS_DIFF;
}

static int diff_content_start_row(int pane_top) {
	bool mixed_tabs = ((g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR) &&
					   editor_current_tab_is_diff());
	return pane_top + 1 + (mixed_tabs ? 1 : 0);
}

static const char *resolve_external_editor(void) {
	const char *ed = getenv("VISUAL");
	if (ed && ed[0]) return ed;
	ed = getenv("EDITOR");
	if (ed && ed[0]) return ed;
	ed = getenv("SELECTED_EDITOR");
	if (ed && ed[0]) return ed;
	return "vi";
}

static void invalidate_front_buffer(void) {
	if (!g_app_state.front.cells) return;
	memset(g_app_state.front.cells, 0,
		   g_app_state.front.w * g_app_state.front.h * sizeof(Cell));
}

/* Open current file/path in $VISUAL/$EDITOR/vi */
static void action_open_in_editor_extern(const char *path) {
	if (!path || !path[0]) return;
	const char *ed = resolve_external_editor();

	printf(T_NORM T_SHOW T_MOUSE_OFF T_RESET);
	fflush(stdout);
	term_restore();

	char cmd[1024];
	if (g_app_state.focus == FOCUS_EDITOR && g_app_state.tab_count > 0 &&
		!editor_current_tab_is_diff()) {
		snprintf(cmd, sizeof(cmd), "%s '%s'", ed, g_app_state.tabs[g_app_state.tab_current].path);
	} else if (g_app_state.focus == FOCUS_CHANGES && g_app_state.file_count > 0) {
		snprintf(cmd, sizeof(cmd), "%s '%s'", ed, g_app_state.files[g_app_state.file_sel].path);
	} else if (path) {
		snprintf(cmd, sizeof(cmd), "%s '%s'", ed, path);
	} else {
		snprintf(cmd, sizeof(cmd), "%s", ed);
	}
	system(cmd);

	term_raw();
	printf(T_ALT T_HIDE T_MOUSE_ON T_CLEAR);
	fflush(stdout);
	get_winsize();
	invalidate_front_buffer(); /* force full redraw after external editor */
	reload_all();
	OK(UI->msg_returned_from_fmt, ed);
}

/* Commit bar actions */
static void do_commit_bar(void) {
	if (!g_app_state.commit_msg_buf[0]) {
		ERR("%s", UI->err_empty_commit_message);
		return;
	}
	int s = 0;
	for (int i = 0; i < g_app_state.file_count; i++)
		if (g_app_state.files[i].staged) s++;
	if (!s) {
		ERR("%s", UI->err_nothing_staged);
		return;
	}
	/* We need to expose do_commit from git.c or use action_commit logic but taking msg.
	   git.c has static do_commit. I should expose a commit function in git.h
	   or just use git_exec here.
	   Let's use git_exec helper essentially.
	   Actually, I should expose `commit_with_msg` in git.h.
	   For now, I'll duplicate the logic or better: make do_commit non-static in git.c?
	   No, I'll just write to temp and commit here.
	*/
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "/tmp/gitui_msg_bar_XXXXXX");
	int fd = mkstemp(tmp);
	if (fd >= 0) {
		write(fd, g_app_state.commit_msg_buf, strlen(g_app_state.commit_msg_buf));
		close(fd);
		int r = git_exec("git commit -F '%s'", tmp);
		unlink(tmp);
		if (r == 0) {
			OK(UI->msg_committed_fmt, g_app_state.commit_msg_buf);
			load_status();
			load_log();
		} else
			ERR("%s", UI->err_commit_failed);
	}

	g_app_state.commit_msg_buf[0] = '\0';
	g_app_state.commit_msg_cursor = 0;
	g_app_state.commit_bar_focused = false;
}

static void do_amend_bar(void) {
	/* Similar duplication or expose helper. */
	if (g_app_state.commit_msg_buf[0]) {
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "/tmp/gitui_amend_bar_XXXXXX");
		int fd = mkstemp(tmp);
		if (fd >= 0) {
			write(fd, g_app_state.commit_msg_buf, strlen(g_app_state.commit_msg_buf));
			close(fd);
			int r = git_exec("git commit --amend -F '%s'", tmp);
			unlink(tmp);
			if (r == 0) {
				OK("%s", UI->msg_amended);
				load_log();
			} else
				ERR("%s", UI->err_amend_failed);
		}
	} else {
		int r = git_exec("git commit --amend --no-edit");
		if (r == 0) {
			OK("%s", UI->msg_amended);
			load_log();
		} else
			ERR("%s", UI->err_amend_failed);
	}
	g_app_state.commit_msg_buf[0] = '\0';
	g_app_state.commit_msg_cursor = 0;
	g_app_state.commit_bar_focused = false;
}

static void toggle_commit_expansion(void) {
	if (g_app_state.commit_count <= 0) return;
	GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
	c->expanded = !c->expanded;
	if (c->expanded) fetch_commit_files(g_app_state.commit_sel);
}

static void open_diff_summary_at(int idx) {
	if (idx < 0 || idx >= g_app_state.diff_count) return;
	DiffLine *dl = &g_app_state.diff_lines[idx];
	if (dl->type != 5) return;

	if (strncmp(g_app_state.diff_title, UI->diff_title_files_prefix,
				strlen(UI->diff_title_files_prefix)) == 0) {
		editor_load(dl->new_line);
		g_app_state.editor_active = true;
		g_app_state.focus = FOCUS_EDITOR;
		return;
	}

	if (strncmp(g_app_state.diff_title, UI->diff_title_search_prefix,
				strlen(UI->diff_title_search_prefix)) == 0) {
		char lb[LINE_MAX_LEN];
		snprintf(lb, sizeof(lb), "%s", dl->new_line);
		char *c1 = strchr(lb, ':');
		if (c1 && isdigit((unsigned char)c1[1])) {
			*c1 = '\0';
			int lno = atoi(c1 + 1);
			editor_load(lb);
			if (g_app_state.tab_count > 0) {
				Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
				ed->cursor_row = imax(0, lno - 1);
				ed->cursor_col = 0;
				ed->needs_sync = true;
			}
			g_app_state.editor_active = true;
			g_app_state.focus = FOCUS_EDITOR;
		}
		return;
	}

	if (!g_app_state.diff_commit[0]) return;
	char fpath[LINE_MAX_LEN];
	snprintf(fpath, sizeof(fpath), "%s", dl->new_line);
	const char *ctx_ = g_app_state.diff_continuous ? "-U1000" : "-U3";
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, g_app_state.diff_commit,
			 fpath);
	char *o = git_run(cmd);
	snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), UI->diff_title_commit_fmt,
			 g_app_state.diff_commit, fpath);
	g_app_state.diff_is_summary = false;
	parse_diff(o ? o : "");
	free(o);
}

static void show_log_commit(void) {
	if (g_app_state.commit_count <= 0) return;
	if (g_app_state.commit_sel < 0 || g_app_state.commit_sel >= g_app_state.commit_count) return;
	GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
	g_app_state.diff_staged = false;
	g_app_state.diff_is_summary = false;
	snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), UI->diff_title_commit_fmt,
			 c->hash, c->subject);
	snprintf(g_app_state.diff_commit, sizeof(g_app_state.diff_commit), "%s", c->hash);
	load_diff_commit(c->hash);
}

static bool diff_is_commit_view(void) {
	if (g_app_state.diff_commit[0]) return true;
	return strncmp(g_app_state.diff_title, UI->diff_title_commit_prefix,
				   strlen(UI->diff_title_commit_prefix)) == 0;
}

static void refresh_diff_after_context_toggle(void) {
	if (g_app_state.diff_is_summary) return;

	if (g_app_state.current_view == VIEW_LOG) {
		show_log_commit();
		return;
	}

	if (g_app_state.current_view == VIEW_STATUS) {
		if (diff_is_commit_view())
			sync_graph_preview();
		else
			update_diff();
	}
}

static void handle_mouse(MouseEvt m) {
	g_app_state.last_mx = m.col;
	g_app_state.last_my = m.row;
	bool su = (m.btn & 64) && !(m.btn & 1);
	bool sd = (m.btn & 64) && (m.btn & 1);
	bool motion = (m.btn & 32) != 0;
	bool cl = !su && !sd && !m.release && (m.btn & 3) != 3 && !motion;
	bool right_cl = cl && (m.btn & 3) == 2;
	int ct = 2;

	if (su || sd) {
		int delta = su ? -3 : 3;
		if (g_app_state.current_view == VIEW_STATUS) {
			int vx = g_app_state.sidebar_w + g_app_state.layout_width;
			if (m.col <= vx) {
				if (g_app_state.browser_active) {
					g_app_state.browser_scroll = imax(0, g_app_state.browser_scroll + delta);
				} else {
					bool in_top = (m.row >= ct && m.row < ct + g_app_state.layout_height_changes);
					if (in_top) {
						g_app_state.file_scroll = imax(0, g_app_state.file_scroll + delta);
					} else {
						/* Scroll through files in expanded commits, not just commits */
						int gvis = g_app_state.layout_height_graph - 2;
						int step = su ? -1 : 1;
						GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
						if (step > 0) {
							/* Scrolling down */
							if (c->expanded && g_app_state.graph_file_sel < c->file_count - 1) {
								g_app_state.graph_file_sel++;
							} else {
								msel(&g_app_state.commit_sel, &g_app_state.commit_scroll,
									 g_app_state.commit_count, step, gvis, true);
								g_app_state.graph_file_sel = -1;
							}
						} else {
							/* Scrolling up */
							if (c->expanded && g_app_state.graph_file_sel >= 0) {
								g_app_state.graph_file_sel--;
							} else {
								int prev_sel = g_app_state.commit_sel;
								msel(&g_app_state.commit_sel, &g_app_state.commit_scroll,
									 g_app_state.commit_count, step, gvis, true);
								c = &g_app_state.commits[g_app_state.commit_sel];
								if (c->expanded && prev_sel != g_app_state.commit_sel)
									g_app_state.graph_file_sel = c->file_count - 1;
								else
									g_app_state.graph_file_sel = -1;
							}
						}
						if (g_app_state.commit_count > 0) sync_graph_preview();
					}
				}
			} else {
				if (right_pane_is_editor_content()) {
					Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
					ed->scroll_row = imax(0, ed->scroll_row + delta);
				} else {
					g_app_state.diff_scroll = imax(0, g_app_state.diff_scroll + delta);
				}
			}
		} else if (g_app_state.current_view == VIEW_LOG) {
			int log_h = (ct + g_app_state.layout_height_log);
			if (m.row < log_h) {
				int vis = g_app_state.layout_height_log - 2;
				int step = su ? -1 : 1;
				msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, g_app_state.commit_count,
					 step, vis, false);
				show_log_commit();
			} else {
				if (right_pane_is_editor_content()) {
					Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
					ed->scroll_row = imax(0, ed->scroll_row + delta);
				} else {
					g_app_state.diff_scroll = imax(0, g_app_state.diff_scroll + delta);
				}
			}
		} else if (g_app_state.current_view == VIEW_BRANCHES) {
			g_app_state.branch_scroll = imax(0, g_app_state.branch_scroll + delta);
		} else if (g_app_state.current_view == VIEW_EDITOR) {
			int vx = g_app_state.sidebar_w + g_app_state.layout_width;
			if (m.col <= vx) {
				g_app_state.browser_scroll = imax(0, g_app_state.browser_scroll + delta);
			} else if (right_pane_is_editor_content()) {
				Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
				ed->scroll_row = imax(0, ed->scroll_row + delta);
			} else {
				g_app_state.diff_scroll = imax(0, g_app_state.diff_scroll + delta);
			}
		}
		return;
	}

	if (m.release) {
		g_app_state.dragging_v = false;
		g_app_state.dragging_h = false;
		g_app_state.dragging_sc = false;
		g_app_state.dragging_diff = false;
		g_app_state.dragging_col_hash = false;
		g_app_state.dragging_col_author = false;
		g_app_state.dragging_col_date = false;
		g_app_state.dragging_h_log = false;
		g_app_state.dragging_ed_sc = false;
	}

	if (cl && g_app_state.commit_bar_focused) {
		int commit_bar_row = 2 + 1;
		if (m.row != commit_bar_row) g_app_state.commit_bar_focused = false;
	}

	if (g_app_state.menu_active) {
		if (cl) {
			if (m.row > g_app_state.menu_y && m.row < g_app_state.menu_y + g_app_state.menu_h - 1 &&
				m.col > g_app_state.menu_x && m.col < g_app_state.menu_x + g_app_state.menu_w - 1) {
				int idx = m.row - g_app_state.menu_y - 1;
				if (idx >= 0 && idx < g_app_state.menu_item_count) {
					void (*act)(void) = g_app_state.menu_actions[idx];
					g_app_state.menu_active = false;
					if (act) act();
					return;
				}
			}
			g_app_state.menu_active = false;
		}
		return;
	}

	int drx = (g_app_state.current_view == VIEW_LOG)
				  ? 1
				  : g_app_state.layout_width + g_app_state.sidebar_w + 1;
	int dtop = (g_app_state.current_view == VIEW_LOG) ? (ct + g_app_state.layout_height_log) : ct;

	if (motion) {
		if (g_app_state.dragging_v) {
			g_app_state.lw_custom = m.col - g_app_state.sidebar_w;
			layout();
			return;
		}
		if (g_app_state.dragging_h) {
			g_app_state.lh_chg_custom = m.row - ct + 1;
			layout();
			return;
		}
		if (g_app_state.dragging_h_log) {
			g_app_state.lh_log_custom = m.row - ct + 1;
			layout();
			return;
		}
		if (g_app_state.dragging_diff) {
			g_app_state.diff_split_custom = m.col - drx;
			layout();
			return;
		}
		if (g_app_state.dragging_sc && g_app_state.scrollbar_height > 0) {
			int rel_y = m.row - g_app_state.scrollbar_y;
			int bh = imax(1, (g_app_state.scrollbar_visible * g_app_state.scrollbar_visible) /
								 g_app_state.scrollbar_total);
			int max_bpos = g_app_state.scrollbar_height - bh;
			if (max_bpos > 0) {
				int bpos = iclamp(rel_y - g_app_state.scrollbar_drag_offset, 0, max_bpos);
				g_app_state.diff_scroll =
					(bpos * (g_app_state.scrollbar_total - g_app_state.scrollbar_visible)) /
					max_bpos;
			}
			return;
		}
		if (g_app_state.dragging_ed_sc && g_app_state.editor_scrollbar_height > 0 &&
			g_app_state.tab_count > 0) {
			/* Using global state CUR_ED logic */
			Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
			int rel_y = m.row - g_app_state.editor_scrollbar_y;
			int bh = imax(
				1, (g_app_state.editor_scrollbar_visible * g_app_state.editor_scrollbar_visible) /
					   g_app_state.editor_scrollbar_total);
			int max_bpos = g_app_state.editor_scrollbar_height - bh;
			if (max_bpos > 0) {
				int bpos = iclamp(rel_y - g_app_state.editor_scrollbar_drag_offset, 0, max_bpos);
				ed->scroll_row = (bpos * (g_app_state.editor_scrollbar_total -
										  g_app_state.editor_scrollbar_visible)) /
								 max_bpos;
			}
			return;
		}
		if (g_app_state.dragging_col_hash) {
			g_app_state.col_hash_w = imax(4, m.col - (2 + GRAPH_COLS));
			return;
		}
		if (g_app_state.dragging_col_author) {
			int prefix = 2 + GRAPH_COLS + g_app_state.col_hash_w + 1 + 21;
			g_app_state.col_author_w = imax(4, m.col - prefix);
			return;
		}
		if (g_app_state.dragging_col_date) {
			int prefix = 2 + GRAPH_COLS + g_app_state.col_hash_w + 1 + 21 +
						 g_app_state.col_author_w + 1;
			g_app_state.col_date_w = imax(4, m.col - prefix);
			return;
		}
		if (g_app_state.ed_selecting) {
			Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
			g_app_state.ed_sel_end_y =
				ed->scroll_row + (m.row - (dtop + 1)); /* +1? Editor starts at dtop? Editor starts
														  at dtop. Content at dtop+2 usually. */
			/* Check view_editor.c: row starts at top+2 */
			g_app_state.ed_sel_end_y = ed->scroll_row + (m.row - (dtop + 2));
			g_app_state.ed_sel_end_x = m.col - (drx + 7) + ed->scroll_col;
			return;
		}
		if (g_app_state.selecting) {
			int diff_top = diff_content_start_row(dtop);
			g_app_state.sel_end_y = g_app_state.diff_scroll + (m.row - diff_top);
			g_app_state.sel_end_x = m.col - drx - 1;
			return;
		}
		return;
	}

	if (right_cl) {
		if (g_app_state.focus == FOCUS_EDITOR) {
			menu_reset(m.col, m.row);
			menu_add_item(UI->menu_copy, action_copy_editor_selection);
			menu_add_item(UI->menu_cut, editor_cut_selection);
			menu_add_item(UI->menu_paste, editor_paste);
			menu_add_item(UI->menu_cancel, NULL);
			return;
		}
		menu_reset(m.col, m.row);
		if (g_app_state.selecting) {
			menu_add_item(UI->menu_copy, action_copy_selection);
			if (!g_app_state.diff_is_summary && !g_app_state.diff_commit[0]) {
				if (g_app_state.diff_staged)
					menu_add_item(UI->menu_unstage, action_unstage_selection);
				else
					menu_add_item(UI->menu_stage, action_stage_selection);
			}
		}
		menu_add_item(UI->menu_cancel, NULL);
		return;
	}

	/* Left pane */
	if (cl && m.col <= g_app_state.sidebar_w) {
		if (m.row == ct + 1) {
			g_app_state.browser_active = true;
			load_browser(".");
			g_app_state.focus = FOCUS_BROWSER;
		} else if (m.row == ct + 3) {
			g_app_state.browser_active = false;
			g_app_state.focus = FOCUS_CHANGES;
		}
		return;
	}

	int toggle_row =
		(g_app_state.current_view == VIEW_LOG) ? (ct + g_app_state.layout_height_log) : ct;
	int toggle_min_x = (g_app_state.current_view == VIEW_LOG) ? 1 : g_app_state.render_x;
	bool editor_visible =
		(g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR);

	/* Editor tabs */
	if (cl && editor_visible && m.row == toggle_row + 1 && m.col > toggle_min_x) {
		int slots = editor_visible_tab_count();
		for (int i = 0; i < slots; i++) {
			if (m.col >= g_app_state.ed_tab_x[i] && m.col < g_app_state.ed_tab_x[i + 1]) {
				editor_select_visible_tab(i);
				return;
			}
		}
	}

	/* Right pane header */
	if (cl && m.row == toggle_row && m.col > toggle_min_x) {
		if (!right_pane_is_editor_content()) {
			focus_diff_pane();
			const char *label_side =
				g_app_state.diff_sidebyside ? UI->diff_side_split : UI->diff_side_unify;
			const char *label_ctx =
				g_app_state.diff_continuous ? UI->diff_ctx_full : UI->diff_ctx_hunk;
			const char *label_wrap =
				g_app_state.diff_wrap ? UI->diff_wrap_label_on : UI->diff_wrap_label_off;
			char extra[64];
			snprintf(extra, sizeof(extra), " [%s] [%s] [%s] ", label_side, label_ctx,
					 label_wrap);
			int elen = (int)strlen(extra);
			int start_x = g_app_state.cols - elen - 1;

			int btn1_start = start_x + 1;
			int btn1_len = (int)strlen(label_side) + 2;
			int btn2_start = btn1_start + btn1_len + 1;
			int btn2_len = (int)strlen(label_ctx) + 2;
			int btn3_start = btn2_start + btn2_len + 1;
			int btn3_len = (int)strlen(label_wrap) + 2;

			if (m.col >= btn1_start && m.col < btn1_start + btn1_len) {
				g_app_state.diff_sidebyside = !g_app_state.diff_sidebyside;
				return;
			}
			if (m.col >= btn2_start && m.col < btn2_start + btn2_len) {
				g_app_state.diff_continuous = !g_app_state.diff_continuous;
				refresh_diff_after_context_toggle();
				return;
			}
			if (m.col >= btn3_start && m.col < btn3_start + btn3_len) {
				g_app_state.diff_wrap = !g_app_state.diff_wrap;
				return;
			}
		} else if (editor_visible) {
			g_app_state.focus = FOCUS_EDITOR;
			int save_x = g_app_state.cols - 8;
			if (m.col >= save_x && m.col < save_x + 6) {
				editor_save();
				return;
			}
		}
		return;
	}

	/* Tab bar */
	if (cl && m.row == 1) {
		for (int i = 0; i < 5; i++) {
			if (m.col >= g_app_state.tab_x[i] && m.col < g_app_state.tab_x[i + 1]) {
				static const View vmap[] = {VIEW_STATUS, VIEW_LOG, VIEW_BRANCHES, VIEW_STASH,
											VIEW_EDITOR};
				g_app_state.current_view = vmap[i];
				if (g_app_state.current_view == VIEW_EDITOR) {
					if (!g_app_state.browser_count) load_browser(".");
					g_app_state.focus = right_pane_focus_pane();
				}
				return;
			}
		}
		if (g_app_state.tab_x[5] > 0 && m.col >= g_app_state.tab_x[5]) {
			g_app_state.current_view = VIEW_HELP;
			return;
		}
		return;
	}

	/* Main Area Clicks */
	int diff_top = diff_content_start_row(dtop);
	if (cl && (g_app_state.current_view == VIEW_STATUS || g_app_state.current_view == VIEW_LOG) &&
		m.col > drx && m.row >= diff_top && m.row < g_app_state.rows - 1) {
		if (right_pane_is_editor_content()) {
			Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
			if (m.col >= g_app_state.editor_scrollbar_x &&
				g_app_state.editor_scrollbar_height > 0 &&
				m.row >= g_app_state.editor_scrollbar_y &&
				m.row < g_app_state.editor_scrollbar_y + g_app_state.editor_scrollbar_height) {
				g_app_state.dragging_ed_sc = true;
				int rel_y = m.row - g_app_state.editor_scrollbar_y;
				int bh = imax(1, (g_app_state.editor_scrollbar_visible *
								  g_app_state.editor_scrollbar_visible) /
									 g_app_state.editor_scrollbar_total);
				int max_bpos = g_app_state.editor_scrollbar_height - bh;
				int bpos = (max_bpos > 0)
							   ? ((long long)ed->scroll_row * max_bpos) /
									 (g_app_state.editor_scrollbar_total -
									  g_app_state.editor_scrollbar_visible)
							   : 0;

				if (rel_y >= bpos && rel_y < bpos + bh) {
					g_app_state.editor_scrollbar_drag_offset = rel_y - bpos;
				} else {
					g_app_state.editor_scrollbar_drag_offset = bh / 2;
					int new_bpos = iclamp(rel_y - g_app_state.editor_scrollbar_drag_offset,
										  0, max_bpos);
					ed->scroll_row =
						(new_bpos * (g_app_state.editor_scrollbar_total -
									 g_app_state.editor_scrollbar_visible)) /
						max_bpos;
				}
				return;
			}
			g_app_state.ed_selecting = true;
			g_app_state.ed_sel_start_y = g_app_state.ed_sel_end_y =
				ed->scroll_row + (m.row - (dtop + 2));
			g_app_state.ed_sel_start_x = g_app_state.ed_sel_end_x =
				m.col - (drx + 7) + ed->scroll_col;
			if (g_app_state.ed_sel_start_y >= 0 && g_app_state.ed_sel_start_y < ed->line_count) {
				ed->cursor_row = g_app_state.ed_sel_start_y;
				ed->cursor_col =
					iclamp(g_app_state.ed_sel_start_x, 0, (int)strlen(ed->lines[ed->cursor_row]));
			}
			g_app_state.focus = FOCUS_EDITOR;
			return;
		} else {
			int sc_x = g_app_state.render_x + g_app_state.render_width - 3;
			if (m.col >= sc_x && g_app_state.scrollbar_height > 0 &&
				m.row >= g_app_state.scrollbar_y &&
				m.row < g_app_state.scrollbar_y + g_app_state.scrollbar_height) {
				g_app_state.dragging_sc = true;
				int rel_y = m.row - g_app_state.scrollbar_y;
				int bh = imax(1, (g_app_state.scrollbar_visible * g_app_state.scrollbar_visible) /
									 g_app_state.scrollbar_total);
				int max_bpos = g_app_state.scrollbar_height - bh;
				int bpos =
					(max_bpos > 0)
						? ((long long)g_app_state.diff_scroll * max_bpos) /
							  (g_app_state.scrollbar_total - g_app_state.scrollbar_visible)
						: 0;
				if (rel_y >= bpos && rel_y < bpos + bh) {
					g_app_state.scrollbar_drag_offset = rel_y - bpos;
				} else {
					g_app_state.scrollbar_drag_offset = bh / 2;
					/* Immediate jump */
					int new_bpos = iclamp(rel_y - g_app_state.scrollbar_drag_offset, 0, max_bpos);
					g_app_state.diff_scroll =
						(new_bpos * (g_app_state.scrollbar_total - g_app_state.scrollbar_visible)) /
						max_bpos;
				}
				return;
			}

			g_app_state.selecting = true;
			g_app_state.sel_start_y = g_app_state.sel_end_y =
				g_app_state.diff_scroll + (m.row - diff_top);
			g_app_state.sel_start_x = g_app_state.sel_end_x = m.col - drx - 1;
			focus_diff_pane();
		}
	} else if (cl) {
		g_app_state.selecting = false;
		g_app_state.ed_selecting = false;
	}

	/* CLI focus */
	if (cl && m.row == g_app_state.rows - 1) {
		g_app_state.focus = FOCUS_CLI;
		return;
	}

	/* STATUS VIEW Specifics (Changes/Graph lists) */
	if (g_app_state.current_view == VIEW_STATUS) {
		layout();
		int vx = g_app_state.sidebar_w + g_app_state.layout_width;
		if (cl && m.col == vx) {
			g_app_state.dragging_v = true;
			return;
		}
		if (cl && m.col < vx && m.row == ct + g_app_state.layout_height_changes - 1) {
			g_app_state.dragging_h = true;
			return;
		}
		if (cl && g_app_state.diff_sidebyside && m.col == drx + g_app_state.diff_split) {
			g_app_state.dragging_diff = true;
			return;
		}
		if (cl && g_app_state.focus == FOCUS_GRAPH) {
			int cur_x = 2 + GRAPH_COLS;
			if (m.col == cur_x + g_app_state.col_hash_w) {
				g_app_state.dragging_col_hash = true;
				return;
			}
			cur_x += g_app_state.col_hash_w + 1 + 21;
			if (m.col == cur_x + g_app_state.col_author_w) {
				g_app_state.dragging_col_author = true;
				return;
			}
			cur_x += g_app_state.col_author_w + 1;
			if (m.col == cur_x + g_app_state.col_date_w) {
				g_app_state.dragging_col_date = true;
				return;
			}
		}

		bool in_l = (m.col >= 1 && m.col <= vx);
		bool in_r = (m.col > vx);
		bool in_top = (m.row >= ct && m.row < ct + g_app_state.layout_height_changes);
		bool in_bot = (m.row >= ct + g_app_state.layout_height_changes);

		if (in_l) {
			if (g_app_state.browser_active) {
				if (cl) g_app_state.focus = FOCUS_BROWSER;
				if (cl) {
					int t = g_app_state.browser_scroll + (m.row - (ct + 1));
					if (t >= 0 && t < g_app_state.browser_count) {
						g_app_state.browser_sel = t;
						BrowserFile *f = &g_app_state.browser_files[t];
						if (f->is_dir) {
							char next[1024];
							snprintf(next, sizeof(next), "%s/%s", g_app_state.browser_path,
									 f->path);
							load_browser(next);
							g_app_state.browser_sel = 0;
							g_app_state.browser_scroll = 0;
						} else {
							char full[1024];
							snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path,
									 f->path);
							editor_load(full);
							g_app_state.editor_active = true;
							g_app_state.focus = FOCUS_EDITOR;
						}
					}
				}
				return;
			}
			if (in_top) {
				if (cl) g_app_state.focus = FOCUS_CHANGES;
				if (cl) {
					/* Commit bar logic */
					int commit_bar_row = ct + 1;
					if (m.row == commit_bar_row) {
						int sx = g_app_state.sidebar_w + 1;
						int iw = g_app_state.layout_width - 2;
						int btn_total_w = COMMIT_BTN_W + AMEND_BTN_W;
						int field_w = iw - 3 - btn_total_w;
						if (field_w < 4) field_w = 4;
						int btn_x = sx + 4 + field_w;

						if (m.col >= btn_x && m.col < btn_x + COMMIT_BTN_W) {
							g_app_state.commit_bar_focused = false;
							do_commit_bar();
						} else if (m.col >= btn_x + COMMIT_BTN_W &&
								   m.col < btn_x + btn_total_w) {
							g_app_state.commit_bar_focused = false;
							do_amend_bar();
						} else if (m.col >= sx + 4 && m.col < btn_x) {
							g_app_state.commit_bar_focused = true;
							g_app_state.focus = FOCUS_CHANGES;
							int len = (int)strlen(g_app_state.commit_msg_buf);
							int disp_start = 0;
							if (g_app_state.commit_msg_cursor >= field_w)
								disp_start = g_app_state.commit_msg_cursor - field_w + 1;
							int clicked_pos = (m.col - (sx + 4)) + disp_start;
							g_app_state.commit_msg_cursor = iclamp(clicked_pos, 0, len);
						} else {
							g_app_state.commit_bar_focused = true;
							g_app_state.focus = FOCUS_CHANGES;
						}
						return;
					}

					/* Linearize items to find clicked file, matching draw_changes logic */
					struct {
						bool is_header;
						int file_idx;
					} items[MAX_FILES + 4];
					int item_count = 0;
					int staged_n = 0, unstaged_n = 0;
					for (int i = 0; i < g_app_state.file_count; i++)
						g_app_state.files[i].staged ? staged_n++ : unstaged_n++;

					if (staged_n > 0) {
						items[item_count++].is_header = true;
						for (int i = 0; i < g_app_state.file_count; i++)
							if (g_app_state.files[i].staged) {
								items[item_count].is_header = false;
								items[item_count++].file_idx = i;
							}
					}
					if (unstaged_n > 0) {
						if (staged_n > 0) items[item_count++].is_header = true;
						items[item_count++].is_header = true;
						for (int i = 0; i < g_app_state.file_count; i++)
							if (!g_app_state.files[i].staged) {
								items[item_count].is_header = false;
								items[item_count++].file_idx = i;
							}
					}

					int click_row = m.row - (ct + 3);
					int idx = g_app_state.file_scroll + click_row;
					if (idx >= 0 && idx < item_count && !items[idx].is_header) {
						g_app_state.file_sel = items[idx].file_idx;
					}
				}
				update_diff();
			} else if (in_bot) {
				if (cl) g_app_state.focus = FOCUS_GRAPH;
				if (cl) {
					int top = ct + g_app_state.layout_height_changes;
					int rel_row = m.row - (top + 1);
					if (rel_row >= 0 && rel_row < g_app_state.graph_rows_count) {
						int ci = g_app_state.graph_rows[rel_row].commit_idx;
						int fi = g_app_state.graph_rows[rel_row].file_idx;
						if (ci >= 0 && ci < g_app_state.commit_count) {
							int arrow_col = g_app_state.sidebar_w + 2;
							if (m.col == arrow_col || m.col == arrow_col + 1) {
								g_app_state.commits[ci].expanded =
									!g_app_state.commits[ci].expanded;
								if (g_app_state.commits[ci].expanded) fetch_commit_files(ci);
								return;
							}
							g_app_state.commit_sel = ci;
							g_app_state.graph_file_sel = fi;
							sync_graph_preview();
						}
					}
				}
			}
		}
		if (in_r) {
			if (cl) g_app_state.focus = right_pane_focus_pane();
			int diff_top = diff_content_start_row(dtop);
			if (cl && !right_pane_is_editor_content() && g_app_state.diff_is_summary &&
				m.row >= diff_top && m.row < g_app_state.rows - 1) {
				int t = g_app_state.diff_scroll + (m.row - diff_top);
				if (t >= 0 && t < g_app_state.diff_count) {
					g_app_state.diff_sel = t;
					focus_diff_pane();
					open_diff_summary_at(t);
				}
			}
		}
	}
	if (cl && g_app_state.current_view == VIEW_LOG) {
		if (m.row == ct + g_app_state.layout_height_log - 1) {
			g_app_state.dragging_h_log = true;
			return;
		}
		if (g_app_state.diff_sidebyside && m.col == drx + g_app_state.diff_split) {
			g_app_state.dragging_diff = true;
			return;
		}
		int start_y = ct + 2;
		if (m.row >= start_y && m.row < start_y + g_app_state.layout_height_log - 2) {
			int idx = g_app_state.commit_scroll + (m.row - start_y);
			if (idx >= 0 && idx < g_app_state.commit_count) {
				g_app_state.commit_sel = idx;
				show_log_commit();
			}
		} else if (m.row == ct + 1) {
			int x = 2 + GRAPH_COLS;
			if (m.col == x + g_app_state.col_hash_w) g_app_state.dragging_col_hash = true;
			x += g_app_state.col_hash_w + 1 + 21;
			if (m.col == x + g_app_state.col_author_w) g_app_state.dragging_col_author = true;
			x += g_app_state.col_author_w + 1;
			if (m.col == x + g_app_state.col_date_w) g_app_state.dragging_col_date = true;
		} else if (!right_pane_is_editor_content() && g_app_state.diff_is_summary &&
				   m.row < g_app_state.rows - 1) {
			int diff_top = diff_content_start_row(ct + g_app_state.layout_height_log);
			if (m.row >= diff_top) {
				int t = g_app_state.diff_scroll + (m.row - diff_top);
				if (t >= 0 && t < g_app_state.diff_count) {
					g_app_state.diff_sel = t;
					focus_diff_pane();
					open_diff_summary_at(t);
				}
			}
		}
	} else if (cl && g_app_state.current_view == VIEW_BRANCHES) {
		int start_y = ct + 2;
		if (m.row >= start_y && m.row < g_app_state.rows - 1) {
			int idx = g_app_state.branch_scroll + (m.row - start_y);
			if (idx >= 0 && idx < g_app_state.branch_count) {
				g_app_state.branch_sel = idx;
			}
		}
	} else if (cl && g_app_state.current_view == VIEW_STASH) {
		int start_y = ct + 1;
		if (m.row >= start_y && m.row < g_app_state.rows - 1) {
			int idx = m.row - start_y;
			if (idx >= 0 && idx < g_app_state.stash_count) {
				g_app_state.stash_sel = idx;
			}
		}
	} else if (cl && g_app_state.current_view == VIEW_EDITOR) {
		layout();
		int vx = g_app_state.sidebar_w + g_app_state.layout_width;
		if (m.col <= vx) {
			g_app_state.focus = FOCUS_BROWSER;
			int t = g_app_state.browser_scroll + (m.row - (ct + 1));
			if (t >= 0 && t < g_app_state.browser_count) {
				g_app_state.browser_sel = t;
				BrowserFile *f = &g_app_state.browser_files[t];
				if (f->is_dir) {
					char next[1024];
					snprintf(next, sizeof(next), "%s/%s", g_app_state.browser_path, f->path);
					load_browser(next);
					g_app_state.browser_sel = 0;
					g_app_state.browser_scroll = 0;
				} else {
					char full[1024];
					snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path, f->path);
					editor_load(full);
					g_app_state.focus = FOCUS_EDITOR;
				}
			}
		} else {
			if (right_pane_is_editor_content()) {
				g_app_state.focus = FOCUS_EDITOR;
				Editor *ed = &g_app_state.tabs[g_app_state.tab_current].ed;
				int ty = ed->scroll_row + (m.row - (ct + 2));
				if (ty >= 0 && ty < ed->line_count) {
					ed->cursor_row = ty;
					ed->cursor_col =
						iclamp(m.col - (g_app_state.render_x + 7) + ed->scroll_col, 0,
							   (int)strlen(ed->lines[ty]));
					ed->needs_sync = true;
				}
			} else {
				focus_diff_pane();
				int sc_x = g_app_state.render_x + g_app_state.render_width - 3;
				if (m.col >= sc_x && g_app_state.scrollbar_height > 0 &&
					m.row >= g_app_state.scrollbar_y &&
					m.row < g_app_state.scrollbar_y + g_app_state.scrollbar_height) {
					g_app_state.dragging_sc = true;
					int rel_y = m.row - g_app_state.scrollbar_y;
					int bh = imax(1, (g_app_state.scrollbar_visible * g_app_state.scrollbar_visible) /
									 g_app_state.scrollbar_total);
					int max_bpos = g_app_state.scrollbar_height - bh;
					int bpos =
						(max_bpos > 0)
							? ((long long)g_app_state.diff_scroll * max_bpos) /
								  (g_app_state.scrollbar_total - g_app_state.scrollbar_visible)
							: 0;
					if (rel_y >= bpos && rel_y < bpos + bh) {
						g_app_state.scrollbar_drag_offset = rel_y - bpos;
					} else {
						g_app_state.scrollbar_drag_offset = bh / 2;
						int new_bpos = iclamp(rel_y - g_app_state.scrollbar_drag_offset, 0, max_bpos);
						g_app_state.diff_scroll =
							(new_bpos * (g_app_state.scrollbar_total - g_app_state.scrollbar_visible)) /
							max_bpos;
					}
					return;
				}

				int diff_top = diff_content_start_row(ct);
				if (g_app_state.diff_is_summary && m.row >= diff_top && m.row < g_app_state.rows - 1) {
					int t = g_app_state.diff_scroll + (m.row - diff_top);
					if (t >= 0 && t < g_app_state.diff_count) {
						g_app_state.diff_sel = t;
						focus_diff_pane();
						open_diff_summary_at(t);
						return;
					}
				}

				g_app_state.selecting = true;
				g_app_state.sel_start_y = g_app_state.sel_end_y =
					g_app_state.diff_scroll + (m.row - diff_top);
				g_app_state.sel_start_x = g_app_state.sel_end_x =
					m.col - g_app_state.render_x - 1;
			}
		}
	}
}

void handle_key(Key k) {
	if (k.type == KEY_MOUSE) {
		handle_mouse(k.mouse);
		return;
	}
	g_app_state.needs_sync = true;
	if (g_app_state.menu_active) {
		g_app_state.menu_active = false;
		return;
	}
	if (g_app_state.in_prompt) {
		handle_prompt_key(k);
		return;
	}
	if (g_app_state.focus == FOCUS_CLI) {
		handle_cli_key(k);
		return;
	}
	if (k.type == KEY_CHAR && k.ch == ':') {
		g_app_state.focus = FOCUS_CLI;
		return;
	}

	if (g_app_state.commit_bar_focused) {
		if (k.type == KEY_TAB || k.type == KEY_SHIFT_TAB) {
			g_app_state.commit_bar_focused = false;
		} else {
			int len = (int)strlen(g_app_state.commit_msg_buf);
			switch (k.type) {
				case KEY_ENTER:
					do_commit_bar();
					return;
				case KEY_ESC:
					g_app_state.commit_bar_focused = false;
					return;
				case KEY_BACKSPACE:
					if (g_app_state.commit_msg_cursor > 0) {
						memmove(&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor - 1],
								&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor],
								len - g_app_state.commit_msg_cursor + 1);
						g_app_state.commit_msg_cursor--;
					}
					return;
				case KEY_DEL:
					if (g_app_state.commit_msg_cursor < len) {
						memmove(&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor],
								&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor + 1],
								len - g_app_state.commit_msg_cursor);
					}
					return;
				case KEY_LEFT:
					if (g_app_state.commit_msg_cursor > 0) g_app_state.commit_msg_cursor--;
					return;
				case KEY_RIGHT:
					if (g_app_state.commit_msg_cursor < len) g_app_state.commit_msg_cursor++;
					return;
				case KEY_HOME:
				case KEY_CTRL_A:
					g_app_state.commit_msg_cursor = 0;
					return;
				case KEY_END:
					g_app_state.commit_msg_cursor = len;
					return;
				case KEY_CTRL_U:
					g_app_state.commit_msg_buf[0] = '\0';
					g_app_state.commit_msg_cursor = 0;
					return;
				case KEY_F4:
					do_amend_bar();
					return;
				case KEY_CHAR:
					if (len + 1 < INPUT_MAX) {
						memmove(&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor + 1],
								&g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor],
								len - g_app_state.commit_msg_cursor + 1);
						g_app_state.commit_msg_buf[g_app_state.commit_msg_cursor++] = k.ch;
					}
					return;
				default:
					break;
			}
			return;
		}
	}

	if (k.type == KEY_CTRL_F) {
		if (g_app_state.focus == FOCUS_EDITOR && !editor_current_tab_is_diff())
			prompt_start(UI->prompt_find, editor_find, false);
		else
			action_pull();
		return;
	}
	if (k.type == KEY_CTRL_G) {
		if (g_app_state.focus == FOCUS_EDITOR && !editor_current_tab_is_diff())
			prompt_start(UI->prompt_go_to_line, editor_goto_line, false);
		return;
	}
	if (k.type == KEY_CTRL_S) {
		if (g_app_state.focus == FOCUS_EDITOR && !editor_current_tab_is_diff())
			editor_save();
		else if (g_app_state.current_view == VIEW_STATUS && g_app_state.focus == FOCUS_CHANGES)
			action_stage();
		return;
	}
	if ((k.type == KEY_F1 || k.type == KEY_F2) &&
		(g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR)) {
		if (k.type == KEY_F1)
			editor_prev_tab();
		else
			editor_next_tab();
		return;
	}

	if (g_app_state.focus == FOCUS_BROWSER) {
		int cnt = g_app_state.browser_count;
		int bvis = g_app_state.rows - 6;
		switch (k.type) {
			case KEY_UP:
				msel(&g_app_state.browser_sel, &g_app_state.browser_scroll, cnt, -1, bvis, false);
				break;
			case KEY_DOWN:
				msel(&g_app_state.browser_sel, &g_app_state.browser_scroll, cnt, 1, bvis, false);
				break;
			case KEY_LEFT:
				load_browser("..");
				g_app_state.browser_sel = 0;
				break;
			case KEY_RIGHT:
			case KEY_ENTER:
				if (cnt > 0) {
					BrowserFile *f = &g_app_state.browser_files[g_app_state.browser_sel];
					if (f->is_dir) {
						char next[1024];
						snprintf(next, sizeof(next), "%s/%s", g_app_state.browser_path,
								 f->path);
						load_browser(next);
						g_app_state.browser_sel = 0;
					} else {
						char full[1024];
						snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path,
								 f->path);
						editor_load(full);
						g_app_state.editor_active = true;
						g_app_state.focus = FOCUS_EDITOR;
					}
				}
				break;
			case KEY_CHAR:
				if (k.ch == 'n') menu_new_file();
				if (k.ch == 'D') menu_delete_file();
				if (k.ch == 'b') {
					g_app_state.browser_active = false;
					g_app_state.focus = FOCUS_CHANGES;
				}
				break;
			case KEY_ESC:
			case KEY_TAB:
				g_app_state.focus = FOCUS_CHANGES;
				break;
			default:
				break;
		}
		if (!(k.type == KEY_CHAR && isdigit((unsigned char)k.ch))) return;
	}

	if (g_app_state.focus == FOCUS_EDITOR && g_app_state.tab_count > 0 &&
		!editor_current_tab_is_diff() &&
		k.type != KEY_TAB && k.type != KEY_SHIFT_TAB) {
		handle_editor_key(k);
		return;
	}

	if (k.type == KEY_CHAR) {
		switch (k.ch) {
			case '1':
				g_app_state.current_view = VIEW_STATUS;
				return;
			case '2':
				g_app_state.current_view = VIEW_LOG;
				return;
			case '3':
				g_app_state.current_view = VIEW_BRANCHES;
				return;
			case '4':
				g_app_state.current_view = VIEW_STASH;
				return;
			case '?':
				g_app_state.current_view =
					(g_app_state.current_view == VIEW_HELP) ? VIEW_STATUS : VIEW_HELP;
				return;
			case 'q':
				if (g_app_state.current_view == VIEW_HELP) {
					g_app_state.current_view = VIEW_STATUS;
					return;
				}
				if (g_app_state.focus == FOCUS_DIFF && g_app_state.current_view == VIEW_STATUS) {
					g_app_state.focus = FOCUS_CHANGES;
					return;
				}
				g_app_state.running = false;
				return;
			case 'R':
				reload_all();
				return;
			case 'c':
				g_app_state.commit_bar_focused = true;
				return;
			case 'A':
				action_amend();
				return;
			case 'P':
				action_push();
				return;
			case 'f':
				action_pull();
				return;
			case 's':
				action_stash();
				return;
			case 'y':
				if (g_app_state.ed_selecting)
					action_copy_editor_selection();
				else if (g_app_state.selecting)
					action_copy_selection();
				return;
			case 'e':
				/* Toggle editor */
				if (g_app_state.current_view == VIEW_STATUS) {
					if (g_app_state.focus == FOCUS_CHANGES && g_app_state.file_count > 0) {
						if (g_app_state.editor_active) {
							g_app_state.editor_active = false;
							g_app_state.focus = FOCUS_CHANGES;
							update_diff();
						} else {
							editor_load(g_app_state.files[g_app_state.file_sel].path);
							g_app_state.editor_active = true;
							g_app_state.focus = FOCUS_EDITOR;
						}
					} else if (g_app_state.focus == FOCUS_GRAPH && g_app_state.commit_count > 0) {
						GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
						if (g_app_state.graph_file_sel > 0) {
							editor_load(c->files[g_app_state.graph_file_sel]);
							g_app_state.editor_active = true;
							g_app_state.focus = FOCUS_EDITOR;
						} else {
							g_app_state.editor_active = !g_app_state.editor_active;
							if (!g_app_state.editor_active) {
								g_app_state.focus = FOCUS_CHANGES;
								update_diff();
							}
						}
					} else {
						g_app_state.editor_active = !g_app_state.editor_active;
						if (!g_app_state.editor_active) {
							g_app_state.focus = FOCUS_CHANGES;
							update_diff();
						}
					}
				} else if (g_app_state.current_view == VIEW_LOG) {
					if (!g_app_state.editor_active && g_app_state.commit_count > 0) {
						GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
						if (g_app_state.graph_file_sel > 0)
							editor_load(c->files[g_app_state.graph_file_sel]);
						else if (c->hash[0]) {
							g_app_state.editor_active = true;
							g_app_state.focus = FOCUS_EDITOR;
						}
					}
					g_app_state.editor_active = !g_app_state.editor_active;
					if (!g_app_state.editor_active) {
						g_app_state.focus = FOCUS_CHANGES;
						update_diff();
					}
				}
				return;
			case 'b':
				if (g_app_state.current_view == VIEW_STATUS) {
					g_app_state.browser_active = !g_app_state.browser_active;
					if (g_app_state.browser_active) {
						if (!g_app_state.browser_count) load_browser(".");
						g_app_state.focus = FOCUS_BROWSER;
					} else
						g_app_state.focus = FOCUS_CHANGES;
				}
				return;
			case 'T':
				g_app_state.theme_idx = (g_app_state.theme_idx + 1) % NTHEMES;
				OK(UI->msg_theme_fmt, TH->name);
				return;
			case 'H':
				g_app_state.diff_continuous = !g_app_state.diff_continuous;
				refresh_diff_after_context_toggle();
				OK(UI->msg_continuous_diff_fmt,
				   g_app_state.diff_continuous ? UI->diff_continuous_on
											  : UI->diff_continuous_off);
				return;
			case 'V': {
				const char *path = NULL;
				if (g_app_state.focus == FOCUS_EDITOR && g_app_state.tab_count > 0 &&
					!editor_current_tab_is_diff())
					path = g_app_state.tabs[g_app_state.tab_current].path;
				else if (g_app_state.focus == FOCUS_CHANGES && g_app_state.file_count > 0)
					path = g_app_state.files[g_app_state.file_sel].path;
				action_open_in_editor_extern(path ? path : "");
				return;
			}
			case 'W':
				g_app_state.diff_wrap = !g_app_state.diff_wrap;
				OK(UI->msg_diff_wrap_fmt,
				   g_app_state.diff_wrap ? UI->toggle_on : UI->toggle_off);
				return;
		}
	}

	if (k.type == KEY_CTRL_C) {
		if (g_app_state.ed_selecting)
			action_copy_editor_selection();
		else if (g_app_state.selecting)
			action_copy_selection();
		else
			action_commit();
		return;
	}
	if (k.type == KEY_CTRL_R || k.type == KEY_CTRL_L) {
		reload_all();
		return;
	}
	if (k.type == KEY_CTRL_P) {
		prompt_start(UI->prompt_go_to_file, action_find_file, false);
		return;
	}
	if (k.type == KEY_CTRL_K) {
		prompt_start(UI->prompt_global_search, action_grep, false);
		return;
	}
	if (k.type == KEY_CTRL_Q) {
		g_app_state.running = false;
		return;
	}
	if (k.type == KEY_ESC) {
		if (g_app_state.current_view == VIEW_HELP) {
			g_app_state.current_view = VIEW_STATUS;
			return;
		}
		if (g_app_state.focus == FOCUS_DIFF) {
			g_app_state.focus = FOCUS_CHANGES;
			return;
		}
		return;
	}

	if (k.type == KEY_TAB) {
		if (g_app_state.current_view == VIEW_STATUS) {
			FocusPane right_focus = right_pane_focus_pane();
			if (g_app_state.focus == FOCUS_CHANGES)
				g_app_state.focus = FOCUS_GRAPH;
			else if (g_app_state.focus == FOCUS_GRAPH)
				g_app_state.focus = right_focus;
			else if (g_app_state.focus == FOCUS_DIFF || g_app_state.focus == FOCUS_EDITOR)
				g_app_state.focus = g_app_state.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES;
			else if (g_app_state.focus == FOCUS_BROWSER)
				g_app_state.focus = right_focus;
		} else {
			g_app_state.current_view = (View)((g_app_state.current_view + 1) % VIEW_COUNT);
		}
		return;
	}
	if (k.type == KEY_SHIFT_TAB) {
		if (g_app_state.current_view == VIEW_STATUS) {
			FocusPane right_focus = right_pane_focus_pane();
			if (g_app_state.focus == FOCUS_CHANGES)
				g_app_state.focus = right_focus;
			else if (g_app_state.focus == FOCUS_GRAPH)
				g_app_state.focus = g_app_state.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES;
			else if (g_app_state.focus == FOCUS_DIFF || g_app_state.focus == FOCUS_EDITOR)
				g_app_state.focus = FOCUS_GRAPH;
			else if (g_app_state.focus == FOCUS_BROWSER)
				g_app_state.focus = right_focus;
		} else {
			g_app_state.current_view =
				(View)((g_app_state.current_view + VIEW_COUNT - 1) % VIEW_COUNT);
		}
		return;
	}

	/* View specific navigation */
	if (g_app_state.current_view == VIEW_STATUS) {
		if (k.type == KEY_LEFT) {
			if (g_app_state.focus == FOCUS_DIFF)
				g_app_state.focus = FOCUS_GRAPH;
			else if (g_app_state.focus == FOCUS_GRAPH)
				g_app_state.focus = FOCUS_CHANGES;
			return;
		}
		if (k.type == KEY_RIGHT) {
			if (g_app_state.focus == FOCUS_CHANGES)
				g_app_state.focus = FOCUS_GRAPH;
			else if (g_app_state.focus == FOCUS_GRAPH)
				g_app_state.focus = right_pane_focus_pane();
			return;
		}
	}
	if (g_app_state.current_view == VIEW_STATUS && g_app_state.focus == FOCUS_CHANGES) {
		int cnt = g_app_state.file_count;
		int cvis = imax(1, g_app_state.layout_height_changes - 6);
		switch (k.type) {
			case KEY_UP:
				msel(&g_app_state.file_sel, &g_app_state.file_scroll, cnt, -1, cvis, false);
				break;
			case KEY_DOWN:
				msel(&g_app_state.file_sel, &g_app_state.file_scroll, cnt, 1, cvis, false);
				break;
			case KEY_PGUP:
				msel(&g_app_state.file_sel, &g_app_state.file_scroll, cnt, -cvis, cvis, false);
				break;
			case KEY_PGDN:
				msel(&g_app_state.file_sel, &g_app_state.file_scroll, cnt, cvis, cvis, false);
				break;
			case KEY_HOME:
				g_app_state.file_sel = 0;
				g_app_state.file_scroll = 0;
				break;
			case KEY_END:
				g_app_state.file_sel = cnt > 0 ? cnt - 1 : 0;
				break;
			case KEY_CHAR:
				if (k.ch == ' ')
					action_stage();
				else if (k.ch == 'a')
					action_stage_all();
				else if (k.ch == 'u')
					action_unstage_all();
				else if (k.ch == 'd')
					action_discard();
				else if (k.ch == 's')
					action_stash();
				else if (k.ch == 'g') {
					g_app_state.file_sel = 0;
					g_app_state.file_scroll = 0;
				} else if (k.ch == 'G') {
					g_app_state.file_sel = cnt > 0 ? cnt - 1 : 0;
				} else if (k.ch == '>') {
					toggle_commit_expansion();
				} else if (k.ch == '=') {
					focus_diff_pane();
				}
				break;
			case KEY_ENTER:
				focus_diff_pane();
				break;
			default:
				break;
		}
		update_diff();
	} else if (g_app_state.current_view == VIEW_STATUS && g_app_state.focus == FOCUS_GRAPH) {
		int cnt = g_app_state.commit_count;
		int gvis = g_app_state.layout_height_graph - 2;
		GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
		switch (k.type) {
			case KEY_UP:
				if (c->expanded && g_app_state.graph_file_sel >= 0) {
					g_app_state.graph_file_sel--;
				} else {
					msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, -1, gvis,
						 true);
					c = &g_app_state.commits[g_app_state.commit_sel];
					if (c->expanded)
						g_app_state.graph_file_sel = c->file_count - 1;
					else
						g_app_state.graph_file_sel = -1;
				}
				break;
			case KEY_DOWN:
				if (c->expanded && g_app_state.graph_file_sel < c->file_count - 1) {
					g_app_state.graph_file_sel++;
				} else {
					msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, 1, gvis,
						 true);
					g_app_state.graph_file_sel = -1;
				}
				break;
			case KEY_PGUP:
				msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, -gvis / 2, gvis,
					 true);
				g_app_state.graph_file_sel = -1;
				break;
			case KEY_PGDN:
				msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, gvis / 2, gvis,
					 true);
				g_app_state.graph_file_sel = -1;
				break;
			case KEY_HOME:
				g_app_state.commit_sel = 0;
				g_app_state.commit_scroll = 0;
				g_app_state.graph_file_sel = -1;
				break;
			case KEY_END:
				g_app_state.commit_sel = cnt ? cnt - 1 : 0;
				g_app_state.graph_file_sel = -1;
				break;
			case KEY_RIGHT:
				if (!c->expanded) {
					c->expanded = true;
					fetch_commit_files(g_app_state.commit_sel);
				} else if (g_app_state.graph_file_sel == -1 && c->file_count > 0) {
					g_app_state.graph_file_sel = 0;
				}
				break;
			case KEY_LEFT:
				if (c->expanded) {
					if (g_app_state.graph_file_sel >= 0)
						g_app_state.graph_file_sel = -1;
					else
						c->expanded = false;
				}
				break;
			case KEY_CHAR:
				if (k.ch == ' ')
					toggle_commit_expansion();
				else if (k.ch == 'a')
					action_stage_all();
				else if (k.ch == 'u')
					action_unstage_all();
				else if (k.ch == 'd')
					action_discard();
				else if (k.ch == 's')
					action_stash();
				else if (k.ch == 'g') {
					g_app_state.commit_sel = 0;
					g_app_state.commit_scroll = 0;
					g_app_state.graph_file_sel = -1;
				} else if (k.ch == 'G') {
					g_app_state.commit_sel = g_app_state.commit_count > 0
												 ? g_app_state.commit_count - 1
												 : 0;
					g_app_state.graph_file_sel = -1;
				} else if (k.ch == '>' || k.ch == '=') {
					toggle_commit_expansion();
				}
				break;
			case KEY_ENTER:
				if (g_app_state.commit_count > 0) {
					g_app_state.diff_staged = false;
					g_app_state.diff_is_summary = false;
					sync_graph_preview();
					focus_diff_pane();
				}
				break;
			default:
				break;
		}
		if (g_app_state.commit_count > 0 && k.type != KEY_ENTER) sync_graph_preview();
	} else if (g_app_state.current_view == VIEW_LOG) {
		int cnt = g_app_state.commit_count;
		int vis = g_app_state.layout_height_log - 2;
		switch (k.type) {
			case KEY_UP:
				msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, -1, vis, false);
				show_log_commit();
				break;
			case KEY_DOWN:
				msel(&g_app_state.commit_sel, &g_app_state.commit_scroll, cnt, 1, vis, false);
				show_log_commit();
				break;
			case KEY_ENTER:
				show_log_commit();
				break;
			default:
				break;
		}
	} else if (g_app_state.current_view == VIEW_BRANCHES) {
		int cnt = g_app_state.branch_count;
		int vis = g_app_state.rows - 2;
		switch (k.type) {
			case KEY_UP:
				msel(&g_app_state.branch_sel, &g_app_state.branch_scroll, cnt, -1, vis, false);
				break;
			case KEY_DOWN:
				msel(&g_app_state.branch_sel, &g_app_state.branch_scroll, cnt, 1, vis, false);
				break;
			case KEY_ENTER:
				action_checkout();
				break;
			case KEY_CHAR:
				if (k.ch == 'n') action_new_branch();
				if (k.ch == 'D') action_delete_branch();
				break;
			default:
				break;
		}
	} else if (g_app_state.current_view == VIEW_STASH) {
		int cnt = g_app_state.stash_count;
		switch (k.type) {
			case KEY_UP:
				g_app_state.stash_sel =
					iclamp(g_app_state.stash_sel - 1, 0, cnt > 0 ? cnt - 1 : 0);
				break;
			case KEY_DOWN:
				g_app_state.stash_sel =
					iclamp(g_app_state.stash_sel + 1, 0, cnt > 0 ? cnt - 1 : 0);
				break;
			case KEY_ENTER:
				action_apply_stash();
				break;
			case KEY_CHAR:
				if (k.ch == 'p') action_pop_stash();
				if (k.ch == 'D') action_drop_stash();
				if (k.ch == 's') action_stash();
				break;
			default:
				break;
		}
	} else if (g_app_state.focus == FOCUS_DIFF) {
		int vis =
			((g_app_state.current_view == VIEW_LOG) ? g_app_state.diff_height_log
													: (g_app_state.rows - 2)) -
			2;
		vis = imax(1, vis);
		switch (k.type) {
			case KEY_UP:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel = imax(0, g_app_state.diff_sel - 1);
				else
					g_app_state.diff_scroll = imax(0, g_app_state.diff_scroll - 1);
				break;
			case KEY_DOWN:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel =
						imin(g_app_state.diff_count > 0 ? g_app_state.diff_count - 1 : 0,
							 g_app_state.diff_sel + 1);
				else
					g_app_state.diff_scroll++;
				break;
			case KEY_PGUP:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel = imax(0, g_app_state.diff_sel - vis);
				else
					g_app_state.diff_scroll = imax(0, g_app_state.diff_scroll - vis);
				break;
			case KEY_PGDN:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel =
						imin(g_app_state.diff_count > 0 ? g_app_state.diff_count - 1 : 0,
							 g_app_state.diff_sel + vis);
				else
					g_app_state.diff_scroll += vis;
				break;
			case KEY_HOME:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel = 0;
				else
					g_app_state.diff_scroll = 0;
				break;
			case KEY_END:
				if (g_app_state.diff_is_summary)
					g_app_state.diff_sel =
						g_app_state.diff_count > 0 ? g_app_state.diff_count - 1 : 0;
				else
					g_app_state.diff_scroll = g_app_state.diff_count;
				break;
			case KEY_CTRL_C:
				if (g_app_state.selecting) action_copy_selection();
				break;
			case KEY_CHAR:
				if (k.ch == ' ') {
					if (g_app_state.diff_staged)
						action_unstage_selection();
					else
						action_stage_selection();
				} else if (k.ch == 'S') {
					action_stage_selection();
				} else if (k.ch == 'U') {
					action_unstage_selection();
				}
				break;
			case KEY_ENTER:
				if (g_app_state.diff_is_summary && g_app_state.diff_count > 0)
					open_diff_summary_at(g_app_state.diff_sel);
				break;
			default:
				break;
		}
		if (g_app_state.diff_is_summary) {
			int max_sel = g_app_state.diff_count > 0 ? g_app_state.diff_count - 1 : 0;
			g_app_state.diff_sel = iclamp(g_app_state.diff_sel, 0, max_sel);
			int maxsc = imax(0, g_app_state.diff_count - vis);
			if (g_app_state.diff_sel < g_app_state.diff_scroll)
				g_app_state.diff_scroll = g_app_state.diff_sel;
			if (g_app_state.diff_sel >= g_app_state.diff_scroll + vis)
				g_app_state.diff_scroll = g_app_state.diff_sel - vis + 1;
			g_app_state.diff_scroll = iclamp(g_app_state.diff_scroll, 0, maxsc);
		}
	}
}
