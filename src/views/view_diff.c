#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../editor.h"
#include "../render.h"
#include "../state.h"
#include "../ui_strings.h"
#include "../syntax.h"
#include "../util.h"
#include "../views.h"

/* For storing the current diff file's language for syntax highlighting */
static LangType current_diff_lang = LANG_NONE;

/* Set the language for the current diff based on file path */
void diff_set_language(const char *filepath) {
	current_diff_lang = syntax_detect_language(filepath);
}

/* Helper: draw text with syntax highlighting and specific background */
static void draw_text_with_syntax(const char *text, int text_len, int max_width,
                                   int row, int start_col, Color bg_color,
                                   int diff_idx, int pane_rx) {
	if (!text || text_len <= 0 || max_width <= 0) return;

	/* Tokenize the line */
	Token tokens[256];
	int token_count = syntax_tokenize_line(text, text_len, current_diff_lang,
	                                        tokens, 256);

	int pos = 0;
	int col = start_col;
	int tok_idx = 0;

	while (pos < text_len && col < start_col + max_width) {
		/* Find current token */
		Color fg = TH->fg_normal;
		bool in_token = false;

		while (tok_idx < token_count) {
			Token *tok = &tokens[tok_idx];
			if (tok->start <= pos && pos < tok->end) {
				fg = syntax_token_color(tok->type, current_diff_lang);
				in_token = true;
				break;
			} else if (tok->start >= pos) {
				break;
			}
			tok_idx++;
		}

		/* Check if we're at a token boundary */
		if (tok_idx < token_count && pos == tokens[tok_idx].start) {
			fg = syntax_token_color(tokens[tok_idx].type, current_diff_lang);
			in_token = true;
		}

		cfg(fg);
		cbg(bg_color);

		char cs[2] = {text[pos], 0};
		put_cell(row, col++, cs);
		pos++;
	}

	/* Fill remaining with background */
	while (col < start_col + max_width) {
		cfg(TH->fg_normal);
		cbg(bg_color);
		put_cell(row, col++, " ");
	}
}

static int diff_wrap_rows(int text_len, int code_w) {
	if (!g_app_state.diff_wrap || code_w <= 0 || text_len <= code_w) return 1;
	int rows = (text_len + code_w - 1) / code_w;
	if (rows > 20) rows = 20;
	return rows;
}

static int diff_wrap_slice(const char *text, int text_len, int code_w, int wr, char *out) {
	int wrap_offset = wr * code_w;
	int wrap_avail = text_len - wrap_offset;
	if (wrap_avail < 0) wrap_avail = 0;
	int wrap_this = wrap_avail < code_w ? wrap_avail : code_w;
	if (wrap_this > 0) memcpy(out, text + wrap_offset, wrap_this);
	out[wrap_this] = '\0';
	return wrap_this;
}

void draw_diff(int top, int render_x, int render_width, int h) {
	if (h <= 2) return;
	bool act = (g_app_state.focus == FOCUS_DIFF);
	bool mixed_tabs =
		((g_app_state.editor_active || g_app_state.current_view == VIEW_EDITOR) &&
		 editor_current_tab_is_diff());
	char title[128];
	if (g_app_state.diff_title[0])
		snprintf(title, sizeof(title), "%.60s%s", g_app_state.diff_title,
				 g_app_state.diff_staged ? UI->diff_title_staged_suffix : "");
	else
		snprintf(title, sizeof(title), "%s", UI->diff_title_empty);

	char extra[64];
	const char *wrap_label =
		g_app_state.diff_wrap ? UI->diff_wrap_label_on : UI->diff_wrap_label_off;
	snprintf(extra, sizeof(extra), " [%s] [%s] [%s] ",
			 g_app_state.diff_sidebyside ? UI->diff_side_split : UI->diff_side_unify,
			 g_app_state.diff_continuous ? UI->diff_ctx_full : UI->diff_ctx_hunk, wrap_label);

	box_top(top, render_x, render_width, title, act, extra);
	box_sides(top, render_x, render_width, h, act);
	box_fill(top, render_x, render_width, h, TH->bg_base);
	box_bot(top + h - 1, render_x, render_width, act);

	int content_top = top + 1 + (mixed_tabs ? 1 : 0);
	if (mixed_tabs) draw_editor_tabs_row(top + 1, render_x, render_width);

	int row = content_top, lim = top + h - 1, vis = lim - row;
	if (vis < 1) return;
	int maxsc = imax(0, g_app_state.diff_count - vis);
	if (g_app_state.diff_scroll < 0) g_app_state.diff_scroll = 0;
	if (g_app_state.diff_scroll > maxsc) g_app_state.diff_scroll = maxsc;

	if (!g_app_state.diff_count) {
		int msg_w = str_display_width(UI->diff_empty_msg);
		int max_w = render_width - 2;
		if (max_w < 1) max_w = 1;
		int draw_w = iclamp(msg_w, 1, max_w);
		int msg_x = render_x + (render_width - draw_w) / 2;
		at(row + vis / 2, msg_x);
		cfg(TH->fg_dim);
		ppad(UI->diff_empty_msg, draw_w);
		rst();
		return;
	}

	bool ssb = g_app_state.diff_sidebyside;
	int lnum_w = 4;
	int half = g_app_state.diff_split;
	int code_w_left = half - lnum_w - 2;
	int code_w_right = (render_width - half) - lnum_w - 3;
	if (code_w_left < 2) code_w_left = 2;
	if (code_w_right < 2) code_w_right = 2;

	if (ssb) {
		cfg(TH->fg_dim);
		for (int r = row + 1; r < lim; r++) {
			at(r, render_x + half);
			put_cell(r, render_x + half, "│");
		}
		at(row, render_x + 1);
		cbg(TH->bg_header);
		cfg(TH->fg_accent3);
		g_app_state.cur_bold = true;
		ppad(UI->diff_label_old, half - 1);
		at(row, render_x + half + 1);
		ppad(UI->diff_label_new, (render_width - half) - 1);
		rst();
		row++;
		lim--;
		vis--;
		if (g_app_state.diff_scroll > imax(0, g_app_state.diff_count - vis))
			g_app_state.diff_scroll = imax(0, g_app_state.diff_count - vis);
	}

	int di = g_app_state.diff_scroll;

	if (!ssb) {
		int gutter_w = 1;
		int code_w = render_width - lnum_w - 5 - gutter_w - 3;
		if (code_w < 8) code_w = 8;
		for (; di < g_app_state.diff_count && row < lim;) {
			DiffLine *dl = &g_app_state.diff_lines[di];
			const char *text = (dl->type == 2) ? dl->old_line : dl->new_line;
			int text_len = (int)strlen(text);
			int nrows = 1;
			if (g_app_state.diff_wrap && code_w > 0 && text_len > code_w && dl->type <= 2) {
				nrows = (text_len + code_w - 1) / code_w;
				if (nrows > 20) nrows = 20;
			}
			if (row + nrows > lim) nrows = lim - row;
			if (nrows < 1) nrows = 1;

			char lno[16];
			Color bg_line = TH->bg_base;
			Color fg_gutter = TH->fg_dim;
			const char *gutter_char = " ";
			bool is_add = (dl->type == 1);
			bool is_del = (dl->type == 2);
			if (is_add) {
				bg_line = TH->bg_diff_add;
				fg_gutter = TH->fg_ok;
				gutter_char = "\xe2\x96\x8c";
			} else if (is_del) {
				bg_line = TH->bg_diff_del;
				fg_gutter = TH->fg_err;
				gutter_char = "\xe2\x96\x8c";
			} else if (dl->type == 3) {
				bg_line = TH->bg_diff_hdr;
				fg_gutter = TH->fg_accent3;
				gutter_char = "\xe2\x97\x89";
			} else if (dl->type == 4) {
				bg_line = TH->bg_header;
				fg_gutter = TH->fg_accent2;
				gutter_char = "\xe2\x94\x80";
			}

			for (int wr = 0; wr < nrows && row < lim; wr++, row++) {
				at(row, render_x + 1);
				int wrap_offset = wr * code_w;
				int wrap_avail = text_len - wrap_offset;
				if (wrap_avail < 0) wrap_avail = 0;
				int wrap_this = wrap_avail < code_w ? wrap_avail : code_w;
				char tmp[LINE_MAX_LEN + 1];

				switch (dl->type) {
					case 0: /* Context */
						cbg(TH->bg_base);
						cfg(TH->fg_dim);
						put_cell(row, render_x + 1, " ");
						at(row, render_x + 2);
						cfg(TH->fg_linenum);
						if (wr == 0) {
							snprintf(lno, sizeof(lno), "%*d ", lnum_w, dl->old_lno);
							ppad_ext(lno, lnum_w + 1, di, render_x);
						} else {
							for (int kk = 0; kk <= lnum_w; kk++)
								put_cell(row, render_x + 2 + kk, " ");
							at(row, render_x + 3 + lnum_w);
						}
						cfg(TH->fg_dim);
						ppad_ext(" ", 1, di, render_x);
						/* Use syntax highlighting for context */
						if (wrap_this > 0) {
							int content_col = render_x + 3 + lnum_w + 1;
							draw_text_with_syntax(text + wrap_offset, wrap_this, code_w,
							                     row, content_col, TH->bg_base, di, render_x);
						} else {
							/* Fill with spaces */
							int content_col = render_x + 3 + lnum_w + 1;
							for (int k = 0; k < code_w; k++) {
								cbg(TH->bg_base);
								cfg(TH->fg_normal);
								put_cell(row, content_col + k, " ");
							}
						}
						break;
					case 1: /* Added */
						cbg(bg_line);
						cfg(fg_gutter);
						g_app_state.cur_bold = true;
						put_cell(row, render_x + 1, gutter_char);
						g_app_state.cur_bold = false;
						at(row, render_x + 2);
						cfg(TH->fg_linenum);
						if (wr == 0) {
							if (dl->new_lno > 0)
								snprintf(lno, sizeof(lno), "%*d ", lnum_w, dl->new_lno);
							else
								snprintf(lno, sizeof(lno), "%*s ", lnum_w, "");
							ppad_ext(lno, lnum_w + 1, di, render_x);
						} else {
							for (int kk = 0; kk <= lnum_w; kk++)
								put_cell(row, render_x + 2 + kk, " ");
							at(row, render_x + 3 + lnum_w);
						}
						cfg(fg_gutter);
						ppad_ext("+", 1, di, render_x);
						/* Use syntax highlighting for added lines */
						if (wrap_this > 0) {
							int content_col = render_x + 3 + lnum_w + 1;
							draw_text_with_syntax(text + wrap_offset, wrap_this, code_w,
							                     row, content_col, bg_line, di, render_x);
						} else {
							/* Fill with spaces */
							int content_col = render_x + 3 + lnum_w + 1;
							for (int k = 0; k < code_w; k++) {
								cbg(bg_line);
								cfg(TH->fg_normal);
								put_cell(row, content_col + k, " ");
							}
						}
						break;
					case 2: /* Deleted */
						cbg(bg_line);
						cfg(fg_gutter);
						g_app_state.cur_bold = true;
						put_cell(row, render_x + 1, gutter_char);
						g_app_state.cur_bold = false;
						at(row, render_x + 2);
						cfg(TH->fg_linenum);
						if (wr == 0) {
							if (dl->old_lno > 0)
								snprintf(lno, sizeof(lno), "%*d ", lnum_w, dl->old_lno);
							else
								snprintf(lno, sizeof(lno), "%*s ", lnum_w, "");
							ppad_ext(lno, lnum_w + 1, di, render_x);
						} else {
							for (int kk = 0; kk <= lnum_w; kk++)
								put_cell(row, render_x + 2 + kk, " ");
							at(row, render_x + 3 + lnum_w);
						}
						cfg(fg_gutter);
						ppad_ext("-", 1, di, render_x);
						/* Use syntax highlighting for deleted lines */
						if (wrap_this > 0) {
							int content_col = render_x + 3 + lnum_w + 1;
							draw_text_with_syntax(text + wrap_offset, wrap_this, code_w,
							                     row, content_col, bg_line, di, render_x);
						} else {
							/* Fill with spaces */
							int content_col = render_x + 3 + lnum_w + 1;
							for (int k = 0; k < code_w; k++) {
								cbg(bg_line);
								cfg(TH->fg_normal);
								put_cell(row, content_col + k, " ");
							}
						}
						break;
					case 3: { /* Hunk header */
						cbg(bg_line);
						cfg(fg_gutter);
						g_app_state.cur_bold = true;
						put_cell(row, render_x + 1, gutter_char);
						g_app_state.cur_bold = false;
						at(row, render_x + 2);
						cbg(bg_line);
						cfg(TH->fg_accent3);
						char *hs2 = strstr(dl->new_line, "@@");
						char *he = hs2 ? strstr(hs2 + 2, "@@") : NULL;
						char range[64] = "";
						char ctx2[LINE_MAX_LEN] = "";
						if (hs2 && he) {
							int rlen = (int)(he - hs2 + 2);
							if (rlen > 63) rlen = 63;
							memcpy(range, hs2, rlen);
							range[rlen] = '\0';
							const char *ctxp = he + 2;
							while (*ctxp == ' ') ctxp++;
							snprintf(ctx2, sizeof(ctx2), "%s", ctxp);
						}
						char hbuf[LINE_MAX_LEN];
						snprintf(hbuf, sizeof(hbuf), " %s  %s", range, ctx2[0] ? ctx2 : "");
						g_app_state.cur_bold = true;
						ppad_ext(hbuf, render_width - 3, di, render_x);
						break;
					}
					case 4: /* File header */
						cbg(bg_line);
						cfg(fg_gutter);
						g_app_state.cur_bold = true;
						put_cell(row, render_x + 1, gutter_char);
						g_app_state.cur_bold = false;
						at(row, render_x + 2);
						cbg(bg_line);
						cfg(TH->fg_accent2);
						g_app_state.cur_bold = true;
						ppad_ext(dl->new_line[0] ? dl->new_line : dl->old_line, render_width - 3,
								 di, render_x);
						break;
					case 5: {
						bool sel =
							(g_app_state.diff_is_summary && g_app_state.diff_sel == di && act);
						cbg(TH->bg_base);
						cfg(TH->fg_dim);
						put_cell(row, render_x + 1, " ");
						at(row, render_x + 2);
						if (sel) {
							cbg(TH->bg_sel);
							cfg(TH->fg_sel);
							g_app_state.cur_bold = true;
						} else {
							cbg(TH->bg_base);
							cfg(TH->fg_accent1);
						}
						for (int kk = 0; kk < lnum_w + 1; kk++)
							put_cell(row, render_x + 2 + kk, " ");
						at(row, render_x + 3 + lnum_w);
						char fbuf[LINE_MAX_LEN + 4];
						snprintf(fbuf, sizeof(fbuf), " \xe2\x86\x92 %s", dl->new_line);
						ppad_ext(fbuf, render_width - 4 - lnum_w, di, render_x);
						break;
					}
					case 6:
						cbg(TH->bg_panel);
						cfg(TH->fg_dim);
						put_cell(row, render_x + 1, " ");
						at(row, render_x + 2);
						cbg(TH->bg_panel);
						cfg(TH->fg_dim);
						g_app_state.cur_italic = true;
						for (int kk = 0; kk < lnum_w + 1; kk++)
							put_cell(row, render_x + 2 + kk, " ");
						at(row, render_x + 3 + lnum_w);
						ppad_ext(dl->new_line[0] ? dl->new_line : dl->old_line,
								 render_width - 4 - lnum_w, di, render_x);
						g_app_state.cur_italic = false;
						break;
					default:
						break;
				}
				rst();
			}
			di++;
		}
	} else {
		while (di < g_app_state.diff_count && row < lim) {
			DiffLine *dl = &g_app_state.diff_lines[di];
			if (dl->type == 3 || dl->type == 4 || dl->type == 6) {
				at(row, render_x + 1);
				if (dl->type == 3) {
					cbg(TH->bg_diff_hdr);
					cfg(TH->fg_accent3);
					char hsub[LINE_MAX_LEN];
					char *hs = strstr(dl->new_line, "@@");
					if (hs) {
						hs = strstr(hs + 2, "@@");
						if (hs) hs += 2;
					}
					snprintf(hsub, sizeof(hsub), " \xe2\x97\x89 %s", hs ? hs : dl->new_line);
					g_app_state.cur_bold = true;
					ppad_ext(hsub, half - 1, di, render_x);
					at(row, render_x + half);
					cfg(TH->fg_dim);
					put_cell(row, render_x + half, "\xe2\x94\x82");
					at(row, render_x + half + 1);
					cbg(TH->bg_diff_hdr);
					ppad_ext("", (render_width - half) - 1, di, render_x);
				} else if (dl->type == 4) {
					cbg(TH->bg_header);
					cfg(TH->fg_accent2);
					g_app_state.cur_bold = true;
					put_cell(row, render_x + 1, "\xe2\x94\x80");
					at(row, render_x + 2);
					ppad_ext(dl->new_line[0] ? dl->new_line : dl->old_line, half - 2, di, render_x);
					at(row, render_x + half);
					cfg(TH->fg_dim);
					put_cell(row, render_x + half, "\xe2\x94\x82");
					at(row, render_x + half + 1);
					cbg(TH->bg_header);
					ppad_ext("", (render_width - half) - 1, di, render_x);
				} else {
					cbg(TH->bg_panel);
					cfg(TH->fg_dim);
					g_app_state.cur_italic = true;
					ppad_ext(dl->new_line[0] ? dl->new_line : dl->old_line, half - 1, di, render_x);
					at(row, render_x + half);
					cfg(TH->fg_dim);
					put_cell(row, render_x + half, "\xe2\x94\x82");
					at(row, render_x + half + 1);
					cbg(TH->bg_panel);
					ppad_ext("", (render_width - half) - 1, di, render_x);
					g_app_state.cur_italic = false;
				}
				rst();
				di++;
				row++;
				continue;
			}
			if (dl->type == 0) {
				int left_len = (int)strlen(dl->old_line);
				int right_len = (int)strlen(dl->new_line);
				int left_rows = diff_wrap_rows(left_len, code_w_left);
				int right_rows = diff_wrap_rows(right_len, code_w_right);
				int nrows = imax(left_rows, right_rows);
				if (row + nrows > lim) nrows = lim - row;
				if (nrows < 1) nrows = 1;

				for (int wr = 0; wr < nrows && row < lim; wr++, row++) {
					char lno[16];
					char tmp[LINE_MAX_LEN + 1];

					at(row, render_x + 1);
					cbg(TH->bg_base);
					cfg(TH->fg_dim);
					put_cell(row, render_x + 1, " ");
					at(row, render_x + 2);
					cfg(TH->fg_linenum);
					if (wr == 0) {
						snprintf(lno, sizeof(lno), "%*d ", lnum_w, dl->old_lno);
						ppad_ext(lno, lnum_w + 1, di, render_x);
					} else {
						for (int kk = 0; kk <= lnum_w; kk++)
							put_cell(row, render_x + 2 + kk, " ");
						at(row, render_x + 3 + lnum_w);
					}
					cfg(TH->fg_dim);
					ppad_ext("\xe2\x94\x82", 1, di, render_x);
					/* Use syntax highlighting for left side */
					if (wr < left_rows) {
						int left_content_len = diff_wrap_slice(dl->old_line, left_len, code_w_left, wr, tmp);
						int content_col = render_x + 3 + lnum_w + 1;
						draw_text_with_syntax(tmp, left_content_len, code_w_left,
						                     row, content_col, TH->bg_base, di, render_x);
					} else {
						int content_col = render_x + 3 + lnum_w + 1;
						for (int k = 0; k < code_w_left; k++) {
							cbg(TH->bg_base);
							cfg(TH->fg_normal);
							put_cell(row, content_col + k, " ");
						}
					}

					at(row, render_x + half + 1);
					cbg(TH->bg_base);
					cfg(TH->fg_dim);
					put_cell(row, render_x + half + 1, " ");
					at(row, render_x + half + 2);
					cfg(TH->fg_linenum);
					if (wr == 0) {
						snprintf(lno, sizeof(lno), "%*d ", lnum_w, dl->new_lno);
						ppad_ext(lno, lnum_w + 1, di, render_x);
					} else {
						for (int kk = 0; kk <= lnum_w; kk++)
							put_cell(row, render_x + half + 2 + kk, " ");
						at(row, render_x + half + 3 + lnum_w);
					}
					cfg(TH->fg_dim);
					ppad_ext("\xe2\x94\x82", 1, di, render_x);
					/* Use syntax highlighting for right side */
					if (wr < right_rows) {
						int right_content_len = diff_wrap_slice(dl->new_line, right_len, code_w_right, wr, tmp);
						int content_col = render_x + half + 3 + lnum_w + 1;
						draw_text_with_syntax(tmp, right_content_len, code_w_right,
						                     row, content_col, TH->bg_base, di, render_x);
					} else {
						int content_col = render_x + half + 3 + lnum_w + 1;
						for (int k = 0; k < code_w_right; k++) {
							cbg(TH->bg_base);
							cfg(TH->fg_normal);
							put_cell(row, content_col + k, " ");
						}
					}
					rst();
				}
				di++;
				continue;
			}
			int ndel = 0, nadd = 0;
			while (di + ndel < g_app_state.diff_count &&
				   g_app_state.diff_lines[di + ndel].type == 2)
				ndel++;
			while (di + ndel + nadd < g_app_state.diff_count &&
				   g_app_state.diff_lines[di + ndel + nadd].type == 1)
				nadd++;

			if (ndel > 0 || nadd > 0) {
				int nmax = imax(ndel, nadd);
				for (int i = 0; i < nmax && row < lim; i++) {
					DiffLine *od = (i < ndel) ? &g_app_state.diff_lines[di + i] : NULL;
					DiffLine *nd = (i < nadd) ? &g_app_state.diff_lines[di + ndel + i] : NULL;
					int left_len = od ? (int)strlen(od->old_line) : 0;
					int right_len = nd ? (int)strlen(nd->new_line) : 0;
					int left_rows = od ? diff_wrap_rows(left_len, code_w_left) : 1;
					int right_rows = nd ? diff_wrap_rows(right_len, code_w_right) : 1;
					int nrows = imax(left_rows, right_rows);
					if (row + nrows > lim) nrows = lim - row;
					if (nrows < 1) nrows = 1;

					for (int wr = 0; wr < nrows && row < lim; wr++, row++) {
						char lno[16];
						char tmp[LINE_MAX_LEN + 1];

						at(row, render_x + 1);
						if (od) {
							cbg(TH->bg_diff_del);
							cfg(TH->fg_err);
							g_app_state.cur_bold = true;
							put_cell(row, render_x + 1, "\xe2\x96\x8c");
							g_app_state.cur_bold = false;
							at(row, render_x + 2);
							cfg(TH->fg_linenum);
							if (wr == 0) {
								snprintf(lno, sizeof(lno), "%*d ", lnum_w, od->old_lno);
								ppad_ext(lno, lnum_w + 1, di + i, render_x);
							} else {
								for (int kk = 0; kk <= lnum_w; kk++)
									put_cell(row, render_x + 2 + kk, " ");
								at(row, render_x + 3 + lnum_w);
							}
							cfg(TH->fg_err);
							ppad_ext("-", 1, di + i, render_x);
							/* Use syntax highlighting for deleted lines */
							if (wr < left_rows) {
								int left_content_len = diff_wrap_slice(od->old_line, left_len, code_w_left, wr, tmp);
								int content_col = render_x + 3 + lnum_w + 1;
								draw_text_with_syntax(tmp, left_content_len, code_w_left,
								                     row, content_col, TH->bg_diff_del, di + i, render_x);
							} else {
								int content_col = render_x + 3 + lnum_w + 1;
								for (int k = 0; k < code_w_left; k++) {
									cbg(TH->bg_diff_del);
									cfg(TH->fg_normal);
									put_cell(row, content_col + k, " ");
								}
							}
						} else {
							cbg(TH->bg_panel);
							cfg(TH->fg_dim);
							put_cell(row, render_x + 1, " ");
							at(row, render_x + 2);
							ppad_ext("", lnum_w + 1, -1, render_x);
							ppad_ext("\xe2\x94\x86", 1, -1, render_x);
							ppad_ext("", code_w_left, -1, render_x);
						}
						rst();

						at(row, render_x + half + 1);
						if (nd) {
							cbg(TH->bg_diff_add);
							cfg(TH->fg_ok);
							g_app_state.cur_bold = true;
							put_cell(row, render_x + half + 1, "\xe2\x96\x8c");
							g_app_state.cur_bold = false;
							at(row, render_x + half + 2);
							cfg(TH->fg_linenum);
							if (wr == 0) {
								snprintf(lno, sizeof(lno), "%*d ", lnum_w, nd->new_lno);
								ppad_ext(lno, lnum_w + 1, di + ndel + i, render_x);
							} else {
								for (int kk = 0; kk <= lnum_w; kk++)
									put_cell(row, render_x + half + 2 + kk, " ");
								at(row, render_x + half + 3 + lnum_w);
							}
							cfg(TH->fg_ok);
							ppad_ext("+", 1, di + ndel + i, render_x);
							/* Use syntax highlighting for added lines */
							if (wr < right_rows) {
								int right_content_len = diff_wrap_slice(nd->new_line, right_len, code_w_right, wr, tmp);
								int content_col = render_x + half + 3 + lnum_w + 1;
								draw_text_with_syntax(tmp, right_content_len, code_w_right,
								                     row, content_col, TH->bg_diff_add, di + ndel + i, render_x);
							} else {
								int content_col = render_x + half + 3 + lnum_w + 1;
								for (int k = 0; k < code_w_right; k++) {
									cbg(TH->bg_diff_add);
									cfg(TH->fg_normal);
									put_cell(row, content_col + k, " ");
								}
							}
						} else {
							cbg(TH->bg_panel);
							cfg(TH->fg_dim);
							put_cell(row, render_x + half + 1, " ");
							at(row, render_x + half + 2);
							ppad_ext("", lnum_w + 1, -1, render_x);
							ppad_ext("\xe2\x94\x86", 1, -1, render_x);
							ppad_ext("", code_w_right, -1, render_x);
						}
						rst();
					}
				}
				di += ndel + nadd;
			} else {
				di++;
			}
		}
	}
	while (row < lim) {
		at(row, render_x + 1);
		cbg(TH->bg_base);
		for (int i = 0; i < render_width - 2; i++) put_cell(row, render_x + 1 + i, " ");
		rst();
		row++;
	}
	if (g_app_state.diff_count > vis && vis > 2) {
		int bh = imax(1, (vis * vis) / g_app_state.diff_count);
		int bpos = maxsc > 0 ? ((g_app_state.diff_scroll * (vis - bh)) / maxsc) : 0;
		g_app_state.scrollbar_y = content_top;
		g_app_state.scrollbar_height = vis;
		g_app_state.scrollbar_total = g_app_state.diff_count;
		g_app_state.scrollbar_visible = vis;

		char *markers = calloc(vis, 1);
		if (markers && vis < 2048) { /* Limit minimap logic size */
			for (int i = 0; i < g_app_state.diff_count; i++) {
				int r = (int)((long long)i * vis / g_app_state.diff_count);
				if (r < vis) {
					if (g_app_state.diff_lines[i].type == 1)
						markers[r] |= 1;
					else if (g_app_state.diff_lines[i].type == 2)
						markers[r] |= 2;
				}
			}
		}

		for (int r = 0; r < vis; r++) {
			bool thumb = (r >= bpos && r < bpos + bh);
			for (int sw = 0; sw < 3; sw++) {
				at(content_top + r, render_x + render_width - 1 - sw);
				if (thumb) {
					if (g_app_state.dragging_sc)
						cfg(TH->fg_sel);
					else if (g_app_state.last_mx >= render_x + render_width - 3)
						cfg(TH->fg_accent1);
					else
						cfg(TH->fg_accent2);
					put_cell(content_top + r, render_x + render_width - 1 - sw, "█");
				} else {
					if (markers && markers[r] == 1) {
						cfg(TH->fg_staged);
						put_cell(content_top + r, render_x + render_width - 1 - sw, "▒");
					} else if (markers && markers[r] == 2) {
						cfg(TH->fg_unstaged);
						put_cell(content_top + r, render_x + render_width - 1 - sw, "▒");
					} else if (markers && markers[r] == 3) {
						cfg(TH->fg_accent1);
						put_cell(content_top + r, render_x + render_width - 1 - sw, "▒");
					} else {
						cfg(TH->fg_dim);
						put_cell(content_top + r, render_x + render_width - 1 - sw,
								 sw == 0 ? "│" : " ");
					}
				}
			}
		}
		rst();
		free(markers);
	} else {
		g_app_state.scrollbar_height = 0;
	}
}
