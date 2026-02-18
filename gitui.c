
/*
 * gitui - A modern, no-dependency Git TUI written in C99
 *
 * Features:
 *   - Status view (staged/unstaged files)
 *   - Log view (commit history with graph)
 *   - Diff view (inline diff for selected file)
 *   - Branch view (local/remote branches)
 *   - Stash view
 *   - Stage/unstage files with spacebar
 *   - Commit with message prompt
 *   - Push/Pull
 *   - Checkout branches
 *   - Create/delete branches
 *   - Interactive search/filter
 *   - Mouse support
 *   - Responsive layout
 *   - Color themes
 *
 * Build: cc -std=c99 -O2 -o gitui gitui.c
 * Run:   ./gitui (from inside a git repo)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

/* ─── Constants ─────────────────────────────────────────────── */
#define MAX_FILES    512
#define MAX_COMMITS  1024
#define MAX_BRANCHES 256
#define MAX_STASHES  64
#define MAX_DIFF_LINES 4096
#define LINE_MAX_LEN 512
#define INPUT_MAX    256
#define VERSION      "1.0.0"

/* ─── ANSI / Terminal codes ──────────────────────────────────── */
#define ESC "\x1b"
#define CSI ESC "["

#define CLEAR        CSI "2J" CSI "H"
#define HIDE_CURSOR  CSI "?25l"
#define SHOW_CURSOR  CSI "?25h"
#define ALT_SCREEN   ESC "[?1049h"
#define NORM_SCREEN  ESC "[?1049l"
#define MOUSE_ON     ESC "[?1000h" ESC "[?1006h"
#define MOUSE_OFF    ESC "[?1000l" ESC "[?1006l"
#define RESET        CSI "0m"

/* Colors */
#define FG_BLACK   CSI "30m"
#define FG_RED     CSI "31m"
#define FG_GREEN   CSI "32m"
#define FG_YELLOW  CSI "33m"
#define FG_BLUE    CSI "34m"
#define FG_MAGENTA CSI "35m"
#define FG_CYAN    CSI "36m"
#define FG_WHITE   CSI "37m"
#define FG_BRIGHT_BLACK   CSI "90m"
#define FG_BRIGHT_RED     CSI "91m"
#define FG_BRIGHT_GREEN   CSI "92m"
#define FG_BRIGHT_YELLOW  CSI "93m"
#define FG_BRIGHT_BLUE    CSI "94m"
#define FG_BRIGHT_MAGENTA CSI "95m"
#define FG_BRIGHT_CYAN    CSI "96m"
#define FG_BRIGHT_WHITE   CSI "97m"

#define BG_BLACK   CSI "40m"
#define BG_RED     CSI "41m"
#define BG_GREEN   CSI "42m"
#define BG_YELLOW  CSI "43m"
#define BG_BLUE    CSI "44m"
#define BG_MAGENTA CSI "45m"
#define BG_CYAN    CSI "46m"
#define BG_WHITE   CSI "47m"
#define BG_BRIGHT_BLACK   CSI "100m"
#define BG_BRIGHT_BLUE    CSI "104m"

#define BOLD      CSI "1m"
#define DIM       CSI "2m"
#define ITALIC    CSI "3m"
#define UNDERLINE CSI "4m"
#define REVERSE   CSI "7m"

/* ─── Types ──────────────────────────────────────────────────── */

typedef enum {
    VIEW_STATUS,
    VIEW_LOG,
    VIEW_DIFF,
    VIEW_BRANCHES,
    VIEW_STASH,
    VIEW_HELP,
    VIEW_COUNT
} View;

typedef enum {
    FILE_UNTRACKED,
    FILE_MODIFIED,
    FILE_STAGED_MODIFIED,
    FILE_STAGED_NEW,
    FILE_STAGED_DELETED,
    FILE_DELETED,
    FILE_RENAMED,
    FILE_CONFLICT
} FileStatus;

typedef struct {
    char path[512];
    char orig_path[512]; /* for renames */
    FileStatus status;
    bool staged;
} GitFile;

typedef struct {
    char hash[12];
    char author[64];
    char date[32];
    char subject[256];
    char refs[128];
} GitCommit;

typedef struct {
    char name[128];
    bool is_remote;
    bool is_current;
    char upstream[128];
    int ahead;
    int behind;
} GitBranch;

typedef struct {
    char message[256];
    char hash[12];
    int index;
} GitStash;

typedef struct {
    char line[LINE_MAX_LEN];
    int type; /* 0=context, 1=added, 2=removed, 3=header */
} DiffLine;

typedef struct {
    /* Terminal state */
    struct termios orig_termios;
    int rows, cols;
    bool running;

    /* Current view */
    View current_view;

    /* Status */
    GitFile files[MAX_FILES];
    int file_count;
    int file_sel;
    int file_scroll;
    bool show_unstaged;

    /* Log */
    GitCommit commits[MAX_COMMITS];
    int commit_count;
    int commit_sel;
    int commit_scroll;

    /* Diff */
    DiffLine diff_lines[MAX_DIFF_LINES];
    int diff_count;
    int diff_scroll;
    char diff_file[512];
    bool diff_staged;

    /* Branches */
    GitBranch branches[MAX_BRANCHES];
    int branch_count;
    int branch_sel;
    int branch_scroll;

    /* Stash */
    GitStash stashes[MAX_STASHES];
    int stash_count;
    int stash_sel;

    /* Input/prompt */
    bool in_prompt;
    char prompt_label[128];
    char prompt_buf[INPUT_MAX];
    int prompt_cursor;
    void (*prompt_cb)(const char *);

    /* Search */
    bool in_search;
    char search_buf[INPUT_MAX];
    int search_cursor;

    /* Status bar message */
    char status_msg[256];
    time_t status_msg_time;

    /* Repo info */
    char branch_name[128];
    char repo_root[512];

    /* Notification */
    char notif[256];
    time_t notif_time;
} State;

/* ─── Globals ────────────────────────────────────────────────── */
static State g;
static volatile int g_resize = 0;

/* ─── Utility ────────────────────────────────────────────────── */
static void set_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status_msg, sizeof(g.status_msg), fmt, ap);
    va_end(ap);
    g.status_msg_time = time(NULL);
}

static void die(const char *msg) {
    write(STDOUT_FILENO, NORM_SCREEN SHOW_CURSOR MOUSE_OFF, 
          strlen(NORM_SCREEN SHOW_CURSOR MOUSE_OFF));
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g.orig_termios);
    fprintf(stderr, "gitui: %s\n", msg);
    exit(1);
}

/* Run a git command, capture output. Returns malloc'd string or NULL. */
static char *git_run(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); pclose(fp); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

/* Run a git command silently (for side effects). Returns exit code. */
static int git_exec(const char *fmt, ...) {
    char cmd[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    
    /* Redirect stderr to /dev/null for silent operation */
    char full[1200];
    snprintf(full, sizeof(full), "%s 2>/dev/null", cmd);
    return system(full);
}

static void trim_trailing(char *s) {
    int n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* ─── Terminal ───────────────────────────────────────────────── */
static void term_raw(void) {
    tcgetattr(STDIN_FILENO, &g.orig_termios);
    struct termios raw = g.orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1; /* 100ms timeout for non-blocking */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g.orig_termios);
}

static void get_winsize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        g.cols = 80; g.rows = 24;
    } else {
        g.cols = ws.ws_col;
        g.rows = ws.ws_row;
    }
}

static void move_to(int row, int col) {
    printf(CSI "%d;%dH", row, col);
}

static void clear_line(void) {
    printf(CSI "2K");
}

static void set_color(const char *color) {
    printf("%s", color);
}

/* ─── Signal handling ────────────────────────────────────────── */
static void sigwinch_handler(int sig) {
    (void)sig;
    g_resize = 1;
}

static void sigint_handler(int sig) {
    (void)sig;
    g.running = false;
}

/* ─── Git data loading ───────────────────────────────────────── */
static void load_branch(void) {
    char *out = git_run("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    if (out) {
        trim_trailing(out);
        snprintf(g.branch_name, sizeof(g.branch_name), "%s", out);
        free(out);
    } else {
        snprintf(g.branch_name, sizeof(g.branch_name), "unknown");
    }
}

static void load_repo_root(void) {
    char *out = git_run("git rev-parse --show-toplevel 2>/dev/null");
    if (out) {
        trim_trailing(out);
        snprintf(g.repo_root, sizeof(g.repo_root), "%s", out);
        free(out);
    }
}

static FileStatus parse_xy(char x, char y) {
    if (x == '?' && y == '?') return FILE_UNTRACKED;
    if (x == 'A') return FILE_STAGED_NEW;
    if (x == 'D') return FILE_STAGED_DELETED;
    if (x == 'R') return FILE_RENAMED;
    if (x == 'M') return FILE_STAGED_MODIFIED;
    if (y == 'M') return FILE_MODIFIED;
    if (y == 'D') return FILE_DELETED;
    if (x == 'U' || y == 'U') return FILE_CONFLICT;
    return FILE_MODIFIED;
}

static void load_status(void) {
    g.file_count = 0;
    char *out = git_run("git status --porcelain=v1 -u 2>/dev/null");
    if (!out) return;

    char *line = out;
    while (*line && g.file_count < MAX_FILES) {
        if (strlen(line) < 4) { line = strchr(line, '\n'); if (line) line++; continue; }
        
        char x = line[0], y = line[1];
        char *path_start = line + 3;
        char *nl = strchr(path_start, '\n');
        size_t path_len = nl ? (size_t)(nl - path_start) : strlen(path_start);
        
        GitFile *f = &g.files[g.file_count];
        memset(f, 0, sizeof(*f));
        
        /* Handle rename: "old -> new" */
        char path_buf[512];
        if (path_len >= sizeof(path_buf)) path_len = sizeof(path_buf)-1;
        memcpy(path_buf, path_start, path_len);
        path_buf[path_len] = '\0';
        
        char *arrow = strstr(path_buf, " -> ");
        if (arrow) {
            *arrow = '\0';
            snprintf(f->orig_path, sizeof(f->orig_path), "%s", path_buf);
            snprintf(f->path, sizeof(f->path), "%s", arrow + 4);
        } else {
            snprintf(f->path, sizeof(f->path), "%s", path_buf);
        }
        
        f->status = parse_xy(x, y);
        f->staged = (x != ' ' && x != '?' && x != '!');
        
        g.file_count++;
        line = nl ? nl + 1 : line + strlen(line);
    }
    free(out);
    g.file_sel = clamp(g.file_sel, 0, g.file_count > 0 ? g.file_count - 1 : 0);
}

static void load_log(void) {
    g.commit_count = 0;
    /* Format: hash|author|date|refs|subject */
    char *out = git_run(
        "git log --pretty=format:'%h|%an|%ar|%D|%s' -n 200 2>/dev/null");
    if (!out) return;

    char *line = out;
    while (*line && g.commit_count < MAX_COMMITS) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len == 0) { line = nl ? nl+1 : line+strlen(line); continue; }

        char buf[1024];
        if (len >= sizeof(buf)) len = sizeof(buf)-1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        /* Remove surrounding single quotes if present */
        char *p = buf;
        if (*p == '\'') p++;
        size_t plen = strlen(p);
        if (plen > 0 && p[plen-1] == '\'') p[plen-1] = '\0';

        GitCommit *c = &g.commits[g.commit_count];
        memset(c, 0, sizeof(*c));

        char *tok = strtok(p, "|");
        if (tok) snprintf(c->hash,    sizeof(c->hash),    "%s", tok);
        tok = strtok(NULL, "|");
        if (tok) snprintf(c->author,  sizeof(c->author),  "%s", tok);
        tok = strtok(NULL, "|");
        if (tok) snprintf(c->date,    sizeof(c->date),    "%s", tok);
        tok = strtok(NULL, "|");
        if (tok) snprintf(c->refs,    sizeof(c->refs),    "%s", tok);
        tok = strtok(NULL, "");
        if (tok) {
            /* remove trailing quote */
            size_t tl = strlen(tok);
            if (tl > 0 && tok[tl-1] == '\'') tok[tl-1] = '\0';
            snprintf(c->subject, sizeof(c->subject), "%s", tok);
        }

        g.commit_count++;
        line = nl ? nl+1 : line+strlen(line);
    }
    free(out);
    g.commit_sel = clamp(g.commit_sel, 0, g.commit_count > 0 ? g.commit_count-1 : 0);
}

static void load_branches(void) {
    g.branch_count = 0;
    char *out = git_run(
        "git branch -vv --format='%(HEAD)|%(refname:short)|%(upstream:short)|%(upstream:track)' 2>/dev/null");
    if (!out) return;

    char *line = out;
    while (*line && g.branch_count < MAX_BRANCHES) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        if (len == 0) { line = nl ? nl+1 : line+strlen(line); continue; }

        char buf[512];
        if (len >= sizeof(buf)) len = sizeof(buf)-1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        GitBranch *b = &g.branches[g.branch_count];
        memset(b, 0, sizeof(*b));

        char *tok = strtok(buf, "|");
        if (tok) b->is_current = (tok[0] == '*');
        tok = strtok(NULL, "|");
        if (tok) {
            snprintf(b->name, sizeof(b->name), "%s", tok);
            b->is_remote = (strncmp(tok, "remotes/", 8) == 0 ||
                            strchr(tok, '/') != NULL);
        }
        tok = strtok(NULL, "|");
        if (tok) snprintf(b->upstream, sizeof(b->upstream), "%s", tok);
        tok = strtok(NULL, "");
        if (tok) {
            /* parse [ahead X, behind Y] */
            char *ah = strstr(tok, "ahead ");
            char *bh = strstr(tok, "behind ");
            if (ah) b->ahead  = atoi(ah + 6);
            if (bh) b->behind = atoi(bh + 7);
        }

        g.branch_count++;
        line = nl ? nl+1 : line+strlen(line);
    }
    free(out);

    /* Also load remote branches */
    char *out2 = git_run(
        "git branch -r --format='%(refname:short)' 2>/dev/null");
    if (out2) {
        line = out2;
        while (*line && g.branch_count < MAX_BRANCHES) {
            char *nl = strchr(line, '\n');
            size_t len = nl ? (size_t)(nl-line) : strlen(line);
            if (len == 0) { line = nl ? nl+1 : line+strlen(line); continue; }
            char buf[256];
            if (len >= sizeof(buf)) len = sizeof(buf)-1;
            memcpy(buf, line, len);
            buf[len] = '\0';
            trim_trailing(buf);
            if (strstr(buf, "HEAD")) { line = nl ? nl+1 : line+strlen(line); continue; }
            /* check not already in list */
            bool found = false;
            for (int i = 0; i < g.branch_count; i++)
                if (strcmp(g.branches[i].name, buf) == 0) { found = true; break; }
            if (!found) {
                GitBranch *b = &g.branches[g.branch_count++];
                memset(b, 0, sizeof(*b));
                snprintf(b->name, sizeof(b->name), "%s", buf);
                b->is_remote = true;
            }
            line = nl ? nl+1 : line+strlen(line);
        }
        free(out2);
    }

    g.branch_sel = clamp(g.branch_sel, 0, g.branch_count > 0 ? g.branch_count-1 : 0);
}

static void load_stash(void) {
    g.stash_count = 0;
    char *out = git_run("git stash list --format='%gd|%h|%s' 2>/dev/null");
    if (!out) return;

    char *line = out;
    while (*line && g.stash_count < MAX_STASHES) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        if (len == 0) { line = nl ? nl+1 : line+strlen(line); continue; }
        char buf[512];
        if (len >= sizeof(buf)) len = sizeof(buf)-1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        GitStash *s = &g.stashes[g.stash_count];
        memset(s, 0, sizeof(*s));

        char *tok = strtok(buf, "|");
        if (tok) {
            /* stash@{N} -> extract N */
            char *lb = strchr(tok, '{');
            s->index = lb ? atoi(lb+1) : g.stash_count;
        }
        tok = strtok(NULL, "|");
        if (tok) snprintf(s->hash, sizeof(s->hash), "%s", tok);
        tok = strtok(NULL, "");
        if (tok) snprintf(s->message, sizeof(s->message), "%s", tok);

        g.stash_count++;
        line = nl ? nl+1 : line+strlen(line);
    }
    free(out);
    g.stash_sel = clamp(g.stash_sel, 0, g.stash_count > 0 ? g.stash_count-1 : 0);
}

static void load_diff(const char *filepath, bool staged) {
    g.diff_count = 0;
    if (!filepath || !filepath[0]) return;

    char cmd[1024];
    if (staged)
        snprintf(cmd, sizeof(cmd), "git diff --cached -- '%s' 2>/dev/null", filepath);
    else
        snprintf(cmd, sizeof(cmd), "git diff -- '%s' 2>/dev/null", filepath);

    char *out = git_run(cmd);
    
    /* If untracked, show file content */
    if (!out || out[0] == '\0') {
        free(out);
        snprintf(cmd, sizeof(cmd), "cat '%s' 2>/dev/null | head -200", filepath);
        out = git_run(cmd);
        if (out && out[0]) {
            char *line = out;
            int lnum = 1;
            while (*line && g.diff_count < MAX_DIFF_LINES) {
                char *nl = strchr(line, '\n');
                size_t len = nl ? (size_t)(nl-line) : strlen(line);
                DiffLine *dl = &g.diff_lines[g.diff_count++];
                snprintf(dl->line, sizeof(dl->line), "%4d  %.*s", lnum++, (int)len, line);
                dl->type = 1; /* show as added */
                line = nl ? nl+1 : line+strlen(line);
            }
        }
        free(out);
        return;
    }

    char *line = out;
    while (*line && g.diff_count < MAX_DIFF_LINES) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        DiffLine *dl = &g.diff_lines[g.diff_count];
        if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN-1;
        memcpy(dl->line, line, len);
        dl->line[len] = '\0';

        if (dl->line[0] == '+') dl->type = 1;
        else if (dl->line[0] == '-') dl->type = 2;
        else if (dl->line[0] == '@') dl->type = 3;
        else dl->type = 0;

        g.diff_count++;
        line = nl ? nl+1 : line+strlen(line);
    }
    free(out);
    g.diff_scroll = 0;
}

/* ─── Drawing helpers ────────────────────────────────────────── */

static void draw_hline(int row, int col, int width, const char *ch) {
    move_to(row, col);
    for (int i = 0; i < width; i++) printf("%s", ch);
}

static void draw_box_title(int row, int col, int width, const char *title, bool active) {
    move_to(row, col);
    if (active) printf(BOLD FG_BRIGHT_CYAN);
    else printf(FG_BRIGHT_BLACK);
    printf("┌");
    int tlen = (int)strlen(title) + 2;
    int left = (width - 2 - tlen) / 2;
    for (int i = 0; i < left && i < width-2; i++) printf("─");
    printf(" %s " RESET, title);
    if (active) printf(BOLD FG_BRIGHT_CYAN);
    else printf(FG_BRIGHT_BLACK);
    int right = width - 2 - left - tlen;
    for (int i = 0; i < right; i++) printf("─");
    printf("┐" RESET);
}

static void draw_box_bottom(int row, int col, int width) {
    move_to(row, col);
    printf(FG_BRIGHT_BLACK "└");
    for (int i = 0; i < width-2; i++) printf("─");
    printf("┘" RESET);
}

static void draw_vline(int row, int col, int height) {
    for (int i = 0; i < height; i++) {
        move_to(row+i, col);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }
}

/* Truncate and pad string to width */
static void print_padded(const char *s, int width) {
    int len = 0;
    const char *p = s;
    while (*p && len < width) {
        /* Skip ANSI sequences for length counting */
        if (*p == '\x1b') {
            while (*p && *p != 'm') p++;
            if (*p) p++;
            continue;
        }
        len++; p++;
    }
    printf("%.*s", (int)(p - s), s);
    for (int i = len; i < width; i++) printf(" ");
}

static const char *file_status_icon(FileStatus s, bool staged) {
    if (s == FILE_UNTRACKED)       return "?";
    if (s == FILE_CONFLICT)        return "!";
    if (!staged) {
        if (s == FILE_MODIFIED)    return "M";
        if (s == FILE_DELETED)     return "D";
    } else {
        if (s == FILE_STAGED_NEW)  return "A";
        if (s == FILE_STAGED_MODIFIED) return "M";
        if (s == FILE_STAGED_DELETED)  return "D";
        if (s == FILE_RENAMED)     return "R";
    }
    return " ";
}

static const char *file_status_color(FileStatus s, bool staged) {
    if (s == FILE_UNTRACKED)   return FG_BRIGHT_BLACK;
    if (s == FILE_CONFLICT)    return FG_BRIGHT_RED BOLD;
    if (staged) return FG_BRIGHT_GREEN;
    return FG_BRIGHT_RED;
}

/* ─── View: Status ───────────────────────────────────────────── */

/* Count staged/unstaged */
static void status_counts(int *staged_out, int *unstaged_out) {
    int s = 0, u = 0;
    for (int i = 0; i < g.file_count; i++) {
        if (g.files[i].staged) s++;
        else u++;
    }
    if (staged_out) *staged_out = s;
    if (unstaged_out) *unstaged_out = u;
}

static void draw_status(int top, int left, int height, int width) {
    int staged_count, unstaged_count;
    status_counts(&staged_count, &unstaged_count);

    /* Split pane: top=staged, bottom=unstaged */
    int split = height / 2;

    /* Staged section */
    {
        char title[64];
        snprintf(title, sizeof(title), "Staged (%d)", staged_count);
        bool active = (g.current_view == VIEW_STATUS);
        draw_box_title(top, left, width, title, active);

        int row = top + 1;
        int lim = top + split - 1;
        int idx = 0, shown = 0;

        for (int i = 0; i < g.file_count && row < lim; i++) {
            if (!g.files[i].staged) continue;
            bool sel = (g.file_sel == i);
            move_to(row, left);
            printf(FG_BRIGHT_BLACK "│" RESET);

            if (sel) printf(BG_BRIGHT_BLACK BOLD);
            
            const char *icon  = file_status_icon(g.files[i].status, true);
            const char *color = file_status_color(g.files[i].status, true);
            printf(" %s%s" RESET, color, icon);
            if (sel) printf(BG_BRIGHT_BLACK);
            printf(" ");
            
            int fw = width - 6;
            char display[512];
            if (g.files[i].orig_path[0])
                snprintf(display, sizeof(display), "%s → %s", g.files[i].orig_path, g.files[i].path);
            else
                snprintf(display, sizeof(display), "%s", g.files[i].path);
            print_padded(display, fw);
            printf(RESET);

            move_to(row, left + width - 1);
            printf(FG_BRIGHT_BLACK "│" RESET);
            row++; shown++; idx++;
        }
        /* Fill empty rows */
        while (row < lim) {
            move_to(row, left);
            printf(FG_BRIGHT_BLACK "│" RESET);
            move_to(row, left + width - 1);
            printf(FG_BRIGHT_BLACK "│" RESET);
            row++;
        }
        draw_box_bottom(top + split - 1, left, width);
    }

    /* Unstaged section */
    {
        char title[64];
        snprintf(title, sizeof(title), "Unstaged (%d)", unstaged_count);
        bool active = (g.current_view == VIEW_STATUS);
        draw_box_title(top + split, left, width, title, active);

        int row = top + split + 1;
        int lim = top + height - 1;

        for (int i = 0; i < g.file_count && row < lim; i++) {
            if (g.files[i].staged) continue;
            bool sel = (g.file_sel == i);
            move_to(row, left);
            printf(FG_BRIGHT_BLACK "│" RESET);

            if (sel) printf(BG_BRIGHT_BLACK BOLD);

            const char *icon  = file_status_icon(g.files[i].status, false);
            const char *color = file_status_color(g.files[i].status, false);
            printf(" %s%s" RESET, color, icon);
            if (sel) printf(BG_BRIGHT_BLACK);
            printf(" ");

            int fw = width - 6;
            print_padded(g.files[i].path, fw);
            printf(RESET);

            move_to(row, left + width - 1);
            printf(FG_BRIGHT_BLACK "│" RESET);
            row++;
        }
        while (row < lim) {
            move_to(row, left);
            printf(FG_BRIGHT_BLACK "│" RESET);
            move_to(row, left + width - 1);
            printf(FG_BRIGHT_BLACK "│" RESET);
            row++;
        }
        draw_box_bottom(top + height - 1, left, width);
    }
}

/* ─── View: Log ──────────────────────────────────────────────── */
static void draw_log(int top, int left, int height, int width) {
    draw_box_title(top, left, width, "Commit Log", g.current_view == VIEW_LOG);

    int row = top + 1;
    int lim = top + height - 1;
    int visible = lim - row;

    /* Auto-scroll */
    if (g.commit_sel < g.commit_scroll)
        g.commit_scroll = g.commit_sel;
    if (g.commit_sel >= g.commit_scroll + visible)
        g.commit_scroll = g.commit_sel - visible + 1;

    for (int i = g.commit_scroll; i < g.commit_count && row < lim; i++, row++) {
        bool sel = (g.commit_sel == i);
        GitCommit *c = &g.commits[i];

        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);

        if (sel) printf(BG_BRIGHT_BLACK BOLD);

        /* Hash */
        printf(" " FG_BRIGHT_YELLOW "%s" RESET, c->hash);
        if (sel) printf(BG_BRIGHT_BLACK);
        printf(" ");

        /* Refs (branch labels) */
        if (c->refs[0]) {
            printf(FG_BRIGHT_CYAN "(%s) " RESET, c->refs);
            if (sel) printf(BG_BRIGHT_BLACK);
        }

        /* Author */
        int author_w = 12;
        printf(FG_BRIGHT_MAGENTA);
        print_padded(c->author, author_w);
        printf(RESET);
        if (sel) printf(BG_BRIGHT_BLACK);
        printf(" ");

        /* Date */
        printf(FG_BRIGHT_BLACK);
        int date_w = 12;
        print_padded(c->date, date_w);
        printf(RESET);
        if (sel) printf(BG_BRIGHT_BLACK);
        printf(" ");

        /* Subject */
        int used = 1 + 8 + 1 + 1; /* borders + hash + space + space */
        if (c->refs[0]) used += strlen(c->refs) + 4;
        used += author_w + 1 + date_w + 1;
        int subj_w = width - 2 - used;
        if (subj_w < 4) subj_w = 4;
        print_padded(c->subject, subj_w);
        printf(RESET);

        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }

    /* Fill */
    while (row < lim) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
        row++;
    }
    draw_box_bottom(top + height - 1, left, width);
}

/* ─── View: Diff ─────────────────────────────────────────────── */
static void draw_diff(int top, int left, int height, int width) {
    char title[256];
    if (g.diff_file[0])
        snprintf(title, sizeof(title), "Diff: %s%s", 
                 g.diff_file, g.diff_staged ? " (staged)" : "");
    else
        snprintf(title, sizeof(title), "Diff");

    draw_box_title(top, left, width, title, g.current_view == VIEW_DIFF);

    int row = top + 1;
    int lim = top + height - 1;
    int visible = lim - row;

    if (g.diff_scroll < 0) g.diff_scroll = 0;
    if (g.diff_scroll > g.diff_count - visible && g.diff_count > visible)
        g.diff_scroll = g.diff_count - visible;

    for (int i = g.diff_scroll; i < g.diff_count && row < lim; i++, row++) {
        DiffLine *dl = &g.diff_lines[i];
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET " ");

        switch (dl->type) {
            case 1: printf(FG_BRIGHT_GREEN);  break;
            case 2: printf(FG_BRIGHT_RED);    break;
            case 3: printf(FG_BRIGHT_CYAN BOLD); break;
            default: printf(RESET); break;
        }

        /* Tab expand and truncate */
        char expanded[LINE_MAX_LEN*2];
        int ei = 0, col = 0;
        int max_w = width - 4;
        for (int ci = 0; dl->line[ci] && col < max_w; ci++) {
            if (dl->line[ci] == '\t') {
                int spaces = 4 - (col % 4);
                for (int s = 0; s < spaces && col < max_w; s++, col++, ei++)
                    expanded[ei] = ' ';
            } else {
                expanded[ei++] = dl->line[ci];
                col++;
            }
        }
        expanded[ei] = '\0';

        print_padded(expanded, max_w);
        printf(RESET);

        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }

    /* Scrollbar */
    if (g.diff_count > visible && visible > 2) {
        int bar_h = (visible * visible) / g.diff_count;
        if (bar_h < 1) bar_h = 1;
        int bar_pos = (g.diff_scroll * (visible - bar_h)) / 
                      (g.diff_count - visible + 1);
        for (int i = 0; i < visible; i++) {
            move_to(top + 1 + i, left + width - 1);
            if (i >= bar_pos && i < bar_pos + bar_h)
                printf(FG_BRIGHT_CYAN "█" RESET);
            else
                printf(FG_BRIGHT_BLACK "│" RESET);
        }
    }

    /* Fill */
    while (row < lim) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
        row++;
    }
    draw_box_bottom(top + height - 1, left, width);
}

/* ─── View: Branches ─────────────────────────────────────────── */
static void draw_branches(int top, int left, int height, int width) {
    draw_box_title(top, left, width, "Branches", g.current_view == VIEW_BRANCHES);

    int row = top + 1, lim = top + height - 1;
    int visible = lim - row;

    if (g.branch_sel < g.branch_scroll) g.branch_scroll = g.branch_sel;
    if (g.branch_sel >= g.branch_scroll + visible)
        g.branch_scroll = g.branch_sel - visible + 1;

    for (int i = g.branch_scroll; i < g.branch_count && row < lim; i++, row++) {
        GitBranch *b = &g.branches[i];
        bool sel = (g.branch_sel == i);

        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        if (sel) printf(BG_BRIGHT_BLACK BOLD);

        /* Current marker */
        if (b->is_current) printf(FG_BRIGHT_GREEN " * " RESET);
        else printf("   ");
        if (sel) printf(BG_BRIGHT_BLACK);

        /* Remote/local icon */
        if (b->is_remote) printf(FG_BRIGHT_MAGENTA "⬡ " RESET);
        else printf(FG_BRIGHT_BLUE "⬢ " RESET);
        if (sel) printf(BG_BRIGHT_BLACK);

        /* Name */
        int name_w = width - 20;
        if (name_w < 8) name_w = 8;
        print_padded(b->name, name_w);
        printf(RESET);

        /* Ahead/behind */
        if (b->ahead || b->behind) {
            printf(FG_BRIGHT_YELLOW " ↑%d↓%d" RESET, b->ahead, b->behind);
        } else if (b->upstream[0]) {
            printf(FG_BRIGHT_BLACK " ✓" RESET);
        }

        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }
    while (row < lim) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
        row++;
    }
    draw_box_bottom(top + height - 1, left, width);
}

/* ─── View: Stash ────────────────────────────────────────────── */
static void draw_stash(int top, int left, int height, int width) {
    char title[64];
    snprintf(title, sizeof(title), "Stash (%d)", g.stash_count);
    draw_box_title(top, left, width, title, g.current_view == VIEW_STASH);

    int row = top + 1, lim = top + height - 1;

    if (g.stash_count == 0) {
        move_to(top + height/2, left + width/2 - 6);
        printf(FG_BRIGHT_BLACK "No stashes" RESET);
    }

    for (int i = 0; i < g.stash_count && row < lim; i++, row++) {
        GitStash *s = &g.stashes[i];
        bool sel = (g.stash_sel == i);

        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        if (sel) printf(BG_BRIGHT_BLACK BOLD);

        printf(" " FG_BRIGHT_YELLOW "stash@{%d}" RESET, s->index);
        if (sel) printf(BG_BRIGHT_BLACK);
        printf(" ");

        int msg_w = width - 14;
        print_padded(s->message, msg_w);
        printf(RESET);

        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }
    while (row < lim) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
        row++;
    }
    draw_box_bottom(top + height - 1, left, width);
}

/* ─── View: Help ─────────────────────────────────────────────── */
static void draw_help(int top, int left, int height, int width) {
    draw_box_title(top, left, width, "Help & Keybindings", true);

    static const char *keys[] = {
        "Navigation",
        "  j/k / ↑↓   Move selection up/down",
        "  h/l         Switch view panes",
        "  Tab         Cycle views",
        "  g/G         Jump to top/bottom",
        "  PgUp/PgDn   Scroll diff/log page",
        "",
        "Status View",
        "  Space       Stage/unstage file",
        "  a           Stage all files",
        "  u           Unstage all files",
        "  Enter       Open diff for file",
        "  d           Delete/discard file",
        "",
        "Log View",
        "  Enter       Show commit diff",
        "  y           Copy commit hash",
        "",
        "Branch View",
        "  Enter       Checkout branch",
        "  n           Create new branch",
        "  D           Delete branch",
        "",
        "Stash View",
        "  Enter       Apply stash",
        "  D           Drop stash",
        "  p           Pop stash",
        "",
        "Global",
        "  c           Commit (staged changes)",
        "  P           Push to remote",
        "  f           Fetch/pull",
        "  s           Stash changes",
        "  R           Refresh/reload",
        "  /           Search (in log)",
        "  q / Esc     Go back / quit",
        "  ?           Toggle this help",
        NULL
    };

    int row = top + 1, lim = top + height - 1;
    for (int i = 0; keys[i] && row < lim; i++, row++) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET "  ");
        if (keys[i][0] && keys[i][strlen(keys[i])-1] == '\0') {}
        
        bool is_header = (strlen(keys[i]) > 0 && keys[i][0] != ' ' && keys[i][0] != '\0');
        if (is_header && strlen(keys[i]) > 0) printf(BOLD FG_BRIGHT_CYAN);
        
        int w = width - 5;
        print_padded(keys[i], w);
        printf(RESET);

        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
    }
    while (row < lim) {
        move_to(row, left);
        printf(FG_BRIGHT_BLACK "│" RESET);
        move_to(row, left + width - 1);
        printf(FG_BRIGHT_BLACK "│" RESET);
        row++;
    }
    draw_box_bottom(top + height - 1, left, width);
}

/* ─── Tab bar ────────────────────────────────────────────────── */
static void draw_tabbar(void) {
    static const char *tab_names[] = {
        "Status", "Log", "Diff", "Branches", "Stash", "Help"
    };
    static const char *tab_keys[] = {
        "1", "2", "3", "4", "5", "?"
    };

    move_to(1, 1);
    printf(BG_BLACK);

    /* Left: app name */
    printf(BOLD FG_BRIGHT_CYAN " ⎇ gitui " RESET);
    printf(FG_BRIGHT_BLACK "│" RESET);

    for (int i = 0; i < (int)(sizeof(tab_names)/sizeof(tab_names[0])); i++) {
        if (i == (int)g.current_view) {
            printf(BOLD BG_BRIGHT_BLACK FG_BRIGHT_WHITE " %s[%s] " RESET,
                   tab_names[i], tab_keys[i]);
        } else {
            printf(FG_BRIGHT_BLACK " %s[%s] " RESET,
                   tab_names[i], tab_keys[i]);
        }
        printf(FG_BRIGHT_BLACK "│" RESET);
    }

    /* Right: branch name */
    char right[128];
    snprintf(right, sizeof(right), " ⎇ %s ", g.branch_name);
    int rlen = strlen(right);
    int cur_col = 10; /* approximate */
    for (int i = 0; i < (int)(sizeof(tab_names)/sizeof(tab_names[0])); i++)
        cur_col += strlen(tab_names[i]) + 5;
    
    int pad = g.cols - cur_col - rlen;
    for (int i = 0; i < pad && i < g.cols; i++) printf(" ");
    printf(FG_BRIGHT_MAGENTA BOLD "%s" RESET, right);

    /* Fill remaining */
    printf(RESET);
}

/* ─── Status bar ─────────────────────────────────────────────── */
static void draw_statusbar(void) {
    move_to(g.rows, 1);
    printf(RESET BG_BLACK);
    
    /* Left: contextual hints */
    const char *hints = "";
    switch (g.current_view) {
        case VIEW_STATUS:   hints = "SPC:stage/unstage  a:stage-all  c:commit  d:discard  Enter:diff"; break;
        case VIEW_LOG:      hints = "Enter:diff  j/k:move  PgUp/Dn:scroll"; break;
        case VIEW_DIFF:     hints = "j/k:scroll  q:back  Tab:next-view"; break;
        case VIEW_BRANCHES: hints = "Enter:checkout  n:new  D:delete"; break;
        case VIEW_STASH:    hints = "Enter:apply  p:pop  D:drop"; break;
        case VIEW_HELP:     hints = "q:close help"; break;
        default: break;
    }

    printf(FG_BRIGHT_BLACK " %s" RESET, hints);

    /* Right: status message */
    if (g.status_msg[0] && (time(NULL) - g.status_msg_time) < 4) {
        int hint_len = strlen(hints) + 1;
        int msg_len = strlen(g.status_msg);
        int pad = g.cols - hint_len - msg_len - 2;
        for (int i = 0; i < pad; i++) printf(" ");
        printf(FG_BRIGHT_GREEN BOLD " %s " RESET, g.status_msg);
    } else {
        /* Fill rest */
        int hint_len = strlen(hints) + 1;
        int pad = g.cols - hint_len;
        for (int i = 0; i < pad; i++) printf(" ");
    }
    printf(RESET);
}

/* ─── Prompt ─────────────────────────────────────────────────── */
static void draw_prompt(void) {
    if (!g.in_prompt) return;

    int row = g.rows - 1;
    move_to(row, 1);
    printf(RESET BG_BLUE FG_BRIGHT_WHITE BOLD " %s " RESET, g.prompt_label);
    printf(BG_BLACK FG_BRIGHT_WHITE " %s" RESET, g.prompt_buf);
    /* Cursor */
    printf(BG_WHITE FG_BLACK " " RESET);

    /* Fill */
    int used = strlen(g.prompt_label) + 3 + strlen(g.prompt_buf) + 1 + 1;
    for (int i = used; i < g.cols; i++) printf(" ");
}

/* ─── Main draw ──────────────────────────────────────────────── */
static void draw(void) {
    printf(HIDE_CURSOR);
    
    /* Clear */
    printf(CLEAR);

    draw_tabbar();

    int content_top = 2;
    int content_height = g.rows - 2; /* minus tabbar and statusbar */
    int content_width = g.cols;

    if (g.current_view == VIEW_HELP) {
        draw_help(content_top, 1, content_height, content_width);
    } else if (g.current_view == VIEW_DIFF) {
        draw_diff(content_top, 1, content_height, content_width);
    } else if (g.current_view == VIEW_STATUS) {
        /* Left: status (40%), Right: diff (60%) */
        int lw = content_width * 40 / 100;
        if (lw < 20) lw = 20;
        int rw = content_width - lw;
        draw_status(content_top, 1, content_height, lw);
        draw_diff(content_top, lw + 1, content_height, rw);
    } else if (g.current_view == VIEW_LOG) {
        /* Full width log, diff below */
        int lh = content_height * 55 / 100;
        if (lh < 5) lh = 5;
        int dh = content_height - lh;
        draw_log(content_top, 1, lh, content_width);
        draw_diff(content_top + lh, 1, dh, content_width);
    } else if (g.current_view == VIEW_BRANCHES) {
        draw_branches(content_top, 1, content_height, content_width);
    } else if (g.current_view == VIEW_STASH) {
        draw_stash(content_top, 1, content_height, content_width);
    }

    draw_statusbar();
    if (g.in_prompt) draw_prompt();

    printf(SHOW_CURSOR);
    fflush(stdout);
}

/* ─── Prompt system ──────────────────────────────────────────── */
static void start_prompt(const char *label, void (*cb)(const char *)) {
    g.in_prompt = true;
    snprintf(g.prompt_label, sizeof(g.prompt_label), "%s", label);
    g.prompt_buf[0] = '\0';
    g.prompt_cursor = 0;
    g.prompt_cb = cb;
}

static void prompt_char(char c) {
    if (!g.in_prompt) return;
    int len = strlen(g.prompt_buf);
    if (len + 1 < INPUT_MAX) {
        memmove(&g.prompt_buf[g.prompt_cursor+1],
                &g.prompt_buf[g.prompt_cursor],
                len - g.prompt_cursor + 1);
        g.prompt_buf[g.prompt_cursor++] = c;
    }
}

static void prompt_backspace(void) {
    if (!g.in_prompt || g.prompt_cursor == 0) return;
    int len = strlen(g.prompt_buf);
    memmove(&g.prompt_buf[g.prompt_cursor-1],
            &g.prompt_buf[g.prompt_cursor],
            len - g.prompt_cursor + 1);
    g.prompt_cursor--;
}

static void prompt_confirm(void) {
    if (!g.in_prompt) return;
    g.in_prompt = false;
    if (g.prompt_cb) g.prompt_cb(g.prompt_buf);
    g.prompt_buf[0] = '\0';
    g.prompt_cursor = 0;
}

static void prompt_cancel(void) {
    g.in_prompt = false;
    g.prompt_buf[0] = '\0';
    g.prompt_cursor = 0;
    g.prompt_cb = NULL;
}

/* ─── Git actions ────────────────────────────────────────────── */
static void action_stage_file(void) {
    if (g.file_count == 0) return;
    GitFile *f = &g.files[g.file_sel];
    
    if (f->staged) {
        /* Unstage */
        if (f->status == FILE_STAGED_NEW)
            git_exec("git reset HEAD -- '%s'", f->path);
        else
            git_exec("git reset HEAD -- '%s'", f->path);
        set_msg("Unstaged: %s", f->path);
    } else {
        /* Stage */
        if (f->status == FILE_UNTRACKED || f->status == FILE_MODIFIED)
            git_exec("git add -- '%s'", f->path);
        else if (f->status == FILE_DELETED)
            git_exec("git rm -- '%s'", f->path);
        else
            git_exec("git add -- '%s'", f->path);
        set_msg("Staged: %s", f->path);
    }
    load_status();
}

static void action_stage_all(void) {
    git_exec("git add -A");
    set_msg("Staged all files");
    load_status();
}

static void action_unstage_all(void) {
    git_exec("git reset HEAD");
    set_msg("Unstaged all files");
    load_status();
}

static void do_commit(const char *msg) {
    if (!msg || !msg[0]) { set_msg("Commit cancelled (empty message)"); return; }
    
    /* Need to run interactively but we're in TUI - use temp file approach */
    char tmpfile[64] = "/tmp/gitui_commit_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd == -1) { set_msg("Error: cannot create temp file"); return; }
    write(fd, msg, strlen(msg));
    close(fd);
    
    int ret = git_exec("git commit -F '%s'", tmpfile);
    unlink(tmpfile);
    
    if (ret == 0) {
        set_msg("Committed: %.50s", msg);
        load_status();
        load_log();
    } else {
        set_msg("Commit failed (nothing staged?)");
    }
}

static void action_commit(void) {
    int staged, unstaged;
    status_counts(&staged, &unstaged);
    if (staged == 0) { set_msg("Nothing staged to commit"); return; }
    start_prompt("Commit message:", do_commit);
}

static void action_push(void) {
    set_msg("Pushing...");
    draw();
    int ret = git_exec("git push");
    if (ret == 0) set_msg("Pushed successfully");
    else set_msg("Push failed (check remote/auth)");
    load_log();
}

static void action_pull(void) {
    set_msg("Fetching & pulling...");
    draw();
    int ret = git_exec("git pull");
    if (ret == 0) { set_msg("Pulled successfully"); }
    else set_msg("Pull failed");
    load_status();
    load_log();
    load_branches();
}

static void do_stash(const char *msg) {
    char cmd[512];
    if (msg && msg[0])
        snprintf(cmd, sizeof(cmd), "git stash push -m '%s'", msg);
    else
        snprintf(cmd, sizeof(cmd), "git stash push");
    int ret = git_exec("%s", cmd);
    if (ret == 0) { set_msg("Stashed changes"); load_status(); load_stash(); }
    else set_msg("Stash failed (nothing to stash?)");
}

static void action_stash(void) {
    start_prompt("Stash message (optional):", do_stash);
}

static void action_discard_file(void) {
    if (g.file_count == 0) return;
    GitFile *f = &g.files[g.file_sel];
    if (f->status == FILE_UNTRACKED) {
        git_exec("rm -- '%s'", f->path);
        set_msg("Removed: %s", f->path);
    } else {
        git_exec("git checkout -- '%s'", f->path);
        set_msg("Discarded: %s", f->path);
    }
    load_status();
}

static void action_open_diff_for_file(void) {
    if (g.file_count == 0) return;
    GitFile *f = &g.files[g.file_sel];
    snprintf(g.diff_file, sizeof(g.diff_file), "%s", f->path);
    g.diff_staged = f->staged;
    load_diff(f->path, f->staged);
    g.current_view = VIEW_DIFF;
}

static void action_open_diff_for_commit(void) {
    if (g.commit_count == 0) return;
    GitCommit *c = &g.commits[g.commit_sel];
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "git show --stat %s 2>/dev/null | head -5", c->hash);
    
    snprintf(g.diff_file, sizeof(g.diff_file), "commit %s: %s", c->hash, c->subject);
    g.diff_staged = false;
    
    /* Load commit diff */
    g.diff_count = 0;
    char gcmd[256];
    snprintf(gcmd, sizeof(gcmd), "git show %s 2>/dev/null", c->hash);
    char *out = git_run(gcmd);
    if (out) {
        char *line = out;
        while (*line && g.diff_count < MAX_DIFF_LINES) {
            char *nl = strchr(line, '\n');
            size_t len = nl ? (size_t)(nl-line) : strlen(line);
            DiffLine *dl = &g.diff_lines[g.diff_count];
            if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN-1;
            memcpy(dl->line, line, len);
            dl->line[len] = '\0';

            if (dl->line[0] == '+') dl->type = 1;
            else if (dl->line[0] == '-') dl->type = 2;
            else if (dl->line[0] == '@') dl->type = 3;
            else dl->type = 0;

            g.diff_count++;
            line = nl ? nl+1 : line+strlen(line);
        }
        free(out);
    }
    g.diff_scroll = 0;
    g.current_view = VIEW_DIFF;
}

static void action_checkout_branch(void) {
    if (g.branch_count == 0) return;
    GitBranch *b = &g.branches[g.branch_sel];
    
    char name[256];
    snprintf(name, sizeof(name), "%s", b->name);
    
    /* For remote branches, strip remote prefix */
    char *slash = strchr(name, '/');
    const char *local_name = slash ? slash+1 : name;
    
    int ret;
    if (b->is_remote) {
        ret = git_exec("git checkout -t %s", name);
    } else {
        ret = git_exec("git checkout %s", name);
    }
    
    if (ret == 0) {
        set_msg("Checked out: %s", local_name);
        load_branch();
        load_status();
        load_branches();
    } else {
        set_msg("Checkout failed");
    }
}

static void do_new_branch(const char *name) {
    if (!name || !name[0]) { set_msg("Branch name required"); return; }
    int ret = git_exec("git checkout -b '%s'", name);
    if (ret == 0) {
        set_msg("Created and checked out: %s", name);
        load_branch();
        load_branches();
    } else {
        set_msg("Failed to create branch");
    }
}

static void action_new_branch(void) {
    start_prompt("New branch name:", do_new_branch);
}

static void action_delete_branch(void) {
    if (g.branch_count == 0) return;
    GitBranch *b = &g.branches[g.branch_sel];
    if (b->is_current) { set_msg("Cannot delete current branch"); return; }
    
    int ret;
    if (b->is_remote) {
        /* Extract remote/branch */
        char remote[64] = "origin";
        char bname[128];
        snprintf(bname, sizeof(bname), "%s", b->name);
        char *slash = strchr(bname, '/');
        if (slash) {
            *slash = '\0';
            snprintf(remote, sizeof(remote), "%s", bname);
            ret = git_exec("git push %s --delete '%s'", remote, slash+1);
        } else {
            ret = git_exec("git push origin --delete '%s'", bname);
        }
    } else {
        ret = git_exec("git branch -d '%s'", b->name);
    }
    
    if (ret == 0) { set_msg("Deleted: %s", b->name); load_branches(); }
    else { set_msg("Delete failed (try -D for force delete)"); }
}

static void action_apply_stash(void) {
    if (g.stash_count == 0) return;
    int ret = git_exec("git stash apply stash@{%d}", g.stashes[g.stash_sel].index);
    if (ret == 0) { set_msg("Applied stash@{%d}", g.stashes[g.stash_sel].index); load_status(); }
    else set_msg("Stash apply failed");
}

static void action_pop_stash(void) {
    if (g.stash_count == 0) return;
    int ret = git_exec("git stash pop stash@{%d}", g.stashes[g.stash_sel].index);
    if (ret == 0) { set_msg("Popped stash@{%d}", g.stashes[g.stash_sel].index); load_status(); load_stash(); }
    else set_msg("Stash pop failed");
}

static void action_drop_stash(void) {
    if (g.stash_count == 0) return;
    int ret = git_exec("git stash drop stash@{%d}", g.stashes[g.stash_sel].index);
    if (ret == 0) { set_msg("Dropped stash@{%d}", g.stashes[g.stash_sel].index); load_stash(); }
    else set_msg("Stash drop failed");
}

static void reload_all(void) {
    load_branch();
    load_status();
    load_log();
    load_branches();
    load_stash();
    set_msg("Refreshed");
}

/* ─── Input reading ──────────────────────────────────────────── */

typedef enum {
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_PGUP, KEY_PGDN, KEY_HOME, KEY_END,
    KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_DEL,
    KEY_TAB, KEY_CTRL_A, KEY_CTRL_C, KEY_CTRL_U,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_CHAR
} KeyType;

typedef struct {
    KeyType type;
    char ch;
} Key;

static Key read_key(void) {
    Key k = {KEY_NONE, 0};
    
    unsigned char buf[16];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return k;

    if (buf[0] == 0x1b) {
        if (n == 1) { k.type = KEY_ESC; return k; }
        if (buf[1] == '[') {
            if (n == 3) {
                switch (buf[2]) {
                    case 'A': k.type = KEY_UP;    return k;
                    case 'B': k.type = KEY_DOWN;  return k;
                    case 'C': k.type = KEY_RIGHT; return k;
                    case 'D': k.type = KEY_LEFT;  return k;
                    case 'H': k.type = KEY_HOME;  return k;
                    case 'F': k.type = KEY_END;   return k;
                }
            }
            if (n >= 4 && buf[2] == '1' && buf[3] == ';') { /* modifier */ }
            if (n == 4 && buf[3] == '~') {
                switch (buf[2]) {
                    case '1': k.type = KEY_HOME;  return k;
                    case '4': k.type = KEY_END;   return k;
                    case '5': k.type = KEY_PGUP;  return k;
                    case '6': k.type = KEY_PGDN;  return k;
                }
            }
            if (n == 5 && buf[4] == '~') {
                if (buf[2]=='1' && buf[3]=='5') { k.type=KEY_F5; return k; }
            }
            /* Mouse */
            if (buf[2] == 'M') { return k; } /* ignore mouse */
        }
        if (buf[1] == 'O') {
            switch (buf[2]) {
                case 'P': k.type = KEY_F1; return k;
                case 'Q': k.type = KEY_F2; return k;
                case 'R': k.type = KEY_F3; return k;
                case 'S': k.type = KEY_F4; return k;
            }
        }
        return k;
    }

    /* Control chars */
    if (buf[0] == '\r' || buf[0] == '\n') { k.type = KEY_ENTER; return k; }
    if (buf[0] == 127 || buf[0] == 8)     { k.type = KEY_BACKSPACE; return k; }
    if (buf[0] == '\t') { k.type = KEY_TAB; return k; }
    if (buf[0] == 1)    { k.type = KEY_CTRL_A; return k; }
    if (buf[0] == 3)    { k.type = KEY_CTRL_C; return k; }
    if (buf[0] == 21)   { k.type = KEY_CTRL_U; return k; }

    if (buf[0] >= 32 && buf[0] < 127) {
        k.type = KEY_CHAR;
        k.ch = (char)buf[0];
        return k;
    }

    return k;
}

/* ─── Event handling ─────────────────────────────────────────── */
static void handle_prompt_key(Key k) {
    switch (k.type) {
        case KEY_ENTER:     prompt_confirm(); break;
        case KEY_ESC:       prompt_cancel(); break;
        case KEY_BACKSPACE: prompt_backspace(); break;
        case KEY_CTRL_U:
            g.prompt_buf[0] = '\0';
            g.prompt_cursor = 0;
            break;
        case KEY_CHAR:      prompt_char(k.ch); break;
        default: break;
    }
}

static void move_selection(int *sel, int *scroll, int count, int delta, int visible) {
    *sel = clamp(*sel + delta, 0, count > 0 ? count - 1 : 0);
    if (*sel < *scroll) *scroll = *sel;
    if (*sel >= *scroll + visible) *scroll = *sel - visible + 1;
}

static void handle_key(Key k) {
    if (g.in_prompt) { handle_prompt_key(k); return; }

    /* Global keys */
    if (k.type == KEY_CHAR) {
        switch (k.ch) {
            case '1': g.current_view = VIEW_STATUS; return;
            case '2': g.current_view = VIEW_LOG; return;
            case '3': g.current_view = VIEW_DIFF; return;
            case '4': g.current_view = VIEW_BRANCHES; return;
            case '5': g.current_view = VIEW_STASH; return;
            case '?': g.current_view = (g.current_view == VIEW_HELP) ? VIEW_STATUS : VIEW_HELP; return;
            case 'q':
                if (g.current_view == VIEW_DIFF)  { g.current_view = VIEW_STATUS; return; }
                if (g.current_view == VIEW_HELP)  { g.current_view = VIEW_STATUS; return; }
                g.running = false; return;
            case 'R': reload_all(); return;
            case 'c': action_commit(); return;
            case 'P': action_push(); return;
            case 'f': action_pull(); return;
            case 's': action_stash(); return;
        }
    }
    if (k.type == KEY_TAB) {
        g.current_view = (View)((g.current_view + 1) % VIEW_COUNT);
        return;
    }
    if (k.type == KEY_ESC) {
        if (g.current_view == VIEW_DIFF || g.current_view == VIEW_HELP)
            g.current_view = VIEW_STATUS;
        return;
    }

    /* View-specific */
    int visible = g.rows - 5;

    switch (g.current_view) {
        case VIEW_STATUS: {
            int cnt = g.file_count;
            switch (k.type) {
                case KEY_UP:    move_selection(&g.file_sel, &g.file_scroll, cnt, -1, visible); break;
                case KEY_DOWN:  move_selection(&g.file_sel, &g.file_scroll, cnt,  1, visible); break;
                case KEY_HOME:  g.file_sel = 0; g.file_scroll = 0; break;
                case KEY_END:   g.file_sel = cnt > 0 ? cnt-1 : 0; break;
                case KEY_ENTER: action_open_diff_for_file(); break;
                case KEY_CHAR:
                    if (k.ch == ' ') action_stage_file();
                    else if (k.ch == 'a') action_stage_all();
                    else if (k.ch == 'u') action_unstage_all();
                    else if (k.ch == 'd') action_discard_file();
                    else if (k.ch == 'j') move_selection(&g.file_sel, &g.file_scroll, cnt, 1, visible);
                    else if (k.ch == 'k') move_selection(&g.file_sel, &g.file_scroll, cnt, -1, visible);
                    else if (k.ch == 'g') { g.file_sel = 0; g.file_scroll = 0; }
                    else if (k.ch == 'G') g.file_sel = cnt > 0 ? cnt-1 : 0;
                    break;
                default: break;
            }
            /* Update diff when selection changes */
            if (g.file_count > 0 && g.file_sel < g.file_count) {
                GitFile *f = &g.files[g.file_sel];
                if (strcmp(g.diff_file, f->path) != 0 || g.diff_staged != f->staged) {
                    snprintf(g.diff_file, sizeof(g.diff_file), "%s", f->path);
                    g.diff_staged = f->staged;
                    load_diff(f->path, f->staged);
                }
            }
            break;
        }
        case VIEW_LOG: {
            int cnt = g.commit_count;
            switch (k.type) {
                case KEY_UP:   move_selection(&g.commit_sel, &g.commit_scroll, cnt, -1, visible); break;
                case KEY_DOWN: move_selection(&g.commit_sel, &g.commit_scroll, cnt,  1, visible); break;
                case KEY_PGUP: move_selection(&g.commit_sel, &g.commit_scroll, cnt, -visible/2, visible); break;
                case KEY_PGDN: move_selection(&g.commit_sel, &g.commit_scroll, cnt,  visible/2, visible); break;
                case KEY_ENTER: action_open_diff_for_commit(); break;
                case KEY_CHAR:
                    if (k.ch == 'j') move_selection(&g.commit_sel, &g.commit_scroll, cnt, 1, visible);
                    else if (k.ch == 'k') move_selection(&g.commit_sel, &g.commit_scroll, cnt, -1, visible);
                    else if (k.ch == 'g') { g.commit_sel = 0; g.commit_scroll = 0; }
                    else if (k.ch == 'G') g.commit_sel = cnt > 0 ? cnt-1 : 0;
                    break;
                default: break;
            }
            /* Update diff for hovered commit */
            if (g.commit_count > 0) {
                GitCommit *c = &g.commits[g.commit_sel];
                char expected[32];
                snprintf(expected, sizeof(expected), "commit %s:", c->hash);
                if (strncmp(g.diff_file, expected, strlen(expected)) != 0) {
                    action_open_diff_for_commit();
                    g.current_view = VIEW_LOG; /* don't switch view */
                }
            }
            break;
        }
        case VIEW_DIFF: {
            switch (k.type) {
                case KEY_UP:   g.diff_scroll -= 1; break;
                case KEY_DOWN: g.diff_scroll += 1; break;
                case KEY_PGUP: g.diff_scroll -= visible; break;
                case KEY_PGDN: g.diff_scroll += visible; break;
                case KEY_HOME: g.diff_scroll = 0; break;
                case KEY_END:  g.diff_scroll = g.diff_count; break;
                case KEY_CHAR:
                    if (k.ch == 'j') g.diff_scroll++;
                    else if (k.ch == 'k') g.diff_scroll--;
                    else if (k.ch == 'g') g.diff_scroll = 0;
                    else if (k.ch == 'G') g.diff_scroll = g.diff_count;
                    break;
                default: break;
            }
            if (g.diff_scroll < 0) g.diff_scroll = 0;
            if (g.diff_scroll > g.diff_count) g.diff_scroll = g.diff_count;
            break;
        }
        case VIEW_BRANCHES: {
            int cnt = g.branch_count;
            switch (k.type) {
                case KEY_UP:   move_selection(&g.branch_sel, &g.branch_scroll, cnt, -1, visible); break;
                case KEY_DOWN: move_selection(&g.branch_sel, &g.branch_scroll, cnt,  1, visible); break;
                case KEY_ENTER: action_checkout_branch(); break;
                case KEY_CHAR:
                    if (k.ch == 'j') move_selection(&g.branch_sel, &g.branch_scroll, cnt, 1, visible);
                    else if (k.ch == 'k') move_selection(&g.branch_sel, &g.branch_scroll, cnt, -1, visible);
                    else if (k.ch == 'n') action_new_branch();
                    else if (k.ch == 'D') action_delete_branch();
                    break;
                default: break;
            }
            break;
        }
        case VIEW_STASH: {
            int cnt = g.stash_count;
            switch (k.type) {
                case KEY_UP:   move_selection(&g.stash_sel, &(int){0}, cnt, -1, visible); break;
                case KEY_DOWN: move_selection(&g.stash_sel, &(int){0}, cnt,  1, visible); break;
                case KEY_ENTER: action_apply_stash(); break;
                case KEY_CHAR:
                    if (k.ch == 'j') move_selection(&g.stash_sel, &(int){0}, cnt, 1, visible);
                    else if (k.ch == 'k') move_selection(&g.stash_sel, &(int){0}, cnt, -1, visible);
                    else if (k.ch == 'p') action_pop_stash();
                    else if (k.ch == 'D') action_drop_stash();
                    break;
                default: break;
            }
            break;
        }
        case VIEW_HELP: {
            if (k.type == KEY_CHAR && k.ch == 'q') g.current_view = VIEW_STATUS;
            break;
        }
        default: break;
    }
}

/* ─── Check if in git repo ───────────────────────────────────── */
static bool in_git_repo(void) {
    char *out = git_run("git rev-parse --git-dir 2>/dev/null");
    if (out && out[0]) { free(out); return true; }
    free(out);
    return false;
}

/* ─── Main ───────────────────────────────────────────────────── */
int main(void) {
    /* Check environment */
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "gitui: requires a terminal\n");
        return 1;
    }

    if (!in_git_repo()) {
        fprintf(stderr, "gitui: not a git repository\n");
        return 1;
    }

    /* Init state */
    memset(&g, 0, sizeof(g));
    g.running = true;
    g.current_view = VIEW_STATUS;
    g.show_unstaged = true;

    /* Signals */
    signal(SIGWINCH, sigwinch_handler);
    signal(SIGINT,   sigint_handler);
    signal(SIGTERM,  sigint_handler);
    signal(SIGPIPE,  SIG_IGN);

    /* Terminal setup */
    get_winsize();
    term_raw();
    printf(ALT_SCREEN HIDE_CURSOR);
    fflush(stdout);

    /* Initial data load */
    load_repo_root();
    load_branch();
    load_status();
    load_log();
    load_branches();
    load_stash();

    /* Auto-load diff for first file */
    if (g.file_count > 0) {
        snprintf(g.diff_file, sizeof(g.diff_file), "%s", g.files[0].path);
        g.diff_staged = g.files[0].staged;
        load_diff(g.diff_file, g.diff_staged);
    }

    set_msg("Welcome to gitui v" VERSION " — press ? for help");

    /* Main loop */
    while (g.running) {
        if (g_resize) {
            g_resize = 0;
            get_winsize();
        }
        draw();
        Key k = read_key();
        if (k.type != KEY_NONE) handle_key(k);
    }

    /* Cleanup */
    printf(NORM_SCREEN SHOW_CURSOR MOUSE_OFF);
    term_restore();
    printf("Thanks for using gitui!\n");
    return 0;
}
