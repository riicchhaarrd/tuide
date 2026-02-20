#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "state.h"
#include "strings.h"

/* Global configuration instance */
Config g_config = {0};

/* Get XDG config directory path */
const char *config_get_dir(void) {
    static char dir_path[512] = {0};
    if (dir_path[0]) return dir_path;

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0]) {
        snprintf(dir_path, sizeof(dir_path), "%s/tuide", xdg_config);
    } else {
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(dir_path, sizeof(dir_path), "%s/.config/tuide", home);
        } else {
            /* Fallback to /tmp if no home directory */
            snprintf(dir_path, sizeof(dir_path), "/tmp/tuide");
        }
    }
    return dir_path;
}

/* Get main config file path */
const char *config_get_path(void) {
    static char cfg_path[512] = {0};
    if (cfg_path[0]) return cfg_path;

    const char *dir = config_get_dir();
    snprintf(cfg_path, sizeof(cfg_path), "%s/config", dir);
    return cfg_path;
}

/* Trim whitespace from string */
static char *trim(char *str) {
    char *end;

    /* Trim leading space */
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) return str;

    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';
    return str;
}

/* Parse color string "r,g,b" into RGB values */
bool config_parse_color(const char *str, int *r, int *g, int *b) {
    if (!str || !r || !g || !b) return false;

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", str);
    char *trimmed = trim(buf);

    int ir, ig, ib;
    if (sscanf(trimmed, "%d,%d,%d", &ir, &ig, &ib) == 3) {
        *r = iclamp(ir, 0, 255);
        *g = iclamp(ig, 0, 255);
        *b = iclamp(ib, 0, 255);
        return true;
    }
    return false;
}

/* Parse a .theme file into ConfigTheme */
bool config_parse_theme(const char *path, ConfigTheme *theme) {
    if (!path || !theme) return false;

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    memset(theme, 0, sizeof(ConfigTheme));

    char line[512];
    char section[64] = {0};

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (!trimmed[0] || trimmed[0] == '#') continue;

        /* Section header */
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", trimmed + 1);
                continue;
            }
        }

        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);

        /* Parse based on section and key */
        if (strcmp(section, "name") == 0) {
            snprintf(theme->name, sizeof(theme->name), "%s", value);
        } else if (strcmp(section, "background") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "base") == 0) c = &theme->bg_base;
            else if (strcmp(key, "panel") == 0) c = &theme->bg_panel;
            else if (strcmp(key, "sel") == 0) c = &theme->bg_sel;
            else if (strcmp(key, "tab_act") == 0) c = &theme->bg_tab_act;
            else if (strcmp(key, "tab_inact") == 0) c = &theme->bg_tab_inact;
            else if (strcmp(key, "header") == 0) c = &theme->bg_header;
            else if (strcmp(key, "diff_add") == 0) c = &theme->bg_diff_add;
            else if (strcmp(key, "diff_del") == 0) c = &theme->bg_diff_del;
            else if (strcmp(key, "diff_hdr") == 0) c = &theme->bg_diff_hdr;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "foreground") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "normal") == 0) c = &theme->fg_normal;
            else if (strcmp(key, "dim") == 0) c = &theme->fg_dim;
            else if (strcmp(key, "bright") == 0) c = &theme->fg_bright;
            else if (strcmp(key, "sel") == 0) c = &theme->fg_sel;
            else if (strcmp(key, "accent1") == 0) c = &theme->fg_accent1;
            else if (strcmp(key, "accent2") == 0) c = &theme->fg_accent2;
            else if (strcmp(key, "accent3") == 0) c = &theme->fg_accent3;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "status_colors") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "staged") == 0) c = &theme->fg_staged;
            else if (strcmp(key, "unstaged") == 0) c = &theme->fg_unstaged;
            else if (strcmp(key, "untracked") == 0) c = &theme->fg_untracked;
            else if (strcmp(key, "conflict") == 0) c = &theme->fg_conflict;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "diff_colors") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "add") == 0) c = &theme->fg_diff_add;
            else if (strcmp(key, "del") == 0) c = &theme->fg_diff_del;
            else if (strcmp(key, "header") == 0) c = &theme->fg_diff_hdr;
            else if (strcmp(key, "context") == 0) c = &theme->fg_diff_ctx;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "graph_colors") == 0) {
            int idx = atoi(key);
            if (idx >= 0 && idx < 6) {
                ConfigColor *c = &theme->fg_graph[idx];
                snprintf(c->name, sizeof(c->name), "graph_%d", idx);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "ref_colors") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "local") == 0) c = &theme->fg_ref_local;
            else if (strcmp(key, "remote") == 0) c = &theme->fg_ref_remote;
            else if (strcmp(key, "tag") == 0) c = &theme->fg_ref_tag;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        } else if (strcmp(section, "ui_colors") == 0) {
            ConfigColor *c = NULL;
            if (strcmp(key, "ok") == 0) c = &theme->fg_ok;
            else if (strcmp(key, "error") == 0) c = &theme->fg_err;
            else if (strcmp(key, "linenum") == 0) c = &theme->fg_linenum;

            if (c) {
                snprintf(c->name, sizeof(c->name), "%s", key);
                config_parse_color(value, &c->r, &c->g, &c->b);
            }
        }
    }

    fclose(fp);

    /* Check if theme has a name */
    if (theme->name[0] == '\0') {
        snprintf(theme->name, sizeof(theme->name), "Custom");
    }

    return true;
}

/* Parse boolean value from string */
static bool parse_bool(const char *value, bool default_val) {
    if (!value) return default_val;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", value);
    char *trimmed = trim(buf);

    if (strcasecmp(trimmed, "true") == 0 || strcasecmp(trimmed, "yes") == 0 ||
        strcasecmp(trimmed, "on") == 0 || strcasecmp(trimmed, "1") == 0) {
        return true;
    }
    if (strcasecmp(trimmed, "false") == 0 || strcasecmp(trimmed, "no") == 0 ||
        strcasecmp(trimmed, "off") == 0 || strcasecmp(trimmed, "0") == 0) {
        return false;
    }
    return default_val;
}

/* Parse main config file */
bool config_parse_main(const char *path, Config *cfg) {
    if (!path || !cfg) return false;

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[512];
    char section[64] = {0};
    cfg->keybinding_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (!trimmed[0] || trimmed[0] == '#') continue;

        /* Section header */
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", trimmed + 1);
                continue;
            }
        }

        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);

        if (strcmp(section, "general") == 0) {
            if (strcmp(key, "theme") == 0) {
                snprintf(cfg->general.theme_name, sizeof(cfg->general.theme_name), "%s", value);
            } else if (strcmp(key, "sidebar_width") == 0) {
                cfg->general.sidebar_width = atoi(value);
            } else if (strcmp(key, "diff_sidebyside") == 0) {
                cfg->general.diff_sidebyside = parse_bool(value, true);
            } else if (strcmp(key, "diff_wrap") == 0) {
                cfg->general.diff_wrap = parse_bool(value, false);
            } else if (strcmp(key, "diff_continuous") == 0) {
                cfg->general.diff_continuous = parse_bool(value, false);
            } else if (strcmp(key, "col_hash_width") == 0) {
                cfg->general.col_hash_width = atoi(value);
            } else if (strcmp(key, "col_author_width") == 0) {
                cfg->general.col_author_width = atoi(value);
            } else if (strcmp(key, "col_date_width") == 0) {
                cfg->general.col_date_width = atoi(value);
            }
        } else if (strcmp(section, "keybindings") == 0) {
            if (cfg->keybinding_count < 50) {
                ConfigKeybinding *kb = &cfg->keybindings[cfg->keybinding_count];
                snprintf(kb->key, sizeof(kb->key), "%s", key);
                snprintf(kb->action, sizeof(kb->action), "%s", value);
                cfg->keybinding_count++;
            }
        }
    }

    fclose(fp);
    return true;
}

/* Create config directory if it doesn't exist */
static void ensure_config_dir(void) {
    const char *dir = config_get_dir();
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0755);
    }

    /* Create themes directory */
    char themes_dir[512];
    snprintf(themes_dir, sizeof(themes_dir), "%s/themes", dir);
    if (stat(themes_dir, &st) == -1) {
        mkdir(themes_dir, 0755);
    }
}

/* Save default config as template */
bool config_save_default(const char *path) {
    if (!path) return false;

    ensure_config_dir();

    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    fprintf(fp, "# tuide configuration file\n");
    fprintf(fp, "# Lines starting with # are comments\n");
    fprintf(fp, "\n");
    fprintf(fp, "[general]\n");
    fprintf(fp, "# Theme name (built-in: Dark+, VS Community Light, Solarized Dark, One Dark)\n");
    fprintf(fp, "# Or path to custom .theme file in ~/.config/tuide/themes/\n");
    fprintf(fp, "theme = Dark+\n");
    fprintf(fp, "\n");
    fprintf(fp, "# Layout settings\n");
    fprintf(fp, "sidebar_width = 25\n");
    fprintf(fp, "diff_sidebyside = true\n");
    fprintf(fp, "diff_wrap = false\n");
    fprintf(fp, "diff_continuous = false\n");
    fprintf(fp, "\n");
    fprintf(fp, "# Column widths in log view\n");
    fprintf(fp, "col_hash_width = 9\n");
    fprintf(fp, "col_author_width = 14\n");
    fprintf(fp, "col_date_width = 13\n");
    fprintf(fp, "\n");
    fprintf(fp, "[keybindings]\n");
    fprintf(fp, "# View switching\n");
    fprintf(fp, "status = S\n");
    fprintf(fp, "log = G\n");
    fprintf(fp, "branches = B\n");
    fprintf(fp, "stash = Z\n");
    fprintf(fp, "help = ?\n");
    fprintf(fp, "\n");
    fprintf(fp, "# Actions\n");
    fprintf(fp, "stage = s\n");
    fprintf(fp, "unstage = u\n");
    fprintf(fp, "commit = c\n");
    fprintf(fp, "amend = A\n");
    fprintf(fp, "push = P\n");
    fprintf(fp, "pull = f\n");
    fprintf(fp, "stash = s\n");
    fprintf(fp, "\n");
    fprintf(fp, "# Navigation\n");
    fprintf(fp, "next_tab = Tab\n");
    fprintf(fp, "prev_tab = Shift+Tab\n");
    fprintf(fp, "toggle_focus = Tab\n");

    fclose(fp);
    return true;
}

/* Apply general configuration to app state */
void config_apply_general(void) {
    if (!g_config.loaded) return;

    if (g_config.general.sidebar_width > 0) {
        g_app_state.sidebar_w = g_config.general.sidebar_width;
    }

    g_app_state.diff_sidebyside = g_config.general.diff_sidebyside;
    g_app_state.diff_wrap = g_config.general.diff_wrap;
    g_app_state.diff_continuous = g_config.general.diff_continuous;

    if (g_config.general.col_hash_width > 0) {
        g_app_state.col_hash_w = g_config.general.col_hash_width;
    }
    if (g_config.general.col_author_width > 0) {
        g_app_state.col_author_w = g_config.general.col_author_width;
    }
    if (g_config.general.col_date_width > 0) {
        g_app_state.col_date_w = g_config.general.col_date_width;
    }
}

/* Load configuration */
void config_load(void) {
    memset(&g_config, 0, sizeof(Config));
    snprintf(g_config.config_path, sizeof(g_config.config_path), "%s", config_get_path());

    /* Set default values */
    snprintf(g_config.general.theme_name, sizeof(g_config.general.theme_name), "Dark+");
    g_config.general.sidebar_width = 25;
    g_config.general.diff_sidebyside = true;
    g_config.general.diff_wrap = false;
    g_config.general.diff_continuous = false;
    g_config.general.col_hash_width = 9;
    g_config.general.col_author_width = 14;
    g_config.general.col_date_width = 13;

    /* Try to load config file */
    if (config_parse_main(g_config.config_path, &g_config)) {
        g_config.loaded = true;
    }

    /* Check if theme is a custom file path */
    if (g_config.general.theme_name[0] == '/' ||
        (g_config.general.theme_name[0] == '.' && g_config.general.theme_name[1] == '/') ||
        strstr(g_config.general.theme_name, "/") != NULL) {
        /* It's a path - could be absolute or relative */
        snprintf(g_config.custom_theme_path, sizeof(g_config.custom_theme_path),
                 "%s", g_config.general.theme_name);
    } else {
        /* Check if it's in the themes directory */
        char theme_path[512];
        const char *dir = config_get_dir();
        snprintf(theme_path, sizeof(theme_path), "%s/themes/%s.theme", dir, g_config.general.theme_name);

        /* Try to load as custom theme file */
        FILE *tfp = fopen(theme_path, "r");
        if (tfp) {
            fclose(tfp);
            snprintf(g_config.custom_theme_path, sizeof(g_config.custom_theme_path), "%s", theme_path);
        }
    }
}

/* Get global config pointer */
Config *config_get(void) {
    return &g_config;
}
