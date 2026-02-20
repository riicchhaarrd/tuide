#define _XOPEN_SOURCE 700
#include "render.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

#include "term.h"
#include "config.h"

/* Dark+ (VSCode Dark+) */
static const Theme TH_DARK = {"Dark+ (VSCode)",
							  {30, 30, 30},
							  {37, 37, 38},
							  {58, 58, 58},
							  {30, 30, 30},
							  {45, 45, 45},
							  {50, 50, 50},
							  {19, 41, 19},
							  {51, 18, 18},
							  {25, 40, 60},
							  {212, 212, 212},
							  {90, 90, 90},
							  {255, 255, 255},
							  {255, 255, 255},
							  {86, 156, 214},
							  {220, 220, 170},
							  {106, 153, 85},
							  {78, 201, 176},
							  {206, 145, 120},
							  {128, 128, 128},
							  {252, 100, 100},
							  {130, 255, 130},
							  {255, 100, 100},
							  {86, 156, 214},
							  {180, 180, 180},
							  {{86, 156, 214},
							   {220, 220, 170},
							   {78, 201, 176},
							   {215, 186, 125},
							   {244, 71, 71},
							   {197, 134, 192}},
							  {78, 201, 176},
							  {197, 134, 192},
							  {220, 220, 170},
							  {78, 201, 176},
							  {244, 71, 71},
							  {90, 90, 90},
							  /* Syntax highlighting colors */
							  {197, 134, 192},  /* syn_keyword - purple/pink (control flow) */
							  {86, 156, 214},   /* syn_storage - blue (types) */
							  {206, 145, 120},  /* syn_string - orange */
							  {106, 153, 85},   /* syn_comment - green */
							  {181, 206, 168},  /* syn_number - light green */
							  {220, 220, 170},  /* syn_function - yellow */
							  {78, 201, 176},   /* syn_type - teal */
							  {212, 212, 212},  /* syn_variable - white/light gray */
							  {212, 212, 212},  /* syn_operator - white */
							  {86, 156, 214},   /* syn_preproc - blue */
							  {86, 156, 214},   /* syn_constant - blue */
							  {86, 156, 214},   /* syn_tag - blue */
							  {206, 145, 120},  /* syn_attribute - orange */
							  {197, 134, 192}}; /* syn_decorator - purple */

/* Visual Studio Community Light Blue C++ */
static const Theme TH_VSLIGHT = {
	"VS Community Light",
	{242, 242, 242},
	{230, 234, 242},
	{0, 120, 215},
	{242, 242, 242},
	{218, 223, 230},
	{205, 214, 229},
	{210, 240, 210},
	{250, 210, 210},
	{210, 225, 250},
	{30, 30, 30},
	{140, 140, 150},
	{0, 0, 0},
	{255, 255, 255},
	{116, 83, 31},
	{0, 80, 170},
	{80, 100, 80},
	{0, 128, 0},
	{180, 0, 0},
	{100, 100, 120},
	{200, 0, 0},
	{0, 100, 0},
	{150, 0, 0},
	{0, 50, 180},
	{60, 60, 60},
	{{0, 80, 170}, {0, 120, 0}, {170, 0, 0}, {140, 90, 0}, {120, 0, 120}, {0, 110, 120}},
	{0, 100, 0},
	{100, 0, 150},
	{140, 90, 0},
	{0, 100, 0},
	{180, 0, 0},
	{160, 160, 170},
	/* Syntax highlighting colors */
	{0, 0, 160},        /* syn_keyword - dark blue (control flow) */
	{0, 0, 255},        /* syn_storage - bright blue (types) */
	{163, 21, 21},      /* syn_string - red */
	{0, 128, 0},        /* syn_comment - green */
	{0, 0, 0},          /* syn_number - black */
	{0, 0, 255},        /* syn_function - blue */
	{0, 80, 170},       /* syn_type - dark blue */
	{30, 30, 30},       /* syn_variable - dark gray */
	{30, 30, 30},       /* syn_operator - dark gray */
	{128, 0, 128},      /* syn_preproc - purple */
	{0, 0, 255},        /* syn_constant - blue */
	{128, 0, 128},      /* syn_tag - purple */
	{163, 21, 21},      /* syn_attribute - red */
	{128, 0, 128}};     /* syn_decorator - purple */

/* Solarized Dark */
static const Theme TH_SOL = {
	"Solarized Dark",
	{0, 43, 54},
	{7, 54, 66},
	{0, 73, 89},
	{0, 43, 54},
	{7, 54, 66},
	{14, 65, 78},
	{0, 55, 30},
	{60, 20, 10},
	{0, 55, 80},
	{131, 148, 150},
	{60, 80, 85},
	{253, 246, 227},
	{253, 246, 227},
	{181, 137, 0},
	{38, 139, 210},
	{88, 110, 117},
	{133, 153, 0},
	{220, 50, 47},
	{88, 110, 117},
	{203, 75, 22},
	{133, 153, 0},
	{220, 50, 47},
	{38, 139, 210},
	{131, 148, 150},
	{{38, 139, 210}, {133, 153, 0}, {181, 137, 0}, {203, 75, 22}, {211, 54, 130}, {42, 161, 152}},
	{133, 153, 0},
	{211, 54, 130},
	{181, 137, 0},
	{133, 153, 0},
	{220, 50, 47},
	{60, 80, 85},
	/* Syntax highlighting colors */
	{211, 54, 130},     /* syn_keyword - magenta (control flow) */
	{38, 139, 210},     /* syn_storage - blue (types) */
	{42, 161, 152},     /* syn_string - cyan */
	{88, 110, 117},     /* syn_comment - base1 (gray) */
	{133, 153, 0},      /* syn_number - green */
	{181, 137, 0},      /* syn_function - yellow */
	{42, 161, 152},     /* syn_type - cyan */
	{131, 148, 150},    /* syn_variable - base0 (light gray) */
	{181, 137, 0},      /* syn_operator - yellow */
	{203, 75, 22},      /* syn_preproc - orange */
	{220, 50, 47},      /* syn_constant - red */
	{38, 139, 210},     /* syn_tag - blue */
	{181, 137, 0},      /* syn_attribute - yellow */
	{211, 54, 130}};    /* syn_decorator - magenta */

/* One Dark */
static const Theme TH_ONEDARK = {"One Dark",
								 {40, 44, 52},
								 {33, 37, 43},
								 {62, 68, 81},
								 {40, 44, 52},
								 {44, 50, 60},
								 {50, 56, 66},
								 {45, 60, 45},
								 {60, 45, 45},
								 {45, 55, 75},
								 {171, 178, 191},
								 {92, 99, 112},
								 {255, 255, 255},
								 {255, 255, 255},
								 {224, 108, 117},
								 {97, 175, 239},
								 {152, 195, 121},
								 {152, 195, 121},
								 {224, 108, 117},
								 {92, 99, 112},
								 {209, 154, 102},
								 {152, 195, 121},
								 {224, 108, 117},
								 {97, 175, 239},
								 {171, 178, 191},
								 {{97, 175, 239},
								  {198, 120, 221},
								  {152, 195, 121},
								  {209, 154, 102},
								  {224, 108, 117},
								  {86, 182, 194}},
								 {152, 195, 121},
								 {198, 120, 221},
								 {209, 154, 102},
								 {152, 195, 121},
								 {224, 108, 117},
								 {92, 99, 112},
								 /* Syntax highlighting colors */
								 {198, 120, 221},    /* syn_keyword - purple (control flow) */
								 {229, 192, 123},    /* syn_storage - yellow (types) */
								 {152, 195, 121},    /* syn_string - green */
								 {92, 99, 112},      /* syn_comment - gray */
								 {209, 154, 102},    /* syn_number - orange */
								 {97, 175, 239},     /* syn_function - blue */
								 {229, 192, 123},    /* syn_type - yellow */
								 {171, 178, 191},    /* syn_variable - light gray */
								 {171, 178, 191},    /* syn_operator - light gray */
								 {97, 175, 239},     /* syn_preproc - blue */
								 {209, 154, 102},    /* syn_constant - orange */
								 {224, 108, 117},    /* syn_tag - red */
								 {209, 154, 102},    /* syn_attribute - orange */
								 {198, 120, 221}};   /* syn_decorator - purple */

/* Dynamic theme array and count */
Theme *THEMES[16] = {NULL};
int NTHEMES = THEME_COUNT;

/* Built-in themes array */
static Theme *builtin_themes[THEME_COUNT] = {
	(Theme *)&TH_DARK,
	(Theme *)&TH_VSLIGHT,
	(Theme *)&TH_SOL,
	(Theme *)&TH_ONEDARK
};

/* Initialize theme system */
void themes_init(void) {
	/* Initialize with built-in themes */
	for (int i = 0; i < THEME_COUNT; i++) {
		THEMES[i] = builtin_themes[i];
	}
	NTHEMES = THEME_COUNT;

	/* Load custom theme from config if specified */
	if (g_config.loaded && g_config.custom_theme_path[0]) {
		theme_load_custom(g_config.custom_theme_path);
	}
}

/* Cleanup theme system */
void themes_cleanup(void) {
	/* Free custom themes (indices THEME_COUNT and above) */
	for (int i = THEME_COUNT; i < NTHEMES && i < 16; i++) {
		if (THEMES[i]) {
			free(THEMES[i]);
			THEMES[i] = NULL;
		}
	}
	/* Reset to built-in themes only */
	for (int i = 0; i < THEME_COUNT; i++) {
		THEMES[i] = builtin_themes[i];
	}
	NTHEMES = THEME_COUNT;
}

/* Convert ConfigColor to Color */
static Color configcolor_to_color(ConfigColor cc) {
	Color c;
	c.r = cc.r;
	c.g = cc.g;
	c.b = cc.b;
	return c;
}

/* Load custom theme from file */
bool theme_load_custom(const char *path) {
	if (!path) return false;

	ConfigTheme cfg_theme;
	if (!config_parse_theme(path, &cfg_theme)) {
		return false;
	}

	/* Allocate new theme */
	Theme *theme = calloc(1, sizeof(Theme));
	if (!theme) return false;

	/* Convert ConfigTheme to Theme */
	theme->name = strdup(cfg_theme.name);
	theme->bg_base = configcolor_to_color(cfg_theme.bg_base);
	theme->bg_panel = configcolor_to_color(cfg_theme.bg_panel);
	theme->bg_sel = configcolor_to_color(cfg_theme.bg_sel);
	theme->bg_tab_act = configcolor_to_color(cfg_theme.bg_tab_act);
	theme->bg_tab_inact = configcolor_to_color(cfg_theme.bg_tab_inact);
	theme->bg_header = configcolor_to_color(cfg_theme.bg_header);
	theme->bg_diff_add = configcolor_to_color(cfg_theme.bg_diff_add);
	theme->bg_diff_del = configcolor_to_color(cfg_theme.bg_diff_del);
	theme->bg_diff_hdr = configcolor_to_color(cfg_theme.bg_diff_hdr);

	theme->fg_normal = configcolor_to_color(cfg_theme.fg_normal);
	theme->fg_dim = configcolor_to_color(cfg_theme.fg_dim);
	theme->fg_bright = configcolor_to_color(cfg_theme.fg_bright);
	theme->fg_sel = configcolor_to_color(cfg_theme.fg_sel);
	theme->fg_accent1 = configcolor_to_color(cfg_theme.fg_accent1);
	theme->fg_accent2 = configcolor_to_color(cfg_theme.fg_accent2);
	theme->fg_accent3 = configcolor_to_color(cfg_theme.fg_accent3);

	theme->fg_staged = configcolor_to_color(cfg_theme.fg_staged);
	theme->fg_unstaged = configcolor_to_color(cfg_theme.fg_unstaged);
	theme->fg_untracked = configcolor_to_color(cfg_theme.fg_untracked);
	theme->fg_conflict = configcolor_to_color(cfg_theme.fg_conflict);

	theme->fg_diff_add = configcolor_to_color(cfg_theme.fg_diff_add);
	theme->fg_diff_del = configcolor_to_color(cfg_theme.fg_diff_del);
	theme->fg_diff_hdr = configcolor_to_color(cfg_theme.fg_diff_hdr);
	theme->fg_diff_ctx = configcolor_to_color(cfg_theme.fg_diff_ctx);

	for (int i = 0; i < 6; i++) {
		theme->fg_graph[i] = configcolor_to_color(cfg_theme.fg_graph[i]);
	}

	theme->fg_ref_local = configcolor_to_color(cfg_theme.fg_ref_local);
	theme->fg_ref_remote = configcolor_to_color(cfg_theme.fg_ref_remote);
	theme->fg_ref_tag = configcolor_to_color(cfg_theme.fg_ref_tag);

	theme->fg_ok = configcolor_to_color(cfg_theme.fg_ok);
	theme->fg_err = configcolor_to_color(cfg_theme.fg_err);
	theme->fg_linenum = configcolor_to_color(cfg_theme.fg_linenum);

	/* Syntax highlighting colors */
	theme->syn_keyword = configcolor_to_color(cfg_theme.syn_keyword);
	theme->syn_storage = configcolor_to_color(cfg_theme.syn_storage);
	theme->syn_string = configcolor_to_color(cfg_theme.syn_string);
	theme->syn_comment = configcolor_to_color(cfg_theme.syn_comment);
	theme->syn_number = configcolor_to_color(cfg_theme.syn_number);
	theme->syn_function = configcolor_to_color(cfg_theme.syn_function);
	theme->syn_type = configcolor_to_color(cfg_theme.syn_type);
	theme->syn_variable = configcolor_to_color(cfg_theme.syn_variable);
	theme->syn_operator = configcolor_to_color(cfg_theme.syn_operator);
	theme->syn_preproc = configcolor_to_color(cfg_theme.syn_preproc);
	theme->syn_constant = configcolor_to_color(cfg_theme.syn_constant);
	theme->syn_tag = configcolor_to_color(cfg_theme.syn_tag);
	theme->syn_attribute = configcolor_to_color(cfg_theme.syn_attribute);
	theme->syn_decorator = configcolor_to_color(cfg_theme.syn_decorator);

	/* Add to themes array */
	if (NTHEMES < 16) {
		THEMES[NTHEMES++] = theme;
		return true;
	} else {
		/* Replace first custom theme if array is full */
		free((void *)theme->name);
		free(theme);
		return false;
	}
}

/* Find theme by name */
int theme_find_by_name(const char *name) {
	if (!name) return -1;

	/* Check built-in themes first */
	for (int i = 0; i < NTHEMES; i++) {
		if (THEMES[i] && strcmp(THEMES[i]->name, name) == 0) {
			return i;
		}
	}

	/* Check for partial matches (for "Dark+" vs "Dark+ (VSCode)") */
	for (int i = 0; i < NTHEMES; i++) {
		if (THEMES[i] && strstr(THEMES[i]->name, name) != NULL) {
			return i;
		}
	}

	return -1;
}

void buf_clear(Buffer *b) {
	if (!b->cells) return;
	for (int i = 0; i < b->w * b->h; i++) {
		Cell *c = &b->cells[i];
		memset(c->ch, 0, 8);
		c->ch[0] = ' ';
		c->fg = TH->fg_normal;
		c->bg = TH->bg_base;
		c->bold = c->dim = c->italic = c->under = c->rev = false;
	}
}

void put_cell(int r, int c, const char *s) {
	if (r < 1 || r > g_app_state.rows || c < 1 || c > g_app_state.cols || !g_app_state.back.cells)
		return;
	Cell *cell = &g_app_state.back.cells[(r - 1) * g_app_state.cols + (c - 1)];
	memset(cell->ch, 0, 8);
	if (s && *s) {
		if ((unsigned char)*s < 32 && *s != '\x1b')
			cell->ch[0] = ' ';
		else {
			int i = 0;
			while (s[i] && i < 7) {
				cell->ch[i] = s[i];
				i++;
			}
		}
	} else {
		cell->ch[0] = ' ';
	}
	cell->fg = g_app_state.cur_fg;
	cell->bg = g_app_state.cur_bg;
	cell->bold = g_app_state.cur_bold;
	cell->dim = g_app_state.cur_dim;
	cell->italic = g_app_state.cur_italic;
	cell->under = g_app_state.cur_under;
	cell->rev = g_app_state.cur_rev;
}

void at(int r, int c) {
	g_app_state.cur_r = iclamp(r, 1, g_app_state.rows);
	g_app_state.cur_c = iclamp(c, 1, g_app_state.cols);
}
void cfg(Color c) { g_app_state.cur_fg = c; }
void cbg(Color c) { g_app_state.cur_bg = c; }
void rst(void) {
	g_app_state.cur_fg = TH->fg_normal;
	g_app_state.cur_bg = TH->bg_base;
	g_app_state.cur_bold = g_app_state.cur_dim = g_app_state.cur_italic = g_app_state.cur_under =
		g_app_state.cur_rev = false;
}

void draw_flush(void) {
	if (!g_app_state.back.cells || !g_app_state.front.cells) return;
	Color last_fg = {-1, -1, -1}, last_bg = {-1, -1, -1};
	bool last_bold = false, last_dim = false, last_italic = false, last_under = false,
		 last_rev = false;
	int tr = -1, tc = -1;

	for (int r = 1; r <= g_app_state.rows; r++) {
		for (int c = 1; c <= g_app_state.cols; c++) {
			Cell *b = &g_app_state.back.cells[(r - 1) * g_app_state.cols + (c - 1)];
			Cell *f = &g_app_state.front.cells[(r - 1) * g_app_state.cols + (c - 1)];

			if (memcmp(b, f, sizeof(Cell)) == 0) continue;

			if (tr != r || tc != c) {
				printf(CSI "%d;%dH", r, c);
			}

			bool attr_change =
				(b->bold != last_bold || b->dim != last_dim || b->italic != last_italic ||
				 b->under != last_under || b->rev != last_rev);
			bool color_change = (memcmp(&b->fg, &last_fg, sizeof(Color)) != 0 ||
								 memcmp(&b->bg, &last_bg, sizeof(Color)) != 0);

			if (attr_change || color_change) {
				if ((last_bold && !b->bold) || (last_dim && !b->dim) ||
					(last_italic && !b->italic) || (last_under && !b->under) ||
					(last_rev && !b->rev)) {
					printf(CSI "0m");
					last_fg.r = last_fg.g = last_fg.b = -1;
					last_bg.r = last_bg.g = last_bg.b = -1;
					last_bold = last_dim = last_italic = last_under = last_rev = false;
				}

				if (memcmp(&b->fg, &last_fg, sizeof(Color)) != 0) {
					printf(CSI "38;2;%d;%d;%dm", b->fg.r, b->fg.g, b->fg.b);
					last_fg = b->fg;
				}
				if (memcmp(&b->bg, &last_bg, sizeof(Color)) != 0) {
					printf(CSI "48;2;%d;%d;%dm", b->bg.r, b->bg.g, b->bg.b);
					last_bg = b->bg;
				}
				if (b->bold && !last_bold) {
					printf(T_BOLD);
					last_bold = true;
				}
				if (b->dim && !last_dim) {
					printf(T_DIM);
					last_dim = true;
				}
				if (b->italic && !last_italic) {
					printf(T_ITALIC);
					last_italic = true;
				}
				if (b->under && !last_under) {
					printf(T_UNDER);
					last_under = true;
				}
				if (b->rev && !last_rev) {
					printf(T_REVERSE);
					last_rev = true;
				}
			}

			if (b->ch[0])
				fputs(b->ch, stdout);
			else
				putchar(' ');
			*f = *b;
			tr = r;
			tc = c + 1;
		}
	}
	fflush(stdout);
}

bool is_selected(int y, int x) {
	if (!g_app_state.selecting) return false;
	int s_y = g_app_state.sel_start_y, s_x = g_app_state.sel_start_x;
	int e_y = g_app_state.sel_end_y, e_x = g_app_state.sel_end_x;
	if (s_y > e_y || (s_y == e_y && s_x > e_x)) {
		int t = s_y;
		s_y = e_y;
		e_y = t;
		t = s_x;
		s_x = e_x;
		e_x = t;
	}
	if (y < s_y || y > e_y) return false;
	if (g_app_state.diff_sidebyside) {
		int h = g_app_state.diff_split;
		bool s_left = (s_x < h), e_left = (e_x < h);
		if (s_left && e_left && x >= h) return false;
		if (!s_left && !e_left && x < h) return false;
	}
	if (y == s_y && x < s_x) return false;
	if (y == e_y && x > e_x) return false;
	return true;
}

void ppad_ext(const char *s, int w, int y_idx, int pane_rx) {
	if (w <= 0) return;
	int vis = 0;
	int display_width = 0;
	mbstate_t state = {0};
	const char *p = s;
	while (*p) {
		if (*p == '\x1b' && p[1] == '[') {
			p += 2;
			while (*p && *p != 'm') p++;
			if (*p == 'm') p++;
			continue;
		}
		wchar_t wc;
		size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
		if (len == (size_t)-1 || len == (size_t)-2) {
			p++;
			display_width++;
		} else if (len == 0) {
			break;
		} else {
			int width = wcwidth(wc);
			if (width < 0) width = 1;
			display_width += width;
			p += len;
		}
	}

	bool truncated = (display_width > w);
	int limit = truncated ? (w > 1 ? w - 1 : w) : w;

	while (*s && vis < limit) {
		if (*s == '\x1b' && s[1] == '[') {
			s += 2;
			while (*s && *s != 'm') {
				int val = atoi(s);
				if (val == 0)
					rst();
				else if (val == 1)
					g_app_state.cur_bold = true;
				else if (val == 2)
					g_app_state.cur_dim = true;
				else if (val == 3)
					g_app_state.cur_italic = true;
				else if (val == 4)
					g_app_state.cur_under = true;
				else if (val == 7)
					g_app_state.cur_rev = true;
				else if (val == 22) {
					g_app_state.cur_bold = false;
					g_app_state.cur_dim = false;
				}
				while (*s && isdigit(*s)) s++;
				if (*s == ';') s++;
			}
			if (*s == 'm') s++;
			continue;
		}
		wchar_t wc;
		mbstate_t mb_state = {0};
		size_t len = mbrtowc(&wc, s, MB_CUR_MAX, &mb_state);
		int char_width = 1;
		if (len == (size_t)-1 || len == (size_t)-2) {
			len = 1;
		} else if (len == 0) {
			break;
		} else {
			int width = wcwidth(wc);
			if (width < 0) width = 1;
			char_width = width;
			if (vis + char_width > limit) break;
		}

		char tmp[8] = {0};
		for (size_t i = 0; i < len && *s; i++) tmp[i] = *s++;

		bool sel = false;
		if (y_idx != -1) {
			int abs_x = g_app_state.cur_c - (pane_rx + 1);
			if (is_selected(y_idx, abs_x)) sel = true;
		}

		if (sel) {
			Color old_bg = g_app_state.cur_bg;
			cbg(TH->bg_sel);
			put_cell(g_app_state.cur_r, g_app_state.cur_c, tmp);
			cbg(old_bg);
		} else {
			put_cell(g_app_state.cur_r, g_app_state.cur_c, tmp);
		}
		g_app_state.cur_c++;
		vis += char_width;
	}
	if (truncated && w >= 1) {
		put_cell(g_app_state.cur_r, g_app_state.cur_c, "…");
		g_app_state.cur_c++;
		vis++;
	}
	while (vis < w) {
		bool sel = false;
		if (y_idx != -1) {
			int abs_x = g_app_state.cur_c - (pane_rx + 1);
			if (is_selected(y_idx, abs_x)) sel = true;
		}
		if (sel) {
			Color old_bg = g_app_state.cur_bg;
			cbg(TH->bg_sel);
			put_cell(g_app_state.cur_r, g_app_state.cur_c, " ");
			cbg(old_bg);
		} else {
			put_cell(g_app_state.cur_r, g_app_state.cur_c, " ");
		}
		g_app_state.cur_c++;
		vis++;
	}
}

void ppad(const char *s, int w) { ppad_ext(s, w, -1, 0); }

void box_top(int row, int col, int w, const char *title, bool active, const char *extra) {
	at(row, col);
	if (active) {
		g_app_state.cur_bold = true;
		cfg(TH->fg_accent1);
	} else
		cfg(TH->fg_dim);
	put_cell(row, col, active ? "┏" : "┌");
	int tlen = (int)strlen(title) + 4;
	int left = (w - 2 - tlen) / 2;
	if (left < 0) left = 0;
	for (int i = 0; i < left; i++) put_cell(row, col + 1 + i, active ? "━" : "─");

	if (active) {
		g_app_state.cur_bold = true;
		cfg(TH->fg_bright);
		cbg(TH->fg_accent1);
	} else
		cfg(TH->fg_dim);
	at(row, col + 1 + left);
	char tbuf[128];
	snprintf(tbuf, sizeof(tbuf), " %s %s ", active ? "●" : " ", title);
	ppad(tbuf, tlen);
	rst();

	if (active) {
		g_app_state.cur_bold = true;
		cfg(TH->fg_accent1);
	} else {
		rst();
		cfg(TH->fg_dim);
	}
	int right = w - 2 - left - tlen;

	if (extra) {
		int elen = (int)strlen(extra);
		if (right > elen + 2) {
			for (int i = 0; i < right - elen - 2; i++)
				put_cell(row, col + 1 + left + tlen + i, active ? "━" : "─");
			at(row, col + w - elen - 2);
			if (active) {
				cfg(TH->fg_bright);
				cbg(TH->bg_panel);
			}
			ppad(extra, elen);
			if (active) {
				rst();
				cfg(TH->fg_accent1);
			}
			put_cell(row, col + w - 2, active ? "━" : "─");
		} else {
			for (int i = 0; i < right; i++)
				put_cell(row, col + 1 + left + tlen + i, active ? "━" : "─");
		}
	} else {
		for (int i = 0; i < right; i++)
			put_cell(row, col + 1 + left + tlen + i, active ? "━" : "─");
	}

	put_cell(row, col + w - 1, active ? "┓" : "┐");
	rst();
}

void box_bot(int row, int col, int w, bool active) {
	at(row, col);
	if (active)
		cfg(TH->fg_accent1);
	else
		cfg(TH->fg_dim);
	put_cell(row, col, active ? "┗" : "└");
	for (int i = 0; i < w - 2; i++) put_cell(row, col + 1 + i, active ? "━" : "─");
	put_cell(row, col + w - 1, active ? "┛" : "┘");
	rst();
}

void box_sides(int top, int col, int w, int h, bool active) {
	if (active)
		cfg(TH->fg_accent1);
	else
		cfg(TH->fg_dim);
	for (int r = top + 1; r < top + h - 1; r++) {
		put_cell(r, col, active ? "┃" : "│");
		put_cell(r, col + w - 1, active ? "┃" : "│");
	}
	rst();
}

void box_fill(int top, int col, int w, int h, Color c) {
	cfg(TH->fg_normal);
	cbg(c);
	for (int r = top + 1; r < top + h - 1; r++) {
		for (int i = 0; i < w - 2; i++) put_cell(r, col + 1 + i, " ");
	}
	rst();
}

void draw_scrollbar(int r, int c, int h, int total, int vis, int scroll, bool active) {
	if (total <= vis || h <= 0) return;
	int bh = imax(1, (vis * h) / total);
	int bpos = ((scroll * (h - bh)) / (total - vis));
	for (int i = 0; i < h; i++) {
		at(r + i, c);
		if (i >= bpos && i < bpos + bh) {
			cfg(active ? TH->fg_accent1 : TH->fg_dim);
			put_cell(r + i, c, "█");
		} else {
			cfg(TH->fg_dim);
			put_cell(r + i, c, "│");
		}
	}
	rst();
}
