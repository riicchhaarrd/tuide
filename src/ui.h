#ifndef UI_H
#define UI_H

#include <stdarg.h>

#include "state.h"
#include "term.h"

void set_status(bool err, const char *fmt, ...);

#define OK(...) set_status(false, __VA_ARGS__)
#define ERR(...) set_status(true, __VA_ARGS__)

/* Layout */
void layout(void);

/* Master Draw */
void draw(void);

/* Components */
void draw_tabbar(void);
void draw_statusbar(void);
void draw_cli(void);
void draw_prompt_overlay(void);
void draw_sidebar(void);
void draw_dividers(void);
void draw_menu(void);

/* Menu */
void menu_reset(int x, int y);
void menu_add_item(const char *label, void (*action)(void));

/* Prompt */
void prompt_start(const char *label, void (*cb)(const char *), bool obs);
void handle_prompt_key(Key k);

/* CLI */
void handle_cli_key(Key k);

#endif
