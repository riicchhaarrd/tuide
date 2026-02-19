#ifndef RENDER_H
#define RENDER_H

#include "state.h"

/* Theme Structure */
typedef struct {
	const char *name;
	Color bg_base, bg_panel, bg_sel, bg_tab_act, bg_tab_inact, bg_header;
	Color bg_diff_add, bg_diff_del, bg_diff_hdr;
	Color fg_normal, fg_dim, fg_bright, fg_sel;
	Color fg_accent1, fg_accent2, fg_accent3;
	Color fg_staged, fg_unstaged, fg_untracked, fg_conflict;
	Color fg_diff_add, fg_diff_del, fg_diff_hdr, fg_diff_ctx;
	Color fg_graph[6];
	Color fg_ref_local, fg_ref_remote, fg_ref_tag;
	Color fg_ok, fg_err, fg_linenum;
} Theme;

#define NTHEMES 4
extern const Theme *THEMES[NTHEMES];
#define TH (THEMES[g_app_state.theme_idx])

/* Drawing Primitives */
void buf_clear(Buffer *b);
void put_cell(int r, int c, const char *s);
void at(int r, int c);
void cfg(Color c);
void cbg(Color c);
void rst(void);
void draw_flush(void);
void ppad_ext(const char *s, int w, int y_idx, int pane_rx);
void ppad(const char *s, int w);

/* Box Drawing */
void box_top(int row, int col, int w, const char *title, bool active, const char *extra);
void box_bot(int row, int col, int w, bool active);
void box_sides(int top, int col, int w, int h, bool active);
void box_fill(int top, int col, int w, int h, Color c);

/* Misc */
void draw_scrollbar(int r, int c, int h, int total, int vis, int scroll, bool active);
bool is_selected(int y, int x);

#endif
