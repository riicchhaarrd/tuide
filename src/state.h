#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <termios.h>
#include <time.h>

#include "util.h"

/* ================================================================
   ENUMS & STRUCTS
================================================================ */
typedef enum {
	VIEW_STATUS,
	VIEW_LOG,
	VIEW_BRANCHES,
	VIEW_STASH,
	VIEW_EDITOR,
	VIEW_HELP,
	VIEW_COUNT
} View;

typedef enum {
	FOCUS_CHANGES,
	FOCUS_GRAPH,
	FOCUS_DIFF,
	FOCUS_BROWSER,
	FOCUS_EDITOR,
	FOCUS_CLI
} FocusPane;

typedef enum {
	FS_UNTRACKED,
	FS_MODIFIED,
	FS_STAGED_MODIFY,
	FS_STAGED_NEW,
	FS_STAGED_DEL,
	FS_DELETED,
	FS_RENAMED,
	FS_CONFLICT,
	FS_COPIED
} FileStatus;

typedef struct {
	char *text;
	int cursor_row, cursor_col;
} HistEntry;

typedef struct {
	char **lines;
	int line_count, line_capacity;
	int cursor_row, cursor_col;
	int scroll_row, scroll_col;
	char filename[512];
	bool modified;
	uint64_t saved_hash;
	bool needs_sync;
	HistEntry undo_stack[MAX_UNDO];
	int undo_top;
	HistEntry redo_stack[MAX_UNDO];
	int redo_top;
} Editor;

typedef struct {
	Editor ed;
	char path[512];
} Tab;

typedef struct {
	char path[512];
	bool is_dir;
} BrowserFile;

typedef struct {
	char path[512];
	char original_path[512];
	FileStatus status;
	bool staged;
} GitFile;

typedef struct {
	char hash[16], author[64], email[64], date[32], subject[256], refs[192];
	char graph[24];
	int graph_col;
	bool expanded;
	char files[16][128];
	int file_count;
} GitCommit;

typedef struct {
	char name[128], upstream[128];
	bool is_remote, is_current;
	int ahead, behind;
} GitBranch;

typedef struct {
	char message[256], hash[16];
	int index;
} GitStash;

typedef struct {
	char old_line[LINE_MAX_LEN], new_line[LINE_MAX_LEN];
	int old_lno, new_lno;
	int type; /* 0=ctx 1=add 2=del 3=hunk 4=fhdr 5=file */
} DiffLine;

typedef struct {
	int r, g, b;
} Color;

typedef struct {
	char ch[8];
	Color fg, bg;
	bool bold, dim, italic, under, rev;
} Cell;

typedef struct {
	Cell *cells;
	int w, h;
} Buffer;

/* ================================================================
   GLOBAL STATE
================================================================ */
typedef struct {
	struct termios orig_termios;
	int rows, cols;
	bool running;
	int theme_idx;

	/* Rendering */
	Buffer front, back;
	Color cur_fg, cur_bg;
	bool cur_bold, cur_dim, cur_italic, cur_under, cur_rev;
	int cur_r, cur_c;

	/* View / focus */
	View current_view;
	FocusPane focus;

	/* Changes */
	GitFile files[MAX_FILES];
	int file_count, file_sel, file_scroll;

	/* Log/graph */
	GitCommit commits[MAX_COMMITS];
	int commit_count, commit_sel, commit_scroll;
	int graph_file_sel;
	struct {
		int commit_idx, file_idx;
	} graph_rows[MAX_COMMITS * 17];
	int graph_rows_count;

	/* Diff */
	DiffLine diff_lines[MAX_DIFF_LINES];
	int diff_count, diff_scroll, diff_hscroll;
	char diff_title[512], diff_commit[64];
	bool diff_staged, diff_sidebyside, diff_is_summary, diff_continuous, diff_wrap;
	int diff_sel;
	int diff_split, diff_split_custom;

	/* Branches */
	GitBranch branches[MAX_BRANCHES];
	int branch_count, branch_sel, branch_scroll;

	/* Stash */
	GitStash stashes[MAX_STASHES];
	int stash_count, stash_sel;

	/* Prompt */
	bool in_prompt, prompt_obscure;
	char prompt_label[128], prompt_buf[INPUT_MAX];
	int prompt_cursor;
	void (*prompt_cb)(const char *);

	/* CLI */
	char cli_buf[INPUT_MAX];
	int cli_cursor;

	/* Status */
	char status_msg[256];
	time_t status_msg_time;
	bool status_is_err;
	bool needs_sync;

	/* Repo */
	char branch_name[128];

	/* Editor & Browser */
	Tab tabs[MAX_TABS];
	int tab_count, tab_current;
	bool editor_diff_tab;

	BrowserFile browser_files[1024];
	int browser_count, browser_sel, browser_scroll;
	char browser_path[512];
	bool editor_active;
	bool browser_active;
	int ed_sel_start_y, ed_sel_start_x;
	int ed_sel_end_y, ed_sel_end_x;
	bool ed_selecting;

	/* Selection in Diff */
	int sel_start_y, sel_start_x;
	int sel_end_y, sel_end_x;
	bool selecting;
	char *clipboard;

	/* Layout */
	int sidebar_w;
	int layout_width, layout_height_changes, layout_height_graph, render_x, render_width;
	int layout_height_log, diff_height_log;
	int lw_custom, lh_chg_custom, lh_log_custom;
	bool dragging_v, dragging_h, dragging_sc, dragging_diff, dragging_h_log;
	bool dragging_col_hash, dragging_col_author, dragging_col_date;
	int col_hash_w, col_author_w, col_date_w;
	int scrollbar_y, scrollbar_height, scrollbar_total, scrollbar_visible, scrollbar_drag_offset;
	bool dragging_ed_sc;
	int editor_scrollbar_x, editor_scrollbar_y, editor_scrollbar_height, editor_scrollbar_total,
		editor_scrollbar_visible, editor_scrollbar_drag_offset;
	int tab_x[7];
	int ed_tab_x[MAX_TABS + 2];

	/* Commit Bar */
	char commit_msg_buf[INPUT_MAX];
	int commit_msg_cursor;
	bool commit_bar_focused;

	/* Editor search */
	char ed_search[INPUT_MAX];

	/* Context Menu */
	bool menu_active;
	int menu_x, menu_y, menu_w, menu_h;
	char menu_items[12][32];
	void (*menu_actions[12])(void);
	int menu_item_count;
	int last_mx, last_my;
} AppState;

extern AppState g_app_state;

#endif
