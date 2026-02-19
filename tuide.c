
/*
 * tuide v3.0.0 - Modern Git TUI & IDE, pure C99, zero dependencies
 *
 *  Layout (Status view):
 *  ┌─── Changes ───┬──────────── Side-by-Side Diff ────────────┐
 *  │ staged files  │  old (left)         │  new (right)        │
 *  │ unstaged files│                     │                      │
 *  ├─── Graph ─────┤                     │                      │
 *  │ commit graph  │                     │                      │
 *  └───────────────┴─────────────────────┴──────────────────────┘
 *
 *  Themes: T cycles Dark+ / VS-Light-Blue / Solarized-Dark / One Dark
 *  Mouse:   click to focus panes, scroll wheel, click to select
 *
 *  Build:  cc -std=c99 -O2 -o tuide tuide.c
 *  Run:    ./tuide
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdint.h>

/* ================================================================
   CONSTANTS
================================================================ */
#define VERSION        "3.0.0"
#define MAX_FILES      512
#define MAX_COMMITS    512
#define MAX_BRANCHES   256
#define MAX_STASHES    64
#define MAX_DIFF_LINES 16384
#define MAX_TABS       16
#define LINE_MAX_LEN   512
#define INPUT_MAX      512
#define GRAPH_COLS     8

/* ================================================================
   TERMINAL CODES
================================================================ */
#define ESC "\x1b"
#define CSI ESC "["

#define T_CLEAR      CSI "2J" CSI "H"
#define T_HIDE       CSI "?25l"
#define T_SHOW       CSI "?25h"
#define T_ALT        ESC "[?1049h"
#define T_NORM       ESC "[?1049l"
#define T_MOUSE_ON   ESC "[?1000h" ESC "[?1002h" ESC "[?1006h"
#define T_MOUSE_OFF  ESC "[?1000l" ESC "[?1002l" ESC "[?1006l"
#define T_RESET      CSI "0m"
#define T_BOLD       CSI "1m"
#define T_DIM        CSI "2m"
#define T_ITALIC     CSI "3m"
#define T_UNDER      CSI "4m"
#define T_REVERSE    CSI "7m"

/* ================================================================
   THEME SYSTEM
================================================================ */
typedef struct { int r, g, b; } Color;

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

/* Rendering Cell */
typedef struct {
    char ch[8];
    Color fg, bg;
    bool bold, dim, italic, under, rev;
} Cell;

typedef struct {
    Cell *cells;
    int w, h;
} Buffer;

/* Dark+ (VSCode Dark+) */
static const Theme TH_DARK = {
    "Dark+ (VSCode)",
    {30,30,30},{37,37,38},{58,58,58},{30,30,30},{45,45,45},{50,50,50},
    {19,41,19},{51,18,18},{25,40,60},
    {212,212,212},{90,90,90},{255,255,255},{255,255,255},
    {220,220,170},{156,220,254},{103,150,100},
    {78,201,176},{244,71,71},{128,128,128},{252,100,100},
    {130,255,130},{255,100,100},{86,156,214},{180,180,180},
    {{86,156,214},{220,220,170},{78,201,176},{215,186,125},{244,71,71},{197,134,192}},
    {78,201,176},{197,134,192},{220,220,170},
    {78,201,176},{244,71,71},{90,90,90}
};

/* Visual Studio Community Light Blue C++ */
static const Theme TH_VSLIGHT = {
    "VS Community Light",
    {242,242,242},{230,234,242},{0,120,215},{242,242,242},{218,223,230},{205,214,229},
    {210,240,210},{250,210,210},{210,225,250},
    {30,30,30},{140,140,150},{0,0,0},{255,255,255},
    {116,83,31},{0,80,170},{80,100,80},
    {0,128,0},{180,0,0},{100,100,120},{200,0,0},
    {0,100,0},{150,0,0},{0,50,180},{60,60,60},
    {{0,80,170},{0,120,0},{170,0,0},{140,90,0},{120,0,120},{0,110,120}},
    {0,100,0},{100,0,150},{140,90,0},
    {0,100,0},{180,0,0},{160,160,170}
};

/* Solarized Dark */
static const Theme TH_SOL = {
    "Solarized Dark",
    {0,43,54},{7,54,66},{0,73,89},{0,43,54},{7,54,66},{14,65,78},
    {0,55,30},{60,20,10},{0,55,80},
    {131,148,150},{60,80,85},{253,246,227},{253,246,227},
    {181,137,0},{38,139,210},{88,110,117},
    {133,153,0},{220,50,47},{88,110,117},{203,75,22},
    {133,153,0},{220,50,47},{38,139,210},{131,148,150},
    {{38,139,210},{133,153,0},{181,137,0},{203,75,22},{211,54,130},{42,161,152}},
    {133,153,0},{211,54,130},{181,137,0},
    {133,153,0},{220,50,47},{60,80,85}
};

/* One Dark */
static const Theme TH_ONEDARK = {
    "One Dark",
    {40,44,52},{33,37,43},{62,68,81},{40,44,52},{44,50,60},{50,56,66},
    {45,60,45},{60,45,45},{45,55,75},
    {171,178,191},{92,99,112},{255,255,255},{255,255,255},
    {224,108,117},{97,175,239},{152,195,121},
    {152,195,121},{224,108,117},{92,99,112},{209,154,102},
    {152,195,121},{224,108,117},{97,175,239},{171,178,191},
    {{97,175,239},{198,120,221},{152,195,121},{209,154,102},{224,108,117},{86,182,194}},
    {152,195,121},{198,120,221},{209,154,102},
    {152,195,121},{224,108,117},{92,99,112}
};

static const Theme *THEMES[] = {&TH_DARK, &TH_VSLIGHT, &TH_SOL, &TH_ONEDARK};
#define NTHEMES 4

/* ================================================================
   ENUMS & STRUCTS
================================================================ */
typedef enum { VIEW_STATUS,VIEW_LOG,VIEW_BRANCHES,VIEW_STASH,VIEW_EDITOR,VIEW_HELP,VIEW_COUNT } View;
typedef enum { FOCUS_CHANGES,FOCUS_GRAPH,FOCUS_DIFF,FOCUS_BROWSER,FOCUS_EDITOR,FOCUS_CLI } FocusPane;
typedef enum {
    FS_UNTRACKED,FS_MODIFIED,FS_STAGED_MODIFY,FS_STAGED_NEW,
    FS_STAGED_DEL,FS_DELETED,FS_RENAMED,FS_CONFLICT,FS_COPIED
} FileStatus;

/* Undo/redo history entry: snapshot of entire file as flat string + cursor */
#define MAX_UNDO 80
typedef struct { char *text; int cy, cx; } HistEntry;

typedef struct {
    char **lines;
    int line_count, line_cap;
    int cur_y, cur_x;
    int scroll_y, scroll_x;
    char filename[512];
    bool modified;
    uint64_t saved_hash; /* FNV-64 hash of content at last save/load */
    /* Undo/redo stacks */
    HistEntry undo_stack[MAX_UNDO];
    int       undo_top;      /* 0 = empty */
    HistEntry redo_stack[MAX_UNDO];
    int       redo_top;
} Editor;

typedef struct {
    Editor ed;
    char path[512];
} Tab;

typedef struct { char path[512]; bool is_dir; } BrowserFile;

typedef struct { char path[512]; char orig[512]; FileStatus st; bool staged; } GitFile;
typedef struct {
    char hash[16],author[64],email[64],date[32],subject[256],refs[192];
    char graph[24];
    int  graph_col;
    bool expanded;
    char files[16][128];
    int  f_count;
} GitCommit;
typedef struct { char name[128],upstream[128]; bool is_remote,is_current; int ahead,behind; } GitBranch;
typedef struct { char message[256],hash[16]; int index; } GitStash;
typedef struct {
    char old_line[LINE_MAX_LEN], new_line[LINE_MAX_LEN];
    int old_lno, new_lno;
    int type; /* 0=ctx 1=add 2=del 3=hunk 4=fhdr 5=file */
} DiffLine;

typedef struct { int btn,col,row; bool release,shift,ctrl; } MouseEvt;
typedef enum {
    KEY_NONE=0, KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,
    KEY_PGUP,KEY_PGDN,KEY_HOME,KEY_END,
    KEY_ENTER,KEY_ESC,KEY_BACKSPACE,KEY_DEL,
    KEY_TAB,KEY_SHIFT_TAB,
    KEY_CTRL_A,KEY_CTRL_B,KEY_CTRL_C,KEY_CTRL_D,KEY_CTRL_E,
    KEY_CTRL_F,KEY_CTRL_G,KEY_CTRL_K,KEY_CTRL_N,KEY_CTRL_P,
    KEY_CTRL_U,KEY_CTRL_W,KEY_CTRL_Y,
    KEY_CTRL_R,KEY_CTRL_S,KEY_CTRL_L,KEY_CTRL_Q,KEY_CTRL_V,KEY_CTRL_X,
    KEY_CTRL_Z,
    KEY_SHIFT_UP,KEY_SHIFT_DOWN,KEY_SHIFT_LEFT,KEY_SHIFT_RIGHT,
    KEY_MOUSE,KEY_CHAR,
    KEY_F1,KEY_F2,KEY_F3,KEY_F4,KEY_F5
} KeyType;
typedef struct { KeyType type; char ch; MouseEvt mouse; } Key;

/* ================================================================
   GLOBAL STATE
================================================================ */
typedef struct {
    struct termios orig_termios;
    int rows, cols;
    bool running;
    int  theme_idx;

    /* Rendering */
    Buffer front, back;
    Color cur_fg, cur_bg;
    bool cur_bold, cur_dim, cur_italic, cur_under, cur_rev;
    int cur_r, cur_c;

    /* View / focus */
    View      current_view;
    FocusPane focus;

    /* Changes */
    GitFile files[MAX_FILES];
    int file_count, file_sel, file_scroll;

    /* Log/graph */
    GitCommit commits[MAX_COMMITS];
    int commit_count, commit_sel, commit_scroll;
    int graph_file_sel;
    struct { int commit_idx, file_idx; } graph_rows[MAX_COMMITS*17];
    int graph_rows_count;

    /* Diff */
    DiffLine diff_lines[MAX_DIFF_LINES];
    int diff_count, diff_scroll, diff_hscroll;
    char diff_title[512], diff_commit[64];
    bool diff_staged, diff_sidebyside, diff_is_summary, diff_continuous;
    int  diff_sel;
    int  diff_split, diff_split_custom;

    /* Branches */
    GitBranch branches[MAX_BRANCHES];
    int branch_count, branch_sel, branch_scroll;

    /* Stash */
    GitStash stashes[MAX_STASHES];
    int stash_count, stash_sel;

    /* Prompt */
    bool in_prompt, prompt_obscure;
    char prompt_label[128], prompt_buf[INPUT_MAX];
    int  prompt_cursor;
    void (*prompt_cb)(const char *);

    /* CLI */
    char cli_buf[INPUT_MAX];
    int  cli_cursor;

    /* Status */
    char   status_msg[256];
    time_t status_msg_time;
    bool   status_is_err;

    /* Repo */
    char branch_name[128];

    /* Editor & Browser */
    Tab tabs[MAX_TABS];
    int tab_count, tab_current;

    BrowserFile browser_files[1024];
    int browser_count, browser_sel, browser_scroll;
    char browser_path[512];
    bool editor_active;   /* Right pane toggle: false=Diff, true=Editor */
    bool browser_active;  /* Left pane toggle:  false=Git,  true=Browser */
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
    int lw, lh_chg, lh_gph, rx, rw;
    int lh_log, dh_log;
    int lw_custom, lh_chg_custom, lh_log_custom;
    bool dragging_v, dragging_h, dragging_sc, dragging_diff, dragging_h_log;
    bool dragging_col_hash, dragging_col_author, dragging_col_date;
    int  col_hash_w, col_author_w, col_date_w;
    int  sc_y, sc_h, sc_total, sc_vis, sc_drag_offset;
    bool dragging_ed_sc;
    int  ed_sc_x, ed_sc_y, ed_sc_h, ed_sc_total, ed_sc_vis, ed_sc_drag_offset;
    int  tab_x[7];
    int  ed_tab_x[MAX_TABS+1]; /* Editor tab bar click positions */

    /* Context Menu */
    bool menu_active;
    int  menu_x, menu_y, menu_w, menu_h;
    char menu_items[12][32];
    void (*menu_actions[12])(void);
    int  menu_item_count;
    int  last_mx, last_my;
} State;

static State G;
static volatile int g_resize = 0;

#define TH (THEMES[G.theme_idx])
#define CUR_ED (G.tabs[G.tab_current].ed)

/* ================================================================
   UTIL
================================================================ */
static void set_status(bool err, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    vsnprintf(G.status_msg,sizeof(G.status_msg),fmt,ap); va_end(ap);
    G.status_msg_time=time(NULL); G.status_is_err=err;
}
#define OK(...)  set_status(false,__VA_ARGS__)
#define ERR(...) set_status(true, __VA_ARGS__)

static int iclamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static int imin(int a,int b){return a<b?a:b;}
static int imax(int a,int b){return a>b?a:b;}
static void strtrim(char *s){int n=(int)strlen(s);while(n>0&&(s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '))s[--n]='\0';}

static char *git_run(const char *cmd){
    FILE *fp=popen(cmd,"r"); if(!fp)return NULL;
    size_t cap=8192,len=0; char *buf=malloc(cap); if(!buf){pclose(fp);return NULL;}
    int c; while((c=fgetc(fp))!=EOF){
        if(len+2>=cap){cap*=2;char *nb=realloc(buf,cap);if(!nb){free(buf);pclose(fp);return NULL;}buf=nb;}
        buf[len++]=(char)c;
    } buf[len]='\0'; pclose(fp); return buf;
}
static int git_exec(const char *fmt,...){
    char cmd[2048]; va_list ap; va_start(ap,fmt); vsnprintf(cmd,sizeof(cmd),fmt,ap); va_end(ap);
    char full[2200]; snprintf(full,sizeof(full),"%s >/dev/null 2>&1",cmd);
    return system(full);
}

/* ================================================================
   TERMINAL
================================================================ */
static void term_raw(void){
    tcgetattr(STDIN_FILENO,&G.orig_termios);
    struct termios r=G.orig_termios;
    r.c_lflag&=~(ECHO|ICANON|ISIG|IEXTEN);
    r.c_iflag&=~(IXON|ICRNL|BRKINT|INPCK|ISTRIP);
    r.c_oflag&=~OPOST; r.c_cflag|=CS8;
    r.c_cc[VMIN]=0; r.c_cc[VTIME]=1;
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&r);
}
static void term_restore(void){tcsetattr(STDIN_FILENO,TCSAFLUSH,&G.orig_termios);}
static void buf_resize(Buffer *b, int w, int h){
    if(b->w == w && b->h == h) return;
    free(b->cells);
    b->cells = calloc(w * h, sizeof(Cell));
    b->w = w; b->h = h;
}
static void buf_clear(Buffer *b){
    if(!b->cells) return;
    for(int i=0; i < b->w * b->h; i++){
        Cell *c = &b->cells[i];
        memset(c->ch, 0, 8);
        c->ch[0] = ' ';
        c->fg = TH->fg_normal;
        c->bg = TH->bg_base;
        c->bold = c->dim = c->italic = c->under = c->rev = false;
    }
}

static void get_winsize(void){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==-1||!ws.ws_col){G.cols=80;G.rows=24;}
    else{G.cols=ws.ws_col;G.rows=ws.ws_row;}
    buf_resize(&G.front, G.cols, G.rows);
    buf_resize(&G.back, G.cols, G.rows);
    if(G.front.cells) memset(G.front.cells, 0, G.front.w * G.front.h * sizeof(Cell));
}

static void put_cell(int r, int c, const char *s){
    if(r<1 || r>G.rows || c<1 || c>G.cols || !G.back.cells) return;
    Cell *cell = &G.back.cells[(r-1)*G.cols + (c-1)];
    memset(cell->ch, 0, 8);
    if(s && *s){
        if((unsigned char)*s < 32 && *s != '\x1b') cell->ch[0] = ' ';
        else {
            int i=0; while(s[i] && i<7) { cell->ch[i] = s[i]; i++; }
        }
    } else {
        cell->ch[0] = ' ';
    }
    cell->fg = G.cur_fg; cell->bg = G.cur_bg;
    cell->bold = G.cur_bold; cell->dim = G.cur_dim; cell->italic = G.cur_italic;
    cell->under = G.cur_under; cell->rev = G.cur_rev;
}

static void at(int r,int c){ 
    G.cur_r = iclamp(r, 1, G.rows); 
    G.cur_c = iclamp(c, 1, G.cols); 
}
static void cfg(Color c){ G.cur_fg = c; }
static void cbg(Color c){ G.cur_bg = c; }
static void rst(void){ 
    G.cur_fg = TH->fg_normal; G.cur_bg = TH->bg_base; 
    G.cur_bold=G.cur_dim=G.cur_italic=G.cur_under=G.cur_rev=false; 
}

static void draw_flush(void){
    if(!G.back.cells || !G.front.cells) return;
    Color last_fg = {-1,-1,-1}, last_bg = {-1,-1,-1};
    bool last_bold=false, last_dim=false, last_italic=false, last_under=false, last_rev=false;
    int tr=-1, tc=-1;

    for(int r=1; r<=G.rows; r++){
        for(int c=1; c<=G.cols; c++){
            Cell *b = &G.back.cells[(r-1)*G.cols + (c-1)];
            Cell *f = &G.front.cells[(r-1)*G.cols + (c-1)];
            
            if(memcmp(b, f, sizeof(Cell)) == 0) continue;
            
            if(tr != r || tc != c){
                printf(CSI "%d;%dH", r, c);
            }
            
            bool attr_change = (b->bold != last_bold || b->dim != last_dim || b->italic != last_italic || b->under != last_under || b->rev != last_rev);
            bool color_change = (memcmp(&b->fg, &last_fg, sizeof(Color)) != 0 || memcmp(&b->bg, &last_bg, sizeof(Color)) != 0);

            if(attr_change || color_change){
                if((last_bold && !b->bold) || (last_dim && !b->dim) || (last_italic && !b->italic) || (last_under && !b->under) || (last_rev && !b->rev)){
                    printf(CSI "0m");
                    last_fg.r = last_fg.g = last_fg.b = -1;
                    last_bg.r = last_bg.g = last_bg.b = -1;
                    last_bold = last_dim = last_italic = last_under = last_rev = false;
                }
                
                if(memcmp(&b->fg, &last_fg, sizeof(Color)) != 0){
                    printf(CSI "38;2;%d;%d;%dm", b->fg.r, b->fg.g, b->fg.b);
                    last_fg = b->fg;
                }
                if(memcmp(&b->bg, &last_bg, sizeof(Color)) != 0){
                    printf(CSI "48;2;%d;%d;%dm", b->bg.r, b->bg.g, b->bg.b);
                    last_bg = b->bg;
                }
                if(b->bold && !last_bold) { printf(T_BOLD); last_bold = true; }
                if(b->dim && !last_dim) { printf(T_DIM); last_dim = true; }
                if(b->italic && !last_italic) { printf(T_ITALIC); last_italic = true; }
                if(b->under && !last_under) { printf(T_UNDER); last_under = true; }
                if(b->rev && !last_rev) { printf(T_REVERSE); last_rev = true; }
            }
            
            if(b->ch[0]) fputs(b->ch, stdout); else putchar(' ');
            *f = *b;
            tr = r; tc = c+1;
        }
    }
    fflush(stdout);
}

static void ppad(const char *s,int w){
    if(w<=0)return;
    int vis=0;
    int actual_len = 0;
    const char *p = s;
    while(*p){
        if(*p=='\x1b'&&p[1]=='['){
            p+=2; while(*p && *p != 'm') p++;
            if(*p == 'm') p++;
            continue;
        }
        int len = 1;
        if(((unsigned char)*p & 0xe0) == 0xc0) len = 2;
        else if(((unsigned char)*p & 0xf0) == 0xe0) len = 3;
        else if(((unsigned char)*p & 0xf8) == 0xf0) len = 4;
        p += len; actual_len++;
    }

    bool truncated = (actual_len > w);
    int limit = truncated ? (w > 1 ? w - 1 : w) : w;

    while(*s&&vis<limit){
        if(*s=='\x1b'&&s[1]=='['){
            s+=2;
            while(*s && *s != 'm'){
                int val = atoi(s);
                if(val == 0) rst();
                else if(val == 1) G.cur_bold = true;
                else if(val == 2) G.cur_dim = true;
                else if(val == 3) G.cur_italic = true;
                else if(val == 4) G.cur_under = true;
                else if(val == 7) G.cur_rev = true;
                else if(val == 22) {G.cur_bold = false; G.cur_dim = false;}
                while(*s && isdigit(*s)) s++;
                if(*s == ';') s++;
            }
            if(*s == 'm') s++;
            continue;
        }
        int len = 1;
        if(((unsigned char)*s & 0xe0) == 0xc0) len = 2;
        else if(((unsigned char)*s & 0xf0) == 0xe0) len = 3;
        else if(((unsigned char)*s & 0xf8) == 0xf0) len = 4;
        
        char tmp[8] = {0};
        for(int i=0; i<len && *s; i++) tmp[i] = *s++;
        put_cell(G.cur_r, G.cur_c, tmp);
        G.cur_c++; vis++;
    }
    if(truncated && w >= 1){
        put_cell(G.cur_r, G.cur_c, "…");
        G.cur_c++; vis++;
    }
    while(vis<w){put_cell(G.cur_r, G.cur_c, " "); G.cur_c++; vis++;}
}

static void layout(void){
    G.sidebar_w = 10;
    if(G.lw_custom > 0) G.lw = iclamp(G.lw_custom, 20, G.cols-20);
    else G.lw=imax(26,imin(48,G.cols*32/100));
    
    G.rx=G.sidebar_w+G.lw+1; G.rw=G.cols-G.rx+1;
    int ch=G.rows-2;
    
    if(G.lh_chg_custom > 0) G.lh_chg = iclamp(G.lh_chg_custom, 4, ch-4);
    else G.lh_chg=imax(5,ch*58/100);
    
    G.lh_gph=ch-G.lh_chg+1;
    if(G.lh_gph<4){G.lh_gph=4; G.lh_chg=ch-G.lh_gph+1;}

    int drw = (G.current_view == VIEW_LOG) ? G.cols : G.rw;
    if(G.diff_split_custom > 0) G.diff_split = iclamp(G.diff_split_custom, 10, drw-10);
    else G.diff_split = drw / 2;

    if(G.lh_log_custom > 0) G.lh_log = iclamp(G.lh_log_custom, 4, ch-4);
    else G.lh_log = ch * 55 / 100;
    G.dh_log = ch - G.lh_log + 1;
    if(G.dh_log < 4){ G.dh_log = 4; G.lh_log = ch - G.dh_log + 1; }
}

/* ================================================================
   SIGNAL HANDLERS
================================================================ */
static void sig_winch(int s){(void)s;g_resize=1;}
static void sig_int(int s){(void)s;G.running=false;}

/* ================================================================
   DATA LOADING
================================================================ */
static void fetch_commit_files(int idx); /* forward declaration */
static void load_branch(void){
    char *o=git_run("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    if(o){strtrim(o);snprintf(G.branch_name,sizeof(G.branch_name),"%s",o);free(o);}
    else snprintf(G.branch_name,sizeof(G.branch_name),"(unknown)");
}
static FileStatus parse_xy(char x,char y){
    if(x=='?'&&y=='?')return FS_UNTRACKED;
    if(x=='A')return FS_STAGED_NEW;
    if(x=='D')return FS_STAGED_DEL;
    if(x=='R')return FS_RENAMED;
    if(x=='C')return FS_COPIED;
    if(x=='M')return FS_STAGED_MODIFY;
    if(y=='M')return FS_MODIFIED;
    if(y=='D')return FS_DELETED;
    if(x=='U'||y=='U')return FS_CONFLICT;
    return FS_MODIFIED;
}
static void load_status(void){
    G.file_count=0;
    char *o=git_run("git status --porcelain=v1 -u 2>/dev/null"); if(!o)return;
    char *line=o;
    while(*line&&G.file_count<MAX_FILES){
        if(strlen(line)<4){char *nl=strchr(line,'\n');line=nl?nl+1:line+strlen(line);continue;}
        char x=line[0],y=line[1];
        char *ps=line+3; char *nl=strchr(ps,'\n');
        size_t plen=nl?(size_t)(nl-ps):strlen(ps);
        char pb[512]; if(plen>=sizeof(pb))plen=sizeof(pb)-1;
        memcpy(pb,ps,plen); pb[plen]='\0';
        GitFile *f=&G.files[G.file_count++]; memset(f,0,sizeof(*f));
        char *arrow=strstr(pb," -> ");
        if(arrow){*arrow='\0';snprintf(f->orig,sizeof(f->orig),"%s",pb);snprintf(f->path,sizeof(f->path),"%s",arrow+4);}
        else snprintf(f->path,sizeof(f->path),"%s",pb);
        f->st=parse_xy(x,y);
        f->staged=(x!=' '&&x!='?'&&x!='!');
        line=nl?nl+1:line+strlen(line);
    }
    free(o);
    G.file_sel=iclamp(G.file_sel,0,G.file_count>0?G.file_count-1:0);
}
static void load_log(void){
    G.commit_count=0;
    char *graph_o=git_run("git log --oneline --graph --decorate=short -n 300 2>/dev/null");
    char *data_o =git_run("git log --format='%h\x01%an\x01%ae\x01%ar\x01%D\x01%s' -n 300 2>/dev/null");
    if(data_o){
        char *line=data_o;
        while(*line&&G.commit_count<MAX_COMMITS){
            char *nl=strchr(line,'\n');
            size_t len=nl?(size_t)(nl-line):strlen(line);
            if(!len){line=nl?nl+1:line+strlen(line);continue;}
            char buf[1024]; if(len>=sizeof(buf))len=sizeof(buf)-1;
            memcpy(buf,line,len); buf[len]='\0';
            GitCommit *c=&G.commits[G.commit_count]; memset(c,0,sizeof(*c));
            char *tok=strtok(buf,"\x01");
            if(tok)snprintf(c->hash,sizeof(c->hash),"%s",tok);
            tok=strtok(NULL,"\x01"); if(tok)snprintf(c->author,sizeof(c->author),"%s",tok);
            tok=strtok(NULL,"\x01"); if(tok)snprintf(c->email,sizeof(c->email),"%s",tok);
            tok=strtok(NULL,"\x01"); if(tok)snprintf(c->date,sizeof(c->date),"%s",tok);
            tok=strtok(NULL,"\x01"); if(tok)snprintf(c->refs,sizeof(c->refs),"%s",tok);
            tok=strtok(NULL,""); if(tok){strtrim(tok);snprintf(c->subject,sizeof(c->subject),"%s",tok);}
            G.commit_count++;
            line=nl?nl+1:line+strlen(line);
        }
        free(data_o);
    }
    if(graph_o){
        int ci=0; char *line=graph_o;
        while(*line&&ci<G.commit_count){
            char *nl=strchr(line,'\n');
            size_t len=nl?(size_t)(nl-line):strlen(line);
            char buf[256]; if(len>=sizeof(buf))len=sizeof(buf)-1;
            memcpy(buf,line,len); buf[len]='\0';
            char *star=strchr(buf,'*');
            if(star){
                int gc=(int)(star-buf);
                int gn=imin(GRAPH_COLS,gc+3);
                snprintf(G.commits[ci].graph,sizeof(G.commits[ci].graph),"%.*s",gn,buf);
                G.commits[ci].graph_col=gc;
                ci++;
            }
            line=nl?nl+1:line+strlen(line);
        }
        free(graph_o);
    }
    G.commit_sel=iclamp(G.commit_sel,0,G.commit_count>0?G.commit_count-1:0);
}
static void load_branches(void){
    G.branch_count=0;
    char *o=git_run("git branch -vv --format='%(HEAD)|%(refname:short)|%(upstream:short)|%(upstream:track)' 2>/dev/null");
    if(o){
        char *line=o;
        while(*line&&G.branch_count<MAX_BRANCHES){
            char *nl=strchr(line,'\n');
            size_t len=nl?(size_t)(nl-line):strlen(line);
            if(!len){line=nl?nl+1:line+strlen(line);continue;}
            char buf[512]; if(len>=sizeof(buf))len=sizeof(buf)-1;
            memcpy(buf,line,len); buf[len]='\0';
            GitBranch *b=&G.branches[G.branch_count++]; memset(b,0,sizeof(*b));
            char *tok=strtok(buf,"|"); if(tok)b->is_current=tok[0]=='*';
            tok=strtok(NULL,"|"); if(tok)snprintf(b->name,sizeof(b->name),"%s",tok);
            tok=strtok(NULL,"|"); if(tok)snprintf(b->upstream,sizeof(b->upstream),"%s",tok);
            tok=strtok(NULL,""); if(tok){
                char *ah=strstr(tok,"ahead ");  if(ah)b->ahead=atoi(ah+6);
                char *bh=strstr(tok,"behind "); if(bh)b->behind=atoi(bh+7);
            }
            line=nl?nl+1:line+strlen(line);
        }
        free(o);
    }
    char *o2=git_run("git branch -r --format='%(refname:short)' 2>/dev/null");
    if(o2){
        char *line=o2;
        while(*line&&G.branch_count<MAX_BRANCHES){
            char *nl=strchr(line,'\n');
            size_t len=nl?(size_t)(nl-line):strlen(line);
            if(!len){line=nl?nl+1:line+strlen(line);continue;}
            char buf[256]; if(len>=sizeof(buf))len=sizeof(buf)-1;
            memcpy(buf,line,len); buf[len]='\0'; strtrim(buf);
            if(strstr(buf,"HEAD")){line=nl?nl+1:line+strlen(line);continue;}
            bool found=false;
            for(int i=0;i<G.branch_count;i++) if(strcmp(G.branches[i].name,buf)==0){found=true;break;}
            if(!found){GitBranch *b=&G.branches[G.branch_count++];memset(b,0,sizeof(*b));snprintf(b->name,sizeof(b->name),"%s",buf);b->is_remote=true;}
            line=nl?nl+1:line+strlen(line);
        }
        free(o2);
    }
    G.branch_sel=iclamp(G.branch_sel,0,G.branch_count>0?G.branch_count-1:0);
}
static void load_stash(void){
    G.stash_count=0;
    char *o=git_run("git stash list --format='%gd|%h|%s' 2>/dev/null"); if(!o)return;
    char *line=o;
    while(*line&&G.stash_count<MAX_STASHES){
        char *nl=strchr(line,'\n');
        size_t len=nl?(size_t)(nl-line):strlen(line);
        if(!len){line=nl?nl+1:line+strlen(line);continue;}
        char buf[512]; if(len>=sizeof(buf))len=sizeof(buf)-1;
        memcpy(buf,line,len); buf[len]='\0';
        GitStash *s=&G.stashes[G.stash_count++]; memset(s,0,sizeof(*s));
        char *tok=strtok(buf,"|"); if(tok){char *lb=strchr(tok,'{');s->index=lb?atoi(lb+1):0;}
        tok=strtok(NULL,"|"); if(tok)snprintf(s->hash,sizeof(s->hash),"%s",tok);
        tok=strtok(NULL,""); if(tok)snprintf(s->message,sizeof(s->message),"%s",tok);
        line=nl?nl+1:line+strlen(line);
    }
    free(o);
    G.stash_sel=iclamp(G.stash_sel,0,G.stash_count>0?G.stash_count-1:0);
}

/* Diff parsing */
static void parse_diff(const char *raw){
    G.diff_count=0; if(!raw||!raw[0])return;
    const char *line=raw;
    int old_lno=0,new_lno=0;
    while(*line&&G.diff_count<MAX_DIFF_LINES){
        const char *nl=strchr(line,'\n');
        size_t len=nl?(size_t)(nl-line):strlen(line);
        char lb[LINE_MAX_LEN]; if(len>=LINE_MAX_LEN)len=LINE_MAX_LEN-1;
        memcpy(lb,line,len); lb[len]='\0';
        
        /* Identify line type by prefix - efficient lb[0] dispatch */
        int type = -1;
        if(lb[0] == '+') {
            if(lb[1] == '+' && lb[2] == '+') type = 4; /* File header (+++) */
            else type = 1; /* Added */
        } else if(lb[0] == '-') {
            if(lb[1] == '-' && lb[2] == '-') type = 4; /* File header (---) */
            else type = 2; /* Deleted */
        } else if(lb[0] == '@' && lb[1] == '@') {
            type = 3; /* Hunk header */
        } else if(lb[0] == 'd' && strncmp(lb, "diff --git ", 11) == 0) {
            type = 4; /* File header (diff --git) */
        } else if(lb[0] == ' ' || lb[0] == '\0') {
            type = 0; /* Context */
        } else {
            type = 6; /* Metadata (index, mode, etc.) */
        }

        /* Filtering: move logic from draw_diff to parse_diff for correct scrolling */
        bool skip = false;
        if(type == 4) {
            /* Always skip ---/+++ lines in individual file views as they are redundant */
            if(lb[0] == '+' || lb[0] == '-') skip = true;
        }
        /* No longer skipping anything else to keep the view continuous and matching 1:1 */

        if(skip) { line=nl?nl+1:line+len; continue; }

        /* Handle hunk header line numbers */
        if(type == 3){
            int om=0,nm=0;
            sscanf(lb,"@@ -%d",&om);
            sscanf(lb,"@@ -%*d,%*d +%d",&nm);
            if(!om)sscanf(lb,"@@ -%d,",&om);
            if(!nm)sscanf(lb,"@@ -%*d +%d",&nm);
            old_lno=om>0?om:1; new_lno=nm>0?nm:1;
        }

        DiffLine *dl=&G.diff_lines[G.diff_count]; memset(dl,0,sizeof(*dl));
        dl->type = type;
        if(type == 3){
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb);
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb);
        } else if(type == 1){
            dl->new_lno=new_lno++; snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb+1);
        } else if(type == 2){
            dl->old_lno=old_lno++; snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb+1);
        } else if(type == 6){
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb);
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb);
        } else {
            dl->old_lno=old_lno++; dl->new_lno=new_lno++;
            const char *src=(lb[0]==' ')?lb+1:lb;
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",src);
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",src);
        }
        G.diff_count++; line=nl?nl+1:line+len;
    }
    G.diff_scroll=0; G.diff_hscroll=0;
}
static void load_commit_summary(const char *hash){
    char h[64]; snprintf(h, sizeof(h), "%s", hash);
    char cmd[256]; snprintf(cmd,sizeof(cmd),"git show --name-only --format='%%s%%n%%b' %s 2>/dev/null", h);
    char *o=git_run(cmd); if(!o)return;
    G.diff_count=0; G.diff_scroll=0; G.diff_sel=0; G.diff_is_summary=true;
    snprintf(G.diff_commit, sizeof(G.diff_commit), "%s", h);
    
    char *line=o;
    bool in_files=false;
    while(*line && G.diff_count < MAX_DIFF_LINES){
        char *nl=strchr(line,'\n');
        size_t len=nl?(size_t)(nl-line):strlen(line);
        if(len==0){ in_files=true; line=nl?nl+1:line+len; continue; }
        
        DiffLine *dl=&G.diff_lines[G.diff_count++]; memset(dl,0,sizeof(*dl));
        if(len>=LINE_MAX_LEN)len=LINE_MAX_LEN-1;
        memcpy(dl->new_line, line, len); dl->new_line[len]='\0';
        dl->type = in_files ? 5 : 4;
        line=nl?nl+1:line+len;
    }
    free(o);
}
static void load_diff_file(const char *path,bool staged){
    G.diff_is_summary=false;
    G.diff_commit[0]='\0';
    char cmd[1024];
    const char *ctx = G.diff_continuous ? "-U1000" : "-U3";
    if(staged)snprintf(cmd,sizeof(cmd),"git diff --cached %s -- '%s' 2>/dev/null",ctx,path);
    else       snprintf(cmd,sizeof(cmd),"git diff %s -- '%s' 2>/dev/null",ctx,path);
    char *o=git_run(cmd);
    if(!o||!o[0]){
        free(o);
        snprintf(cmd,sizeof(cmd),"cat '%s' 2>/dev/null",path);
        o=git_run(cmd);
        if(o&&o[0]){
            G.diff_count=0;
            const char *line=o; int lno=1;
            while(*line&&G.diff_count<MAX_DIFF_LINES){
                const char *nl=strchr(line,'\n');
                size_t len=nl?(size_t)(nl-line):strlen(line);
                DiffLine *dl=&G.diff_lines[G.diff_count++]; memset(dl,0,sizeof(*dl));
                dl->type=1; dl->new_lno=lno++;
                if(len>=LINE_MAX_LEN)len=LINE_MAX_LEN-1;
                memcpy(dl->new_line,line,len); dl->new_line[len]='\0';
                line=nl?nl+1:line+strlen(line);
            }
            G.diff_scroll=0; G.diff_hscroll=0;
        } else G.diff_count=0;
        free(o); return;
    }
    parse_diff(o); free(o);
}
static void load_diff_commit(const char *hash){
    char cmd[256]; 
    const char *ctx = G.diff_continuous ? "-U1000" : "-U3";
    snprintf(cmd,sizeof(cmd),"git show %s %s 2>/dev/null",ctx,hash);
    char *o=git_run(cmd); parse_diff(o?o:""); free(o);
}
static void update_diff(void){
    if(G.file_count>0&&G.file_sel<G.file_count){
        GitFile *f=&G.files[G.file_sel];
        snprintf(G.diff_title,sizeof(G.diff_title),"%s",f->path);
        G.diff_staged=f->staged;
        load_diff_file(f->path,f->staged);
    }
}

static void sync_graph_preview(void){
    if(G.commit_count <= 0 || G.commit_sel < 0 || G.commit_sel >= G.commit_count) return;
    GitCommit *c = &G.commits[G.commit_sel];
    if(G.graph_file_sel < 0 || G.graph_file_sel == 0) {
        snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
        G.diff_is_summary = false; load_diff_commit(c->hash);
    } else {
        if(G.graph_file_sel >= c->f_count) G.graph_file_sel = c->f_count - 1;
        char *fpath = c->files[G.graph_file_sel];
        const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
        char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, c->hash, fpath);
        char *o = git_run(cmd);
        snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", c->hash, fpath);
        G.diff_is_summary = false;
        snprintf(G.diff_commit, sizeof(G.diff_commit), "%s", c->hash);
        parse_diff(o?o:""); free(o);
    }
}

static void reload_all(void){
    load_branch(); load_status(); load_log(); load_branches(); load_stash();
    /* Auto-expand the currently selected commit in the graph */
    if(G.commit_count > 0){
        G.commits[G.commit_sel].expanded = true;
        fetch_commit_files(G.commit_sel);
    }
    update_diff(); OK("Refreshed");
}

/* ================================================================
   BOX DRAWING HELPERS
================================================================ */
static void box_top(int row,int col,int w,const char *title,bool active, const char *extra){
    at(row,col);
    if(active){G.cur_bold=true;cfg(TH->fg_accent1);}else cfg(TH->fg_dim);
    put_cell(row,col,active?"┏":"┌");
    int tlen=(int)strlen(title)+4;
    int left=(w-2-tlen)/2; if(left<0)left=0;
    for(int i=0;i<left;i++)put_cell(row,col+1+i,active?"━":"─");
    
    if(active){G.cur_bold=true;cfg(TH->fg_bright);cbg(TH->fg_accent1);}else cfg(TH->fg_dim);
    at(row, col+1+left);
    char tbuf[128]; snprintf(tbuf, sizeof(tbuf), " %s %s ", active?"●":" ", title);
    ppad(tbuf, tlen);
    rst();
    
    if(active){G.cur_bold=true;cfg(TH->fg_accent1);}else{rst();cfg(TH->fg_dim);}
    int right=w-2-left-tlen;
    
    if(extra){
        int elen = (int)strlen(extra);
        if(right > elen + 2){
            for(int i=0;i<right-elen-2;i++)put_cell(row, col+1+left+tlen+i,active?"━":"─");
            at(row, col+w-elen-2);
            if(active) { cfg(TH->fg_bright); cbg(TH->bg_panel); }
            ppad(extra, elen);
            if(active) { rst(); cfg(TH->fg_accent1); }
            put_cell(row, col+w-2, active?"━":"─");
        } else {
            for(int i=0;i<right;i++)put_cell(row, col+1+left+tlen+i,active?"━":"─");
        }
    } else {
        for(int i=0;i<right;i++)put_cell(row, col+1+left+tlen+i,active?"━":"─");
    }
    
    put_cell(row, col+w-1, active?"┓":"┐");
    rst();
}
static void box_bot(int row,int col,int w,bool active){
    at(row,col); if(active)cfg(TH->fg_accent1); else cfg(TH->fg_dim); 
    put_cell(row,col,active?"┗":"└");
    for(int i=0;i<w-2;i++)put_cell(row,col+1+i,active?"━":"─");
    put_cell(row,col+w-1,active?"┛":"┘"); rst();
}
static void box_sides(int top,int col,int w,int h,bool active){
    if(active)cfg(TH->fg_accent1); else cfg(TH->fg_dim);
    for(int r=top+1;r<top+h-1;r++){
        put_cell(r,col,active?"┃":"│");
        put_cell(r,col+w-1,active?"┃":"│");
    }
    rst();
}
static void box_fill(int top,int col,int w,int h,Color c){
    cfg(TH->fg_normal); cbg(c);
    for(int r=top+1;r<top+h-1;r++){
        for(int i=0;i<w-2;i++)put_cell(r,col+1+i," ");
    }
    rst();
}

/* ================================================================
   CONTEXT MENU
================================================================ */
static void menu_reset(int x, int y){
    G.menu_active=true; G.menu_x=x; G.menu_y=y; G.menu_item_count=0;
    G.menu_w=20; G.menu_h=2;
}
static void menu_add_item(const char *label, void (*action)(void)){
    if(G.menu_item_count>=12)return;
    snprintf(G.menu_items[G.menu_item_count],32,"%s",label);
    G.menu_actions[G.menu_item_count]=action;
    G.menu_item_count++;
    G.menu_h++;
    int l=(int)strlen(label)+4;
    if(l>G.menu_w)G.menu_w=l;
}
static void draw_menu(void){
    if(!G.menu_active)return;
    int x=G.menu_x, y=G.menu_y, w=G.menu_w, h=G.menu_h;
    if(x+w>G.cols)x=G.cols-w;
    if(y+h>G.rows)y=G.rows-h;
    if(x<1)x=1;
    if(y<1)y=1;
    G.menu_x=x; G.menu_y=y;

    box_top(y,x,w,"Menu",true,NULL);
    box_sides(y,x,w,h,true);
    box_fill(y,x,w,h,TH->bg_panel);
    box_bot(y+h-1,x,w,true);
    for(int i=0;i<G.menu_item_count;i++){
        at(y+1+i,x+1);
        bool hover=(G.last_my==y+1+i && G.last_mx>x && G.last_mx<x+w-1);
        if(hover){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}
        else{cfg(TH->fg_normal);}
        char mitem[64]; snprintf(mitem, sizeof(mitem), " %-*s ", w-4, G.menu_items[i]);
        ppad(mitem, w-2);
        rst();
    }
}

/* ================================================================
   TAB BAR
================================================================ */
static void draw_cli(void){
    int row = G.rows - 1;
    at(row, 1);
    bool focused = (G.focus == FOCUS_CLI);
    if(focused){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}
    else{cbg(TH->bg_panel);cfg(TH->fg_dim);}
    ppad(" $ ", 3);
    if(focused){cfg(TH->fg_bright);}else{cfg(TH->fg_normal);}
    
    int len = (int)strlen(G.cli_buf);
    at(row, 4);
    ppad(G.cli_buf, G.cli_cursor);
    
    if(focused){
        at(row, 4 + G.cli_cursor);
        cbg(TH->fg_accent1); cfg(TH->bg_base);
        char c = G.cli_buf[G.cli_cursor];
        char cs[2] = {c?c:' ', 0};
        put_cell(row, 4 + G.cli_cursor, cs);
        rst();
        if(focused) cbg(TH->bg_sel); else cbg(TH->bg_panel);
        cfg(TH->fg_bright);
    }
    
    at(row, 4 + G.cli_cursor + (G.cli_buf[G.cli_cursor]?1:0));
    ppad(G.cli_buf + G.cli_cursor + (G.cli_buf[G.cli_cursor]?1:0), len - G.cli_cursor - (G.cli_buf[G.cli_cursor]?1:0));
    
    int used = 3 + len;
    at(row, 1 + used);
    for(int i=used; i<G.cols; i++) put_cell(row, 1+i, " ");
    rst();
}

static void draw_dividers(void){
    int ct=2;
    if(G.current_view == VIEW_STATUS){
        int vx = G.sidebar_w + G.lw;
        /* Vertical divider */
        if(vx >= 1 && vx <= G.cols){
            for(int r=ct; r<G.rows-1; r++){
                at(r, vx);
                bool hover = (G.last_mx == vx && G.last_my == r);
                if(G.dragging_v || hover){cfg(TH->fg_accent1); G.cur_bold=true; put_cell(r, vx, "┃");}
                else {cfg(TH->fg_dim); put_cell(r, vx, "│");}
                rst();
            }
        }
        /* Horizontal divider */
        if(G.browser_active) return;
        int hr = ct + G.lh_chg - 1;
        int hstart = G.sidebar_w + 1;
        if(hr >= 1 && hr < G.rows){
            for(int c=hstart; c<vx; c++){
                at(hr, c);
                bool hover = (G.last_my == hr && G.last_mx == c);
                if(G.dragging_h || hover){cfg(TH->fg_accent1); G.cur_bold=true; put_cell(hr, c, "━");}
                else {cfg(TH->fg_dim); put_cell(hr, c, "─");}
                rst();
            }
        }
    } else if(G.current_view == VIEW_LOG){
        int hr = ct + G.lh_log - 1;
        if(hr >= 1 && hr < G.rows){
            for(int c=1; c<G.cols; c++){
                at(hr, c);
                bool hover = (G.last_my == hr && G.last_mx == c);
                if(G.dragging_h_log || hover){cfg(TH->fg_accent1); G.cur_bold=true; put_cell(hr, c, "━");}
                else {cfg(TH->fg_dim); put_cell(hr, c, "─");}
                rst();
            }
        }
    }
}

static void draw_tabbar(void){
    at(1,1); cbg(TH->bg_tab_inact); cfg(TH->fg_accent2); G.cur_bold=true;
    ppad(" ⎇ tuide ", 9); rst();
    static const struct{const char *n,*k;View v;}tabs[]={
        {"Changes","1",VIEW_STATUS},{"Log","2",VIEW_LOG},
        {"Branches","3",VIEW_BRANCHES},{"Stash","4",VIEW_STASH},
        {"Help","?",VIEW_HELP}
    };
    int cur_c = 10;
    for(int i=0;i<5;i++){
        G.tab_x[i] = cur_c;
        bool act=(tabs[i].v==G.current_view);
        at(1, cur_c);
        if(act){cbg(TH->bg_tab_act);cfg(TH->fg_bright);G.cur_bold=true;}
        else{cbg(TH->bg_tab_inact);cfg(TH->fg_dim);}
        char tbuf[32]; snprintf(tbuf,sizeof(tbuf)," %s[%s] ",tabs[i].n,tabs[i].k);
        ppad(tbuf, (int)strlen(tbuf)); rst();
        cur_c += (int)strlen(tbuf);
        at(1, cur_c); cbg(TH->bg_tab_inact); cfg(TH->fg_dim); put_cell(1, cur_c, "│");
        cur_c++;
    }
    G.tab_x[5] = cur_c;
    /* Theme & branch - right aligned */
    cbg(TH->bg_tab_inact); cfg(TH->fg_accent3);
    char rbuf[160]; int rlen=snprintf(rbuf,sizeof(rbuf)," ◈ %s  ⎇ %s ",TH->name,G.branch_name);
    int pad=G.cols-cur_c-rlen; if(pad<0)pad=0;
    for(int i=0;i<pad;i++)put_cell(1, cur_c+i, " ");
    at(1, G.cols-rlen+1);
    cfg(TH->fg_accent3); ppad("◈ ", 2); ppad(TH->name, (int)strlen(TH->name)); ppad("  ", 2);
    cfg(TH->fg_ref_local); G.cur_bold=true; ppad("⎇ ", 2); ppad(G.branch_name, (int)strlen(G.branch_name));
    rst();
}

/* ================================================================
   STATUS BAR
================================================================ */
static void draw_statusbar(void){
    at(G.rows,1); cbg(TH->bg_panel);
    
    /* Focus Indicator */
    cfg(TH->fg_bright); cbg(TH->fg_accent1); G.cur_bold=true;
    const char *fstr = " ??? ";
    switch(G.focus){
        case FOCUS_CHANGES: fstr = " CHANGES "; break;
        case FOCUS_GRAPH:   fstr = " GRAPH   "; break;
        case FOCUS_DIFF:    fstr = " DIFF    "; break;
        case FOCUS_BROWSER: fstr = " BROWSER "; break;
        case FOCUS_EDITOR:  fstr = " EDITOR  "; break;
        case FOCUS_CLI:     fstr = " CLI     "; break;
    }
    ppad(fstr, 10);
    rst(); cbg(TH->bg_panel);
    
    const char *hint="";
    if(G.current_view==VIEW_STATUS){
        if(G.focus==FOCUS_CHANGES) hint="e:edit  SPC:stage  a:stage-all  u:unstage  d:discard  ↵:diff  c:commit  P:push  f:pull  T:theme";
        else if(G.focus==FOCUS_GRAPH) hint="e:edit  ↑/↓:move  ↵:diff  Home/End:top/bot  T:theme";
        else if(G.focus==FOCUS_BROWSER) hint="↑/↓:move  ↵/→:open  ←:back  b:close";
        else if(G.focus==FOCUS_EDITOR) hint="Arrows:move  Shift+Arrows:select  Ctrl+Z:undo  Ctrl+Y:redo  Ctrl+X:cut  Ctrl+C:copy  Ctrl+V:paste  Ctrl+S:save  e:toggle diff";
        else hint="↑/↓:scroll  [/]:hscroll  s:side-by-side  q:back  T:theme";
    } else if(G.current_view==VIEW_LOG) hint="e:edit  ↑/↓:move  ↵:diff  n:branch  s:side-by-side  T:theme";
    else if(G.current_view==VIEW_BRANCHES) hint="↵:checkout  n:new  D:delete";
    else if(G.current_view==VIEW_STASH) hint="↵:apply  p:pop  D:drop  s:stash";
    else if(G.current_view==VIEW_EDITOR) {
        if(G.focus==FOCUS_BROWSER) hint="↑/↓:move  ↵/→:open/enter  ←:back  Tab:editor";
        else hint="Arrows:move  Shift+Arrows:select  Ctrl+Z:undo  Ctrl+Y:redo  Ctrl+X:cut  Ctrl+C:copy  Ctrl+V:paste  Ctrl+S:save  f:files";
    }
    else if(G.current_view==VIEW_HELP) hint="q:close help";
    
    cfg(TH->fg_dim);
    ppad(" ", 1);
    ppad(hint, G.cols-2);
    
    if(G.status_msg[0]&&(time(NULL)-G.status_msg_time)<5){
        int mlen=(int)strlen(G.status_msg)+2;
        at(G.rows,G.cols-mlen);
        if(G.status_is_err)cfg(TH->fg_err); else cfg(TH->fg_ok);
        G.cur_bold=true;
        char sbuf[258]; snprintf(sbuf,sizeof(sbuf)," %s ",G.status_msg);
        ppad(sbuf, mlen);
    }
    rst();
}

static void fetch_commit_files(int idx){
    if(idx<0 || idx>=G.commit_count) return;
    GitCommit *c = &G.commits[idx];
    if(c->f_count > 0) return;
    snprintf(c->files[0], 128, "[View Full Diff]");
    c->f_count = 1;
    char cmd[256]; snprintf(cmd, sizeof(cmd), "git show --name-only --format='' %s 2>/dev/null | head -n 15", c->hash);
    char *o = git_run(cmd); if(!o) return;
    char *line = o;
    while(*line && c->f_count < 16){
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        if(len > 0){
            if(len >= 128) len = 127;
            memcpy(c->files[c->f_count], line, len);
            c->files[c->f_count][len] = '\0';
            c->f_count++;
        }
        line = nl ? nl + 1 : line + len;
    }
    free(o);
}

/* ================================================================
   CHANGES PANE
================================================================ */
static void draw_changes(int top,int h){
    if(h<=2) return;
    int w=G.lw;
    int sx=G.sidebar_w+1;
    bool act=(G.focus==FOCUS_CHANGES&&G.current_view==VIEW_STATUS);
    box_top(top,sx,w,"Changes",act,NULL);
    box_sides(top,sx,w,h,act);
    box_fill(top,sx,w,h,TH->bg_panel);

    int staged_n=0,unstaged_n=0;
    for(int i=0;i<G.file_count;i++) G.files[i].staged?staged_n++:unstaged_n++;

    int row=top+1, lim=top+h-1, iw=w-2;

    /* ── Staged section ── */
    if(row<lim){
        at(row,sx+1); cbg(TH->bg_header); cfg(TH->fg_staged); G.cur_bold=true;
        char hdr[64]; snprintf(hdr,sizeof(hdr)," ✓ Staged (%d) ",staged_n);
        ppad(hdr,iw); rst(); row++;
    }
    for(int i=0;i<G.file_count&&row<lim;i++){
        if(!G.files[i].staged)continue;
        bool sel=(G.file_sel==i&&act);
        at(row,sx+1);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true; ppad(" »", 2);}
        else {cbg(TH->bg_panel); cfg(TH->fg_staged); ppad("  ", 2);}
        
        const char *ic="M";
        switch(G.files[i].st){
            case FS_STAGED_NEW:ic="A";break; case FS_STAGED_DEL:ic="D";break;
            case FS_RENAMED:ic="R";break;    case FS_COPIED:ic="C";break;
            default:ic="M";break;
        }
        ppad(ic, 1); ppad(" ", 1);
        if(sel){cfg(TH->fg_sel);cbg(TH->bg_sel);}else{cfg(TH->fg_normal);cbg(TH->bg_panel);}
        char disp[512];
        if(G.files[i].orig[0])snprintf(disp,sizeof(disp),"%s→%s",G.files[i].orig,G.files[i].path);
        else snprintf(disp,sizeof(disp),"%s",G.files[i].path);
        ppad(disp,iw-4); rst(); row++;
    }
    /* spacer */
    if(row<lim){at(row,sx+1);cbg(TH->bg_panel);for(int i=0;i<iw;i++)put_cell(row,sx+1+i," ");rst();row++;}

    /* ── Unstaged section ── */
    if(row<lim){
        at(row,sx+1); cbg(TH->bg_header); cfg(TH->fg_unstaged); G.cur_bold=true;
        char hdr[64]; snprintf(hdr,sizeof(hdr)," ✗ Unstaged (%d) ",unstaged_n);
        ppad(hdr,iw); rst(); row++;
    }
    for(int i=0;i<G.file_count&&row<lim;i++){
        if(G.files[i].staged)continue;
        bool sel=(G.file_sel==i&&act);
        at(row,sx+1);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true; ppad(" »", 2);}
        else {cbg(TH->bg_panel); cfg(TH->fg_unstaged); ppad("  ", 2);}
        
        Color ic_col=TH->fg_unstaged;
        const char *ic="M";
        switch(G.files[i].st){
            case FS_UNTRACKED:ic="?";ic_col=TH->fg_untracked;break;
            case FS_DELETED:ic="D";break;
            case FS_CONFLICT:ic="!";ic_col=TH->fg_conflict;break;
            default:ic="M";break;
        }
        cfg(sel?TH->fg_sel:ic_col); ppad(ic, 1); ppad(" ", 1);
        if(sel){cfg(TH->fg_sel);cbg(TH->bg_sel);}else{cfg(TH->fg_normal);cbg(TH->bg_panel);}
        ppad(G.files[i].path,iw-4); rst(); row++;
    }
    /* fill */
    while(row<lim){at(row,sx+1);cbg(TH->bg_panel);for(int i=0;i<iw;i++)put_cell(row,sx+1+i," ");rst();row++;}
    box_bot(top+h-1,sx,w,act);
}

/* ================================================================
   GRAPH PANE
================================================================ */
static void draw_graph(int top,int h){
    if(h<=2) return;
    int w=G.lw;
    int sx=G.sidebar_w+1;
    bool act=(G.focus==FOCUS_GRAPH&&G.current_view==VIEW_STATUS);
    box_top(top,sx,w,"Graph",act,NULL);
    box_sides(top,sx,w,h,act);
    box_fill(top,sx,w,h,TH->bg_base);

    int row=top+1,lim=top+h-1,iw=w-2;
    int vis=lim-row;
    if(vis<=0) { box_bot(top+h-1,sx,w,act); return; }

    if(G.commit_sel<G.commit_scroll)G.commit_scroll=G.commit_sel;
    if(G.commit_sel>=G.commit_scroll+vis)G.commit_scroll=G.commit_sel-vis+1;

    G.graph_rows_count = 0;
    for(int i=G.commit_scroll;i<G.commit_count&&row<lim;i++,row++){
        GitCommit *c=&G.commits[i];
        bool sel=(G.commit_sel==i);
        
        if(G.graph_rows_count < MAX_COMMITS*17){
            G.graph_rows[G.graph_rows_count].commit_idx = i;
            G.graph_rows[G.graph_rows_count].file_idx = -1;
            G.graph_rows_count++;
        }

        at(row,sx+1);
        if(sel){cbg(TH->bg_sel); G.cur_bold=true;}else cbg(TH->bg_base);

        /* Expansion indicator */
        cfg(sel?TH->fg_sel:TH->fg_dim);
        ppad(c->expanded ? "▾ " : "▸ ", 2);

        /* Graph chars */
        char *gp=c->graph; int gc=0;
        int gx = sx+3;
        while(*gp&&gc<GRAPH_COLS){
            int ci_=(c->graph_col/2)%6;
            at(row, gx+gc);
            if(*gp=='*'){
                cfg(TH->fg_graph[ci_]); G.cur_bold=true; put_cell(row, gx+gc, "●");
                rst(); if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);
            } else if(*gp=='|'){cfg(TH->fg_graph[gc/2%6]);put_cell(row, gx+gc, "|");}
            else if(*gp=='/'){cfg(TH->fg_graph[1]);put_cell(row, gx+gc, "/");}
            else if(*gp=='\\'){cfg(TH->fg_graph[2]);put_cell(row, gx+gc, "\\");}
            else if(*gp=='-'){cfg(TH->fg_graph[2]);put_cell(row, gx+gc, "-");}
            else { char tmp[2]={*gp,0}; put_cell(row, gx+gc, tmp); }
            rst(); if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);
            gp++; gc++;
        }
        while(gc<GRAPH_COLS){at(row, gx+gc); put_cell(row, gx+gc, " "); gc++;}

        at(row, gx+GRAPH_COLS);
        cfg(sel?TH->fg_sel:TH->fg_accent1); G.cur_bold=true;
        char hbuf[16]; snprintf(hbuf,sizeof(hbuf),"%.8s ",c->hash);
        ppad(hbuf, 9); rst();
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);}else cbg(TH->bg_base);

        int used=GRAPH_COLS+9+2;
        if(c->refs[0]&&iw>used+8){
            cfg(sel?TH->fg_sel:TH->fg_ref_local);
            char rf[32]; snprintf(rf,sizeof(rf),"(%.14s) ",c->refs);
            ppad(rf, (int)strlen(rf));
            used+=(int)strlen(rf);
            if(sel){cfg(TH->fg_sel);cbg(TH->bg_sel);}else{rst();cbg(TH->bg_base);}
        }
        int sw=iw-used;
        if(sw>0){cfg(sel?TH->fg_sel:TH->fg_normal);ppad(c->subject,sw);}
        rst();

        if(c->expanded && row+1 < lim){
            for(int fi=0; fi<c->f_count && row+1 < lim; fi++){
                row++;
                bool fsel = (sel && G.graph_file_sel == fi);
                if(G.graph_rows_count < MAX_COMMITS*17){
                    G.graph_rows[G.graph_rows_count].commit_idx = i;
                    G.graph_rows[G.graph_rows_count].file_idx = fi;
                    G.graph_rows_count++;
                }
                at(row, sx+1); 
                if(fsel){cbg(TH->bg_sel); G.cur_bold=true;} else cbg(TH->bg_base);
                
                cfg(TH->fg_dim);
                ppad(fi == c->f_count-1 ? "  └─ " : "  ├─ ", 5);
                
                cfg(fsel ? TH->fg_sel : TH->fg_accent2);
                ppad(c->files[fi], iw-7);
                rst();
            }
        }
    }
    while(row<lim){at(row,sx+1);cbg(TH->bg_base);for(int i=0;i<iw;i++)put_cell(row,sx+1+i," ");rst();row++;}
    box_bot(top+h-1,sx,w,act);
}

/* ================================================================
   DIFF PANE (side-by-side + unified)
================================================================ */
/* Selection check for editor */
static bool is_ed_selected(int y, int x){
    if(!G.ed_selecting) return false;
    int s_y = G.ed_sel_start_y, s_x = G.ed_sel_start_x;
    int e_y = G.ed_sel_end_y, e_x = G.ed_sel_end_x;
    if(s_y > e_y || (s_y == e_y && s_x > e_x)){
        int t=s_y; s_y=e_y; e_y=t;
        t=s_x; s_x=e_x; e_x=t;
    }
    if(y < s_y || y > e_y) return false;
    if(y == s_y && x < s_x) return false;
    if(y == e_y && x > e_x) return false;
    return true;
}

/* Selection check for diff */
static bool is_selected(int y, int x){
    if(!G.selecting) return false;
    int s_y = G.sel_start_y, s_x = G.sel_start_x;
    int e_y = G.sel_end_y, e_x = G.sel_end_x;
    if(s_y > e_y || (s_y == e_y && s_x > e_x)){
        int t=s_y; s_y=e_y; e_y=t;
        t=s_x; s_x=e_x; e_x=t;
    }
    if(y < s_y || y > e_y) return false;
    if(y == s_y && x < s_x) return false;
    if(y == e_y && x > e_x) return false;
    return true;
}

static void draw_diff_line_with_sel(int r, int c, int w, const char *s, int y_idx, int x_off){
    at(r, c);
    int len = (int)strlen(s);
    for(int i=0; i<w-1; i++){
        int char_idx = i + x_off;
        if(char_idx < len){
            if(is_selected(y_idx, char_idx)){
                Color old_bg = G.cur_bg;
                cbg(TH->bg_sel);
                char tmp[2] = {s[char_idx], 0};
                put_cell(r, c+i, tmp);
                cbg(old_bg);
            } else {
                char tmp[2] = {s[char_idx], 0};
                put_cell(r, c+i, tmp);
            }
        } else {
            put_cell(r, c+i, " ");
        }
    }
}

static void draw_diff(int top,int rx,int rw,int h){
    if(h<=2) return;
    bool act=(G.focus==FOCUS_DIFF);
    char title[128];
    if(G.diff_title[0])snprintf(title,sizeof(title),"%.60s%s",G.diff_title,G.diff_staged?" [staged]":"");
    else snprintf(title,sizeof(title),"Diff (select file or commit)");

    char extra[64];
    snprintf(extra, sizeof(extra), " [%s] [%s] ", G.diff_sidebyside?"Split":"Unify", G.diff_continuous?"Full":"Hunk");

    box_top(top,rx,rw,title,act,extra);
    box_sides(top,rx,rw,h,act);
    box_fill(top,rx,rw,h,TH->bg_base);
    box_bot(top+h-1,rx,rw,act);

    int row=top+1,lim=top+h-1,vis=lim-row;
    int maxsc=imax(0,G.diff_count-vis);
    if(G.diff_scroll<0)G.diff_scroll=0;
    if(G.diff_scroll>maxsc)G.diff_scroll=maxsc;

    if(!G.diff_count){
        at(row+vis/2,rx+rw/2-12);
        cfg(TH->fg_dim); ppad("(no diff — select a file or commit)", 35); rst(); return;
    }

    bool ssb=G.diff_sidebyside;
    int lnum_w=4;
    int half = G.diff_split;
    int code_w_left = half - lnum_w - 2;
    int code_w_right = (rw - half) - lnum_w - 3;
    if(code_w_left<2)code_w_left=2;
    if(code_w_right<2)code_w_right=2;

    /* Column headers for side-by-side */
    if(ssb){
        /* Center divider */
        cfg(TH->fg_dim);
        for(int r=row+1;r<lim;r++){at(r,rx+half);put_cell(r, rx+half, "│");}
        /* Header */
        at(row,rx+1); cbg(TH->bg_header); cfg(TH->fg_accent3); G.cur_bold=true;
        ppad(" ◀ OLD",half-1);
        at(row,rx+half+1); ppad(" NEW ▶ ", (rw - half)-1);
        rst(); row++; lim--; vis--;
        if(G.diff_scroll>imax(0,G.diff_count-vis))G.diff_scroll=imax(0,G.diff_count-vis);
    }

    int di=G.diff_scroll;
    if(!ssb){
        /* Unified */
        int code_w = rw - lnum_w - 6;
        if(code_w < 8) code_w = 8;
        for(;di<G.diff_count&&row<lim;di++,row++){
            DiffLine *dl=&G.diff_lines[di];
            at(row,rx+1);
            char lno[16];
            switch(dl->type){
            case 0: /* Context */
                cbg(TH->bg_base);cfg(TH->fg_linenum);snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno);ppad(lno,lnum_w+1);
                cfg(TH->fg_dim); ppad("│", 1); cfg(TH->fg_diff_ctx); draw_diff_line_with_sel(row,rx+lnum_w+3,code_w,dl->old_line,di,0); break;
            case 1: /* Added */
                cbg(TH->bg_diff_add);cfg(TH->fg_linenum);if(dl->new_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->new_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");ppad(lno,lnum_w+1);
                cfg(TH->fg_ok); ppad("+", 1); cfg(TH->fg_diff_add);G.cur_bold=true;draw_diff_line_with_sel(row,rx+lnum_w+3,code_w-1,dl->new_line,di,0);break;
            case 2: /* Deleted */
                cbg(TH->bg_diff_del);cfg(TH->fg_linenum);if(dl->old_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");ppad(lno,lnum_w+1);
                cfg(TH->fg_err); ppad("-", 1); cfg(TH->fg_diff_del);G.cur_bold=true;draw_diff_line_with_sel(row,rx+lnum_w+3,code_w-1,dl->old_line,di,0);break;
            case 3: /* Hunk header */
                cbg(TH->bg_diff_hdr);cfg(TH->fg_accent3);G.cur_bold=true;
                char hsub[LINE_MAX_LEN]; char *hs = strstr(dl->new_line, "@@"); 
                if(hs) { hs = strstr(hs+2, "@@"); if(hs) hs += 2; }
                snprintf(hsub, sizeof(hsub), "  %s", hs?hs:dl->new_line);
                ppad(hsub, rw-2); break;
            case 4: /* File header */
                cbg(TH->bg_header);cfg(TH->fg_accent2);G.cur_bold=true;ppad(dl->new_line[0]?dl->new_line:dl->old_line,rw-2);break;
            case 5: /* Commit summary file list item */ {
                bool sel = (G.diff_is_summary && G.diff_sel == di && act);
                if(sel) { cbg(TH->bg_sel); cfg(TH->fg_sel); G.cur_bold=true; }
                else { cbg(TH->bg_base); cfg(TH->fg_accent1); }
                char fbuf[LINE_MAX_LEN+4]; snprintf(fbuf, sizeof(fbuf), "  → %s", dl->new_line);
                ppad(fbuf, rw-2); break;
            }
            case 6: /* Metadata / Info */
                cbg(TH->bg_panel); cfg(TH->fg_dim); G.cur_italic=true;
                ppad(dl->new_line[0] ? dl->new_line : dl->old_line, rw-2);
                G.cur_italic=false; break;
            }
            rst();
        }
    } else {
        /* Side-by-side */
        while(di<G.diff_count&&row<lim){
            DiffLine *dl=&G.diff_lines[di];
            if(dl->type==3||dl->type==4||dl->type==6){
                at(row,rx+1);
                if(dl->type==3){
                    cbg(TH->bg_diff_hdr);cfg(TH->fg_accent3);
                    char hsub[LINE_MAX_LEN]; char *hs = strstr(dl->new_line, "@@"); 
                    if(hs) { hs = strstr(hs+2, "@@"); if(hs) hs += 2; }
                    snprintf(hsub, sizeof(hsub), "  %s", hs?hs:dl->new_line);
                    G.cur_bold=true; 
                    ppad(hsub, half-1);
                    at(row, rx+half); cfg(TH->fg_dim); put_cell(row, rx+half, "│");
                    at(row, rx+half+1); cbg(TH->bg_diff_hdr); ppad("", (rw-half)-1);
                } else if(dl->type==4) {
                    cbg(TH->bg_header);cfg(TH->fg_accent2); G.cur_bold=true;
                    ppad(dl->new_line[0]?dl->new_line:dl->old_line, half-1);
                    at(row, rx+half); cfg(TH->fg_dim); put_cell(row, rx+half, "│");
                    at(row, rx+half+1); cbg(TH->bg_header); ppad("", (rw-half)-1);
                } else {
                    cbg(TH->bg_panel); cfg(TH->fg_dim); G.cur_italic=true;
                    ppad(dl->new_line[0]?dl->new_line:dl->old_line, half-1);
                    at(row, rx+half); cfg(TH->fg_dim); put_cell(row, rx+half, "│");
                    at(row, rx+half+1); cbg(TH->bg_panel); ppad("", (rw-half)-1);
                    G.cur_italic=false;
                }
                rst(); di++; row++; continue;
            }
            if(dl->type==0){
                at(row,rx+1); cbg(TH->bg_base); cfg(TH->fg_linenum);
                char lno[16]; snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno); ppad(lno, lnum_w+1);
                cfg(TH->fg_dim); ppad("│", 1); cfg(TH->fg_diff_ctx); ppad(dl->old_line,code_w_left);
                at(row,rx+half+1); cbg(TH->bg_base); cfg(TH->fg_linenum);
                snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->new_lno); ppad(lno, lnum_w+1);
                cfg(TH->fg_dim); ppad("│", 1); cfg(TH->fg_diff_ctx); ppad(dl->new_line,code_w_right);
                rst(); di++; row++; continue;
            }
            /* Handle blocks of deletions and additions */
            int ndel = 0, nadd = 0;
            while (di + ndel < G.diff_count && G.diff_lines[di + ndel].type == 2) ndel++;
            while (di + ndel + nadd < G.diff_count && G.diff_lines[di + ndel + nadd].type == 1) nadd++;

            if (ndel > 0 || nadd > 0) {
                int nmax = imax(ndel, nadd);
                for (int i = 0; i < nmax && row < lim; i++, row++) {
                    DiffLine *od = (i < ndel) ? &G.diff_lines[di + i] : NULL;
                    DiffLine *nd = (i < nadd) ? &G.diff_lines[di + ndel + i] : NULL;

                    /* Left: old */
                    at(row, rx + 1);
                    if (od) {
                        cbg(TH->bg_diff_del); cfg(TH->fg_linenum);
                        char lno[16]; snprintf(lno, sizeof(lno), "%*d ", lnum_w, od->old_lno);
                        ppad(lno, lnum_w + 1); cfg(TH->fg_err); ppad("-", 1); cfg(TH->fg_diff_del); G.cur_bold = true; ppad(od->old_line, code_w_left);
                    } else {
                        cbg(TH->bg_panel); cfg(TH->fg_dim);
                        for (int j = 0; j < lnum_w + 1; j++) put_cell(row, rx + 1 + j, " ");
                        put_cell(row, rx + 1 + lnum_w + 1, "┆");
                        for (int j = 0; j < code_w_left; j++) put_cell(row, rx + 1 + lnum_w + 2 + j, " ");
                    }
                    rst();

                    /* Right: new */
                    at(row, rx + half + 1);
                    if (nd) {
                        cbg(TH->bg_diff_add); cfg(TH->fg_linenum);
                        char lno[16]; snprintf(lno, sizeof(lno), "%*d ", lnum_w, nd->new_lno);
                        ppad(lno, lnum_w + 1); cfg(TH->fg_ok); ppad("+", 1); cfg(TH->bg_diff_add); cfg(TH->fg_diff_add); G.cur_bold = true; ppad(nd->new_line, code_w_right);
                    } else {
                        cbg(TH->bg_panel); cfg(TH->fg_dim);
                        for (int j = 0; j < lnum_w + 1; j++) put_cell(row, rx + half + 1 + j, " ");
                        put_cell(row, rx + half + 1 + lnum_w + 1, "┆");
                        for (int j = 0; j < code_w_right; j++) put_cell(row, rx + half + 1 + lnum_w + 2 + j, " ");
                    }
                    rst();
                }
                di += ndel + nadd;
            } else {
                /* Fallback for other types not explicitly handled above (though there shouldn't be any in this loop section) */
                di++;
            }
        }
    }
    /* Fill */
    while(row<lim){at(row,rx+1);cbg(TH->bg_base);for(int i=0;i<rw-2;i++)put_cell(row,rx+1+i," ");rst();row++;}
    /* Scrollbar & Minimap */
    if(G.diff_count>vis&&vis>2){
        int bh=imax(1,(vis*vis)/G.diff_count);
        int bpos=maxsc>0?((G.diff_scroll*(vis-bh))/maxsc):0;
        G.sc_y = top+1; G.sc_h = vis; G.sc_total = G.diff_count; G.sc_vis = vis;

        /* Prepare minimap markers */
        char markers[1024] = {0}; /* 0=none, 1=add, 2=del, 3=both */
        if (vis < 1024) {
            for (int i=0; i<G.diff_count; i++) {
                int r = (i * vis) / G.diff_count;
                if (r < vis) {
                    if (G.diff_lines[i].type == 1) markers[r] |= 1;
                    else if (G.diff_lines[i].type == 2) markers[r] |= 2;
                }
            }
        }

        for(int r=0;r<vis;r++){
            bool thumb = (r>=bpos&&r<bpos+bh);
            for (int sw=0; sw<3; sw++) {
                at(top+1+r,rx+rw-1-sw);
                if (thumb) {
                    if (G.dragging_sc) cfg(TH->fg_sel);
                    else if (G.last_mx >= rx+rw-3) cfg(TH->fg_accent1);
                    else cfg(TH->fg_accent2);
                    put_cell(top+1+r, rx+rw-1-sw, "█");
                } else {
                    if (markers[r] == 1) { cfg(TH->fg_staged); put_cell(top+1+r, rx+rw-1-sw, "▒"); }
                    else if (markers[r] == 2) { cfg(TH->fg_unstaged); put_cell(top+1+r, rx+rw-1-sw, "▒"); }
                    else if (markers[r] == 3) { cfg(TH->fg_accent1); put_cell(top+1+r, rx+rw-1-sw, "▒"); }
                    else { cfg(TH->fg_dim); put_cell(top+1+r, rx+rw-1-sw, sw==0?"│":" "); }
                }
            }
        }
        rst();
    } else {
        G.sc_h = 0;
    }
}

/* ================================================================
   LOG VIEW (full screen)
================================================================ */
static void draw_log(int top,int h){
    int w=G.cols;
    box_top(top,1,w,"Commit Log",true,NULL);
    box_sides(top,1,w,h,true);
    box_fill(top,1,w,h,TH->bg_base);
    box_bot(top+h-1,1,w,true);

    int row=top+1,lim=top+h-1,vis=lim-row;
    if(G.commit_sel<G.commit_scroll)G.commit_scroll=G.commit_sel;
    if(G.commit_sel>=G.commit_scroll+vis)G.commit_scroll=G.commit_sel-vis+1;

    /* Header */
    at(row, 2); cbg(TH->bg_header); cfg(TH->fg_accent3); G.cur_bold=true;
    int cur_x = 2;
    ppad("Graph", GRAPH_COLS); cur_x += GRAPH_COLS;
    
    int hx = cur_x + G.col_hash_w;
    ppad("Hash", G.col_hash_w);
    at(row, hx); if(G.dragging_col_hash) cfg(TH->fg_accent1); else cfg(TH->fg_dim); put_cell(row, hx, "│"); cur_x = hx + 1;
    
    cfg(TH->fg_accent3);
    ppad("Refs", 21); cur_x += 21;
    
    int ax = cur_x + G.col_author_w;
    ppad("Author", G.col_author_w);
    at(row, ax); if(G.dragging_col_author) cfg(TH->fg_accent1); else cfg(TH->fg_dim); put_cell(row, ax, "│"); cur_x = ax + 1;
    
    cfg(TH->fg_accent3);
    int dx = cur_x + G.col_date_w;
    ppad("Date", G.col_date_w);
    at(row, dx); if(G.dragging_col_date) cfg(TH->fg_accent1); else cfg(TH->fg_dim); put_cell(row, dx, "│"); cur_x = dx + 1;
    
    cfg(TH->fg_accent3);
    ppad("Subject", w - cur_x - 1);
    rst(); row++; vis--;

    for(int i=G.commit_scroll;i<G.commit_count&&row<lim;i++,row++){
        GitCommit *c=&G.commits[i];
        bool sel=(G.commit_sel==i);
        at(row,2);
        if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);

        /* graph */
        char *gp=c->graph; int gc=0;
        while(*gp&&gc<GRAPH_COLS){
            int ci_=(c->graph_col/2)%6;
            at(row, 2+gc);
            if(*gp=='*'){cfg(TH->fg_graph[ci_]);G.cur_bold=true;put_cell(row,2+gc,"●");}
            else if(*gp=='|'){cfg(TH->fg_graph[gc/2%6]);put_cell(row,2+gc,"|");}
            else { char tmp[2]={*gp,0}; put_cell(row,2+gc,tmp); }
            rst(); if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);
            gp++;gc++;
        }
        while(gc<GRAPH_COLS){at(row, 2+gc); put_cell(row,2+gc," ");gc++;}

        cur_x = 2 + GRAPH_COLS;
        at(row, cur_x);
        cfg(sel?TH->fg_sel:TH->fg_accent1);G.cur_bold=true; 
        char hbuf[64]; snprintf(hbuf,sizeof(hbuf),"%.*s", G.col_hash_w-1, c->hash); 
        ppad(hbuf, G.col_hash_w); rst();
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);}else cbg(TH->bg_base);
        at(row, cur_x + G.col_hash_w); put_cell(row, cur_x + G.col_hash_w, "│");
        cur_x += G.col_hash_w + 1;

        if(c->refs[0]){
            cfg(sel?TH->fg_sel:TH->fg_ref_local);
            char rf[32];snprintf(rf,sizeof(rf),"(%.18s) ",c->refs);
            ppad(rf,21);
        } else {
            ppad("", 21);
        }
        cur_x += 21;
        
        cfg(sel?TH->fg_sel:TH->fg_accent2); ppad(c->author, G.col_author_w); 
        at(row, cur_x + G.col_author_w); put_cell(row, cur_x + G.col_author_w, "│");
        cur_x += G.col_author_w + 1;

        cfg(sel?TH->fg_sel:TH->fg_accent3); ppad(c->date, G.col_date_w); 
        at(row, cur_x + G.col_date_w); put_cell(row, cur_x + G.col_date_w, "│");
        cur_x += G.col_date_w + 1;

        int sw=w-cur_x-1;
        if(sw>0){cfg(sel?TH->fg_sel:TH->fg_normal);ppad(c->subject,sw);}
        rst();
    }
    while(row<lim){at(row,2);cbg(TH->bg_base);for(int i=0;i<w-3;i++)put_cell(row,2+i," ");rst();row++;}
}

/* ================================================================
   BRANCHES VIEW
================================================================ */
static void draw_branches(int top,int h){
    int w=G.cols;
    box_top(top,1,w,"Branches",true,NULL);
    box_sides(top,1,w,h,true);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w,true);

    int row=top+1,lim=top+h-1,vis=lim-row;
    if(G.branch_sel<G.branch_scroll)G.branch_scroll=G.branch_sel;
    if(G.branch_sel>=G.branch_scroll+vis)G.branch_scroll=G.branch_sel-vis+1;

    /* header */
    at(row,2);cbg(TH->bg_header);cfg(TH->fg_accent3);G.cur_bold=true;
    char hdb[256]; snprintf(hdb, sizeof(hdb), "  %-2s %-40s %-28s %s", "★", "Name", "Upstream", "±");
    ppad(hdb, w-3);
    rst();row++;

    for(int i=G.branch_scroll;i<G.branch_count&&row<lim;i++,row++){
        GitBranch *b=&G.branches[i]; bool sel=(G.branch_sel==i);
        at(row,2);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}else cbg(TH->bg_panel);
        if(b->is_current){cfg(sel?TH->fg_sel:TH->fg_staged);G.cur_bold=true;ppad("✱ ", 2);}else ppad("  ", 2);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);}else cbg(TH->bg_panel);
        if(b->is_remote){cfg(sel?TH->fg_sel:TH->fg_ref_remote);ppad("⬡ ", 2);}
        else{cfg(sel?TH->fg_sel:TH->fg_ref_local);ppad("⬢ ", 2);}
        if(sel){cfg(TH->fg_sel);}else cfg(TH->fg_normal);
        char nb[42]; snprintf(nb,sizeof(nb),"%-40s",b->name); ppad(nb,40); ppad(" ", 1);
        ppad(b->upstream,28); ppad(" ", 1);
        if(b->ahead||b->behind){
            cfg(sel?TH->fg_sel:TH->fg_accent1);
            char ab[32]; snprintf(ab,sizeof(ab),"↑%d ↓%d",b->ahead,b->behind);
            ppad(ab, (int)strlen(ab));
        }
        else if(b->upstream[0]){cfg(sel?TH->fg_sel:TH->fg_staged);ppad("✓", 1);}
        rst();
    }
    while(row<lim){at(row,2);cbg(TH->bg_panel);for(int i=0;i<w-3;i++)put_cell(row,2+i," ");rst();row++;}
}

/* ================================================================
   STASH VIEW
================================================================ */
static void draw_stash(int top,int h){
    int w=G.cols; char title[64];
    snprintf(title,sizeof(title),"Stash (%d)",G.stash_count);
    box_top(top,1,w,title,true,NULL);
    box_sides(top,1,w,h,true);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w,true);
    int row=top+1,lim=top+h-1;
    if(!G.stash_count){
        at(row+2,G.cols/2-14);
        cfg(TH->fg_dim);ppad("No stashes. Press 's' to stash changes.", 38);rst();return;
    }
    for(int i=0;i<G.stash_count&&row<lim;i++,row++){
        bool sel=(G.stash_sel==i);
        at(row,2);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}else cbg(TH->bg_panel);
        cfg(sel?TH->fg_sel:TH->fg_accent1);G.cur_bold=true;
        char sibuf[16]; snprintf(sibuf,sizeof(sibuf)," stash@{%d} ",G.stashes[i].index); ppad(sibuf, (int)strlen(sibuf));
        cfg(sel?TH->fg_sel:TH->fg_accent3); char hbuf[16]; snprintf(hbuf,sizeof(hbuf),"%.8s  ",G.stashes[i].hash); ppad(hbuf, 10);
        cfg(sel?TH->fg_sel:TH->fg_normal);ppad(G.stashes[i].message,w-28);
        rst();
    }
    while(row<lim){at(row,2);cbg(TH->bg_panel);for(int i=0;i<w-3;i++)put_cell(row,2+i," ");rst();row++;}
}

/* ================================================================
   HELP VIEW
================================================================ */
static void draw_help(int top,int h){
    int w=G.cols;
    box_top(top,1,w,"Help & Keybindings",true,NULL);
    box_sides(top,1,w,h,true);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w,true);

    static const char *E[][2]={
        {"NAVIGATION",""},
        {"  Tab / Shift+Tab","Cycle through all VISIBLE panes"},
        {"  1-4 / ?","Jump to specific Git view"},
        {"  ↑/↓","Move selection"},
        {"  ←/→","Switch focus (or Browser back/enter)"},
        {"  Home / End","Top / bottom"},
        {"  PgUp/PgDn","Page scroll"},
        {"",""},
        {"CHANGES PANE",""},
        {"  e","Open selected file in Editor"},
        {"  Space / Ctrl+S","Stage / unstage selected file"},
        {"  a / u","Stage all / Unstage all"},
        {"  d","Discard changes"},
        {"  Enter / =","Toggle Diff pane"},
        {"",""},
        {"EDITOR",""},
        {"  e","Toggle Editor visibility (right pane)"},
        {"  Arrows","Move cursor"},
        {"  Enter","Insert newline"},
        {"  BS","Delete character"},
        {"  Ctrl+Z","Undo"},
        {"  Ctrl+Y","Redo"},
        {"  Ctrl+S","Save file"},
        {"  Ctrl+X","Cut selection"},
        {"  Ctrl+C / y","Copy selection"},
        {"  Ctrl+V","Paste from clipboard"},
        {"  Shift+Arrows","Extend text selection"},
        {"",""},
        {"FILE BROWSER",""},
        {"  b","Toggle Browser visibility (left pane)"},
        {"  ↑/↓","Move selection"},
        {"  Enter / →","Open file in Editor / Enter directory"},
        {"  ←","Go back to parent directory"},
        {"",""},
        {"DIFF SELECTION",""},
        {"  Mouse Drag","Select text in diff view"},
        {"  y / Ctrl+C","Copy selection to clipboard"},
        {"",""},
        {"GLOBAL",""},
        {"  c / Ctrl+C","Commit staged changes"},
        {"  A","Amend last commit"},
        {"  P / Ctrl+P","Push to remote"},
        {"  f / Ctrl+F","Fetch + pull"},
        {"  s","Stash working changes"},
        {"  R / Ctrl+R / Ctrl+L","Full refresh"},
        {"  T","Cycle theme"},
        {"  ?","Toggle this help"},
        {"  q / Esc / Ctrl+Q","Go back / quit"},
        {"",""},
        {"MOUSE",""},
        {"  Click","Focus pane, select item"},
        {"  Scroll wheel","Scroll pane under cursor"},
        {NULL,NULL}
    };
    int row=top+1,lim=top+h-1;
    int split=G.cols/2;
    for(int i=0;E[i][0]&&row<lim;i++,row++){
        at(row,2); cbg(TH->bg_panel);
        if(E[i][1][0]=='\0'&&strlen(E[i][0])>1){
            cfg(TH->fg_accent1);G.cur_bold=true;ppad(E[i][0],split-3);
        } else if(!E[i][0][0]){
            for(int k=0;k<G.cols-3;k++)put_cell(row, 2+k, " ");
        } else {
            cfg(TH->fg_accent2);G.cur_bold=true;ppad(E[i][0],split-3);
            at(row,split);cbg(TH->bg_panel);cfg(TH->fg_normal);
            ppad(E[i][1],G.cols-split-2);
        }
        rst();
    }
    while(row<lim){at(row,2);cbg(TH->bg_panel);for(int i=0;i<G.cols-3;i++)putchar(' ');rst();row++;}
}

/* ================================================================
   PROMPT OVERLAY
================================================================ */
static void draw_prompt_overlay(void){
    if(!G.in_prompt)return;
    at(G.rows-1,1);
    cbg(TH->bg_tab_act);cfg(TH->fg_accent2);G.cur_bold=true;
    char lbuf[132]; snprintf(lbuf, sizeof(lbuf), " %s ", G.prompt_label);
    ppad(lbuf, (int)strlen(lbuf));
    rst(); cbg(TH->bg_panel);cfg(TH->fg_bright);
    ppad(" ", 1);
    
    int label_len = (int)strlen(G.prompt_label) + 3;
    at(G.rows-1, label_len);
    if(G.prompt_obscure){
        for(int i=0;i<G.prompt_cursor;i++) put_cell(G.rows-1, label_len+i, "*");
    } else {
        ppad(G.prompt_buf, G.prompt_cursor);
    }
    
    at(G.rows-1, label_len + G.prompt_cursor);
    cbg(TH->fg_accent1);cfg(TH->bg_base);
    char nc=G.prompt_buf[G.prompt_cursor];
    char ncs[2] = {nc?nc:' ', 0};
    put_cell(G.rows-1, label_len + G.prompt_cursor, ncs);
    rst();
    
    cbg(TH->bg_panel);cfg(TH->fg_bright);
    if(!G.prompt_obscure){
        at(G.rows-1, label_len + G.prompt_cursor + 1);
        ppad(G.prompt_buf+G.prompt_cursor+(nc?1:0), (int)strlen(G.prompt_buf) - G.prompt_cursor - (nc?1:0));
    }
    
    int used=(int)strlen(G.prompt_label)+3+(int)strlen(G.prompt_buf)+2;
    at(G.rows-1, used);
    for(int i=used;i<G.cols;i++) put_cell(G.rows-1, 1+i, " ");
    rst();
}

/* ================================================================
   MASTER DRAW
================================================================ */
/* ================================================================
   EDITOR & BROWSER LOGIC
================================================================ */
static void load_browser(const char *path){
    G.browser_count=0;
    char real[PATH_MAX];
    if(realpath(path, real)){
        snprintf(G.browser_path, sizeof(G.browser_path), "%s", real);
    } else {
        snprintf(G.browser_path, sizeof(G.browser_path), "%s", path);
    }
    
    /* Add .. if not root */
    if(strcmp(G.browser_path, "/") != 0){
        BrowserFile *f = &G.browser_files[G.browser_count++];
        strcpy(f->path, ".."); f->is_dir = true;
    }

    DIR *d = opendir(G.browser_path);
    if(!d) return;
    struct dirent *dir;
    while((dir = readdir(d)) != NULL && G.browser_count < 1024){
        if(strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        BrowserFile *f = &G.browser_files[G.browser_count++];
        snprintf(f->path, sizeof(f->path), "%s", dir->d_name);
        struct stat st;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", G.browser_path, dir->d_name);
        stat(full, &st);
        f->is_dir = S_ISDIR(st.st_mode);
    }
    closedir(d);
    /* Sort: dirs first (excluding ..), then alpha */
    int start = (G.browser_count > 0 && strcmp(G.browser_files[0].path, "..") == 0) ? 1 : 0;
    for(int i=start; i<G.browser_count-1; i++){
        for(int j=i+1; j<G.browser_count; j++){
            bool swap = false;
            if(!G.browser_files[i].is_dir && G.browser_files[j].is_dir) swap = true;
            else if(G.browser_files[i].is_dir == G.browser_files[j].is_dir && strcmp(G.browser_files[i].path, G.browser_files[j].path) > 0) swap = true;
            if(swap){
                BrowserFile tmp = G.browser_files[i];
                G.browser_files[i] = G.browser_files[j];
                G.browser_files[j] = tmp;
            }
        }
    }
}

static void action_find_file(const char *name){
    if(!name || !name[0]) return;
    char cmd[1024]; snprintf(cmd, sizeof(cmd), "find . -maxdepth 4 -name '*%s*' -not -path '*/.*' | head -n 100", name);
    char *o = git_run(cmd);
    if(!o || !o[0]){ ERR("No files matching %s", name); free(o); return; }
    
    G.diff_count = 0; G.diff_scroll = 0; G.diff_sel = 0; G.diff_is_summary = true;
    snprintf(G.diff_title, sizeof(G.diff_title), "Files: %s", name);
    G.diff_commit[0] = '\0';

    char *line = o;
    while(*line && G.diff_count < MAX_DIFF_LINES){
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        DiffLine *dl = &G.diff_lines[G.diff_count++]; memset(dl, 0, sizeof(*dl));
        if(len >= LINE_MAX_LEN) len = LINE_MAX_LEN-1;
        memcpy(dl->new_line, line, len); dl->new_line[len] = '\0';
        dl->type = 5;
        line = nl ? nl + 1 : line + len;
    }
    free(o);
    G.focus = FOCUS_DIFF;
}

static void action_grep(const char *pattern){
    if(!pattern || !pattern[0]) return;
    char cmd[1024]; snprintf(cmd, sizeof(cmd), "grep -rn --exclude-dir=.git --exclude=gitui '%s' . 2>/dev/null | head -n 100", pattern);
    char *o = git_run(cmd);
    if(!o || !o[0]){ ERR("No matches for %s", pattern); free(o); return; }
    
    G.diff_count = 0; G.diff_scroll = 0; G.diff_sel = 0; G.diff_is_summary = true;
    snprintf(G.diff_title, sizeof(G.diff_title), "Search: %s", pattern);
    G.diff_commit[0] = '\0';

    char *line = o;
    while(*line && G.diff_count < MAX_DIFF_LINES){
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl-line) : strlen(line);
        DiffLine *dl = &G.diff_lines[G.diff_count++]; memset(dl, 0, sizeof(*dl));
        if(len >= LINE_MAX_LEN) len = LINE_MAX_LEN-1;
        memcpy(dl->new_line, line, len); dl->new_line[len] = '\0';
        dl->type = 5;
        line = nl ? nl + 1 : line + len;
    }
    free(o);
    G.focus = FOCUS_DIFF;
}

/* FNV-1a 64-bit hash of all editor lines (joined by '\n') */
static uint64_t editor_content_hash(Editor *ed){
    uint64_t h = 14695981039346656037ULL;
    for(int i=0; i<ed->line_count; i++){
        for(const char *s=ed->lines[i]; *s; s++){ h^=(unsigned char)*s; h*=1099511628211ULL; }
        h^='\n'; h*=1099511628211ULL;
    }
    return h;
}

static void editor_free(Editor *ed){
    for(int i=0; i<ed->line_count; i++) free(ed->lines[i]);
    free(ed->lines);
    for(int i=0; i<ed->undo_top; i++) free(ed->undo_stack[i].text);
    for(int i=0; i<ed->redo_top; i++) free(ed->redo_stack[i].text);
    memset(ed, 0, sizeof(Editor));
}

/* Build a single flat string from all lines (joined with '\n') */
static char *editor_snapshot_text(Editor *ed){
    size_t total = 1;
    for(int i=0; i<ed->line_count; i++) total += strlen(ed->lines[i]) + 1;
    char *buf = malloc(total);
    size_t pos = 0;
    for(int i=0; i<ed->line_count; i++){
        size_t l = strlen(ed->lines[i]);
        memcpy(buf+pos, ed->lines[i], l); pos += l;
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

/* Restore editor lines from a flat snapshot string */
static void editor_restore_snapshot(Editor *ed, const char *text, int cy, int cx){
    for(int i=0; i<ed->line_count; i++) free(ed->lines[i]);
    ed->line_count = 0;
    const char *p = text;
    while(*p){
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl-p) : strlen(p);
        if(ed->line_count >= ed->line_cap){
            ed->line_cap = ed->line_cap ? ed->line_cap*2 : 128;
            ed->lines = realloc(ed->lines, sizeof(char*)*ed->line_cap);
        }
        char *line = malloc(len+1);
        memcpy(line, p, len); line[len] = '\0';
        ed->lines[ed->line_count++] = line;
        p = nl ? nl+1 : p+len;
    }
    if(ed->line_count == 0){
        if(ed->line_cap == 0){ ed->line_cap=128; ed->lines=malloc(sizeof(char*)*128); }
        ed->lines[ed->line_count++] = strdup("");
    }
    ed->cur_y = iclamp(cy, 0, ed->line_count-1);
    ed->cur_x = iclamp(cx, 0, (int)strlen(ed->lines[ed->cur_y]));
    ed->modified = true;
}

/* Push current state onto undo stack, clear redo */
static void editor_push_undo(Editor *ed){
    /* Clear redo */
    for(int i=0; i<ed->redo_top; i++) free(ed->redo_stack[i].text);
    ed->redo_top = 0;
    /* Push to undo (drop oldest if full) */
    if(ed->undo_top == MAX_UNDO){
        free(ed->undo_stack[0].text);
        memmove(&ed->undo_stack[0], &ed->undo_stack[1], (MAX_UNDO-1)*sizeof(HistEntry));
        ed->undo_top--;
    }
    ed->undo_stack[ed->undo_top++] = (HistEntry){editor_snapshot_text(ed), ed->cur_y, ed->cur_x};
}

static void editor_undo(Editor *ed){
    if(ed->undo_top == 0){ OK("Nothing to undo"); return; }
    /* Save current to redo */
    if(ed->redo_top < MAX_UNDO)
        ed->redo_stack[ed->redo_top++] = (HistEntry){editor_snapshot_text(ed), ed->cur_y, ed->cur_x};
    /* Restore previous */
    HistEntry e = ed->undo_stack[--ed->undo_top];
    editor_restore_snapshot(ed, e.text, e.cy, e.cx);
    free(e.text);
    /* Clear modified flag if we've returned to the saved state */
    if(editor_content_hash(ed) == ed->saved_hash) ed->modified = false;
    OK("Undo");
}

static void editor_redo(Editor *ed){
    if(ed->redo_top == 0){ OK("Nothing to redo"); return; }
    /* Save current to undo */
    if(ed->undo_top < MAX_UNDO)
        ed->undo_stack[ed->undo_top++] = (HistEntry){editor_snapshot_text(ed), ed->cur_y, ed->cur_x};
    /* Restore next */
    HistEntry e = ed->redo_stack[--ed->redo_top];
    editor_restore_snapshot(ed, e.text, e.cy, e.cx);
    free(e.text);
    /* Clear modified flag if we've returned to the saved state */
    if(editor_content_hash(ed) == ed->saved_hash) ed->modified = false;
    OK("Redo");
}

static void editor_load(const char *path){
    char real[PATH_MAX];
    if(!realpath(path, real)) snprintf(real, sizeof(real), "%s", path);
    
    for(int i=0; i<G.tab_count; i++){
        if(strcmp(G.tabs[i].path, real) == 0){
            G.tab_current = i;
            return;
        }
    }

    if(G.tab_count < MAX_TABS){
        G.tab_current = G.tab_count++;
    }
    
    Tab *t = &G.tabs[G.tab_current];
    editor_free(&t->ed);
    FILE *fp = fopen(real, "r");
    if(!fp) return;
    snprintf(t->path, sizeof(t->path), "%s", real);
    snprintf(t->ed.filename, sizeof(t->ed.filename), "%s", real);
    char buf[4096];
    while(fgets(buf, sizeof(buf), fp)){
        strtrim(buf);
        if(t->ed.line_count >= t->ed.line_cap){
            t->ed.line_cap = t->ed.line_cap ? t->ed.line_cap * 2 : 128;
            t->ed.lines = realloc(t->ed.lines, sizeof(char*) * t->ed.line_cap);
        }
        t->ed.lines[t->ed.line_count++] = strdup(buf);
    }
    fclose(fp);
    /* Record content hash so undo can detect return to clean state */
    t->ed.saved_hash = editor_content_hash(&t->ed);
}

static void editor_next_tab(void){
    if(G.tab_count > 1) G.tab_current = (G.tab_current + 1) % G.tab_count;
}
static void editor_prev_tab(void){
    if(G.tab_count > 1) G.tab_current = (G.tab_current + G.tab_count - 1) % G.tab_count;
}
static void editor_close_tab(void){
    if(G.tab_count == 0) return;
    editor_free(&G.tabs[G.tab_current].ed);
    for(int i=G.tab_current; i<G.tab_count-1; i++) G.tabs[i] = G.tabs[i+1];
    G.tab_count--;
    if(G.tab_current >= G.tab_count && G.tab_count > 0) G.tab_current = G.tab_count - 1;
    if(G.tab_count == 0) { G.editor_active = false; G.focus = G.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES; }
}

static void editor_save(void){
    Tab *t = &G.tabs[G.tab_current];
    if(!t->path[0]) return;
    FILE *fp = fopen(t->path, "w");
    if(!fp) return;
    for(int i=0; i<t->ed.line_count; i++){
        fprintf(fp, "%s\n", t->ed.lines[i]);
    }
    fclose(fp);
    t->ed.modified = false;
    t->ed.saved_hash = editor_content_hash(&t->ed); /* update clean baseline */
    OK("Saved %s", t->path);
}

static void draw_browser(int top, int h){
    int w = G.lw;
    int sx = G.sidebar_w+1;
    bool act = (G.focus == FOCUS_BROWSER);
    box_top(top, sx, w, "Files", act, NULL);
    box_sides(top, sx, w, h, act);
    box_fill(top, sx, w, h, TH->bg_panel);
    int row = top+1, lim = top+h-1;
    if(G.browser_sel < G.browser_scroll) G.browser_scroll = G.browser_sel;
    if(G.browser_sel >= G.browser_scroll + (lim-row)) G.browser_scroll = G.browser_sel - (lim-row) + 1;

    for(int i=G.browser_scroll; i<G.browser_count && row < lim; i++, row++){
        bool sel = (i == G.browser_sel && act);
        at(row, sx+1);
        if(sel){cbg(TH->bg_sel); cfg(TH->fg_sel); G.cur_bold=true; ppad(" »", 2);} 
        else {cbg(TH->bg_panel); ppad("  ", 2);}
        
        if(G.browser_files[i].is_dir){ cfg(sel?TH->fg_sel:TH->fg_accent1); ppad("📁 ", 3); }
        else { cfg(sel?TH->fg_sel:TH->fg_normal); ppad("📄 ", 3); }
        ppad(G.browser_files[i].path, w-7);
        rst();
    }
    box_bot(top+h-1, sx, w, act);
}

static Color get_token_color(const char *tok, bool is_comment, const char *ext){
    if(is_comment) return TH->fg_accent3;
    
    static const char *c_kw[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "int", "long", "register", "return", "short", "signed", "sizeof", "static",
        "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
        "bool", "inline", "restrict", "NULL", "true", "false", "size_t", "uint8_t",
        "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
        "uintptr_t", "intptr_t", "ssize_t", "off_t", "time_t", "FILE", "std", "string",
        "vector", "map", "set", "unordered_map", "unordered_set", "class", "public",
        "private", "protected", "template", "typename", "using", "namespace", "virtual",
        "override", "final", "constexpr", "noexcept", "explicit", "mutable", "friend"
    };
    static const char *js_kw[] = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "export", "extends", "finally", "for", "function",
        "if", "import", "in", "instanceof", "new", "return", "super", "switch",
        "this", "throw", "try", "typeof", "var", "void", "while", "with", "yield",
        "let", "static", "enum", "await", "async", "true", "false", "null", "undefined"
    };
    static const char *py_kw[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await", "break",
        "class", "continue", "def", "del", "elif", "else", "except", "finally",
        "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
        "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"
    };

    const char **kw = c_kw;
    int kw_count = sizeof(c_kw)/sizeof(c_kw[0]);
    
    if(ext){
        if(strcmp(ext, ".py") == 0) { kw = py_kw; kw_count = sizeof(py_kw)/sizeof(py_kw[0]); }
        else if(strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) { kw = js_kw; kw_count = sizeof(js_kw)/sizeof(js_kw[0]); }
        else if(strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cc") == 0 || strcmp(ext, ".h") == 0) { kw = c_kw; kw_count = sizeof(c_kw)/sizeof(c_kw[0]); }
    }

    if(tok[0] == '\'' || tok[0] == '"') return TH->fg_unstaged;
    if(isdigit((unsigned char)tok[0])) return TH->fg_accent2;
    if(tok[0] == '#') return TH->fg_accent1;

    for(int i=0; i<kw_count; i++) if(strcmp(tok, kw[i]) == 0) return TH->fg_accent1;
    
    /* Check for types (simple heuristic) */
    if(ext && (strcmp(ext, ".c")==0 || strcmp(ext, ".h")==0 || strcmp(ext, ".cpp")==0)){
        if(strstr(tok, "_t") || (isupper(tok[0]) && strlen(tok) > 2)) return TH->fg_staged;
    }

    return TH->fg_normal;
}

static const char *get_lang_name(const char *path){
    const char *ext = strrchr(path, '.');
    if(!ext) return "Plain Text";
    if(strcmp(ext, ".c") == 0) return "C";
    if(strcmp(ext, ".h") == 0) return "C Header";
    if(strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cc") == 0) return "C++";
    if(strcmp(ext, ".py") == 0) return "Python";
    if(strcmp(ext, ".js") == 0) return "JavaScript";
    if(strcmp(ext, ".ts") == 0) return "TypeScript";
    if(strcmp(ext, ".md") == 0) return "Markdown";
    return "Plain Text";
}

static void draw_scrollbar(int r, int c, int h, int total, int vis, int scroll, bool active){
    if(total <= vis || h <= 0) return;
    int bh = imax(1, (vis * h) / total);
    int bpos = ((scroll * (h - bh)) / (total - vis));
    for(int i=0; i<h; i++){
        at(r+i, c);
        if(i >= bpos && i < bpos + bh){
            cfg(active ? TH->fg_accent1 : TH->fg_dim);
            put_cell(r+i, c, "█");
        } else {
            cfg(TH->fg_dim);
            put_cell(r+i, c, "│");
        }
    }
    rst();
}

static void draw_editor(int top, int rx, int rw, int h){
    if(G.tab_count == 0) {
        box_top(top, rx, rw, "Editor", (G.focus == FOCUS_EDITOR), NULL);
        box_sides(top, rx, rw, h, (G.focus == FOCUS_EDITOR));
        box_fill(top, rx, rw, h, TH->bg_base);
        at(top+h/2, rx+rw/2-10); cfg(TH->fg_dim); ppad("(no files open)", 15);
        box_bot(top+h-1, rx, rw, (G.focus == FOCUS_EDITOR));
        return;
    }
    Tab *t = &G.tabs[G.tab_current];
    Editor *ed = &t->ed;
    bool act = (G.focus == FOCUS_EDITOR);
    
    char title[512];
    snprintf(title, sizeof(title), " %s ", t->path);
    box_top(top, rx, rw, "Editor", act, " [Save] ");
    box_sides(top, rx, rw, h, act);
    box_fill(top, rx, rw, h, TH->bg_base);

    /* Draw Tab Bar for Editor inside the box */
    at(top+1, rx+1); 
    cbg(TH->bg_panel);
    for(int i=0; i<rw-2; i++) put_cell(top+1, rx+1+i, " ");
    
    int cur_tab_x = rx+1;
    for(int i=0; i<G.tab_count; i++){
        bool cur = (i == G.tab_current);
        at(top+1, cur_tab_x);
        if(cur) { cbg(TH->bg_base); cfg(TH->fg_bright); G.cur_bold=true; }
        else { cbg(TH->bg_panel); cfg(TH->fg_dim); }
        
        char *fname = strrchr(G.tabs[i].path, '/');
        fname = fname ? fname + 1 : G.tabs[i].path;
        
        char tbuf[128]; 
        int tab_w = snprintf(tbuf, sizeof(tbuf), "  %s%s  ", fname, G.tabs[i].ed.modified?"*":"");
        ppad(tbuf, tab_w);
        
        /* Save tab boundaries for clicking (separate from view tab_x) */
        G.ed_tab_x[i] = cur_tab_x;
        if(i == G.tab_count - 1) G.ed_tab_x[i+1] = cur_tab_x + tab_w;

        cur_tab_x += tab_w;
        if(!cur) { cfg(TH->fg_dim); put_cell(top+1, cur_tab_x-1, "│"); }
        rst();
    }
    
    int row = top+2, lim = top+h-1, vis = lim-row;
    if(ed->cur_y < ed->scroll_y) ed->scroll_y = ed->cur_y;
    if(ed->cur_y >= ed->scroll_y + vis) ed->scroll_y = ed->cur_y - vis + 1;

    int vis_w = rw - 10;
    if(ed->cur_x < ed->scroll_x) ed->scroll_x = ed->cur_x;
    if(ed->cur_x >= ed->scroll_x + vis_w) ed->scroll_x = ed->cur_x - vis_w + 1;

    for(int i=ed->scroll_y; i<ed->line_count && row < lim - 1; i++, row++){
        at(row, rx+1);
        cfg(TH->fg_linenum);
        char lno[16]; snprintf(lno, sizeof(lno), "%4d ", i+1); ppad(lno, 5);
        cfg(TH->fg_dim); ppad("│", 1);
        
        /* Improved tokenizing for highlighting with scroll_x support */
        char *full_line = ed->lines[i];
        int full_len = (int)strlen(full_line);
        
        char line_buf[1024];
        int start = ed->scroll_x;
        int len = 0;
        if(start < full_len){
            len = imin(full_len - start, 1023);
            len = imin(len, vis_w);
            memcpy(line_buf, full_line + start, len);
        }
        line_buf[len] = '\0';

        const char *ext = strrchr(t->path, '.');
        int cur_c = rx + 7;
        int j = 0;
        while(j < len && cur_c < rx+rw-3){
            int real_j = j + start;
            char c = line_buf[j];
            
            // Comment
            if((c == '/' && j+1 < len && line_buf[j+1] == '/') || (c == '#' && (ext && strcmp(ext, ".py")==0))){
                cfg(TH->fg_accent3);
                while(j < len && cur_c < rx+rw-3){
                    if(is_ed_selected(i, j + start)) cbg(TH->bg_sel); else cbg(TH->bg_base);
                    char cs[2] = {line_buf[j++], 0};
                    put_cell(row, cur_c++, cs);
                }
                break;
            }
            
            // String
            if(c == '"' || c == '\''){
                char quote = c;
                cfg(TH->fg_unstaged);
                if(is_ed_selected(i, j + start)) cbg(TH->bg_sel); else cbg(TH->bg_base);
                char cs[2] = {line_buf[j++], 0};
                put_cell(row, cur_c++, cs);
                while(j < len && cur_c < rx+rw-3){
                    if(is_ed_selected(i, j + start)) cbg(TH->bg_sel); else cbg(TH->bg_base);
                    char sc = line_buf[j++];
                    char scs[2] = {sc, 0};
                    put_cell(row, cur_c++, scs);
                    if(sc == quote && (j < 2 || line_buf[j-2] != '\\')) break;
                }
                continue;
            }
            
            // Token
            if(isalnum((unsigned char)c) || c == '_' || c == '#'){
                char buf[256]; int bi = 0;
                int tok_start = j;
                while(j < len && (isalnum((unsigned char)line_buf[j]) || line_buf[j] == '_' || line_buf[j] == '#') && bi < 255){
                    buf[bi++] = line_buf[j++];
                }
                buf[bi] = '\0';
                Color tcol = get_token_color(buf, false, ext);
                for(int k=0; k<bi; k++){
                    if(cur_c >= rx+rw-3) break;
                    if(is_ed_selected(i, tok_start + k + start)) cbg(TH->bg_sel); else cbg(TH->bg_base);
                    cfg(tcol);
                    char tcs[2] = {buf[k], 0};
                    put_cell(row, cur_c++, tcs);
                }
                continue;
            }
            
            // Punctuation / Space
            if(is_ed_selected(i, j + start)) cbg(TH->bg_sel); else cbg(TH->bg_base);
            cfg(ispunct((unsigned char)c) ? TH->fg_dim : TH->fg_normal);
            char pcs[2] = {line_buf[j++], 0};
            put_cell(row, cur_c++, pcs);
        }

        /* Cursor: solid block when focused, underline when not focused */
        if(i == ed->cur_y){
            int cursor_screen_x = rx+7+ed->cur_x - ed->scroll_x;
            if(cursor_screen_x >= rx+7 && cursor_screen_x < rx+rw-3){
                at(row, cursor_screen_x);
                char c = (ed->lines[i] && ed->lines[i][ed->cur_x]) ? ed->lines[i][ed->cur_x] : ' ';
                char cs[2] = {c, 0};
                if(act){
                    cbg(TH->fg_accent1); cfg(TH->bg_base); G.cur_bold = true;
                } else {
                    /* Dim underline cursor when not focused */
                    G.cur_under = true; cfg(TH->fg_dim);
                }
                put_cell(row, cursor_screen_x, cs);
                G.cur_bold = false; G.cur_under = false;
            }
        }
        rst();
    }
    while(row < lim - 1){
        at(row, rx+1); cbg(TH->bg_base); 
        for(int i=0; i<rw-2; i++) put_cell(row, rx+1+i, " ");
        row++;
    }

    /* Minimap Scrollbar (3-wide, matches diff style) */
    {
        int vis_rows = h - 4;
        if(ed->line_count > vis_rows && vis_rows > 2){
            int bh = imax(1, (vis_rows * vis_rows) / ed->line_count);
            int maxsc_ed = imax(1, ed->line_count - vis_rows);
            int bpos = (ed->scroll_y * (vis_rows - bh)) / maxsc_ed;
            G.ed_sc_x = rx + rw - 3;
            G.ed_sc_y = top + 2;
            G.ed_sc_h = vis_rows;
            G.ed_sc_total = ed->line_count;
            G.ed_sc_vis = vis_rows;

            /* Minimap markers: 1=cursor line, 2=selection */
            char markers[2048] = {0};
            if(vis_rows < 2048){
                int cr = (ed->cur_y * vis_rows) / ed->line_count;
                if(cr >= 0 && cr < vis_rows) markers[cr] |= 1;
                if(G.ed_selecting){
                    int sy1 = imin(G.ed_sel_start_y, G.ed_sel_end_y);
                    int sy2 = imax(G.ed_sel_start_y, G.ed_sel_end_y);
                    for(int i = sy1; i <= sy2 && i < ed->line_count; i++){
                        int r = (i * vis_rows) / ed->line_count;
                        if(r >= 0 && r < vis_rows) markers[r] |= 2;
                    }
                }
            }

            for(int r = 0; r < vis_rows; r++){
                bool thumb = (r >= bpos && r < bpos + bh);
                for(int sw = 0; sw < 3; sw++){
                    at(top+2+r, rx+rw-1-sw);
                    if(thumb){
                        if(G.dragging_ed_sc) cfg(TH->fg_sel);
                        else if(G.last_mx >= rx+rw-3) cfg(TH->fg_accent1);
                        else cfg(TH->fg_accent2);
                        put_cell(top+2+r, rx+rw-1-sw, "█");
                    } else {
                        if(markers[r] & 2){ cfg(TH->bg_sel); put_cell(top+2+r, rx+rw-1-sw, "▒"); }
                        else if(markers[r] & 1){ cfg(TH->fg_accent1); put_cell(top+2+r, rx+rw-1-sw, "▒"); }
                        else { cfg(TH->fg_dim); put_cell(top+2+r, rx+rw-1-sw, sw==0?"│":" "); }
                    }
                }
            }
            rst();
        } else {
            G.ed_sc_h = 0;
        }
    }
    
    /* Horizontal scrollbar line */
    at(top+h-2, rx+1); cbg(TH->bg_panel);
    for(int i=0; i<rw-2; i++) put_cell(top+h-2, rx+1+i, " ");
    int max_line_w = 0;
    for(int i=0; i<ed->line_count; i++) { int l = (int)strlen(ed->lines[i]); if(l > max_line_w) max_line_w = l; }
    if(max_line_w > vis_w){
        int h_bh = imax(2, (vis_w * (rw-10)) / max_line_w);
        int h_bpos = (ed->scroll_x * (rw-10 - h_bh)) / (max_line_w - vis_w);
        at(top+h-2, rx+7+h_bpos);
        cfg(act?TH->fg_accent1:TH->fg_dim);
        for(int i=0; i<h_bh; i++) put_cell(top+h-2, rx+7+h_bpos+i, "━");
    }

    /* Editor Status Line */
    at(top+h-2, rx+1);
    if(act){ cfg(TH->fg_bright); cbg(TH->bg_panel); G.cur_bold = true; }
    else cfg(TH->fg_dim);
    char sbuf[256];
    if(G.ed_selecting)
        snprintf(sbuf, sizeof(sbuf), " Ln %d, Col %d  [SEL] ", ed->cur_y+1, ed->cur_x+1);
    else
        snprintf(sbuf, sizeof(sbuf), " Ln %d, Col %d  ", ed->cur_y+1, ed->cur_x+1);
    ppad(sbuf, (int)strlen(sbuf));
    at(top+h-2, rx+rw-20);
    if(!act) cfg(TH->fg_dim); else { cfg(TH->fg_dim); cbg(TH->bg_panel); G.cur_bold = false; }
    snprintf(sbuf, sizeof(sbuf), " [UTF-8]  %s ", get_lang_name(t->path));
    ppad(sbuf, (int)strlen(sbuf));
    rst();

    box_bot(top+h-1, rx, rw, act);
}

static void draw_sidebar(void){
    int h = G.rows - 2;
    int top = 2;
    cfg(TH->fg_dim); cbg(TH->bg_panel);
    for(int r=top; r<top+h; r++){
        at(r, 1); ppad(" ", G.sidebar_w);
    }
    
    /* Explorer Icon (File Browser) */
    at(top+1, 1);
    bool explorer_act = G.browser_active;
    if(explorer_act){ cfg(TH->bg_panel); cbg(TH->fg_accent1); G.cur_bold=true; ppad(" ▶ BROWSER", 10); } 
    else { cfg(TH->fg_dim); cbg(TH->bg_panel); ppad("   BROWSER", 10); }
    
    /* Source Control Icon (Git) */
    at(top+3, 1);
    bool git_act = !G.browser_active;
    if(git_act){ cfg(TH->bg_panel); cbg(TH->fg_accent1); G.cur_bold=true; ppad(" ▶ GIT    ", 10); } 
    else { cfg(TH->fg_dim); cbg(TH->bg_panel); ppad("   GIT    ", 10); }
    
    rst();
    /* Divider */
    cfg(TH->fg_dim);
    for(int r=top; r<top+h; r++){
        at(r, G.sidebar_w); put_cell(r, G.sidebar_w, "│");
    }
    rst();
}

static void draw(void){
    layout();
    buf_clear(&G.back);
    rst();
    draw_tabbar();
    draw_sidebar();

    int ct=2, ch=G.rows-2;
    switch(G.current_view){
    case VIEW_STATUS:
        if(G.browser_active){
            draw_browser(ct, ch);
        } else {
            if(G.lh_chg > 2) draw_changes(ct, G.lh_chg);
            if(G.lh_gph > 2) draw_graph(ct+G.lh_chg, G.lh_gph);
        }
        
        if(G.editor_active){
            draw_editor(ct, G.rx, G.rw, ch);
        } else {
            draw_diff(ct, G.rx, G.rw, ch);
        }
        break;
    case VIEW_LOG:{
        draw_log(ct, G.lh_log);
        if(G.editor_active) draw_editor(ct+G.lh_log, 1, G.cols, G.dh_log);
        else draw_diff(ct+G.lh_log, 1, G.cols, G.dh_log);
        break;
    }
    case VIEW_BRANCHES: draw_branches(ct,ch); break;
    case VIEW_STASH:    draw_stash(ct,ch);    break;
    case VIEW_EDITOR:
        draw_browser(ct, ch);
        draw_editor(ct, G.rx, G.rw, ch);
        break;
    case VIEW_HELP:     draw_help(ct,ch);     break;
    default: break;
    }
    draw_statusbar();
    draw_cli();
    draw_prompt_overlay();
    draw_dividers();
    draw_menu();
    
    draw_flush();
}

/* ================================================================
   INPUT READING
================================================================ */
static Key read_key(void){
    Key k={KEY_NONE,0,{0,0,0,false,false,false}};
    unsigned char buf[32];
    int n=(int)read(STDIN_FILENO,buf,sizeof(buf));
    if(n<=0)return k;

    if(buf[0]==0x1b){
        if(n==1){k.type=KEY_ESC;return k;}
        if(buf[1]=='['){
            if(n==3){
                switch(buf[2]){
                case 'A':k.type=KEY_UP;return k;
                case 'B':k.type=KEY_DOWN;return k;
                case 'C':k.type=KEY_RIGHT;return k;
                case 'D':k.type=KEY_LEFT;return k;
                case 'H':k.type=KEY_HOME;return k;
                case 'F':k.type=KEY_END;return k;
                case 'Z':k.type=KEY_SHIFT_TAB;return k;
                }
            }
            if(n==6 && buf[2]=='1' && buf[3]==';' && buf[4]=='2'){
                switch(buf[5]){
                case 'A':k.type=KEY_SHIFT_UP;return k;
                case 'B':k.type=KEY_SHIFT_DOWN;return k;
                case 'C':k.type=KEY_SHIFT_RIGHT;return k;
                case 'D':k.type=KEY_SHIFT_LEFT;return k;
                }
            }
            if(n>=4&&buf[n-1]=='~'){
                switch(buf[2]){
                case '1':k.type=KEY_HOME;return k;
                case '3':k.type=KEY_DEL;return k;
                case '4':k.type=KEY_END;return k;
                case '5':k.type=KEY_PGUP;return k;
                case '6':k.type=KEY_PGDN;return k;
                }
            }
            if(buf[2]=='<'){
                int btn=0,col=0,row=0; char end='M';
                sscanf((char*)buf+3,"%d;%d;%d%c",&btn,&col,&row,&end);
                k.type=KEY_MOUSE;
                k.mouse.btn=btn; k.mouse.col=col; k.mouse.row=row;
                k.mouse.release=(end=='m');
                k.mouse.shift=(btn&4)!=0; k.mouse.ctrl=(btn&16)!=0;
                return k;
            }
            if(buf[2]=='M'&&n>=6){
                k.type=KEY_MOUSE;
                k.mouse.btn=buf[3]-32; k.mouse.col=buf[4]-32; k.mouse.row=buf[5]-32;
                return k;
            }
        }
        if(buf[1]=='O'){
            switch(buf[2]){case 'P':k.type=KEY_F1;return k;case 'Q':k.type=KEY_F2;return k;
                           case 'R':k.type=KEY_F3;return k;case 'S':k.type=KEY_F4;return k;}
        }
        return k;
    }
    switch(buf[0]){
    case '\r':case '\n':k.type=KEY_ENTER;return k;
    case 127:case 8:k.type=KEY_BACKSPACE;return k;
    case '\t':k.type=KEY_TAB;return k;
    case 1:k.type=KEY_CTRL_A;return k;
    case 2:k.type=KEY_CTRL_B;return k;
    case 3:k.type=KEY_CTRL_C;return k;
    case 4:k.type=KEY_CTRL_D;return k;
    case 5:k.type=KEY_CTRL_E;return k;
    case 6:k.type=KEY_CTRL_F;return k;
    case 7:k.type=KEY_CTRL_G;return k;
    case 11:k.type=KEY_CTRL_K;return k;
    case 12:k.type=KEY_CTRL_L;return k;
    case 14:k.type=KEY_CTRL_N;return k;
    case 16:k.type=KEY_CTRL_P;return k;
    case 17:k.type=KEY_CTRL_Q;return k;
    case 18:k.type=KEY_CTRL_R;return k;
    case 19:k.type=KEY_CTRL_S;return k;
    case 21:k.type=KEY_CTRL_U;return k;
    case 22:k.type=KEY_CTRL_V;return k;
    case 23:k.type=KEY_CTRL_W;return k;
    case 24:k.type=KEY_CTRL_X;return k;
    case 25:k.type=KEY_CTRL_Y;return k;
    case 26:k.type=KEY_CTRL_Z;return k;
    }
    if(buf[0]>=32&&buf[0]<127){k.type=KEY_CHAR;k.ch=(char)buf[0];return k;}
    return k;
}

/* ================================================================
   PROMPT
================================================================ */
static void prompt_start(const char *label,void(*cb)(const char*),bool obs){
    G.in_prompt=true; snprintf(G.prompt_label,sizeof(G.prompt_label),"%s",label);
    G.prompt_buf[0]='\0'; G.prompt_cursor=0; G.prompt_cb=cb; G.prompt_obscure=obs;
}
static void prompt_ins(char c){
    int len=(int)strlen(G.prompt_buf);
    if(len+1<INPUT_MAX){
        memmove(&G.prompt_buf[G.prompt_cursor+1],&G.prompt_buf[G.prompt_cursor],len-G.prompt_cursor+1);
        G.prompt_buf[G.prompt_cursor++]=c;
    }
}
static void prompt_bksp(void){
    if(!G.prompt_cursor)return;
    int len=(int)strlen(G.prompt_buf);
    memmove(&G.prompt_buf[G.prompt_cursor-1],&G.prompt_buf[G.prompt_cursor],len-G.prompt_cursor+1);
    G.prompt_cursor--;
}
static void prompt_del(void){
    int len=(int)strlen(G.prompt_buf);
    if(G.prompt_cursor>=len)return;
    memmove(&G.prompt_buf[G.prompt_cursor],&G.prompt_buf[G.prompt_cursor+1],len-G.prompt_cursor);
}
static void prompt_confirm(void){
    G.in_prompt=false;
    void(*cb)(const char*)=G.prompt_cb;
    char copy[INPUT_MAX]; snprintf(copy,sizeof(copy),"%s",G.prompt_buf);
    G.prompt_buf[0]='\0'; G.prompt_cursor=0; G.prompt_cb=NULL;
    if(cb)cb(copy);
}
static void prompt_cancel(void){G.in_prompt=false;G.prompt_buf[0]='\0';G.prompt_cursor=0;G.prompt_cb=NULL;OK("Cancelled");}
static void handle_prompt_key(Key k){
    switch(k.type){
    case KEY_ENTER:    prompt_confirm();break;
    case KEY_ESC:      prompt_cancel();break;
    case KEY_BACKSPACE:prompt_bksp();break;
    case KEY_DEL:      prompt_del();break;
    case KEY_LEFT:     if(G.prompt_cursor>0)G.prompt_cursor--;break;
    case KEY_RIGHT:    if(G.prompt_buf[G.prompt_cursor])G.prompt_cursor++;break;
    case KEY_HOME:case KEY_CTRL_A:G.prompt_cursor=0;break;
    case KEY_END:      G.prompt_cursor=(int)strlen(G.prompt_buf);break;
    case KEY_CTRL_U:   G.prompt_buf[0]='\0';G.prompt_cursor=0;break;
    case KEY_CTRL_W:{
        while(G.prompt_cursor>0&&G.prompt_buf[G.prompt_cursor-1]==' ')prompt_bksp();
        while(G.prompt_cursor>0&&G.prompt_buf[G.prompt_cursor-1]!=' ')prompt_bksp();
        break;
    }
    case KEY_CHAR:prompt_ins(k.ch);break;
    default:break;
    }
}

/* ================================================================
   GIT ACTIONS
================================================================ */
static void action_new_file(const char *name){
    if(!name || !name[0]) return;
    char full[1024]; snprintf(full, sizeof(full), "%s/%s", G.browser_path, name);
    FILE *fp = fopen(full, "w");
    if(fp){ fclose(fp); load_browser(G.browser_path); OK("Created %s", name); }
    else ERR("Failed to create %s", name);
}

static void action_delete_file(void){
    if(G.browser_count == 0) return;
    BrowserFile *f = &G.browser_files[G.browser_sel];
    if(strcmp(f->path, "..") == 0) return;
    char full[1024]; snprintf(full, sizeof(full), "%s/%s", G.browser_path, f->path);
    if(remove(full) == 0){ load_browser(G.browser_path); OK("Deleted %s", f->path); }
    else ERR("Failed to delete %s", f->path);
}

static void menu_new_file(void){ prompt_start("New file name:", action_new_file, false); }
static void menu_delete_file(void){ action_delete_file(); }

static void action_stage(void){
    if(!G.file_count)return;
    GitFile *f=&G.files[G.file_sel];
    if(f->staged){git_exec("git reset HEAD -- '%s'",f->path);OK("Unstaged: %s",f->path);}
    else{
        if(f->st==FS_DELETED)git_exec("git rm -- '%s'",f->path);
        else git_exec("git add -- '%s'",f->path);
        OK("Staged: %s",f->path);
    }
    load_status(); update_diff();
}
static void action_stage_all(void){git_exec("git add -A");OK("Staged all");load_status();update_diff();}
static void action_unstage_all(void){git_exec("git reset HEAD");OK("Unstaged all");load_status();update_diff();}
static void action_discard(void){
    if(!G.file_count)return;
    GitFile *f=&G.files[G.file_sel];
    if(f->st==FS_UNTRACKED){git_exec("rm -f -- '%s'",f->path);OK("Removed: %s",f->path);}
    else{git_exec("git checkout -- '%s'",f->path);OK("Discarded: %s",f->path);}
    load_status(); update_diff();
}

static void do_commit(const char *msg){
    if(!msg||!msg[0]){ERR("Empty message");return;}
    char tmp[64];snprintf(tmp,sizeof(tmp),"/tmp/gitui_msg_XXXXXX");
    int fd=mkstemp(tmp);if(fd<0){ERR("mkstemp failed");return;}
    ssize_t w=write(fd,msg,strlen(msg));(void)w;close(fd);
    int r=git_exec("git commit -F '%s'",tmp);unlink(tmp);
    if(r==0){OK("Committed: %.60s",msg);load_status();load_log();}else ERR("Commit failed");
}
static void action_commit(void){
    int s=0;for(int i=0;i<G.file_count;i++)if(G.files[i].staged)s++;
    if(!s){ERR("Nothing staged");return;}
    prompt_start("Commit message:",do_commit,false);
}
static void do_amend(const char *msg){
    int r;
    if(!msg||!msg[0]){r=git_exec("git commit --amend --no-edit");}
    else{char tmp[64];snprintf(tmp,sizeof(tmp),"/tmp/gitui_amend_XXXXXX");
         int fd=mkstemp(tmp);if(fd<0){ERR("mkstemp");return;}
         ssize_t w=write(fd,msg,strlen(msg));(void)w;close(fd);
         r=git_exec("git commit --amend -F '%s'",tmp);unlink(tmp);}
    if(r==0){OK("Amended");load_log();}else ERR("Amend failed");
}
static void action_amend(void){prompt_start("Amend message (empty=keep):",do_amend,false);}
static void action_push(void){OK("Pushing...");draw();int r=git_exec("git push");if(r==0)OK("Pushed");else ERR("Push failed");load_log();}
static void action_pull(void){OK("Pulling...");draw();int r=git_exec("git pull");if(r==0)OK("Pulled");else ERR("Pull failed");reload_all();}
static void do_stash(const char *msg){
    char cmd[512];
    if(msg&&msg[0])snprintf(cmd,sizeof(cmd),"git stash push -m '%s'",msg);
    else snprintf(cmd,sizeof(cmd),"git stash push");
    int r=git_exec("%s",cmd);
    if(r==0){OK("Stashed");load_status();load_stash();}else ERR("Stash failed");
}
static void action_stash(void){prompt_start("Stash message (optional):",do_stash,false);}
static void action_checkout(void){
    if(!G.branch_count)return;
    GitBranch *b=&G.branches[G.branch_sel];
    int r=b->is_remote?git_exec("git checkout -t '%s'",b->name):git_exec("git checkout '%s'",b->name);
    if(r==0){OK("Checked out: %s",b->name);load_branch();reload_all();}else ERR("Checkout failed");
}
static void do_new_branch(const char *name){
    if(!name||!name[0]){ERR("Name required");return;}
    int r=git_exec("git checkout -b '%s'",name);
    if(r==0){OK("Created: %s",name);load_branch();load_branches();}else ERR("Branch failed");
}
static void action_new_branch(void){prompt_start("New branch name:",do_new_branch,false);}
static void action_delete_branch(void){
    if(!G.branch_count)return;
    GitBranch *b=&G.branches[G.branch_sel];
    if(b->is_current){ERR("Cannot delete current branch");return;}
    int r;
    if(b->is_remote){
        char rn[64]="origin",bn[128];snprintf(bn,sizeof(bn),"%s",b->name);
        char *sl=strchr(bn,'/');
        if(sl){*sl='\0';snprintf(rn,sizeof(rn),"%s",bn);r=git_exec("git push '%s' --delete '%s'",rn,sl+1);}
        else r=git_exec("git push origin --delete '%s'",bn);
    }else r=git_exec("git branch -D '%s'",b->name);
    if(r==0){OK("Deleted: %s",b->name);load_branches();}else ERR("Delete failed");
}
static void action_apply_stash(void){
    if(!G.stash_count)return;
    int r=git_exec("git stash apply stash@{%d}",G.stashes[G.stash_sel].index);
    if(r==0){OK("Applied stash@{%d}",G.stashes[G.stash_sel].index);load_status();}else ERR("Apply failed");
}
static void action_pop_stash(void){
    if(!G.stash_count)return;
    int r=git_exec("git stash pop stash@{%d}",G.stashes[G.stash_sel].index);
    if(r==0){OK("Popped stash@{%d}",G.stashes[G.stash_sel].index);load_status();load_stash();}else ERR("Pop failed");
}
static void action_drop_stash(void){
    if(!G.stash_count)return;
    int r=git_exec("git stash drop stash@{%d}",G.stashes[G.stash_sel].index);
    if(r==0){OK("Dropped stash@{%d}",G.stashes[G.stash_sel].index);load_stash();}else ERR("Drop failed");
}

/* ================================================================
   SELECTION HELPER (Vi-count aware)
================================================================ */
static void msel(int *sel,int *scr,int cnt,int d,int vis, bool is_graph){
    *sel=iclamp(*sel+d,0,cnt>0?cnt-1:0);
    if(*sel<*scr)*scr=*sel;
    if(*sel>=*scr+vis)*scr=*sel-vis+1;
    if(is_graph && cnt > 0){
        G.commits[*sel].expanded = true;
        fetch_commit_files(*sel);
        sync_graph_preview();
    }
}

static void editor_ensure_line(void){
    if(CUR_ED.line_count == 0){
        if(CUR_ED.line_cap == 0){ CUR_ED.line_cap=128; CUR_ED.lines=malloc(sizeof(char*)*128); }
        CUR_ED.lines[CUR_ED.line_count++] = strdup("");
    }
}

static void editor_insert_char(char c){
    editor_push_undo(&G.tabs[G.tab_current].ed);
    editor_ensure_line();
    char *line = CUR_ED.lines[CUR_ED.cur_y];
    int len = (int)strlen(line);
    int cx = iclamp(CUR_ED.cur_x, 0, len);
    char *n = malloc(len + 2);
    memcpy(n, line, cx);
    n[cx] = c;
    memcpy(n + cx + 1, line + cx, len - cx + 1); /* includes null */
    free(line);
    CUR_ED.lines[CUR_ED.cur_y] = n;
    CUR_ED.cur_x = cx + 1;
    CUR_ED.modified = true;
}

static void editor_backspace(void){
    if(CUR_ED.line_count == 0) return;
    if(CUR_ED.cur_y >= CUR_ED.line_count) CUR_ED.cur_y = CUR_ED.line_count - 1;
    editor_push_undo(&G.tabs[G.tab_current].ed);
    char *line = CUR_ED.lines[CUR_ED.cur_y];
    int len = (int)strlen(line);
    int cx = iclamp(CUR_ED.cur_x, 0, len);
    if(cx > 0){
        memmove(line + cx - 1, line + cx, len - cx + 1);
        CUR_ED.cur_x = cx - 1;
        CUR_ED.modified = true;
    } else if(CUR_ED.cur_y > 0){
        char *prev = CUR_ED.lines[CUR_ED.cur_y - 1];
        char *curr = CUR_ED.lines[CUR_ED.cur_y];
        int plen = (int)strlen(prev);
        int clen = (int)strlen(curr);
        char *n = malloc(plen + clen + 1);
        memcpy(n, prev, plen);
        memcpy(n + plen, curr, clen + 1);
        free(prev); free(curr);
        CUR_ED.lines[CUR_ED.cur_y - 1] = n;
        for(int i=CUR_ED.cur_y; i<CUR_ED.line_count-1; i++) CUR_ED.lines[i] = CUR_ED.lines[i+1];
        CUR_ED.line_count--;
        CUR_ED.cur_y--;
        CUR_ED.cur_x = plen;
        CUR_ED.modified = true;
    }
}

/* Delete the character AT the cursor (forward delete) */
static void editor_delete_forward(void){
    if(CUR_ED.line_count == 0) return;
    editor_push_undo(&G.tabs[G.tab_current].ed);
    char *line = CUR_ED.lines[CUR_ED.cur_y];
    int len = (int)strlen(line);
    int cx = iclamp(CUR_ED.cur_x, 0, len);
    if(cx < len){
        memmove(line + cx, line + cx + 1, len - cx);
        CUR_ED.modified = true;
    } else if(CUR_ED.cur_y < CUR_ED.line_count - 1){
        /* Merge current line with next */
        char *next = CUR_ED.lines[CUR_ED.cur_y + 1];
        int nlen = (int)strlen(next);
        char *n = malloc(len + nlen + 1);
        memcpy(n, line, len);
        memcpy(n + len, next, nlen + 1);
        free(line); free(next);
        CUR_ED.lines[CUR_ED.cur_y] = n;
        for(int i=CUR_ED.cur_y+1; i<CUR_ED.line_count-1; i++) CUR_ED.lines[i] = CUR_ED.lines[i+1];
        CUR_ED.line_count--;
        CUR_ED.modified = true;
    }
}

static void editor_newline(void){
    editor_ensure_line();
    editor_push_undo(&G.tabs[G.tab_current].ed);
    char *line = CUR_ED.lines[CUR_ED.cur_y];
    int len = (int)strlen(line);
    int cx = iclamp(CUR_ED.cur_x, 0, len);
    char *next = strdup(line + cx);
    line[cx] = '\0';
    if(CUR_ED.line_count >= CUR_ED.line_cap){
        CUR_ED.line_cap = CUR_ED.line_cap ? CUR_ED.line_cap * 2 : 128;
        CUR_ED.lines = realloc(CUR_ED.lines, sizeof(char*) * CUR_ED.line_cap);
    }
    for(int i=CUR_ED.line_count; i > CUR_ED.cur_y + 1; i--) CUR_ED.lines[i] = CUR_ED.lines[i-1];
    CUR_ED.lines[CUR_ED.cur_y + 1] = next;
    CUR_ED.line_count++;
    CUR_ED.cur_y++;
    CUR_ED.cur_x = 0;
    CUR_ED.modified = true;
}

static void editor_goto_line(const char *lstr){
    int l = atoi(lstr);
    if(l > 0 && l <= CUR_ED.line_count){
        CUR_ED.cur_y = l - 1;
        CUR_ED.cur_x = 0;
        OK("Jumped to line %d", l);
    } else ERR("Invalid line number");
}

static void editor_find(const char *pattern){
    if(!pattern || !pattern[0]) return;
    for(int i=CUR_ED.cur_y; i<CUR_ED.line_count; i++){
        char *line = CUR_ED.lines[i];
        char *pos = strstr(i == CUR_ED.cur_y ? line + CUR_ED.cur_x + 1 : line, pattern);
        if(pos){
            CUR_ED.cur_y = i;
            CUR_ED.cur_x = (int)(pos - line);
            OK("Found: %s", pattern);
            return;
        }
    }
    for(int i=0; i<CUR_ED.cur_y; i++){
        char *line = CUR_ED.lines[i];
        char *pos = strstr(line, pattern);
        if(pos){
            CUR_ED.cur_y = i;
            CUR_ED.cur_x = (int)(pos - line);
            OK("Found (wrapped): %s", pattern);
            return;
        }
    }
    ERR("Not found: %s", pattern);
}

static void editor_paste(void){
    if(!G.clipboard) return;
    /* Push one undo entry for the whole paste, then insert directly without per-char undo */
    editor_push_undo(&G.tabs[G.tab_current].ed);
    editor_ensure_line();
    for(int i=0; G.clipboard[i]; i++){
        char c = G.clipboard[i];
        if(c == '\n'){
            /* newline inline (no extra undo push) */
            char *line = CUR_ED.lines[CUR_ED.cur_y];
            int len = (int)strlen(line);
            int cx = iclamp(CUR_ED.cur_x, 0, len);
            char *next = strdup(line + cx);
            line[cx] = '\0';
            if(CUR_ED.line_count >= CUR_ED.line_cap){
                CUR_ED.line_cap = CUR_ED.line_cap ? CUR_ED.line_cap*2 : 128;
                CUR_ED.lines = realloc(CUR_ED.lines, sizeof(char*)*CUR_ED.line_cap);
            }
            for(int j=CUR_ED.line_count; j>CUR_ED.cur_y+1; j--) CUR_ED.lines[j]=CUR_ED.lines[j-1];
            CUR_ED.lines[CUR_ED.cur_y+1]=next; CUR_ED.line_count++; CUR_ED.cur_y++; CUR_ED.cur_x=0;
        } else {
            char *line = CUR_ED.lines[CUR_ED.cur_y];
            int len = (int)strlen(line);
            int cx = iclamp(CUR_ED.cur_x, 0, len);
            char *n = malloc(len + 2);
            memcpy(n, line, cx); n[cx]=c; memcpy(n+cx+1, line+cx, len-cx+1);
            free(line); CUR_ED.lines[CUR_ED.cur_y]=n; CUR_ED.cur_x=cx+1;
        }
    }
    CUR_ED.modified = true;
}

static void action_copy_editor_selection(void){
    if(!G.ed_selecting) return;
    int sy = G.ed_sel_start_y, sx = G.ed_sel_start_x;
    int ey = G.ed_sel_end_y, ex = G.ed_sel_end_x;
    if(sy > ey || (sy == ey && sx > ex)){ int t=sy; sy=ey; ey=t; t=sx; sx=ex; ex=t; }
    if(G.clipboard) free(G.clipboard);
    size_t cap = 1024, len = 0;
    G.clipboard = malloc(cap);
    for(int y = sy; y <= ey; y++){
        if(y < 0 || y >= CUR_ED.line_count) continue;
        const char *line = CUR_ED.lines[y];
        int line_len = (int)strlen(line);
        int start_x = (y == sy) ? sx : 0;
        int end_x = (y == ey) ? ex : line_len - 1;
        for(int x = start_x; x <= end_x && x < line_len; x++){
            if(len + 2 >= cap){ cap *= 2; G.clipboard = realloc(G.clipboard, cap); }
            G.clipboard[len++] = line[x];
        }
        if(y < ey){
            if(len + 2 >= cap){ cap *= 2; G.clipboard = realloc(G.clipboard, cap); }
            G.clipboard[len++] = '\n';
        }
    }
    G.clipboard[len] = '\0';
    OK("Copied %d chars from editor", (int)len);
}

static void editor_delete_selection(void){
    if(!G.ed_selecting) return;
    if(CUR_ED.line_count == 0){ G.ed_selecting = false; return; }
    int sy = G.ed_sel_start_y, sx = G.ed_sel_start_x;
    int ey = G.ed_sel_end_y, ex = G.ed_sel_end_x;
    /* Normalise so sy,sx <= ey,ex */
    if(sy > ey || (sy == ey && sx > ex)){ int t=sy; sy=ey; ey=t; t=sx; sx=ex; ex=t; }
    /* Clamp rows to valid range */
    sy = iclamp(sy, 0, CUR_ED.line_count - 1);
    ey = iclamp(ey, 0, CUR_ED.line_count - 1);
    editor_push_undo(&G.tabs[G.tab_current].ed);
    if(sy == ey){
        char *line = CUR_ED.lines[sy];
        int len = (int)strlen(line);
        sx = iclamp(sx, 0, len);
        ex = iclamp(ex, sx, len);
        /* Remove chars [sx, ex) */
        memmove(line + sx, line + ex, len - ex + 1);
    } else {
        char *start_line = CUR_ED.lines[sy];
        char *end_line   = CUR_ED.lines[ey];
        int s_len = (int)strlen(start_line);
        int e_len = (int)strlen(end_line);
        sx = iclamp(sx, 0, s_len);
        ex = iclamp(ex, 0, e_len);
        /* tail = chars from ex onwards on the end line */
        int tail = e_len - ex;
        if(tail < 0) tail = 0;
        char *n = malloc(sx + tail + 1);
        memcpy(n, start_line, sx);
        if(tail > 0) memcpy(n + sx, end_line + ex, tail);
        n[sx + tail] = '\0';
        free(CUR_ED.lines[sy]);
        CUR_ED.lines[sy] = n;
        int removed = ey - sy; /* lines sy+1 .. ey */
        for(int i = sy + 1; i <= ey; i++) free(CUR_ED.lines[i]);
        for(int i = sy + 1; i < CUR_ED.line_count - removed; i++)
            CUR_ED.lines[i] = CUR_ED.lines[i + removed];
        CUR_ED.line_count -= removed;
    }
    CUR_ED.cur_y = sy; CUR_ED.cur_x = sx;
    G.ed_selecting = false; CUR_ED.modified = true;
}

static void editor_cut_selection(void){
    if(!G.ed_selecting) return;
    action_copy_editor_selection();
    editor_delete_selection();
}

/* ================================================================
   MOUSE HANDLER
================================================================ */
static void handle_mouse(MouseEvt m){
    G.last_mx=m.col; G.last_my=m.row;
    bool su=(m.btn&64)&&!(m.btn&1);
    bool sd=(m.btn&64)&&(m.btn&1);
    bool motion=(m.btn&32)!=0;
    bool cl=!su&&!sd&&!m.release&&(m.btn&3)!=3 && !motion;
    bool right_cl=cl && (m.btn&3)==2;
    int ct=2;

    if(m.release){
        G.dragging_v=false; G.dragging_h=false; G.dragging_sc=false; G.dragging_diff=false;
        G.dragging_col_hash=false; G.dragging_col_author=false; G.dragging_col_date=false;
        G.dragging_h_log=false; G.dragging_ed_sc=false;
    }

    if(G.menu_active){
        /* ... */
        if(cl){
            if(m.row>G.menu_y && m.row<G.menu_y+G.menu_h-1 && m.col>G.menu_x && m.col<G.menu_x+G.menu_w-1){
                int idx=m.row-G.menu_y-1;
                if(idx>=0 && idx<G.menu_item_count){
                    void (*act)(void)=G.menu_actions[idx];
                    G.menu_active=false;
                    if(act)act();
                    return;
                }
            }
            G.menu_active=false;
        }
        return;
    }

    int drx = (G.current_view == VIEW_LOG) ? 1 : G.lw + G.sidebar_w + 1;
    int dtop = (G.current_view == VIEW_LOG) ? (ct + G.lh_log) : ct;

    if(motion){
        if(G.dragging_v){G.lw_custom=m.col - G.sidebar_w; layout(); return;}
        if(G.dragging_h){G.lh_chg_custom=m.row-ct+1; layout(); return;}
        if(G.dragging_h_log){G.lh_log_custom=m.row-ct+1; layout(); return;}
        if(G.dragging_diff){G.diff_split_custom=m.col-drx; layout(); return;}
        if(G.dragging_sc && G.sc_h > 0){
            int rel_y = m.row - G.sc_y;
            int bh = imax(1, (G.sc_vis * G.sc_vis) / G.sc_total);
            int max_bpos = G.sc_h - bh;
            if(max_bpos > 0){
                int bpos = iclamp(rel_y - G.sc_drag_offset, 0, max_bpos);
                G.diff_scroll = (bpos * (G.sc_total - G.sc_vis)) / max_bpos;
            }
            return;
        }
        if(G.dragging_ed_sc && G.ed_sc_h > 0 && G.tab_count > 0){
            int rel_y = m.row - G.ed_sc_y;
            int bh = imax(1, (G.ed_sc_vis * G.ed_sc_vis) / G.ed_sc_total);
            int max_bpos = G.ed_sc_h - bh;
            if(max_bpos > 0){
                int bpos = iclamp(rel_y - G.ed_sc_drag_offset, 0, max_bpos);
                CUR_ED.scroll_y = (bpos * (G.ed_sc_total - G.ed_sc_vis)) / max_bpos;
            }
            return;
        }
        if(G.dragging_col_hash){
            int start_x = 2 + GRAPH_COLS;
            G.col_hash_w = imax(4, m.col - start_x);
            return;
        }
        if(G.dragging_col_author){
            int start_x = 2 + GRAPH_COLS + G.col_hash_w + 1 + 21;
            G.col_author_w = imax(4, m.col - start_x);
            return;
        }
        if(G.dragging_col_date){
            int start_x = 2 + GRAPH_COLS + G.col_hash_w + 1 + 21 + G.col_author_w + 1;
            G.col_date_w = imax(4, m.col - start_x);
            return;
        }
        if(G.ed_selecting){
            G.ed_sel_end_y = CUR_ED.scroll_y + (m.row - (dtop+1));
            G.ed_sel_end_x = m.col - (drx + 7) + CUR_ED.scroll_x;
            return;
        }
        if(G.selecting){
            G.sel_end_y = G.diff_scroll + (m.row - (dtop+1));
            G.sel_end_x = m.col - drx - 1;
            return;
        }
        return;
    }

    if(right_cl){
        if(G.focus == FOCUS_EDITOR){
            menu_reset(m.col, m.row);
            menu_add_item("Copy", action_copy_editor_selection);
            menu_add_item("Cut", editor_cut_selection);
            menu_add_item("Paste", editor_paste);
            menu_add_item("Cancel", NULL);
            return;
        }
        menu_reset(m.col, m.row);
        menu_add_item("Cancel", NULL);
        return;
    }

    if(cl && m.col <= G.sidebar_w){
        if(m.row == ct+1){ G.browser_active = true; load_browser("."); G.focus = FOCUS_BROWSER; }
        else if(m.row == ct+3){ G.browser_active = false; G.focus = FOCUS_CHANGES; }
        return;
    }

    /* Right pane toggles (Diff / Editor) */
    int toggle_row = (G.current_view == VIEW_LOG) ? (ct + G.lh_log) : ct;
    int toggle_min_x = (G.current_view == VIEW_LOG) ? 1 : G.rx;
    
    /* Editor Tab Clicks — uses ed_tab_x (separate from view tab_x) */
    if(cl && G.editor_active && m.row == toggle_row + 1 && m.col > toggle_min_x){
        for(int i=0; i<G.tab_count; i++){
            if(m.col >= G.ed_tab_x[i] && m.col < G.ed_tab_x[i+1]){
                G.tab_current = i;
                G.focus = FOCUS_EDITOR;
                return;
            }
        }
    }

    if(cl && m.row == toggle_row && m.col > toggle_min_x){
        if(!G.editor_active){
            int unify_x = G.cols - 15;
            int hunk_x = G.cols - 7;
            if(m.col >= unify_x && m.col < unify_x + 7){ G.diff_sidebyside = !G.diff_sidebyside; return; }
            if(m.col >= hunk_x && m.col < hunk_x + 6){ G.diff_continuous = !G.diff_continuous; sync_graph_preview(); update_diff(); return; }
        } else {
            int save_x = G.cols - 8;
            if(m.col >= save_x && m.col < save_x + 6){ editor_save(); return; }
        }
    }

    if(cl && m.row==1){
        for(int i=0; i<5; i++){
            if(m.col >= G.tab_x[i] && m.col < G.tab_x[i+1]){
                static const View vmap[] = {VIEW_STATUS, VIEW_LOG, VIEW_BRANCHES, VIEW_STASH, VIEW_EDITOR};
                G.current_view = vmap[i];
                if(G.current_view == VIEW_EDITOR) { if(!G.browser_count) load_browser("."); G.focus = FOCUS_EDITOR; }
                return;
            }
        }
        if(m.col >= G.tab_x[5]) { G.current_view = VIEW_HELP; return; }
        return;
    }
    if(cl && (G.current_view == VIEW_STATUS || G.current_view == VIEW_LOG) && m.col > drx && m.row > dtop && m.row < G.rows-1){
        if(G.editor_active){
            if(m.col >= G.ed_sc_x && G.ed_sc_h > 0 && m.row >= G.ed_sc_y && m.row < G.ed_sc_y + G.ed_sc_h){
                int bh = imax(1, (G.ed_sc_vis * G.ed_sc_vis) / G.ed_sc_total);
                int max_bpos = G.ed_sc_h - bh;
                int bpos = (max_bpos > 0 && G.ed_sc_total > G.ed_sc_vis) ? ((CUR_ED.scroll_y * max_bpos) / (G.ed_sc_total - G.ed_sc_vis)) : 0;
                if(m.row >= G.ed_sc_y + bpos && m.row < G.ed_sc_y + bpos + bh){
                    G.dragging_ed_sc = true;
                    G.ed_sc_drag_offset = m.row - (G.ed_sc_y + bpos);
                } else if(max_bpos > 0){
                    int target_bpos = iclamp(m.row - G.ed_sc_y - bh/2, 0, max_bpos);
                    CUR_ED.scroll_y = (target_bpos * (G.ed_sc_total - G.ed_sc_vis)) / max_bpos;
                    G.dragging_ed_sc = true;
                    G.ed_sc_drag_offset = bh/2;
                }
                return;
            }
            G.ed_selecting = true;
            G.ed_sel_start_y = G.ed_sel_end_y = CUR_ED.scroll_y + (m.row - (dtop+2));
            G.ed_sel_start_x = G.ed_sel_end_x = m.col - (drx + 7) + CUR_ED.scroll_x;
            if(G.ed_sel_start_y >= 0 && G.ed_sel_start_y < CUR_ED.line_count){
                CUR_ED.cur_y = G.ed_sel_start_y;
                CUR_ED.cur_x = iclamp(G.ed_sel_start_x, 0, (int)strlen(CUR_ED.lines[CUR_ED.cur_y]));
            }
            G.focus = FOCUS_EDITOR;
            return; /* don't fall through to view-specific handler */
        } else {
            G.selecting = true;
            G.sel_start_y = G.sel_end_y = G.diff_scroll + (m.row - (dtop+1));
            G.sel_start_x = G.sel_end_x = m.col - drx - 1;
        }
    } else if(cl){
        G.selecting = false;
        G.ed_selecting = false;
    }

    if(cl && m.row==G.rows-1){
        G.focus=FOCUS_CLI;
        return;
    }

    if(G.current_view==VIEW_STATUS){
        layout();
        int vx = G.sidebar_w + G.lw;
        if(cl && m.col==vx){G.dragging_v=true; return;}
        if(cl && m.col<vx && m.row==ct+G.lh_chg-1){G.dragging_h=true; return;}
        if(cl && G.diff_sidebyside && m.col==drx+G.diff_split){G.dragging_diff=true; return;}
        
        if(cl && G.focus == FOCUS_GRAPH){
            int cur_x = 2 + GRAPH_COLS;
            if(m.col == cur_x + G.col_hash_w) { G.dragging_col_hash = true; return; }
            cur_x += G.col_hash_w + 1 + 21;
            if(m.col == cur_x + G.col_author_w) { G.dragging_col_author = true; return; }
            cur_x += G.col_author_w + 1;
            if(m.col == cur_x + G.col_date_w) { G.dragging_col_date = true; return; }
        }

        if(cl && m.col >= G.cols-2 && G.sc_h > 0 && m.row >= G.sc_y && m.row < G.sc_y + G.sc_h){
            int bh = imax(1, (G.sc_vis * G.sc_vis) / G.sc_total);
            int bpos = (G.sc_total > G.sc_vis) ? ((G.diff_scroll * (G.sc_h - bh)) / (G.sc_total - G.sc_vis)) : 0;
            if(m.row >= G.sc_y + bpos && m.row < G.sc_y + bpos + bh){
                G.dragging_sc = true;
                G.sc_drag_offset = m.row - (G.sc_y + bpos);
            } else {
                int max_bpos = G.sc_h - bh;
                if(max_bpos > 0){
                    int target_bpos = iclamp(m.row - G.sc_y - bh/2, 0, max_bpos);
                    G.diff_scroll = (target_bpos * (G.sc_total - G.sc_vis)) / max_bpos;
                    G.dragging_sc = true;
                    G.sc_drag_offset = bh / 2;
                }
            }
            return;
        }

        bool in_l=(m.col>=1&&m.col<=vx);
        bool in_r=(m.col>vx);
        bool in_top=(m.row>=ct&&m.row<ct+G.lh_chg);
        bool in_bot=(m.row>=ct+G.lh_chg);

        if(in_l){
            if(G.browser_active){
                if(cl) G.focus=FOCUS_BROWSER;
                if(su) G.browser_sel=imax(0, G.browser_sel-1);
                if(sd) G.browser_sel=imin(G.browser_count>0?G.browser_count-1:0, G.browser_sel+1);
                if(cl){
                    int t = G.browser_scroll + (m.row - (ct+1));
                    if(t >= 0 && t < G.browser_count){
                        G.browser_sel = t;
                        BrowserFile *f = &G.browser_files[t];
                        if(f->is_dir){
                            char next[1024]; snprintf(next, sizeof(next), "%s/%s", G.browser_path, f->path);
                            load_browser(next); G.browser_sel = 0; G.browser_scroll = 0;
                        } else {
                            char full[1024]; snprintf(full, sizeof(full), "%s/%s", G.browser_path, f->path);
                            editor_load(full); G.editor_active = true; G.focus = FOCUS_EDITOR;
                        }
                    }
                }
                return;
            }
            if(in_top){
                if(cl)G.focus=FOCUS_CHANGES;
                if(su)G.file_sel=imax(0,G.file_sel-1);
                if(sd)G.file_sel=imin(G.file_count>0?G.file_count-1:0,G.file_sel+1);
                if(cl){
                    int row=m.row-(ct+1);
                    int vis=0;
                    for(int i=0;i<G.file_count;i++){
                        if(G.files[i].staged){vis++; if(vis==row){G.file_sel=i;break;}}
                    }
                    vis++; vis++;
                    for(int i=0;i<G.file_count;i++){
                        if(!G.files[i].staged){vis++; if(vis==row){G.file_sel=i;break;}}
                    }
                }
                update_diff();
            } else if(in_bot){
                if(cl)G.focus=FOCUS_GRAPH;
                int gvis=G.lh_gph-2;
                if(su){ msel(&G.commit_sel,&G.commit_scroll,G.commit_count,-1,gvis,true); sync_graph_preview(); }
                if(sd){ msel(&G.commit_sel,&G.commit_scroll,G.commit_count,1,gvis,true); sync_graph_preview(); }
                if(cl){
                    int row_idx = m.row - (ct + G.lh_chg + 1);
                    if(row_idx >= 0 && row_idx < G.graph_rows_count){
                        int ci = G.graph_rows[row_idx].commit_idx;
                        int fi = G.graph_rows[row_idx].file_idx;
                        G.commit_sel = ci;
                        if(fi == -1){
                            int sx = G.sidebar_w + 1;
                            if(m.col >= sx+1 && m.col <= sx+2) {
                                G.commits[ci].expanded = !G.commits[ci].expanded;
                                if(G.commits[ci].expanded) fetch_commit_files(ci);
                            } else {
                                /* Auto-expand on commit row click */
                                G.graph_file_sel = -1;
                                G.commits[ci].expanded = true;
                                fetch_commit_files(ci);
                                snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",G.commits[ci].hash,G.commits[ci].subject);
                                G.diff_is_summary = false; load_diff_commit(G.commits[ci].hash);
                            }
                        } else {
                            G.graph_file_sel = fi;
                            if(fi == 0) {
                                snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",G.commits[ci].hash,G.commits[ci].subject);
                                G.diff_is_summary = false; load_diff_commit(G.commits[ci].hash);
                            } else {
                                char *fpath = G.commits[ci].files[fi];
                                const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                                char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, G.commits[ci].hash, fpath);
                                char *o = git_run(cmd);
                                snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", G.commits[ci].hash, fpath);
                                G.diff_is_summary = false;
                                snprintf(G.diff_commit, sizeof(G.diff_commit), "%s", G.commits[ci].hash);
                                parse_diff(o?o:""); free(o);
                            }
                        }
                    }
                }
            }
        } else if(in_r){
            if(cl)G.focus = G.editor_active ? FOCUS_EDITOR : FOCUS_DIFF;
            if(cl && G.diff_is_summary){
                int t = G.diff_scroll + (m.row - (ct+1));
                if(t >= 0 && t < G.diff_count){
                    G.diff_sel = t;
                    DiffLine *dl = &G.diff_lines[t];
                    if(dl->type == 5){
                        if(strncmp(G.diff_title, "Files:", 6) == 0){
                            editor_load(dl->new_line);
                            G.editor_active = true; G.focus = FOCUS_EDITOR;
                        } else if(strncmp(G.diff_title, "Search:", 7) == 0){
                            char lb[LINE_MAX_LEN]; snprintf(lb, sizeof(lb), "%s", dl->new_line);
                            char *c1 = strchr(lb, ':');
                            if(c1 && isdigit(c1[1])){
                                *c1 = '\0';
                                int lno = atoi(c1+1);
                                editor_load(lb);
                                CUR_ED.cur_y = imax(0, lno - 1);
                                CUR_ED.cur_x = 0;
                                G.editor_active = true; G.focus = FOCUS_EDITOR;
                            }
                        } else {
                            char fpath[LINE_MAX_LEN]; snprintf(fpath, sizeof(fpath), "%s", dl->new_line);
                            const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, G.diff_commit, fpath);
                            char *o = git_run(cmd);
                            snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", G.diff_commit, fpath);
                            G.diff_is_summary = false;
                            parse_diff(o?o:""); free(o);
                        }
                    }
                }
            }
            if(su)G.diff_scroll=imax(0,G.diff_scroll-3);
            if(sd)G.diff_scroll+=3;
        }
    } else if(G.current_view==VIEW_LOG){
        int lh=G.lh_log;
        bool in_log=(m.row<ct+lh);
        int vis=lh-2;
        
        if(cl && m.row == ct + lh - 1){ G.dragging_h_log = true; return; }
        if(cl && G.diff_sidebyside && m.col == drx + G.diff_split){ G.dragging_diff = true; return; }

        if(cl){
            int cur_x = 2 + GRAPH_COLS;
            if(m.col == cur_x + G.col_hash_w) { G.dragging_col_hash = true; return; }
            cur_x += G.col_hash_w + 1 + 21;
            if(m.col == cur_x + G.col_author_w) { G.dragging_col_author = true; return; }
            cur_x += G.col_author_w + 1;
            if(m.col == cur_x + G.col_date_w) { G.dragging_col_date = true; return; }
        }

        if(cl && m.col >= G.cols-2 && G.sc_h > 0 && m.row >= G.sc_y && m.row < G.sc_y + G.sc_h){
            int bh = imax(1, (G.sc_vis * G.sc_vis) / G.sc_total);
            int bpos = (G.sc_total > G.sc_vis) ? ((G.diff_scroll * (G.sc_h - bh)) / (G.sc_total - G.sc_vis)) : 0;
            if(m.row >= G.sc_y + bpos && m.row < G.sc_y + bpos + bh){
                G.dragging_sc = true;
                G.sc_drag_offset = m.row - (G.sc_y + bpos);
            } else {
                int max_bpos = G.sc_h - bh;
                if(max_bpos > 0){
                    int target_bpos = iclamp(m.row - G.sc_y - bh/2, 0, max_bpos);
                    G.diff_scroll = (target_bpos * (G.sc_total - G.sc_vis)) / max_bpos;
                    G.dragging_sc = true;
                    G.sc_drag_offset = bh / 2;
                }
            }
            return;
        }
        if(in_log){
            if(su){ msel(&G.commit_sel,&G.commit_scroll,G.commit_count,-1,vis,true); sync_graph_preview(); }
            if(sd){ msel(&G.commit_sel,&G.commit_scroll,G.commit_count,1,vis,true); sync_graph_preview(); }
            if(cl){
                int t = G.commit_scroll+(m.row-ct-1);
                if(t>=0&&t<G.commit_count){
                    G.commit_sel=t;
                    sync_graph_preview();
                }
            }
        } else {
            if(cl)G.focus=FOCUS_DIFF;
            if(cl && G.diff_is_summary){
                int t = G.diff_scroll + (m.row - (ct+lh));
                if(t >= 0 && t < G.diff_count){
                    G.diff_sel = t;
                    DiffLine *dl = &G.diff_lines[t];
                    if(dl->type == 5){
                        if(strncmp(G.diff_title, "Files:", 6) == 0){
                            editor_load(dl->new_line);
                            G.editor_active = true; G.focus = FOCUS_EDITOR;
                        } else if(strncmp(G.diff_title, "Search:", 7) == 0){
                            char lb[LINE_MAX_LEN]; snprintf(lb, sizeof(lb), "%s", dl->new_line);
                            char *c1 = strchr(lb, ':');
                            if(c1 && isdigit(c1[1])){
                                *c1 = '\0';
                                int lno = atoi(c1+1);
                                editor_load(lb);
                                CUR_ED.cur_y = imax(0, lno - 1);
                                CUR_ED.cur_x = 0;
                                G.editor_active = true; G.focus = FOCUS_EDITOR;
                            }
                        } else {
                            char fpath[LINE_MAX_LEN]; snprintf(fpath, sizeof(fpath), "%s", dl->new_line);
                            const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, G.diff_commit, fpath);
                            char *o = git_run(cmd);
                            snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", G.diff_commit, fpath);
                            G.diff_is_summary = false;
                            parse_diff(o?o:""); free(o);
                        }
                    }
                }
            }
            if(su)G.diff_scroll=imax(0,G.diff_scroll-3);
            if(sd)G.diff_scroll+=3;
        }
    } else if(G.current_view==VIEW_BRANCHES){
        int vis=G.rows-4;
        if(su)msel(&G.branch_sel,&G.branch_scroll,G.branch_count,-1,vis,false);
        if(sd)msel(&G.branch_sel,&G.branch_scroll,G.branch_count,1,vis,false);
        if(cl){int t=G.branch_scroll+(m.row-ct-2);if(t>=0&&t<G.branch_count)G.branch_sel=t;}
    } else if(G.current_view==VIEW_STASH){
        if(su)G.stash_sel=imax(0,G.stash_sel-1);
        if(sd)G.stash_sel=imin(G.stash_count>0?G.stash_count-1:0,G.stash_sel+1);
        if(cl){int t=m.row-ct-1;if(t>=0&&t<G.stash_count)G.stash_sel=t;}
    } else if(G.current_view==VIEW_EDITOR){
        layout();
        if(m.col <= G.sidebar_w + G.lw){
            if(cl) G.focus=FOCUS_BROWSER;
            if(su) G.browser_sel=imax(0, G.browser_sel-1);
            if(sd) G.browser_sel=imin(G.browser_count>0?G.browser_count-1:0, G.browser_sel+1);
            if(cl){
                int t = G.browser_scroll + (m.row - (ct+1));
                if(t >= 0 && t < G.browser_count){
                    G.browser_sel = t;
                    BrowserFile *f = &G.browser_files[t];
                    if(f->is_dir){
                        char next[1024]; snprintf(next, sizeof(next), "%s/%s", G.browser_path, f->path);
                        load_browser(next); G.browser_sel = 0; G.browser_scroll = 0;
                    } else {
                        char full[1024]; snprintf(full, sizeof(full), "%s/%s", G.browser_path, f->path);
                        editor_load(full); G.focus = FOCUS_EDITOR;
                    }
                }
            }
        } else {
            if(cl) G.focus=FOCUS_EDITOR;
            if(su) CUR_ED.cur_y=imax(0, CUR_ED.cur_y-3);
            if(sd) CUR_ED.cur_y=imin(CUR_ED.line_count>0?CUR_ED.line_count-1:0, CUR_ED.cur_y+3);
            if(cl){
                int ty = CUR_ED.scroll_y + (m.row - (ct+1));
                if(ty >= 0 && ty < CUR_ED.line_count){
                    CUR_ED.cur_y = ty;
                    CUR_ED.cur_x = iclamp(m.col - (G.rx + 7) + CUR_ED.scroll_x, 0, (int)strlen(CUR_ED.lines[ty]));
                }
            }
        }
    }
}

static void action_copy_selection(void){
    if(!G.selecting) return;
    int sy = G.sel_start_y, sx = G.sel_start_x;
    int ey = G.sel_end_y, ex = G.sel_end_x;
    if(sy > ey || (sy == ey && sx > ex)){
        int t=sy; sy=ey; ey=t; t=sx; sx=ex; ex=t;
    }
    if(G.clipboard) free(G.clipboard);
    size_t cap = 1024, len = 0;
    G.clipboard = malloc(cap);
    for(int y = sy; y <= ey; y++){
        if(y < 0 || y >= G.diff_count) continue;
        const char *line = G.diff_lines[y].new_line;
        int line_len = (int)strlen(line);
        int start_x = (y == sy) ? sx : 0;
        int end_x = (y == ey) ? ex : line_len - 1;
        for(int x = start_x; x <= end_x && x < line_len; x++){
            if(len + 2 >= cap){ cap *= 2; G.clipboard = realloc(G.clipboard, cap); }
            G.clipboard[len++] = line[x];
        }
        if(y < ey){
            if(len + 2 >= cap){ cap *= 2; G.clipboard = realloc(G.clipboard, cap); }
            G.clipboard[len++] = '\n';
        }
    }
    G.clipboard[len] = '\0';
    OK("Copied %d chars", (int)len);
}

/* ================================================================
   MAIN KEY HANDLER
================================================================ */
static void handle_cli_key(Key k){
    int len=(int)strlen(G.cli_buf);
    if(k.type==KEY_ESC){G.focus=FOCUS_CHANGES;return;}
    if(k.type==KEY_ENTER){
        if(G.cli_buf[0]){
            char cmd[INPUT_MAX]; snprintf(cmd, sizeof(cmd), "%s", G.cli_buf);
            OK("Executing: %s", cmd);
            draw();
            int r = system(cmd);
            if(r==0)OK("Success: %s", cmd);
            else ERR("Failed (%d): %s", r, cmd);
            G.cli_buf[0]='\0'; G.cli_cursor=0;
            reload_all();
        }
        G.focus=FOCUS_CHANGES;
        return;
    }
    if(k.type==KEY_BACKSPACE){
        if(G.cli_cursor>0){
            memmove(&G.cli_buf[G.cli_cursor-1], &G.cli_buf[G.cli_cursor], len-G.cli_cursor+1);
            G.cli_cursor--;
        }
    } else if(k.type==KEY_DEL){
        if(G.cli_cursor<len){
            memmove(&G.cli_buf[G.cli_cursor], &G.cli_buf[G.cli_cursor+1], len-G.cli_cursor);
        }
    } else if(k.type==KEY_LEFT){if(G.cli_cursor>0)G.cli_cursor--;}
    else if(k.type==KEY_RIGHT){if(G.cli_cursor<len)G.cli_cursor++;}
    else if(k.type==KEY_HOME || k.type==KEY_CTRL_A){G.cli_cursor=0;}
    else if(k.type==KEY_END || k.type==KEY_CTRL_E){G.cli_cursor=len;}
    else if(k.type==KEY_CTRL_U){
        memmove(G.cli_buf, &G.cli_buf[G.cli_cursor], len-G.cli_cursor+1);
        G.cli_cursor=0;
    } else if(k.type==KEY_CTRL_K){
        G.cli_buf[G.cli_cursor]='\0';
    } else if(k.type==KEY_CTRL_W){
        if(G.cli_cursor>0){
            int pos=G.cli_cursor-1;
            while(pos>0 && G.cli_buf[pos-1]==' ')pos--;
            while(pos>0 && G.cli_buf[pos-1]!=' ')pos--;
            memmove(&G.cli_buf[pos], &G.cli_buf[G.cli_cursor], len-G.cli_cursor+1);
            G.cli_cursor=pos;
        }
    } else if(k.type==KEY_CHAR){
        if(len+1<INPUT_MAX){
            memmove(&G.cli_buf[G.cli_cursor+1], &G.cli_buf[G.cli_cursor], len-G.cli_cursor+1);
            G.cli_buf[G.cli_cursor++]=k.ch;
            G.cli_buf[len+1]='\0';
        }
    }
}

static void begin_ed_selection(void){
    if(!G.ed_selecting){
        G.ed_selecting = true;
        G.ed_sel_start_y = G.ed_sel_end_y = CUR_ED.cur_y;
        G.ed_sel_start_x = G.ed_sel_end_x = CUR_ED.cur_x;
    }
}

static void handle_key(Key k){
    if(k.type==KEY_MOUSE){handle_mouse(k.mouse);return;}
    if(G.menu_active){G.menu_active=false;return;}
    if(G.in_prompt){handle_prompt_key(k);return;}
    if(G.focus==FOCUS_CLI){handle_cli_key(k);return;}
    if(k.type==KEY_CHAR && k.ch==':'){G.focus=FOCUS_CLI; return;}

    /* Focus-specific handling first (to allow typing 'e', 'b' etc in editor) */
    if(G.focus == FOCUS_EDITOR && k.type != KEY_TAB && k.type != KEY_SHIFT_TAB){
        switch(k.type){
        case KEY_UP:
            G.ed_selecting = false;
            CUR_ED.cur_y = imax(0, CUR_ED.cur_y-1);
            { int l=(int)strlen(CUR_ED.lines[CUR_ED.cur_y]); if(CUR_ED.cur_x > l) CUR_ED.cur_x = l; }
            break;
        case KEY_DOWN:
            G.ed_selecting = false;
            CUR_ED.cur_y = imin(CUR_ED.line_count>0?CUR_ED.line_count-1:0, CUR_ED.cur_y+1);
            { int l=(int)strlen(CUR_ED.lines[CUR_ED.cur_y]); if(CUR_ED.cur_x > l) CUR_ED.cur_x = l; }
            break;
        case KEY_LEFT: G.ed_selecting = false; CUR_ED.cur_x = imax(0, CUR_ED.cur_x-1); break;
        case KEY_RIGHT:G.ed_selecting = false; CUR_ED.cur_x = imin(CUR_ED.lines[CUR_ED.cur_y] ? (int)strlen(CUR_ED.lines[CUR_ED.cur_y]) : 0, CUR_ED.cur_x+1); break;
        case KEY_SHIFT_UP:
            begin_ed_selection();
            CUR_ED.cur_y = imax(0, CUR_ED.cur_y-1);
            G.ed_sel_end_y = CUR_ED.cur_y; G.ed_sel_end_x = CUR_ED.cur_x;
            break;
        case KEY_SHIFT_DOWN:
            begin_ed_selection();
            CUR_ED.cur_y = imin(CUR_ED.line_count>0?CUR_ED.line_count-1:0, CUR_ED.cur_y+1);
            G.ed_sel_end_y = CUR_ED.cur_y; G.ed_sel_end_x = CUR_ED.cur_x;
            break;
        case KEY_SHIFT_LEFT:
            begin_ed_selection();
            CUR_ED.cur_x = imax(0, CUR_ED.cur_x-1);
            G.ed_sel_end_y = CUR_ED.cur_y; G.ed_sel_end_x = CUR_ED.cur_x;
            break;
        case KEY_SHIFT_RIGHT:
            begin_ed_selection();
            CUR_ED.cur_x = imin(CUR_ED.lines[CUR_ED.cur_y] ? (int)strlen(CUR_ED.lines[CUR_ED.cur_y]) : 0, CUR_ED.cur_x+1);
            G.ed_sel_end_y = CUR_ED.cur_y; G.ed_sel_end_x = CUR_ED.cur_x;
            break;
        case KEY_ENTER:
            if(G.ed_selecting) editor_delete_selection();
            editor_newline(); break;
        case KEY_BACKSPACE:
            if(G.ed_selecting) editor_delete_selection();
            else editor_backspace(); break;
        case KEY_DEL:
            if(G.ed_selecting) editor_delete_selection();
            else editor_delete_forward(); break;
        case KEY_CTRL_A:
            if(CUR_ED.line_count > 0){
                G.ed_selecting = true;
                G.ed_sel_start_y = 0; G.ed_sel_start_x = 0;
                G.ed_sel_end_y = CUR_ED.line_count - 1;
                G.ed_sel_end_x = (int)strlen(CUR_ED.lines[CUR_ED.line_count - 1]);
                CUR_ED.cur_y = G.ed_sel_end_y;
                CUR_ED.cur_x = G.ed_sel_end_x;
            }
            break;
        case KEY_CTRL_S:editor_save(); break;
        case KEY_CTRL_V:editor_paste(); break;
        case KEY_CTRL_X:editor_cut_selection(); break;
        case KEY_CTRL_Z:editor_undo(&G.tabs[G.tab_current].ed); break;
        case KEY_CTRL_Y:editor_redo(&G.tabs[G.tab_current].ed); break;
        case KEY_CTRL_W:editor_close_tab(); break;
        case KEY_F1: editor_prev_tab(); break;
        case KEY_F2: editor_next_tab(); break;
        case KEY_CHAR:
            if(G.ed_selecting) editor_delete_selection();
            editor_insert_char(k.ch); break;
        case KEY_ESC: G.ed_selecting = false; G.focus = (G.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES); break;
        default: break;
        }
        return;
    }

    if(G.focus == FOCUS_BROWSER && k.type != KEY_TAB && k.type != KEY_SHIFT_TAB){
        int cnt=G.browser_count;
        switch(k.type){
        case KEY_UP:   G.browser_sel=imax(0, G.browser_sel-1); break;
        case KEY_DOWN: G.browser_sel=imin(cnt>0?cnt-1:0, G.browser_sel+1); break;
        case KEY_LEFT: {
            char next[1024]; snprintf(next, sizeof(next), "%s/..", G.browser_path);
            load_browser(next); G.browser_sel=0; break;
        }
        case KEY_RIGHT:case KEY_ENTER: {
            if(!cnt) break;
            BrowserFile *f = &G.browser_files[G.browser_sel];
            char next[1024]; snprintf(next, sizeof(next), "%s/%s", G.browser_path, f->path);
            if(f->is_dir){ load_browser(next); G.browser_sel=0; }
            else { editor_load(next); G.editor_active=true; G.focus = FOCUS_EDITOR; }
            break;
        }
        case KEY_CHAR:
            if(k.ch == 'm'){
                menu_reset(G.sidebar_w+2, G.browser_sel - G.browser_scroll + 3);
                menu_add_item("New File", menu_new_file);
                menu_add_item("Delete", menu_delete_file);
                menu_add_item("Cancel", NULL);
                return;
            }
            break;
        case KEY_ESC: G.focus = FOCUS_CHANGES; return;
        default: break;
        }
        /* Fall through to global if not handled? No, browser usually captures most. 
           But let's allow '1-4' to work. */
        if(k.type != KEY_CHAR || !isdigit(k.ch)) return;
    }

    int vis=G.rows-5;

    /* Global */
    if(k.type==KEY_CHAR){
        switch(k.ch){
        case '1':G.current_view=VIEW_STATUS;return;
        case '2':G.current_view=VIEW_LOG;return;
        case '3':G.current_view=VIEW_BRANCHES;return;
        case '4':G.current_view=VIEW_STASH;return;
        case '?':G.current_view=(G.current_view==VIEW_HELP)?VIEW_STATUS:VIEW_HELP;return;
        case 'q':
            if(G.current_view==VIEW_HELP){G.current_view=VIEW_STATUS;return;}
            if(G.focus==FOCUS_DIFF&&G.current_view==VIEW_STATUS){G.focus=FOCUS_CHANGES;return;}
            G.running=false;return;
        case 'R':reload_all();return;
        case 'c':action_commit();return;
        case 'A':action_amend();return;
        case 'P':action_push();return;
        case 'f':action_pull();return;
        case 'y':
            if(G.ed_selecting) action_copy_editor_selection();
            else if(G.selecting) action_copy_selection();
            return;
        case 'e':
            if(G.current_view == VIEW_STATUS){
                if(G.focus == FOCUS_CHANGES && G.file_count > 0){
                    if(G.editor_active){ G.editor_active = false; G.focus = FOCUS_CHANGES; update_diff(); }
                    else { editor_load(G.files[G.file_sel].path); G.editor_active = true; G.focus = FOCUS_EDITOR; }
                } else if(G.focus == FOCUS_GRAPH && G.commit_count > 0){
                    GitCommit *c = &G.commits[G.commit_sel];
                    if(G.graph_file_sel > 0){
                        editor_load(c->files[G.graph_file_sel]); G.editor_active = true; G.focus = FOCUS_EDITOR;
                    } else { G.editor_active = !G.editor_active; if(!G.editor_active) { G.focus = FOCUS_CHANGES; update_diff(); } }
                } else { G.editor_active = !G.editor_active; if(!G.editor_active) { G.focus = FOCUS_CHANGES; update_diff(); } }
            } else if(G.current_view == VIEW_LOG){
                if(!G.editor_active && G.commit_count > 0){
                    GitCommit *c = &G.commits[G.commit_sel];
                    if(G.graph_file_sel > 0) editor_load(c->files[G.graph_file_sel]);
                    else if(c->hash[0]) {
                        /* Maybe load a commit summary? For now just toggle */
                        G.editor_active = true; G.focus = FOCUS_EDITOR;
                    }
                }
                G.editor_active = !G.editor_active;
                if(!G.editor_active) { G.focus = FOCUS_CHANGES; update_diff(); }
            }
            return;
        case 'b':
            if(G.current_view == VIEW_STATUS){
                G.browser_active = !G.browser_active;
                if(G.browser_active){ if(!G.browser_count) load_browser("."); G.focus = FOCUS_BROWSER; }
                else G.focus = FOCUS_CHANGES;
            }
            return;
        case 'T':G.theme_idx=(G.theme_idx+1)%NTHEMES;OK("Theme: %s",TH->name);return;
        case 'H':G.diff_continuous=!G.diff_continuous;
                 if (G.current_view == VIEW_STATUS && G.focus == FOCUS_CHANGES) update_diff();
                 else if (G.current_view == VIEW_STATUS && G.focus == FOCUS_GRAPH) {
                     /* Re-trigger whatever is currently showing in graph */
                     GitCommit *c = &G.commits[G.commit_sel];
                     if (G.graph_file_sel == -1) { G.diff_is_summary = false; load_diff_commit(c->hash); }
                     else if (G.graph_file_sel == 0) load_diff_commit(c->hash);
                     else {
                         char *fpath = c->files[G.graph_file_sel];
                         const char *ctx = G.diff_continuous ? "-U1000" : "-U3";
                         char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx, c->hash, fpath);
                         char *o = git_run(cmd);
                         parse_diff(o?o:""); free(o);
                     }
                 } else if (G.current_view == VIEW_LOG) {
                     GitCommit *c = &G.commits[G.commit_sel];
                     load_diff_commit(c->hash);
                 }
                 OK("Continuous Diff: %s",G.diff_continuous?"ON (full context)":"OFF (hunks only)");
                 return;
        }
    }
    if(k.type==KEY_CTRL_C) { 
        if(G.ed_selecting) action_copy_editor_selection();
        else if(G.selecting) action_copy_selection();
        else action_commit(); 
        return; 
    }
    if(k.type==KEY_CTRL_R || k.type==KEY_CTRL_L) { reload_all(); return; }
    if(k.type==KEY_CTRL_P) { prompt_start("Go to File:", action_find_file, false); return; }
    if(k.type==KEY_CTRL_F) { 
        if(G.focus == FOCUS_EDITOR) prompt_start("Find:", editor_find, false);
        else action_pull(); 
        return; 
    }
    if(k.type==KEY_CTRL_K) { prompt_start("Global Search (grep):", action_grep, false); return; }
    if(k.type==KEY_CTRL_G) { 
        if(G.focus == FOCUS_EDITOR) prompt_start("Go to line:", editor_goto_line, false);
        return; 
    }
    if(k.type==KEY_CTRL_S) { if(G.current_view==VIEW_STATUS && G.focus==FOCUS_CHANGES) action_stage(); return; }
    if(k.type==KEY_CTRL_Q) { G.running=false; return; }
    if(k.type==KEY_ESC){
        if(G.current_view==VIEW_HELP){G.current_view=VIEW_STATUS;return;}
        if(G.focus==FOCUS_DIFF){G.focus=FOCUS_CHANGES;return;}
        return;
    }
    if(k.type==KEY_TAB){
        if(G.current_view==VIEW_STATUS){
            if(G.focus == FOCUS_CHANGES) G.focus = FOCUS_GRAPH;
            else if(G.focus == FOCUS_GRAPH) G.focus = G.editor_active ? FOCUS_EDITOR : FOCUS_DIFF;
            else if(G.focus == FOCUS_DIFF || G.focus == FOCUS_EDITOR) G.focus = G.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES;
            else if(G.focus == FOCUS_BROWSER) G.focus = G.editor_active ? FOCUS_EDITOR : FOCUS_DIFF;
        }
        else G.current_view=(View)((G.current_view+1)%VIEW_COUNT);
        return;
    }
    if(k.type==KEY_SHIFT_TAB){
        if(G.current_view==VIEW_STATUS){
            if(G.focus == FOCUS_CHANGES) G.focus = G.editor_active ? FOCUS_EDITOR : FOCUS_DIFF;
            else if(G.focus == FOCUS_GRAPH) G.focus = G.browser_active ? FOCUS_BROWSER : FOCUS_CHANGES;
            else if(G.focus == FOCUS_DIFF || G.focus == FOCUS_EDITOR) G.focus = FOCUS_GRAPH;
            else if(G.focus == FOCUS_BROWSER) G.focus = G.editor_active ? FOCUS_EDITOR : FOCUS_DIFF;
        }
        else G.current_view=(View)((G.current_view+VIEW_COUNT-1)%VIEW_COUNT);
        return;
    }

    switch(G.current_view){
    /* ──── STATUS ──── */
    case VIEW_STATUS:{
        if(k.type==KEY_LEFT){
            if(G.focus==FOCUS_DIFF)G.focus=FOCUS_GRAPH;
            else if(G.focus==FOCUS_GRAPH)G.focus=FOCUS_CHANGES;
            return;
        }
        if(k.type==KEY_RIGHT){
            if(G.focus==FOCUS_CHANGES)G.focus=FOCUS_GRAPH;
            else if(G.focus==FOCUS_GRAPH)G.focus=FOCUS_DIFF;
            return;
        }
        if(G.focus==FOCUS_CHANGES){
            int cnt=G.file_count;
            switch(k.type){
            case KEY_UP:   msel(&G.file_sel,&G.file_scroll,cnt,-1,vis,false);break;
            case KEY_DOWN: msel(&G.file_sel,&G.file_scroll,cnt, 1,vis,false);break;
            case KEY_PGUP: msel(&G.file_sel,&G.file_scroll,cnt,-vis/2,vis,false);break;
            case KEY_PGDN: msel(&G.file_sel,&G.file_scroll,cnt, vis/2,vis,false);break;
            case KEY_HOME: G.file_sel=0;G.file_scroll=0;break;
            case KEY_END:  G.file_sel=cnt>0?cnt-1:0;break;
            case KEY_ENTER:G.focus=FOCUS_DIFF;break;
            case KEY_CHAR:
                if(k.ch==' ')action_stage();
                else if(k.ch=='a')action_stage_all();
                else if(k.ch=='u')action_unstage_all();
                else if(k.ch=='d')action_discard();
                else if(k.ch=='='||k.ch=='>'){
                    if(G.commit_count>0){
                        GitCommit *c=&G.commits[G.commit_sel];
                        c->expanded = !c->expanded;
                        if(c->expanded) fetch_commit_files(G.commit_sel);
                    }
                }
                else if(k.ch=='s')action_stash();
                break;
            default:break;
            }
            update_diff();
        } else if(G.focus==FOCUS_GRAPH){
            int cnt=G.commit_count,gvis=G.lh_gph-2;
            switch(k.type){
            case KEY_UP: {
                if(G.graph_file_sel > -1) {
                    G.graph_file_sel--;
                } else {
                    int old_sel = G.commit_sel;
                    msel(&G.commit_sel, &G.commit_scroll, cnt, -1, gvis, true);
                    if(old_sel != G.commit_sel && G.commits[G.commit_sel].expanded && G.commits[G.commit_sel].f_count > 0) {
                        G.graph_file_sel = G.commits[G.commit_sel].f_count - 1;
                    }
                }
                break;
            }
            case KEY_DOWN: {
                GitCommit *c = &G.commits[G.commit_sel];
                if(c->expanded && G.graph_file_sel < c->f_count - 1) {
                    G.graph_file_sel++;
                } else {
                    msel(&G.commit_sel, &G.commit_scroll, cnt, 1, gvis, true);
                    G.graph_file_sel = -1;
                }
                break;
            }
            case KEY_PGUP: msel(&G.commit_sel,&G.commit_scroll,cnt,-gvis/2,gvis,true); G.graph_file_sel=-1; break;
            case KEY_PGDN: msel(&G.commit_sel,&G.commit_scroll,cnt, gvis/2,gvis,true); G.graph_file_sel=-1; break;
            case KEY_HOME: G.commit_sel=0;G.commit_scroll=0;G.graph_file_sel=-1; break;
            case KEY_END:  G.commit_sel=cnt?cnt-1:0; G.graph_file_sel=-1; break;
            case KEY_ENTER:
                if(G.commit_count>0){
                    GitCommit *c=&G.commits[G.commit_sel];
                    if(G.graph_file_sel == -1) {
                        snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                        G.diff_staged=false; G.diff_is_summary = false; load_diff_commit(c->hash); G.focus=FOCUS_DIFF;
                    } else if(G.graph_file_sel == 0) {
                        snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                        G.diff_is_summary = false; load_diff_commit(c->hash); G.focus=FOCUS_DIFF;
                    } else {
                        char *fpath = c->files[G.graph_file_sel];
                        const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                        char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, c->hash, fpath);
                        char *o = git_run(cmd);
                        snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", c->hash, fpath);
                        G.diff_is_summary = false;
                        snprintf(G.diff_commit, sizeof(G.diff_commit), "%s", c->hash);
                        parse_diff(o?o:""); free(o);
                        G.focus=FOCUS_DIFF;
                    }
                }
                break;
            case KEY_CHAR:
                if(k.ch==' ') {
                    if(G.commit_count > 0) {
                        G.commits[G.commit_sel].expanded = !G.commits[G.commit_sel].expanded;
                        if(G.commits[G.commit_sel].expanded) fetch_commit_files(G.commit_sel);
                    }
                }
                else if(k.ch=='a')action_stage_all();
                else if(k.ch=='u')action_unstage_all();
                else if(k.ch=='d')action_discard();
                else if(k.ch=='='||k.ch=='>'){
                    if(G.commit_count>0){
                        GitCommit *c=&G.commits[G.commit_sel];
                        c->expanded = !c->expanded;
                        if(c->expanded) fetch_commit_files(G.commit_sel);
                    }
                }
                else if(k.ch=='s')action_stash();
                break;
            default:break;
            }
            /* Sync preview if navigating with keys */
            if(G.commit_count > 0 && k.type != KEY_ENTER) {
                sync_graph_preview();
            }
        } else { /* FOCUS_DIFF */
            int dv=G.rows-4;
            switch(k.type){
            case KEY_UP:   
                if(G.diff_is_summary) G.diff_sel = imax(0, G.diff_sel-1);
                else G.diff_scroll=imax(0,G.diff_scroll-1);
                break;
            case KEY_DOWN: 
                if(G.diff_is_summary) G.diff_sel = imin(G.diff_count-1, G.diff_sel+1);
                else G.diff_scroll++;
                break;
            case KEY_PGUP: 
                if(G.diff_is_summary) G.diff_sel = imax(0, G.diff_sel-dv);
                else G.diff_scroll=imax(0,G.diff_scroll-dv);
                break;
            case KEY_PGDN: 
                if(G.diff_is_summary) G.diff_sel = imin(G.diff_count-1, G.diff_sel+dv);
                else G.diff_scroll+=dv;
                break;
            case KEY_HOME: 
                if(G.diff_is_summary) G.diff_sel = 0;
                else G.diff_scroll=0;
                break;
            case KEY_END:  
                if(G.diff_is_summary) G.diff_sel = G.diff_count-1;
                else G.diff_scroll=G.diff_count;
                break;
            case KEY_CTRL_C:
                if(G.selecting) action_copy_selection();
                break;
            case KEY_ENTER:
                if(G.diff_is_summary && G.diff_count > 0){
                    DiffLine *dl = &G.diff_lines[G.diff_sel];
                    if(dl->type == 5){
                        if(strncmp(G.diff_title, "Files:", 6) == 0){
                            editor_load(dl->new_line);
                            G.editor_active = true; G.focus = FOCUS_EDITOR;
                        } else if(strncmp(G.diff_title, "Search:", 7) == 0){
                            char lb[LINE_MAX_LEN]; snprintf(lb, sizeof(lb), "%s", dl->new_line);
                            char *c1 = strchr(lb, ':');
                            if(c1 && isdigit(c1[1])){
                                *c1 = '\0';
                                int lno = atoi(c1+1);
                                editor_load(lb);
                                CUR_ED.cur_y = imax(0, lno - 1);
                                CUR_ED.cur_x = 0;
                                G.editor_active = true; G.focus = FOCUS_EDITOR;
                            }
                        } else {
                            char fpath[LINE_MAX_LEN]; snprintf(fpath, sizeof(fpath), "%s", dl->new_line);
                            const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, G.diff_commit, fpath);
                            char *o = git_run(cmd);
                            snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", G.diff_commit, fpath);
                            G.diff_is_summary = false;
                            parse_diff(o?o:""); free(o);
                        }
                    }
                }
                break;
            case KEY_CHAR:
                if(k.ch=='s'||k.ch=='S')G.diff_sidebyside=!G.diff_sidebyside;
                else if(k.ch=='[') { G.diff_split_custom = G.diff_split - 4; layout(); }
                else if(k.ch==']') { G.diff_split_custom = G.diff_split + 4; layout(); }
                else if(k.ch=='<') { G.diff_split_custom = G.diff_split - 1; layout(); }
                else if(k.ch=='>') { G.diff_split_custom = G.diff_split + 1; layout(); }
                else if(k.ch=='q' && !G.diff_is_summary && G.diff_commit[0]) {
                    load_commit_summary(G.diff_commit);
                }
                break;
            default:break;
            }
        }
        break;
    }
    /* ──── LOG ──── */
    case VIEW_LOG:{
        int lh=(G.rows-2)*55/100-2;
        bool df=(G.focus==FOCUS_DIFF);
        if(k.type==KEY_LEFT||k.type==KEY_RIGHT){G.focus=(G.focus==FOCUS_DIFF)?FOCUS_CHANGES:FOCUS_DIFF;return;}
        if(!df){
            int cnt=G.commit_count;
            switch(k.type){
            case KEY_UP: {
                if(G.graph_file_sel > -1) {
                    G.graph_file_sel--;
                } else {
                    int old_sel = G.commit_sel;
                    msel(&G.commit_sel, &G.commit_scroll, cnt, -1, lh, true);
                    if(old_sel != G.commit_sel && G.commits[G.commit_sel].expanded && G.commits[G.commit_sel].f_count > 0) {
                        G.graph_file_sel = G.commits[G.commit_sel].f_count - 1;
                    }
                }
                break;
            }
            case KEY_DOWN: {
                GitCommit *c = &G.commits[G.commit_sel];
                if(c->expanded && G.graph_file_sel < c->f_count - 1) {
                    G.graph_file_sel++;
                } else {
                    msel(&G.commit_sel, &G.commit_scroll, cnt, 1, lh, true);
                    G.graph_file_sel = -1;
                }
                break;
            }
            case KEY_PGUP: msel(&G.commit_sel,&G.commit_scroll,cnt,-lh/2,lh,true); G.graph_file_sel=-1; break;
            case KEY_PGDN: msel(&G.commit_sel,&G.commit_scroll,cnt, lh/2,lh,true); G.graph_file_sel=-1; break;
            case KEY_HOME: G.commit_sel=0;G.commit_scroll=0; G.graph_file_sel=-1; break;
            case KEY_END:  G.commit_sel=cnt?cnt-1:0; G.graph_file_sel=-1; break;
            case KEY_ENTER:
                if(G.commit_count>0){
                    GitCommit *c=&G.commits[G.commit_sel];
                    if(G.graph_file_sel == -1) {
                        snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                        G.diff_is_summary = false; load_diff_commit(c->hash); G.focus=FOCUS_DIFF;
                    } else if(G.graph_file_sel == 0) {
                        snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                        G.diff_is_summary = false; load_diff_commit(c->hash); G.focus=FOCUS_DIFF;
                    } else {
                        char *fpath = c->files[G.graph_file_sel];
                        const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                        char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, c->hash, fpath);
                        char *o = git_run(cmd);
                        snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", c->hash, fpath);
                        G.diff_is_summary = false;
                        snprintf(G.diff_commit, sizeof(G.diff_commit), "%s", c->hash);
                        parse_diff(o?o:""); free(o);
                        G.focus=FOCUS_DIFF;
                    }
                }
                break;
            case KEY_CHAR:
                if(k.ch==' ') {
                    if(G.commit_count > 0) {
                        G.commits[G.commit_sel].expanded = !G.commits[G.commit_sel].expanded;
                        if(G.commits[G.commit_sel].expanded) fetch_commit_files(G.commit_sel);
                    }
                }
                else if(k.ch=='n')action_new_branch();
                break;
            default:break;
            }
            /* auto-preview */
            if(G.commit_count>0 && k.type != KEY_ENTER){
                sync_graph_preview();
            }
        } else {
            int dv=G.rows-4;
            switch(k.type){
            case KEY_UP:   
                if(G.diff_is_summary) G.diff_sel = imax(0, G.diff_sel-1);
                else G.diff_scroll=imax(0,G.diff_scroll-1);
                break;
            case KEY_DOWN: 
                if(G.diff_is_summary) G.diff_sel = imin(G.diff_count-1, G.diff_sel+1);
                else G.diff_scroll++;
                break;
            case KEY_PGUP: 
                if(G.diff_is_summary) G.diff_sel = imax(0, G.diff_sel-dv);
                else G.diff_scroll=imax(0,G.diff_scroll-dv);
                break;
            case KEY_PGDN: 
                if(G.diff_is_summary) G.diff_sel = imin(G.diff_count-1, G.diff_sel+dv);
                else G.diff_scroll+=dv;
                break;
            case KEY_ENTER:
                if(G.diff_is_summary && G.diff_count > 0){
                    DiffLine *dl = &G.diff_lines[G.diff_sel];
                    if(dl->type == 5){
                        if(strncmp(G.diff_title, "Files:", 6) == 0){
                            editor_load(dl->new_line);
                            G.editor_active = true; G.focus = FOCUS_EDITOR;
                        } else if(strncmp(G.diff_title, "Search:", 7) == 0){
                            char lb[LINE_MAX_LEN]; snprintf(lb, sizeof(lb), "%s", dl->new_line);
                            char *c1 = strchr(lb, ':');
                            if(c1 && isdigit(c1[1])){
                                *c1 = '\0';
                                int lno = atoi(c1+1);
                                editor_load(lb);
                                CUR_ED.cur_y = imax(0, lno - 1);
                                CUR_ED.cur_x = 0;
                                G.editor_active = true; G.focus = FOCUS_EDITOR;
                            }
                        } else {
                            char fpath[LINE_MAX_LEN]; snprintf(fpath, sizeof(fpath), "%s", dl->new_line);
                            const char *ctx_ = G.diff_continuous ? "-U1000" : "-U3";
                            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, G.diff_commit, fpath);
                            char *o = git_run(cmd);
                            snprintf(G.diff_title, sizeof(G.diff_title), "commit %s: %s", G.diff_commit, fpath);
                            G.diff_is_summary = false;
                            parse_diff(o?o:""); free(o);
                        }
                    }
                }
                break;
            case KEY_CHAR:
                if(k.ch=='s') G.diff_sidebyside=!G.diff_sidebyside;
                else if(k.ch=='[') { G.diff_split_custom = G.diff_split - 4; layout(); }
                else if(k.ch==']') { G.diff_split_custom = G.diff_split + 4; layout(); }
                else if(k.ch=='<') { G.diff_split_custom = G.diff_split - 1; layout(); }
                else if(k.ch=='>') { G.diff_split_custom = G.diff_split + 1; layout(); }
                else if(k.ch=='q' && !G.diff_is_summary && G.diff_commit[0]) {
                    load_commit_summary(G.diff_commit);
                }
                break;
            default:break;
            }
        }
        break;
    }
    /* ──── BRANCHES ──── */
    case VIEW_BRANCHES:{
        int cnt=G.branch_count;
        switch(k.type){
        case KEY_UP:   msel(&G.branch_sel,&G.branch_scroll,cnt,-1,vis,false);break;
        case KEY_DOWN: msel(&G.branch_sel,&G.branch_scroll,cnt, 1,vis,false);break;
        case KEY_PGUP: msel(&G.branch_sel,&G.branch_scroll,cnt,-vis/2,vis,false);break;
        case KEY_PGDN: msel(&G.branch_sel,&G.branch_scroll,cnt, vis/2,vis,false);break;
        case KEY_HOME: G.branch_sel=0;G.branch_scroll=0;break;
        case KEY_END:  G.branch_sel=cnt>0?cnt-1:0;break;
        case KEY_ENTER:action_checkout();break;
        case KEY_CHAR:if(k.ch=='n')action_new_branch();else if(k.ch=='D')action_delete_branch();break;
        default:break;
        }
        break;
    }
    /* ──── STASH ──── */
    case VIEW_STASH:{
        int cnt=G.stash_count;
        int dummy=0;
        switch(k.type){
        case KEY_UP:   msel(&G.stash_sel,&dummy,cnt,-1,vis,false);break;
        case KEY_DOWN: msel(&G.stash_sel,&dummy,cnt, 1,vis,false);break;
        case KEY_ENTER:action_apply_stash();break;
        case KEY_CHAR:
            if(k.ch=='p')action_pop_stash();
            else if(k.ch=='D')action_drop_stash();
            else if(k.ch=='s')action_stash();
            break;
        default:break;
        }
        break;
    }
    case VIEW_HELP:if(k.type==KEY_CHAR&&k.ch=='q')G.current_view=VIEW_STATUS;break;
    default:break;
    }
}

/* ================================================================
   MAIN
================================================================ */
static bool in_git_repo(void){
    char *o=git_run("git rev-parse --git-dir 2>/dev/null");
    bool ok=o&&o[0]; free(o); return ok;
}

int main(int argc, char **argv){
    (void)argc;(void)argv;
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)){fprintf(stderr,"tuide: requires a terminal\n");return 1;}
    if(!in_git_repo()){fprintf(stderr,"tuide: not a git repository\n");return 1;}

    memset(&G,0,sizeof(G));
    G.running=true; G.current_view=VIEW_STATUS; G.focus=FOCUS_CHANGES;
    G.theme_idx=0;
    G.clipboard=NULL;
    G.col_hash_w = 9; G.col_author_w = 14; G.col_date_w = 13;
    G.editor_active=false; G.browser_active=false;
    G.diff_sidebyside=true; G.diff_continuous=false;

    signal(SIGWINCH,sig_winch);
    signal(SIGINT, sig_int);
    signal(SIGTERM,sig_int);
    signal(SIGPIPE,SIG_IGN);

    get_winsize();
    term_raw();
    printf(T_ALT T_HIDE T_MOUSE_ON T_CLEAR);
    fflush(stdout);

    /* Rendering init */
    buf_resize(&G.front, G.cols, G.rows);
    buf_resize(&G.back, G.cols, G.rows);
    memset(G.front.cells, 0, G.front.w * G.front.h * sizeof(Cell));

    load_branch(); load_status(); load_log(); load_branches(); load_stash();
    update_diff();

    OK("tuide v%s — Tab:focus  T:theme  V:vi-mode  s:side-by-side  ?:help  q:quit", VERSION);

    draw(); draw();
    while(G.running){
        Key k=read_key();
        if(k.type!=KEY_NONE){
            handle_key(k);
            draw();
        } else if(g_resize){
            g_resize=0; get_winsize(); printf(T_CLEAR);
            draw();
        }
    }

    printf(T_NORM T_SHOW T_MOUSE_OFF T_RESET);
    term_restore();
    free(G.front.cells); free(G.back.cells);
    printf("tuide — bye!\n");
    return 0;
}
