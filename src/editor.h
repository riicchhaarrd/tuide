#ifndef EDITOR_H
#define EDITOR_H

#include "state.h"
#include "term.h"

void editor_load(const char *path);
void editor_save(void);
void editor_close_tab(void);
void editor_next_tab(void);
void editor_prev_tab(void);
int editor_visible_tab_count(void);
bool editor_current_tab_is_diff(void);
void editor_select_visible_tab(int idx);

void handle_editor_key(Key k);

void editor_undo(Editor *ed);
void editor_redo(Editor *ed);
void editor_paste(void);
void action_copy_editor_selection(void);
void editor_cut_selection(void);
void editor_delete_selection(void);
void editor_find(const char *pattern);
void editor_goto_line(const char *lstr);

Color get_token_color(const char *tok, bool is_comment, const char *ext);
const char *get_lang_name(const char *path);

void load_browser(const char *path);
void menu_new_file(void);
void menu_delete_file(void);

#endif
