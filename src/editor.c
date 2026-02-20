#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "editor.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "render.h" /* for TH */
#include "state.h"
#include "strings.h"
#include "ui.h"
#include "util.h"

#define CUR_ED (g_app_state.tabs[g_app_state.tab_current].ed)

/* FNV-1a 64-bit hash of all editor lines */
static uint64_t editor_content_hash(Editor *ed) {
	uint64_t hash = 14695981039346656037ULL;
	for (int i = 0; i < ed->line_count; i++) {
		for (const char *ptr = ed->lines[i]; *ptr; ptr++) {
			hash ^= (unsigned char)*ptr;
			hash *= 1099511628211ULL;
		}
		hash ^= '\n';
		hash *= 1099511628211ULL;
	}
	return hash;
}

static void editor_free(Editor *ed) {
	for (int i = 0; i < ed->line_count; i++) free(ed->lines[i]);
	free(ed->lines);
	for (int i = 0; i < ed->undo_top; i++) free(ed->undo_stack[i].text);
	for (int i = 0; i < ed->redo_top; i++) free(ed->redo_stack[i].text);
	memset(ed, 0, sizeof(Editor));
}

static char *editor_snapshot_text(Editor *ed) {
	size_t total = 1;
	for (int i = 0; i < ed->line_count; i++) total += strlen(ed->lines[i]) + 1;
	char *buffer = malloc(total);
	size_t buffer_pos = 0;
	for (int i = 0; i < ed->line_count; i++) {
		size_t line_len = strlen(ed->lines[i]);
		memcpy(buffer + buffer_pos, ed->lines[i], line_len);
		buffer_pos += line_len;
		buffer[buffer_pos++] = '\n';
	}
	buffer[buffer_pos] = '\0';
	return buffer;
}

static void editor_restore_snapshot(Editor *ed, const char *text, int cursor_y, int cursor_x) {
	for (int i = 0; i < ed->line_count; i++) free(ed->lines[i]);
	ed->line_count = 0;
	const char *ptr = text;
	while (*ptr) {
		const char *newline_ptr = strchr(ptr, '\n');
		size_t len = newline_ptr ? (size_t)(newline_ptr - ptr) : strlen(ptr);
		if (ed->line_count >= ed->line_capacity) {
			ed->line_capacity = ed->line_capacity ? ed->line_capacity * 2 : 128;
			ed->lines = realloc(ed->lines, sizeof(char *) * ed->line_capacity);
		}
		char *line = malloc(len + 1);
		memcpy(line, ptr, len);
		line[len] = '\0';
		ed->lines[ed->line_count++] = line;
		ptr = newline_ptr ? newline_ptr + 1 : ptr + len;
	}
	if (ed->line_count == 0) {
		if (ed->line_capacity == 0) {
			ed->line_capacity = 128;
			ed->lines = malloc(sizeof(char *) * 128);
		}
		ed->lines[ed->line_count++] = strdup("");
	}
	ed->cursor_row = iclamp(cursor_y, 0, ed->line_count - 1);
	ed->cursor_col = iclamp(cursor_x, 0, (int)strlen(ed->lines[ed->cursor_row]));
	ed->modified = true;
}

static void editor_push_undo(Editor *ed) {
	for (int i = 0; i < ed->redo_top; i++) free(ed->redo_stack[i].text);
	ed->redo_top = 0;
	if (ed->undo_top == MAX_UNDO) {
		free(ed->undo_stack[0].text);
		memmove(&ed->undo_stack[0], &ed->undo_stack[1], (MAX_UNDO - 1) * sizeof(HistEntry));
		ed->undo_top--;
	}
	ed->undo_stack[ed->undo_top++] =
		(HistEntry){editor_snapshot_text(ed), ed->cursor_row, ed->cursor_col};
}

void editor_undo(Editor *ed) {
	if (ed->undo_top == 0) {
		OK("%s", UI->msg_nothing_to_undo);
		return;
	}
	ed->needs_sync = true;
	if (ed->redo_top < MAX_UNDO)
		ed->redo_stack[ed->redo_top++] =
			(HistEntry){editor_snapshot_text(ed), ed->cursor_row, ed->cursor_col};
	HistEntry e = ed->undo_stack[--ed->undo_top];
	editor_restore_snapshot(ed, e.text, e.cursor_row, e.cursor_col);
	free(e.text);
	if (editor_content_hash(ed) == ed->saved_hash) ed->modified = false;
	OK("%s", UI->msg_undo);
}

void editor_redo(Editor *ed) {
	if (ed->redo_top == 0) {
		OK("%s", UI->msg_nothing_to_redo);
		return;
	}
	ed->needs_sync = true;
	if (ed->undo_top < MAX_UNDO)
		ed->undo_stack[ed->undo_top++] =
			(HistEntry){editor_snapshot_text(ed), ed->cursor_row, ed->cursor_col};
	HistEntry e = ed->redo_stack[--ed->redo_top];
	editor_restore_snapshot(ed, e.text, e.cursor_row, e.cursor_col);
	free(e.text);
	if (editor_content_hash(ed) == ed->saved_hash) ed->modified = false;
	OK("%s", UI->msg_redo);
}

static int editor_slot_count(void) { return g_app_state.tab_count + 1; }

static int editor_current_slot(void) {
	if (g_app_state.editor_diff_tab || g_app_state.tab_count == 0) return 0;
	return g_app_state.tab_current + 1;
}

int editor_visible_tab_count(void) { return editor_slot_count(); }

bool editor_current_tab_is_diff(void) {
	return g_app_state.editor_diff_tab;
}

void editor_select_visible_tab(int idx) {
	int slots = editor_slot_count();
	if (slots <= 0) return;
	idx = iclamp(idx, 0, slots - 1);
	g_app_state.ed_selecting = false;
	if (idx == 0) {
		g_app_state.editor_diff_tab = true;
		g_app_state.focus = FOCUS_DIFF;
		return;
	}
	g_app_state.editor_diff_tab = false;
	g_app_state.tab_current = idx - 1;
	g_app_state.tabs[g_app_state.tab_current].ed.needs_sync = true;
	g_app_state.focus = FOCUS_EDITOR;
}

void editor_load(const char *path) {
	char real[PATH_MAX];
	if (!realpath(path, real)) snprintf(real, sizeof(real), "%s", path);

	for (int i = 0; i < g_app_state.tab_count; i++) {
		if (strcmp(g_app_state.tabs[i].path, real) == 0) {
			g_app_state.tab_current = i;
			g_app_state.editor_diff_tab = false;
			g_app_state.tabs[i].ed.needs_sync = true;
			return;
		}
	}

	if (g_app_state.tab_count < MAX_TABS) {
		g_app_state.tab_current = g_app_state.tab_count++;
	}

	Tab *t = &g_app_state.tabs[g_app_state.tab_current];
	editor_free(&t->ed);
	t->ed.needs_sync = true;
	FILE *fp = fopen(real, "r");
	if (!fp) return;
	snprintf(t->path, sizeof(t->path), "%s", real);
	snprintf(t->ed.filename, sizeof(t->ed.filename), "%s", real);
	char buf[4096];
	while (fgets(buf, sizeof(buf), fp)) {
		strtrim(buf);
		if (t->ed.line_count >= t->ed.line_capacity) {
			t->ed.line_capacity = t->ed.line_capacity ? t->ed.line_capacity * 2 : 128;
			t->ed.lines = realloc(t->ed.lines, sizeof(char *) * t->ed.line_capacity);
		}
		t->ed.lines[t->ed.line_count++] = strdup(buf);
	}
	fclose(fp);
	if (t->ed.line_count == 0) {
		if (t->ed.line_capacity == 0) {
			t->ed.line_capacity = 128;
			t->ed.lines = malloc(sizeof(char *) * 128);
		}
		t->ed.lines[t->ed.line_count++] = strdup("");
	}
	t->ed.saved_hash = editor_content_hash(&t->ed);
	g_app_state.editor_diff_tab = false;
}

void editor_next_tab(void) {
	int slots = editor_slot_count();
	if (slots <= 1) return;
	editor_select_visible_tab((editor_current_slot() + 1) % slots);
}
void editor_prev_tab(void) {
	int slots = editor_slot_count();
	if (slots <= 1) return;
	editor_select_visible_tab((editor_current_slot() + slots - 1) % slots);
}
static void do_close_tab(void) {
	if (g_app_state.tab_count == 0) return;
	editor_free(&g_app_state.tabs[g_app_state.tab_current].ed);
	for (int i = g_app_state.tab_current; i < g_app_state.tab_count - 1; i++)
		g_app_state.tabs[i] = g_app_state.tabs[i + 1];
	g_app_state.tab_count--;
	if (g_app_state.tab_current >= g_app_state.tab_count && g_app_state.tab_count > 0)
		g_app_state.tab_current = g_app_state.tab_count - 1;
	if (g_app_state.tab_count == 0) {
		g_app_state.editor_diff_tab = true;
		g_app_state.focus = FOCUS_DIFF;
	} else {
		g_app_state.tabs[g_app_state.tab_current].ed.needs_sync = true;
		g_app_state.focus = FOCUS_EDITOR;
	}
}

void editor_close_tab(void) {
	if (g_app_state.editor_diff_tab) {
		if (g_app_state.tab_count > 0)
			editor_select_visible_tab(g_app_state.tab_current + 1);
		else
			g_app_state.focus = FOCUS_DIFF;
		return;
	}
	if (g_app_state.tab_count == 0) return;

	/* Check for unsaved changes */
	if (g_app_state.tabs[g_app_state.tab_current].ed.modified) {
		char msg[300];
		const char *filename = g_app_state.tabs[g_app_state.tab_current].ed.filename;
		const char *basename = strrchr(filename, '/');
		basename = basename ? basename + 1 : filename;
		snprintf(msg, sizeof(msg), "Close '%s' with unsaved changes?", basename);
		dialog_show(msg, do_close_tab, NULL);
	} else {
		do_close_tab();
	}
}

void editor_save(void) {
	if (editor_current_tab_is_diff() || g_app_state.tab_count == 0) return;
	Tab *t = &g_app_state.tabs[g_app_state.tab_current];
	if (!t->path[0]) return;
	FILE *fp = fopen(t->path, "w");
	if (!fp) return;
	for (int i = 0; i < t->ed.line_count; i++) {
		fprintf(fp, "%s\n", t->ed.lines[i]);
	}
	fclose(fp);
	t->ed.modified = false;
	t->ed.saved_hash = editor_content_hash(&t->ed);
	OK(UI->msg_saved_fmt, t->path);
}

static void editor_ensure_line(void) {
	if (CUR_ED.line_count == 0) {
		if (CUR_ED.line_capacity == 0) {
			CUR_ED.line_capacity = 128;
			CUR_ED.lines = malloc(sizeof(char *) * 128);
		}
		CUR_ED.lines[CUR_ED.line_count++] = strdup("");
	}
}

/* Check if any tab has unsaved changes */
bool editor_has_unsaved_changes(void) {
	for (int i = 0; i < g_app_state.tab_count; i++) {
		if (g_app_state.tabs[i].ed.modified) return true;
	}
	return false;
}

/* Callback for quitting after confirmation */
void editor_confirm_quit(void) {
	g_app_state.running = false;
}

static void editor_insert_char(char char_to_insert) {
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	editor_ensure_line();
	char *line = CUR_ED.lines[CUR_ED.cursor_row];
	int len = (int)strlen(line);
	int cursor_x = iclamp(CUR_ED.cursor_col, 0, len);
	char *new_line = malloc(len + 2);
	memcpy(new_line, line, cursor_x);
	new_line[cursor_x] = char_to_insert;
	memcpy(new_line + cursor_x + 1, line + cursor_x, len - cursor_x + 1);
	free(line);
	CUR_ED.lines[CUR_ED.cursor_row] = new_line;
	CUR_ED.cursor_col = cursor_x + 1;
	CUR_ED.modified = true;
}

static void editor_backspace(void) {
	if (CUR_ED.line_count == 0) return;
	if (CUR_ED.cursor_row >= CUR_ED.line_count) CUR_ED.cursor_row = CUR_ED.line_count - 1;
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	char *line = CUR_ED.lines[CUR_ED.cursor_row];
	int len = (int)strlen(line);
	int cursor_x = iclamp(CUR_ED.cursor_col, 0, len);
	if (cursor_x > 0) {
		memmove(line + cursor_x - 1, line + cursor_x, len - cursor_x + 1);
		CUR_ED.cursor_col = cursor_x - 1;
		CUR_ED.modified = true;
	} else if (CUR_ED.cursor_row > 0) {
		char *prev = CUR_ED.lines[CUR_ED.cursor_row - 1];
		char *curr = CUR_ED.lines[CUR_ED.cursor_row];
		int prev_len = (int)strlen(prev);
		int curr_len = (int)strlen(curr);
		char *new_line = malloc(prev_len + curr_len + 1);
		memcpy(new_line, prev, prev_len);
		memcpy(new_line + prev_len, curr, curr_len + 1);
		free(prev);
		free(curr);
		CUR_ED.lines[CUR_ED.cursor_row - 1] = new_line;
		for (int i = CUR_ED.cursor_row; i < CUR_ED.line_count - 1; i++)
			CUR_ED.lines[i] = CUR_ED.lines[i + 1];
		CUR_ED.line_count--;
		CUR_ED.cursor_row--;
		CUR_ED.cursor_col = prev_len;
		CUR_ED.modified = true;
	}
}

static void editor_delete_forward(void) {
	if (CUR_ED.line_count == 0) return;
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	char *line = CUR_ED.lines[CUR_ED.cursor_row];
	int len = (int)strlen(line);
	int cursor_x = iclamp(CUR_ED.cursor_col, 0, len);
	if (cursor_x < len) {
		memmove(line + cursor_x, line + cursor_x + 1, len - cursor_x);
		CUR_ED.modified = true;
	} else if (CUR_ED.cursor_row < CUR_ED.line_count - 1) {
		char *next = CUR_ED.lines[CUR_ED.cursor_row + 1];
		int next_len = (int)strlen(next);
		char *new_line = malloc(len + next_len + 1);
		memcpy(new_line, line, len);
		memcpy(new_line + len, next, next_len + 1);
		free(line);
		free(next);
		CUR_ED.lines[CUR_ED.cursor_row] = new_line;
		for (int i = CUR_ED.cursor_row + 1; i < CUR_ED.line_count - 1; i++)
			CUR_ED.lines[i] = CUR_ED.lines[i + 1];
		CUR_ED.line_count--;
		CUR_ED.modified = true;
	}
}

static void editor_newline(void) {
	editor_ensure_line();
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	char *line = CUR_ED.lines[CUR_ED.cursor_row];
	int len = (int)strlen(line);
	int cursor_x = iclamp(CUR_ED.cursor_col, 0, len);
	char *next_line = strdup(line + cursor_x);
	line[cursor_x] = '\0';
	if (CUR_ED.line_count >= CUR_ED.line_capacity) {
		CUR_ED.line_capacity = CUR_ED.line_capacity ? CUR_ED.line_capacity * 2 : 128;
		CUR_ED.lines = realloc(CUR_ED.lines, sizeof(char *) * CUR_ED.line_capacity);
	}
	for (int i = CUR_ED.line_count; i > CUR_ED.cursor_row + 1; i--)
		CUR_ED.lines[i] = CUR_ED.lines[i - 1];
	CUR_ED.lines[CUR_ED.cursor_row + 1] = next_line;
	CUR_ED.line_count++;
	CUR_ED.cursor_row++;
	CUR_ED.cursor_col = 0;
	CUR_ED.modified = true;
}

void editor_goto_line(const char *line_str) {
	int line_num = atoi(line_str);
	if (line_num > 0 && line_num <= CUR_ED.line_count) {
		CUR_ED.cursor_row = line_num - 1;
		CUR_ED.cursor_col = 0;
		CUR_ED.needs_sync = true;
		OK(UI->msg_jumped_to_line_fmt, line_num);
	} else
		ERR("%s", UI->err_invalid_line_number);
}

void editor_find(const char *pattern) {
	if (!pattern || !pattern[0]) return;
	CUR_ED.needs_sync = true;
	snprintf(g_app_state.ed_search, sizeof(g_app_state.ed_search), "%s", pattern);
	for (int i = CUR_ED.cursor_row; i < CUR_ED.line_count; i++) {
		char *line = CUR_ED.lines[i];
		char *found_pos =
			strstr(i == CUR_ED.cursor_row ? line + CUR_ED.cursor_col + 1 : line, pattern);
		if (found_pos) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(found_pos - line);
			OK(UI->msg_found_fmt, pattern);
			return;
		}
	}
	for (int i = 0; i < CUR_ED.cursor_row; i++) {
		char *line = CUR_ED.lines[i];
		char *found_pos = strstr(line, pattern);
		if (found_pos) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(found_pos - line);
			OK(UI->msg_found_wrapped_fmt, pattern);
			return;
		}
	}
	ERR(UI->err_not_found_fmt, pattern);
}

static void editor_find_next(void) {
	if (!g_app_state.ed_search[0]) {
		ERR("%s", UI->err_no_search_term);
		return;
	}
	const char *pattern = g_app_state.ed_search;
	for (int i = CUR_ED.cursor_row; i < CUR_ED.line_count; i++) {
		char *line = CUR_ED.lines[i];
		char *pos = strstr(i == CUR_ED.cursor_row ? line + CUR_ED.cursor_col + 1 : line, pattern);
		if (pos) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(pos - line);
			OK(UI->msg_next_fmt, pattern);
			return;
		}
	}
	for (int i = 0; i <= CUR_ED.cursor_row; i++) {
		char *line = CUR_ED.lines[i];
		int start = (i == CUR_ED.cursor_row) ? 0 : 0;
		char *pos = strstr(line + start, pattern);
		if (pos) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(pos - line);
			OK(UI->msg_next_wrapped_fmt, pattern);
			return;
		}
	}
	ERR(UI->err_not_found_fmt, pattern);
}

static void editor_find_prev(void) {
	if (!g_app_state.ed_search[0]) {
		ERR("%s", UI->err_no_search_term);
		return;
	}
	const char *pattern = g_app_state.ed_search;
	int plen = (int)strlen(pattern);
	for (int i = CUR_ED.cursor_row; i >= 0; i--) {
		char *line = CUR_ED.lines[i];
		int search_end = (i == CUR_ED.cursor_row)
							 ? (CUR_ED.cursor_col > 0 ? CUR_ED.cursor_col - 1 : -1)
							 : (int)strlen(line);
		if (search_end < 0) continue;
		char *found = NULL;
		for (int j = 0; j <= search_end - plen + 1 && j <= (int)strlen(line) - plen; j++) {
			if (strncmp(line + j, pattern, plen) == 0) found = line + j;
		}
		if (found) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(found - line);
			OK(UI->msg_prev_fmt, pattern);
			return;
		}
	}
	for (int i = CUR_ED.line_count - 1; i > CUR_ED.cursor_row; i--) {
		char *line = CUR_ED.lines[i];
		int llen = (int)strlen(line);
		char *found = NULL;
		for (int j = 0; j <= llen - plen; j++) {
			if (strncmp(line + j, pattern, plen) == 0) found = line + j;
		}
		if (found) {
			CUR_ED.cursor_row = i;
			CUR_ED.cursor_col = (int)(found - line);
			OK(UI->msg_prev_wrapped_fmt, pattern);
			return;
		}
	}
	ERR(UI->err_not_found_fmt, pattern);
}

void editor_paste(void) {
	if (!g_app_state.clipboard) return;
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	editor_ensure_line();
	for (int i = 0; g_app_state.clipboard[i]; i++) {
		char c = g_app_state.clipboard[i];
		if (c == '\n') {
			char *line = CUR_ED.lines[CUR_ED.cursor_row];
			int len = (int)strlen(line);
			int cursor_col = iclamp(CUR_ED.cursor_col, 0, len);
			char *next = strdup(line + cursor_col);
			line[cursor_col] = '\0';
			if (CUR_ED.line_count >= CUR_ED.line_capacity) {
				CUR_ED.line_capacity = CUR_ED.line_capacity ? CUR_ED.line_capacity * 2 : 128;
				CUR_ED.lines = realloc(CUR_ED.lines, sizeof(char *) * CUR_ED.line_capacity);
			}
			for (int j = CUR_ED.line_count; j > CUR_ED.cursor_row + 1; j--)
				CUR_ED.lines[j] = CUR_ED.lines[j - 1];
			CUR_ED.lines[CUR_ED.cursor_row + 1] = next;
			CUR_ED.line_count++;
			CUR_ED.cursor_row++;
			CUR_ED.cursor_col = 0;
		} else {
			char *line = CUR_ED.lines[CUR_ED.cursor_row];
			int len = (int)strlen(line);
			int cursor_col = iclamp(CUR_ED.cursor_col, 0, len);
			char *n = malloc(len + 2);
			memcpy(n, line, cursor_col);
			n[cursor_col] = c;
			memcpy(n + cursor_col + 1, line + cursor_col, len - cursor_col + 1);
			free(line);
			CUR_ED.lines[CUR_ED.cursor_row] = n;
			CUR_ED.cursor_col = cursor_col + 1;
		}
	}
	CUR_ED.modified = true;
}

void action_copy_editor_selection(void) {
	if (!g_app_state.ed_selecting) return;
	int start_y = g_app_state.ed_sel_start_y, start_x = g_app_state.ed_sel_start_x;
	int end_y = g_app_state.ed_sel_end_y, end_x = g_app_state.ed_sel_end_x;
	if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
		int temp = start_y;
		start_y = end_y;
		end_y = temp;
		temp = start_x;
		start_x = end_x;
		end_x = temp;
	}
	if (g_app_state.clipboard) free(g_app_state.clipboard);
	size_t cap = 1024, len = 0;
	g_app_state.clipboard = malloc(cap);
	for (int y = start_y; y <= end_y; y++) {
		if (y < 0 || y >= CUR_ED.line_count) continue;
		const char *line = CUR_ED.lines[y];
		int line_len = (int)strlen(line);
		int current_start_x = (y == start_y) ? start_x : 0;
		int current_end_x = (y == end_y) ? end_x : line_len - 1;
		for (int x = current_start_x; x <= current_end_x && x < line_len; x++) {
			if (len + 2 >= cap) {
				cap *= 2;
				g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
			}
			g_app_state.clipboard[len++] = line[x];
		}
		if (y < end_y) {
			if (len + 2 >= cap) {
				cap *= 2;
				g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
			}
			g_app_state.clipboard[len++] = '\n';
		}
	}
	g_app_state.clipboard[len] = '\0';
	copy_to_sys_clipboard(g_app_state.clipboard);
	OK(UI->msg_copied_editor_fmt, (int)len);
}

void editor_delete_selection(void) {
	if (!g_app_state.ed_selecting) return;
	if (CUR_ED.line_count == 0) {
		g_app_state.ed_selecting = false;
		return;
	}
	int start_y = g_app_state.ed_sel_start_y, start_x = g_app_state.ed_sel_start_x;
	int end_y = g_app_state.ed_sel_end_y, end_x = g_app_state.ed_sel_end_x;
	if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
		int temp = start_y;
		start_y = end_y;
		end_y = temp;
		temp = start_x;
		start_x = end_x;
		end_x = temp;
	}
	start_y = iclamp(start_y, 0, CUR_ED.line_count - 1);
	end_y = iclamp(end_y, 0, CUR_ED.line_count - 1);
	editor_push_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
	if (start_y == end_y) {
		char *line = CUR_ED.lines[start_y];
		int len = (int)strlen(line);
		start_x = iclamp(start_x, 0, len);
		end_x = iclamp(end_x, start_x, len);
		memmove(line + start_x, line + end_x, len - end_x + 1);
	} else {
		char *start_line_ptr = CUR_ED.lines[start_y];
		char *end_line_ptr = CUR_ED.lines[end_y];
		int start_len = (int)strlen(start_line_ptr);
		int end_len = (int)strlen(end_line_ptr);
		start_x = iclamp(start_x, 0, start_len);
		end_x = iclamp(end_x, 0, end_len);
		int tail_len = end_len - end_x;
		if (tail_len < 0) tail_len = 0;
		char *new_line = malloc(start_x + tail_len + 1);
		memcpy(new_line, start_line_ptr, start_x);
		if (tail_len > 0) memcpy(new_line + start_x, end_line_ptr + end_x, tail_len);
		new_line[start_x + tail_len] = '\0';
		free(CUR_ED.lines[start_y]);
		CUR_ED.lines[start_y] = new_line;
		int removed_count = end_y - start_y;
		for (int i = start_y + 1; i <= end_y; i++) free(CUR_ED.lines[i]);
		for (int i = start_y + 1; i < CUR_ED.line_count - removed_count; i++)
			CUR_ED.lines[i] = CUR_ED.lines[i + removed_count];
		CUR_ED.line_count -= removed_count;
	}
	CUR_ED.cursor_row = start_y;
	CUR_ED.cursor_col = start_x;
	g_app_state.ed_selecting = false;
	CUR_ED.modified = true;
}

void editor_cut_selection(void) {
	if (!g_app_state.ed_selecting) return;
	action_copy_editor_selection();
	editor_delete_selection();
}

static void begin_ed_selection(void) {
	if (!g_app_state.ed_selecting) {
		g_app_state.ed_selecting = true;
		g_app_state.ed_sel_start_y = g_app_state.ed_sel_end_y = CUR_ED.cursor_row;
		g_app_state.ed_sel_start_x = g_app_state.ed_sel_end_x = CUR_ED.cursor_col;
	}
}

void handle_editor_key(Key key_event) {
	if (key_event.type == KEY_TAB || key_event.type == KEY_SHIFT_TAB) return;
	if (editor_current_tab_is_diff() || g_app_state.tab_count == 0) {
		if (key_event.type == KEY_F1)
			editor_prev_tab();
		else if (key_event.type == KEY_F2)
			editor_next_tab();
		return;
	}
	CUR_ED.needs_sync = true;
	switch (key_event.type) {
		case KEY_UP:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_row = imax(0, CUR_ED.cursor_row - 1);
			{
				int line_len = (int)strlen(CUR_ED.lines[CUR_ED.cursor_row]);
				if (CUR_ED.cursor_col > line_len) CUR_ED.cursor_col = line_len;
			}
			break;
		case KEY_DOWN:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_row =
				imin(CUR_ED.line_count > 0 ? CUR_ED.line_count - 1 : 0, CUR_ED.cursor_row + 1);
			{
				int line_len = (int)strlen(CUR_ED.lines[CUR_ED.cursor_row]);
				if (CUR_ED.cursor_col > line_len) CUR_ED.cursor_col = line_len;
			}
			break;
		case KEY_LEFT:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_col = imax(0, CUR_ED.cursor_col - 1);
			break;
		case KEY_RIGHT:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_col = imin(
				CUR_ED.lines[CUR_ED.cursor_row] ? (int)strlen(CUR_ED.lines[CUR_ED.cursor_row]) : 0,
				CUR_ED.cursor_col + 1);
			break;
		case KEY_SHIFT_UP:
			begin_ed_selection();
			CUR_ED.cursor_row = imax(0, CUR_ED.cursor_row - 1);
			g_app_state.ed_sel_end_y = CUR_ED.cursor_row;
			g_app_state.ed_sel_end_x = CUR_ED.cursor_col;
			break;
		case KEY_SHIFT_DOWN:
			begin_ed_selection();
			CUR_ED.cursor_row =
				imin(CUR_ED.line_count > 0 ? CUR_ED.line_count - 1 : 0, CUR_ED.cursor_row + 1);
			g_app_state.ed_sel_end_y = CUR_ED.cursor_row;
			g_app_state.ed_sel_end_x = CUR_ED.cursor_col;
			break;
		case KEY_SHIFT_LEFT:
			begin_ed_selection();
			CUR_ED.cursor_col = imax(0, CUR_ED.cursor_col - 1);
			g_app_state.ed_sel_end_y = CUR_ED.cursor_row;
			g_app_state.ed_sel_end_x = CUR_ED.cursor_col;
			break;
		case KEY_SHIFT_RIGHT:
			begin_ed_selection();
			CUR_ED.cursor_col = imin(
				CUR_ED.lines[CUR_ED.cursor_row] ? (int)strlen(CUR_ED.lines[CUR_ED.cursor_row]) : 0,
				CUR_ED.cursor_col + 1);
			g_app_state.ed_sel_end_y = CUR_ED.cursor_row;
			g_app_state.ed_sel_end_x = CUR_ED.cursor_col;
			break;
		case KEY_ENTER:
			if (g_app_state.ed_selecting) {
				editor_delete_selection();
			} else {
				editor_newline();
			}
			break;
		case KEY_BACKSPACE:
			if (g_app_state.ed_selecting) {
				editor_delete_selection();
			} else {
				editor_backspace();
			}
			break;
		case KEY_DEL:
			if (g_app_state.ed_selecting) {
				editor_delete_selection();
			} else {
				editor_delete_forward();
			}
			break;
		case KEY_CTRL_A:
			if (CUR_ED.line_count > 0) {
				g_app_state.ed_selecting = true;
				g_app_state.ed_sel_start_y = 0;
				g_app_state.ed_sel_start_x = 0;
				g_app_state.ed_sel_end_y = CUR_ED.line_count - 1;
				g_app_state.ed_sel_end_x = (int)strlen(CUR_ED.lines[CUR_ED.line_count - 1]);
				CUR_ED.cursor_row = g_app_state.ed_sel_end_y;
				CUR_ED.cursor_col = g_app_state.ed_sel_end_x;
			}
			break;
		case KEY_CTRL_S:
			editor_save();
			break;
		case KEY_CTRL_V:
			editor_paste();
			break;
		case KEY_CTRL_C:
			if (g_app_state.ed_selecting) action_copy_editor_selection();
			break;
		case KEY_CTRL_X:
			editor_cut_selection();
			break;
		case KEY_CTRL_Z:
			editor_undo(&g_app_state.tabs[g_app_state.tab_current].ed);
			break;
		case KEY_CTRL_Y:
			editor_redo(&g_app_state.tabs[g_app_state.tab_current].ed);
			break;
		case KEY_CTRL_Q:
			if (editor_has_unsaved_changes()) {
				dialog_show("Quit with unsaved changes?", editor_confirm_quit, NULL);
			} else {
				g_app_state.running = false;
			}
			break;
		case KEY_CTRL_W:
			editor_close_tab();
			break;
		case KEY_HOME:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_col = 0;
			break;
		case KEY_END:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_col =
				(CUR_ED.lines[CUR_ED.cursor_row] ? (int)strlen(CUR_ED.lines[CUR_ED.cursor_row])
												 : 0);
			break;
		case KEY_PGUP:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_row = imax(0, CUR_ED.cursor_row - (g_app_state.rows - 6));
			break;
		case KEY_PGDN:
			g_app_state.ed_selecting = false;
			CUR_ED.cursor_row = imin(CUR_ED.line_count > 0 ? CUR_ED.line_count - 1 : 0,
									 CUR_ED.cursor_row + (g_app_state.rows - 6));
			break;
		case KEY_F1:
			editor_prev_tab();
			break;
		case KEY_F2:
			editor_next_tab();
			break;
		case KEY_CHAR:
			if (key_event.ch == 'y' && g_app_state.ed_selecting) {
				action_copy_editor_selection();
				break;
			}
			if (key_event.ch == 'n') {
				editor_find_next();
				break;
			}
			if (key_event.ch == 'N') {
				editor_find_prev();
				break;
			}
			if (g_app_state.ed_selecting) editor_delete_selection();
			editor_insert_char(key_event.ch);
			break;
		case KEY_ESC:
			g_app_state.ed_selecting = false;
			g_app_state.focus = (g_app_state.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES);
			break;
		default:
			break;
	}
}

Color get_token_color(const char *token, bool is_comment, const char *extension) {
	if (is_comment) return TH->fg_accent3;

	static const char *c_keywords[] = {
		"auto",		"break",	   "case",		   "char",
		"const",	"continue",	   "default",	   "do",
		"double",	"else",		   "enum",		   "extern",
		"float",	"for",		   "goto",		   "if",
		"int",		"long",		   "register",	   "return",
		"short",	"signed",	   "sizeof",	   "static",
		"struct",	"switch",	   "typedef",	   "union",
		"unsigned", "void",		   "volatile",	   "while",
		"bool",		"inline",	   "restrict",	   "NULL",
		"true",		"false",	   "class",		   "public",
		"private",	"protected",   "template",	   "typename",
		"using",	"namespace",   "virtual",	   "override",
		"final",	"constexpr",   "noexcept",	   "explicit",
		"mutable",	"friend",	   "new",		   "delete",
		"this",		"operator",	   "throw",		   "try",
		"catch",	"static_cast", "dynamic_cast", "reinterpret_cast",
		"nullptr",	"decltype",	   "auto",		   "export",
		"import",	"module"};
	int kw_count = sizeof(c_keywords) / sizeof(c_keywords[0]);

	if (token[0] == '#') return TH->fg_accent1;
	if (token[0] == '\'' || token[0] == '"') return TH->fg_unstaged;
	if (isdigit((unsigned char)token[0]) ||
		(token[0] == '0' && (token[1] == 'x' || token[1] == 'X')))
		return TH->fg_accent2;

	for (int i = 0; i < kw_count; i++)
		if (strcmp(token, c_keywords[i]) == 0) return TH->fg_accent1;

	if (extension &&
		(strcmp(extension, ".c") == 0 || strcmp(extension, ".h") == 0 || strstr(token, "_t")))
		return TH->fg_staged;

	return TH->fg_normal;
}

const char *get_lang_name(const char *path) {
	const char *ext = strrchr(path, '.');
	if (!ext) return UI->lang_plain_text;
	if (strcmp(ext, ".c") == 0) return UI->lang_c;
	if (strcmp(ext, ".h") == 0) return UI->lang_c_header;
	if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cc") == 0) return UI->lang_cpp;
	if (strcmp(ext, ".py") == 0) return UI->lang_python;
	if (strcmp(ext, ".js") == 0) return UI->lang_javascript;
	if (strcmp(ext, ".ts") == 0) return UI->lang_typescript;
	if (strcmp(ext, ".md") == 0) return UI->lang_markdown;
	return UI->lang_plain_text;
}

static void action_new_file(const char *name) {
	if (!name || !name[0]) return;
	char full[1024];
	snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path, name);
	FILE *fp = fopen(full, "w");
	if (fp) {
		fclose(fp);
		load_browser(g_app_state.browser_path);
		OK(UI->msg_created_file_fmt, name);
	} else
		ERR(UI->err_failed_create_file_fmt, name);
}

static void action_delete_file(void) {
	if (g_app_state.browser_count == 0) return;
	BrowserFile *f = &g_app_state.browser_files[g_app_state.browser_sel];
	if (strcmp(f->path, "..") == 0) return;
	char full[1024];
	snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path, f->path);
	if (remove(full) == 0) {
		load_browser(g_app_state.browser_path);
		OK(UI->msg_deleted_file_fmt, f->path);
	} else
		ERR(UI->err_failed_delete_file_fmt, f->path);
}

void menu_new_file(void) { prompt_start(UI->prompt_new_file, action_new_file, false); }
void menu_delete_file(void) { action_delete_file(); }

void load_browser(const char *path) {
	g_app_state.browser_count = 0;
	char real[PATH_MAX];
	if (realpath(path, real)) {
		snprintf(g_app_state.browser_path, sizeof(g_app_state.browser_path), "%s", real);
	} else {
		snprintf(g_app_state.browser_path, sizeof(g_app_state.browser_path), "%s", path);
	}

	if (strcmp(g_app_state.browser_path, "/") != 0) {
		BrowserFile *f = &g_app_state.browser_files[g_app_state.browser_count++];
		strcpy(f->path, "..");
		f->is_dir = true;
	}

	DIR *d = opendir(g_app_state.browser_path);
	if (!d) return;
	struct dirent *dir;
	while ((dir = readdir(d)) != NULL && g_app_state.browser_count < 1024) {
		if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
		BrowserFile *f = &g_app_state.browser_files[g_app_state.browser_count++];
		snprintf(f->path, sizeof(f->path), "%s", dir->d_name);
		struct stat file_stat;
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", g_app_state.browser_path, dir->d_name);
		stat(full, &file_stat);
		f->is_dir = S_ISDIR(file_stat.st_mode);
	}
	closedir(d);

	int start =
		(g_app_state.browser_count > 0 && strcmp(g_app_state.browser_files[0].path, "..") == 0) ? 1
																								: 0;
	for (int i = start; i < g_app_state.browser_count - 1; i++) {
		for (int j = i + 1; j < g_app_state.browser_count; j++) {
			bool swap = false;
			if (!g_app_state.browser_files[i].is_dir && g_app_state.browser_files[j].is_dir)
				swap = true;
			else if (g_app_state.browser_files[i].is_dir == g_app_state.browser_files[j].is_dir &&
					 strcmp(g_app_state.browser_files[i].path, g_app_state.browser_files[j].path) >
						 0)
				swap = true;
			if (swap) {
				BrowserFile tmp = g_app_state.browser_files[i];
				g_app_state.browser_files[i] = g_app_state.browser_files[j];
				g_app_state.browser_files[j] = tmp;
			}
		}
	}
}
