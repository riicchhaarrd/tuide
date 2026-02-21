#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/* Color structure for theme configuration */
typedef struct {
    char name[64];
    int r, g, b;
} ConfigColor;

/* Theme configuration structure */
typedef struct {
    char name[64];
    ConfigColor bg_base, bg_panel, bg_sel, bg_tab_act, bg_tab_inact, bg_header;
    ConfigColor bg_diff_add, bg_diff_del, bg_diff_hdr;
    ConfigColor fg_normal, fg_dim, fg_bright, fg_sel;
    ConfigColor fg_accent1, fg_accent2, fg_accent3;
    ConfigColor fg_staged, fg_unstaged, fg_untracked, fg_conflict;
    ConfigColor fg_diff_add, fg_diff_del, fg_diff_hdr, fg_diff_ctx;
    ConfigColor fg_graph[6];
    ConfigColor fg_ref_local, fg_ref_remote, fg_ref_tag;
    ConfigColor fg_ok, fg_err, fg_linenum;
    /* Syntax highlighting colors */
    ConfigColor syn_keyword, syn_storage, syn_string, syn_comment;
    ConfigColor syn_number, syn_function, syn_type, syn_variable;
    ConfigColor syn_operator, syn_preproc, syn_constant;
    ConfigColor syn_tag, syn_attribute, syn_decorator;
} ConfigTheme;

/* General configuration settings */
typedef struct {
    char theme_name[64];
    int sidebar_width;
    bool diff_sidebyside;
    bool diff_wrap;
    bool diff_continuous;
    int col_hash_width;
    int col_author_width;
    int col_date_width;
} ConfigGeneral;

/* Keybinding configuration */
typedef struct {
    char key[32];
    char action[64];
} ConfigKeybinding;

/* Main configuration structure */
typedef struct {
    ConfigGeneral general;
    ConfigKeybinding keybindings[50];
    int keybinding_count;
    char custom_theme_path[512];
    bool loaded;
    char config_path[512];
} Config;

/* Global configuration instance */
extern Config g_config;

/* Configuration functions */
const char *config_get_dir(void);
const char *config_get_path(void);
bool config_parse_color(const char *str, int *r, int *g, int *b);
bool config_parse_theme(const char *path, ConfigTheme *theme);
bool config_parse_main(const char *path, Config *cfg);
void config_load(void);
bool config_save_default(const char *path);
void config_apply_general(void);
void config_save_general(void);
Config *config_get(void);

/* Built-in theme count */
#define THEME_COUNT 4

#endif
