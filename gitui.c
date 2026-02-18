
/*
 * gitui v2.0 - Modern Git TUI, pure C99, zero dependencies
 *
 *  Layout (Status view):
 *  ┌─── Changes ───┬──────────── Side-by-Side Diff ────────────┐
 *  │ staged files  │  old (left)         │  new (right)        │
 *  │ unstaged files│                     │                      │
 *  ├─── Graph ─────┤                     │                      │
 *  │ commit graph  │                     │                      │
 *  └───────────────┴─────────────────────┴──────────────────────┘
 *
 *  Themes: T cycles Dark+ / VS-Light-Blue / Solarized-Dark
 *  Vi mode: normal/insert with full motion set
 *  Mouse:   click to focus panes, scroll wheel, click to select
 *
 *  Build:  cc -std=c99 -O2 -o gitui gitui.c
 *  Run:    ./gitui
 */

#define _POSIX_C_SOURCE 200809L
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

/* ================================================================
   CONSTANTS
================================================================ */
#define VERSION        "2.5.5"
#define MAX_FILES      512
#define MAX_COMMITS    512
#define MAX_BRANCHES   256
#define MAX_STASHES    64
#define MAX_DIFF_LINES 8192
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

static void fg(int r, int g_, int b) { printf(CSI "38;2;%d;%d;%dm", r, g_, b); }
static void bg(int r, int g_, int b) { printf(CSI "48;2;%d;%d;%dm", r, g_, b); }

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

static const Theme *THEMES[] = {&TH_DARK, &TH_VSLIGHT, &TH_SOL};
#define NTHEMES 3

/* ================================================================
   ENUMS & STRUCTS
================================================================ */
typedef enum { VIEW_STATUS,VIEW_LOG,VIEW_BRANCHES,VIEW_STASH,VIEW_HELP,VIEW_COUNT } View;
typedef enum { VIMODE_NORMAL,VIMODE_INSERT } ViMode;
typedef enum { FOCUS_CHANGES,FOCUS_GRAPH,FOCUS_DIFF,FOCUS_CLI } FocusPane;
typedef enum {
    FS_UNTRACKED,FS_MODIFIED,FS_STAGED_MODIFY,FS_STAGED_NEW,
    FS_STAGED_DEL,FS_DELETED,FS_RENAMED,FS_CONFLICT,FS_COPIED
} FileStatus;

typedef struct { char path[512]; char orig[512]; FileStatus st; bool staged; } GitFile;
typedef struct {
    char hash[16],author[64],email[64],date[32],subject[256],refs[192];
    char graph[24];
    int  graph_col;
} GitCommit;
typedef struct { char name[128],upstream[128]; bool is_remote,is_current; int ahead,behind; } GitBranch;
typedef struct { char message[256],hash[16]; int index; } GitStash;
typedef struct {
    char old_line[LINE_MAX_LEN], new_line[LINE_MAX_LEN];
    int old_lno, new_lno;
    int type; /* 0=ctx 1=add 2=del 3=hunk 4=fhdr */
} DiffLine;

typedef struct { int btn,col,row; bool release,shift,ctrl; } MouseEvt;
typedef enum {
    KEY_NONE=0, KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,
    KEY_PGUP,KEY_PGDN,KEY_HOME,KEY_END,
    KEY_ENTER,KEY_ESC,KEY_BACKSPACE,KEY_DEL,
    KEY_TAB,KEY_SHIFT_TAB,
    KEY_CTRL_A,KEY_CTRL_B,KEY_CTRL_C,KEY_CTRL_D,KEY_CTRL_E,
    KEY_CTRL_F,KEY_CTRL_K,KEY_CTRL_N,KEY_CTRL_P,
    KEY_CTRL_U,KEY_CTRL_W,KEY_CTRL_Y,
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

    /* Vi */
    ViMode vi_mode;
    bool   vi_enabled;
    int    vi_count;
    bool   vi_gg_pending;

    /* View / focus */
    View      current_view;
    FocusPane focus;

    /* Changes */
    GitFile files[MAX_FILES];
    int file_count, file_sel, file_scroll;

    /* Log/graph */
    GitCommit commits[MAX_COMMITS];
    int commit_count, commit_sel, commit_scroll;

    /* Diff */
    DiffLine diff_lines[MAX_DIFF_LINES];
    int diff_count, diff_scroll, diff_hscroll;
    char diff_title[512];
    bool diff_staged, diff_sidebyside;

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

    /* Layout */
    int lw, lh_chg, lh_gph, rx, rw;
    int lw_custom, lh_chg_custom;
    bool dragging_v, dragging_h;
    int  tab_x[6];

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
    memset(b->cells, 0, b->w * b->h * sizeof(Cell));
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
        int i=0; while(*s && i<7) cell->ch[i++] = *s++;
    } else {
        cell->ch[0] = ' ';
    }
    cell->fg = G.cur_fg; cell->bg = G.cur_bg;
    cell->bold = G.cur_bold; cell->dim = G.cur_dim; cell->italic = G.cur_italic;
    cell->under = G.cur_under; cell->rev = G.cur_rev;
    G.cur_c = c + 1;
}

static void put_char(int r, int c, char ch){
    char s[2] = {ch, 0};
    put_cell(r, c, s);
}

static void at(int r,int c){ G.cur_r = r; G.cur_c = c; }
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
            
            bool attr_changed = (b->bold != last_bold || b->dim != last_dim || 
                                b->italic != last_italic || b->under != last_under || b->rev != last_rev);
            bool fg_changed = (memcmp(&b->fg, &last_fg, sizeof(Color)) != 0);
            bool bg_changed = (memcmp(&b->bg, &last_bg, sizeof(Color)) != 0);

            if(attr_changed || fg_changed || bg_changed){
                printf(CSI "0m");
                /* After reset, all attributes are unknown/default */
                last_fg.r = last_fg.g = last_fg.b = -1;
                last_bg.r = last_bg.g = last_bg.b = -1;
                last_bold = last_dim = last_italic = last_under = last_rev = false;

                printf(CSI "38;2;%d;%d;%dm", b->fg.r, b->fg.g, b->fg.b);
                printf(CSI "48;2;%d;%d;%dm", b->bg.r, b->bg.g, b->bg.b);
                if(b->bold) printf(T_BOLD);
                if(b->dim) printf(T_DIM);
                if(b->italic) printf(T_ITALIC);
                if(b->under) printf(T_UNDER);
                if(b->rev) printf(T_REVERSE);
                last_fg = b->fg; last_bg = b->bg;
                last_bold=b->bold; last_dim=b->dim; last_italic=b->italic;
                last_under=b->under; last_rev=b->rev;
            }
            
            if(b->ch[0]) fputs(b->ch, stdout); else putchar(' ');
            *f = *b;
            tr = r; tc = c+1;
        }
    }
    fflush(stdout);
}

/* Print string padded/truncated to exactly w visible chars */
static void ppad(const char *s,int w){
    if(w<=0)return;
    int vis=0;
    while(*s&&vis<w){
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
        vis++;
    }
    while(vis<w){put_cell(G.cur_r, G.cur_c, " "); vis++;}
}

/* ================================================================
   LAYOUT
================================================================ */
static void layout(void){
    if(G.lw_custom > 0) G.lw = iclamp(G.lw_custom, 20, G.cols-20);
    else G.lw=imax(26,imin(48,G.cols*32/100));
    
    G.rx=G.lw; G.rw=G.cols-G.lw+1;
    int ch=G.rows-2;
    
    if(G.lh_chg_custom > 0) G.lh_chg = iclamp(G.lh_chg_custom, 4, ch-4);
    else G.lh_chg=imax(5,ch*58/100);
    
    G.lh_gph=ch-G.lh_chg+1;
    if(G.lh_gph<4){G.lh_gph=4; G.lh_chg=ch-G.lh_gph+1;}
}

/* ================================================================
   SIGNAL HANDLERS
================================================================ */
static void sig_winch(int s){(void)s;g_resize=1;}
static void sig_int(int s){(void)s;G.running=false;}

/* ================================================================
   DATA LOADING
================================================================ */
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
        DiffLine *dl=&G.diff_lines[G.diff_count]; memset(dl,0,sizeof(*dl));
        char lb[LINE_MAX_LEN]; if(len>=LINE_MAX_LEN)len=LINE_MAX_LEN-1;
        memcpy(lb,line,len); lb[len]='\0';
        if(lb[0]=='+'&&lb[1]=='+'&&lb[2]=='+'){
            dl->type=4; snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb);
        } else if(lb[0]=='-'&&lb[1]=='-'&&lb[2]=='-'){
            dl->type=4; snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb);
        } else if(lb[0]=='@'){
            dl->type=3;
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb);
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb);
            int om=0,nm=0;
            sscanf(lb,"@@ -%d",&om);
            sscanf(lb,"@@ -%*d,%*d +%d",&nm);
            if(!om)sscanf(lb,"@@ -%d,",&om);
            if(!nm)sscanf(lb,"@@ -%*d +%d",&nm);
            old_lno=om>0?om:1; new_lno=nm>0?nm:1;
        } else if(lb[0]=='+'){
            dl->type=1; dl->new_lno=new_lno++;
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",lb+1);
        } else if(lb[0]=='-'){
            dl->type=2; dl->old_lno=old_lno++;
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",lb+1);
        } else {
            dl->type=0; dl->old_lno=old_lno++; dl->new_lno=new_lno++;
            const char *src=(lb[0]==' ')?lb+1:lb;
            snprintf(dl->old_line,sizeof(dl->old_line),"%s",src);
            snprintf(dl->new_line,sizeof(dl->new_line),"%s",src);
        }
        G.diff_count++; line=nl?nl+1:line+strlen(line);
    }
    G.diff_scroll=0; G.diff_hscroll=0;
}
static void load_diff_file(const char *path,bool staged){
    char cmd[1024];
    if(staged)snprintf(cmd,sizeof(cmd),"git diff --cached -- '%s' 2>/dev/null",path);
    else       snprintf(cmd,sizeof(cmd),"git diff -- '%s' 2>/dev/null",path);
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
    char cmd[256]; snprintf(cmd,sizeof(cmd),"git show %s 2>/dev/null",hash);
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
static void reload_all(void){
    load_branch(); load_status(); load_log(); load_branches(); load_stash();
    update_diff(); OK("Refreshed");
}

/* ================================================================
   BOX DRAWING HELPERS
================================================================ */
static void box_top(int row,int col,int w,const char *title,bool active){
    at(row,col);
    if(active){G.cur_bold=true;cfg(TH->fg_accent1);}else cfg(TH->fg_dim);
    put_cell(row,col,"┌");
    int tlen=(int)strlen(title)+2;
    int left=(w-2-tlen)/2; if(left<0)left=0;
    for(int i=0;i<left;i++)put_cell(row,col+1+i,"─");
    
    if(active){G.cur_bold=true;cfg(TH->fg_bright);}else cfg(TH->fg_dim);
    at(row, col+1+left);
    ppad(title, tlen);
    
    if(active){G.cur_bold=true;cfg(TH->fg_accent1);}else{rst();cfg(TH->fg_dim);}
    int right=w-2-left-tlen;
    for(int i=0;i<right;i++)put_cell(row, col+1+left+tlen+i,"─");
    put_cell(row, col+w-1, "┐");
    rst();
}
static void box_bot(int row,int col,int w){
    at(row,col); cfg(TH->fg_dim); put_cell(row,col,"└");
    for(int i=0;i<w-2;i++)put_cell(row,col+1+i,"─");
    put_cell(row,col+w-1,"┘"); rst();
}
static void box_sides(int top,int col,int w,int h){
    cfg(TH->fg_dim);
    for(int r=top+1;r<top+h-1;r++){put_cell(r,col,"│");put_cell(r,col+w-1,"│");}
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
    if(x<1)x=1; if(y<1)y=1;
    G.menu_x=x; G.menu_y=y;

    box_top(y,x,w,"Menu",true);
    box_sides(y,x,w,h);
    box_fill(y,x,w,h,TH->bg_panel);
    box_bot(y+h-1,x,w);
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
    
    char buf[INPUT_MAX+1];
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
    if(G.current_view != VIEW_STATUS) return;
    int ct=2;
    /* Vertical divider */
    if(G.lw >= 1 && G.lw <= G.cols){
        for(int r=ct; r<G.rows-1; r++){
            at(r, G.lw);
            bool hover = (G.last_mx == G.lw && G.last_my == r);
            if(G.dragging_v || hover){cfg(TH->fg_accent1); G.cur_bold=true; put_cell(r, G.lw, "┃");}
            else {cfg(TH->fg_dim); put_cell(r, G.lw, "│");}
            rst();
        }
    }
    /* Horizontal divider */
    int hr = ct + G.lh_chg - 1;
    if(hr >= 1 && hr < G.rows){
        for(int c=1; c<G.lw; c++){
            at(hr, c);
            bool hover = (G.last_my == hr && G.last_mx == c);
            if(G.dragging_h || hover){cfg(TH->fg_accent1); G.cur_bold=true; put_cell(hr, c, "━");}
            else {cfg(TH->fg_dim); put_cell(hr, c, "─");}
            rst();
        }
    }
}

static void draw_tabbar(void){
    at(1,1); cbg(TH->bg_tab_inact); cfg(TH->fg_accent2); G.cur_bold=true;
    ppad(" ⎇ gitui ", 9); rst();
    static const struct{const char *n,*k;View v;}tabs[]={
        {"Changes","1",VIEW_STATUS},{"Log","2",VIEW_LOG},
        {"Branches","3",VIEW_BRANCHES},{"Stash","4",VIEW_STASH},{"Help","?",VIEW_HELP}
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
    /* Vi indicator */
    if(G.vi_enabled){
        at(1, cur_c);
        if(G.vi_mode==VIMODE_NORMAL){cbg(TH->bg_tab_inact);cfg(TH->fg_staged);G.cur_bold=true;ppad(" NORMAL ", 8);}
        else{cbg(TH->bg_tab_inact);cfg(TH->fg_unstaged);G.cur_bold=true;ppad(" INSERT ", 8);}
        cur_c += 8;
    }
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
    const char *hint="";
    if(G.current_view==VIEW_STATUS){
        if(G.focus==FOCUS_CHANGES) hint="SPC:stage  a:stage-all  u:unstage  d:discard  ↵:diff  c:commit  P:push  f:pull  T:theme  V:vi";
        else if(G.focus==FOCUS_GRAPH) hint="j/k:move  ↵:diff  g/G:top/bot  T:theme";
        else hint="j/k:scroll  [/]:hscroll  s:side-by-side  q:back  T:theme";
    } else if(G.current_view==VIEW_LOG) hint="j/k:move  ↵:diff  n:branch  s:side-by-side  T:theme";
    else if(G.current_view==VIEW_BRANCHES) hint="↵:checkout  n:new  D:delete";
    else if(G.current_view==VIEW_STASH) hint="↵:apply  p:pop  D:drop  s:stash";
    else if(G.current_view==VIEW_HELP) hint="q:close help";
    
    cfg(TH->fg_dim);
    ppad(" ", 1);
    ppad(hint, G.cols-2);
    
    if(G.vi_enabled&&G.vi_count>0){
        at(G.rows,G.cols-12); cfg(TH->fg_accent1); G.cur_bold=true;
        char vbuf[16]; snprintf(vbuf,sizeof(vbuf),"[%d]",G.vi_count);
        ppad(vbuf, (int)strlen(vbuf));
    }
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

/* ================================================================
   CHANGES PANE
================================================================ */
static void draw_changes(int top,int h){
    if(h<=2) return;
    int w=G.lw;
    bool act=(G.focus==FOCUS_CHANGES&&G.current_view==VIEW_STATUS);
    box_top(top,1,w,"Changes",act);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_panel);

    int staged_n=0,unstaged_n=0;
    for(int i=0;i<G.file_count;i++) G.files[i].staged?staged_n++:unstaged_n++;

    int row=top+1, lim=top+h-1, iw=w-2;

    /* ── Staged section ── */
    if(row<lim){
        at(row,2); cbg(TH->bg_header); cfg(TH->fg_staged); G.cur_bold=true;
        char hdr[64]; snprintf(hdr,sizeof(hdr)," ✓ Staged (%d) ",staged_n);
        ppad(hdr,iw); rst(); row++;
    }
    for(int i=0;i<G.file_count&&row<lim;i++){
        if(!G.files[i].staged)continue;
        bool sel=(G.file_sel==i&&act);
        at(row,2);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}else cbg(TH->bg_panel);
        cfg(sel?TH->fg_sel:TH->fg_staged);
        const char *ic=" M";
        switch(G.files[i].st){
            case FS_STAGED_NEW:ic=" A";break; case FS_STAGED_DEL:ic=" D";break;
            case FS_RENAMED:ic=" R";break;    case FS_COPIED:ic=" C";break;
            default:ic=" M";break;
        }
        ppad(ic, 2); ppad(" ", 1);
        if(sel){cfg(TH->fg_sel);cbg(TH->bg_sel);}else{cfg(TH->fg_normal);cbg(TH->bg_panel);}
        char disp[512];
        if(G.files[i].orig[0])snprintf(disp,sizeof(disp),"%s→%s",G.files[i].orig,G.files[i].path);
        else snprintf(disp,sizeof(disp),"%s",G.files[i].path);
        ppad(disp,iw-3); rst(); row++;
    }
    /* spacer */
    if(row<lim){at(row,2);cbg(TH->bg_panel);for(int i=0;i<iw;i++)put_cell(row,2+i," ");rst();row++;}

    /* ── Unstaged section ── */
    if(row<lim){
        at(row,2); cbg(TH->bg_header); cfg(TH->fg_unstaged); G.cur_bold=true;
        char hdr[64]; snprintf(hdr,sizeof(hdr)," ✗ Unstaged (%d) ",unstaged_n);
        ppad(hdr,iw); rst(); row++;
    }
    for(int i=0;i<G.file_count&&row<lim;i++){
        if(G.files[i].staged)continue;
        bool sel=(G.file_sel==i&&act);
        at(row,2);
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);G.cur_bold=true;}else cbg(TH->bg_panel);
        Color ic_col=TH->fg_unstaged;
        const char *ic=" M";
        switch(G.files[i].st){
            case FS_UNTRACKED:ic=" ?";ic_col=TH->fg_untracked;break;
            case FS_DELETED:ic=" D";break;
            case FS_CONFLICT:ic=" !";ic_col=TH->fg_conflict;break;
            default:ic=" M";break;
        }
        cfg(sel?TH->fg_sel:ic_col); ppad(ic, 2); ppad(" ", 1);
        if(sel){cfg(TH->fg_sel);cbg(TH->bg_sel);}else{cfg(TH->fg_normal);cbg(TH->bg_panel);}
        ppad(G.files[i].path,iw-3); rst(); row++;
    }
    /* fill */
    while(row<lim){at(row,2);cbg(TH->bg_panel);for(int i=0;i<iw;i++)put_cell(row,2+i," ");rst();row++;}
    box_bot(top+h-1,1,w);
}

/* ================================================================
   GRAPH PANE
================================================================ */
static void draw_graph(int top,int h){
    if(h<=2) return;
    int w=G.lw;
    bool act=(G.focus==FOCUS_GRAPH&&G.current_view==VIEW_STATUS);
    box_top(top,1,w,"Graph",act);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_base);

    int row=top+1,lim=top+h-1,iw=w-2;
    int vis=lim-row;
    if(vis<=0) { box_bot(top+h-1,1,w); return; }

    if(G.commit_sel<G.commit_scroll)G.commit_scroll=G.commit_sel;
    if(G.commit_sel>=G.commit_scroll+vis)G.commit_scroll=G.commit_sel-vis+1;

    for(int i=G.commit_scroll;i<G.commit_count&&row<lim;i++,row++){
        GitCommit *c=&G.commits[i];
        bool sel=(G.commit_sel==i);
        at(row,2);
        if(sel){cbg(TH->bg_sel); G.cur_bold=true;}else cbg(TH->bg_base);

        /* Graph chars */
        char *gp=c->graph; int gc=0;
        while(*gp&&gc<GRAPH_COLS){
            int ci_=(c->graph_col/2)%6;
            at(row, 2+gc);
            if(*gp=='*'){
                cfg(TH->fg_graph[ci_]); G.cur_bold=true; put_cell(row, 2+gc, "●");
                rst(); if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);
            } else if(*gp=='|'){cfg(TH->fg_graph[gc/2%6]);put_cell(row, 2+gc, "|");}
            else if(*gp=='/'){cfg(TH->fg_graph[1]);put_cell(row, 2+gc, "/");}
            else if(*gp=='\\'){cfg(TH->fg_graph[2]);put_cell(row, 2+gc, "\\");}
            else if(*gp=='-'){cfg(TH->fg_graph[2]);put_cell(row, 2+gc, "-");}
            else { char tmp[2]={*gp,0}; put_cell(row, 2+gc, tmp); }
            rst(); if(sel){cbg(TH->bg_sel);G.cur_bold=true;}else cbg(TH->bg_base);
            gp++; gc++;
        }
        while(gc<GRAPH_COLS){at(row, 2+gc); put_cell(row, 2+gc, " "); gc++;}

        at(row, 2+GRAPH_COLS);
        cfg(sel?TH->fg_sel:TH->fg_accent1); G.cur_bold=true;
        char hbuf[16]; snprintf(hbuf,sizeof(hbuf),"%.8s ",c->hash);
        ppad(hbuf, 9); rst();
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);}else cbg(TH->bg_base);

        int used=GRAPH_COLS+9;
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
    }
    while(row<lim){at(row,2);cbg(TH->bg_base);for(int i=0;i<iw;i++)put_cell(row,2+i," ");rst();row++;}
    box_bot(top+h-1,1,w);
}

/* ================================================================
   DIFF PANE (side-by-side + unified)
================================================================ */
static void draw_diff(int top,int rx,int rw,int h){
    if(h<=2) return;
    bool act=(G.focus==FOCUS_DIFF);
    char title[128];
    if(G.diff_title[0])snprintf(title,sizeof(title),"%.60s%s",G.diff_title,G.diff_staged?" [staged]":"");
    else snprintf(title,sizeof(title),"Diff (select file or commit)");

    box_top(top,rx,rw,title,act);
    box_sides(top,rx,rw,h);
    box_fill(top,rx,rw,h,TH->bg_base);
    box_bot(top+h-1,rx,rw);

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
    int half=(rw-2)/2;
    int code_w=ssb?(half-lnum_w-2):(rw-2-lnum_w-2);
    if(code_w<8)code_w=8;

    /* Column headers for side-by-side */
    if(ssb){
        /* Center divider */
        cfg(TH->fg_dim);
        for(int r=row+1;r<lim;r++){at(r,rx+half);put_cell(r, rx+half, "│");}
        /* Header */
        at(row,rx+1); cbg(TH->bg_header); cfg(TH->fg_accent3); G.cur_bold=true;
        ppad(" ◀ OLD",half-1);
        at(row,rx+half+1); ppad(" NEW ▶ ",half-1);
        rst(); row++; lim--; vis--;
        if(G.diff_scroll>imax(0,G.diff_count-vis))G.diff_scroll=imax(0,G.diff_count-vis);
    }

    int di=G.diff_scroll;
    if(!ssb){
        /* Unified */
        for(;di<G.diff_count&&row<lim;di++,row++){
            DiffLine *dl=&G.diff_lines[di];
            at(row,rx+1);
            char lno[16];
            switch(dl->type){
            case 0: cbg(TH->bg_base);cfg(TH->fg_linenum);snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno);ppad(lno,lnum_w+1);cfg(TH->fg_diff_ctx);ppad(dl->old_line,code_w);break;
            case 1: cbg(TH->bg_diff_add);cfg(TH->fg_linenum);if(dl->new_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->new_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");ppad(lno,lnum_w+1);cfg(TH->fg_diff_add);G.cur_bold=true;ppad("+",1);ppad(dl->new_line,code_w-1);break;
            case 2: cbg(TH->bg_diff_del);cfg(TH->fg_linenum);if(dl->old_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");ppad(lno,lnum_w+1);cfg(TH->fg_diff_del);G.cur_bold=true;ppad("-",1);ppad(dl->old_line,code_w-1);break;
            case 3: cbg(TH->bg_diff_hdr);cfg(TH->fg_diff_hdr);G.cur_bold=true;ppad(dl->new_line,code_w+lnum_w+2);break;
            case 4: cbg(TH->bg_header);cfg(TH->fg_accent2);G.cur_bold=true;ppad(dl->new_line[0]?dl->new_line:dl->old_line,code_w+lnum_w+2);break;
            }
            rst();
        }
    } else {
        /* Side-by-side */
        while(di<G.diff_count&&row<lim){
            DiffLine *dl=&G.diff_lines[di];
            if(dl->type==3||dl->type==4){
                at(row,rx+1);
                if(dl->type==3){cbg(TH->bg_diff_hdr);cfg(TH->fg_diff_hdr);}
                else{cbg(TH->bg_header);cfg(TH->fg_accent2);}
                G.cur_bold=true;
                ppad(dl->new_line[0]?dl->new_line:dl->old_line,rw-2);
                rst(); di++; row++; continue;
            }
            if(dl->type==0){
                at(row,rx+1); cbg(TH->bg_base); cfg(TH->fg_linenum);
                char lno[16]; snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->old_lno); ppad(lno, lnum_w+1);
                cfg(TH->fg_diff_ctx); ppad(dl->old_line,code_w);
                at(row,rx+half+1); cbg(TH->bg_base); cfg(TH->fg_linenum);
                snprintf(lno,sizeof(lno),"%*d ",lnum_w,dl->new_lno); ppad(lno, lnum_w+1);
                cfg(TH->fg_diff_ctx); ppad(dl->new_line,code_w);
                rst(); di++; row++; continue;
            }
            DiffLine *od=NULL,*nd=NULL;
            if(dl->type==2){od=dl;if(di+1<G.diff_count&&G.diff_lines[di+1].type==1){nd=&G.diff_lines[di+1];di+=2;}else di++;}
            else if(dl->type==1){nd=dl;di++;}
            /* Left: old */
            at(row,rx+1);
            if(od){
                cbg(TH->bg_diff_del);cfg(TH->fg_linenum);
                char lno[16]; if(od->old_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,od->old_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");
                ppad(lno, lnum_w+1); cfg(TH->fg_diff_del); G.cur_bold=true; ppad(od->old_line,code_w);
            } else {
                cbg(TH->bg_base); for(int i=0;i<code_w+lnum_w+1;i++) put_cell(row, rx+1+i, " ");
            }
            rst();
            /* Right: new */
            at(row,rx+half+1);
            if(nd){
                cbg(TH->bg_diff_add);cfg(TH->fg_linenum);
                char lno[16]; if(nd->new_lno>0)snprintf(lno,sizeof(lno),"%*d ",lnum_w,nd->new_lno);else snprintf(lno,sizeof(lno),"%*s ",lnum_w,"");
                ppad(lno, lnum_w+1); cfg(TH->fg_diff_add); G.cur_bold=true; ppad(nd->new_line,code_w);
            } else {
                cbg(TH->bg_base); for(int i=0;i<code_w+lnum_w+1;i++) put_cell(row, rx+half+1+i, " ");
            }
            rst(); row++;
        }
    }
    /* Fill */
    while(row<lim){at(row,rx+1);cbg(TH->bg_base);for(int i=0;i<rw-2;i++)put_cell(row,rx+1+i," ");rst();row++;}
    /* Scrollbar */
    if(G.diff_count>vis&&vis>2){
        int bh=imax(1,(vis*vis)/G.diff_count);
        int bpos=maxsc>0?((G.diff_scroll*(vis-bh))/maxsc):0;
        for(int r=0;r<vis;r++){
            at(top+1+r,rx+rw-1);
            cfg(r>=bpos&&r<bpos+bh?TH->fg_accent2:TH->fg_dim);
            put_cell(top+1+r, rx+rw-1, r>=bpos&&r<bpos+bh?"█":"│");
        }
        rst();
    }
}

/* ================================================================
   LOG VIEW (full screen)
================================================================ */
static void draw_log(int top,int h){
    int w=G.cols;
    box_top(top,1,w,"Commit Log",true);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_base);
    box_bot(top+h-1,1,w);

    int row=top+1,lim=top+h-1,vis=lim-row;
    if(G.commit_sel<G.commit_scroll)G.commit_scroll=G.commit_sel;
    if(G.commit_sel>=G.commit_scroll+vis)G.commit_scroll=G.commit_sel-vis+1;

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

        at(row, 2+GRAPH_COLS);
        cfg(sel?TH->fg_sel:TH->fg_accent1);G.cur_bold=true; char hbuf[16]; snprintf(hbuf,sizeof(hbuf),"%.8s ",c->hash); ppad(hbuf, 9); rst();
        if(sel){cbg(TH->bg_sel);cfg(TH->fg_sel);}else cbg(TH->bg_base);
        if(c->refs[0]){
            cfg(sel?TH->fg_sel:TH->fg_ref_local);
            char rf[32];snprintf(rf,sizeof(rf),"(%.18s) ",c->refs);
            ppad(rf,21);
        } else {
            ppad("", 21);
        }
        cfg(sel?TH->fg_sel:TH->fg_accent2); ppad(c->author,14); ppad(" ", 1);
        cfg(sel?TH->fg_sel:TH->fg_accent3); ppad(c->date, 13); ppad(" ", 1);
        int used=GRAPH_COLS+10+21+14+15;
        int sw=w-3-used;
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
    box_top(top,1,w,"Branches",true);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w);

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
    box_top(top,1,w,title,true);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w);
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
    box_top(top,1,w,"Help & Keybindings",true);
    box_sides(top,1,w,h);
    box_fill(top,1,w,h,TH->bg_panel);
    box_bot(top+h-1,1,w);

    static const char *E[][2]={
        {"NAVIGATION",""},
        {"  Tab / Shift+Tab","Cycle views / focus panes"},
        {"  1-4 / ?","Jump: Changes, Log, Branches, Stash, Help"},
        {"  j/k  ↑/↓","Move selection"},
        {"  h/l  ←/→","Switch focus pane"},
        {"  g/G","Top / bottom (or gg in Vi mode)"},
        {"  Ctrl+D/U","Half-page down/up"},
        {"  PgUp/PgDn","Page scroll"},
        {"",""},
        {"CHANGES PANE",""},
        {"  Space","Stage / unstage selected file"},
        {"  a / u","Stage all / Unstage all"},
        {"  d","Discard changes (careful!)"},
        {"  Enter / =","View diff for file, switch to diff pane"},
        {"",""},
        {"DIFF PANE",""},
        {"  s","Toggle side-by-side / unified"},
        {"  j/k  ↑/↓","Scroll diff vertically"},
        {"  [ / ]","Horizontal scroll"},
        {"  g/G","Top / bottom of diff"},
        {"",""},
        {"GRAPH PANE (in Changes view)",""},
        {"  j/k","Move through commits"},
        {"  Enter","Show commit diff in diff pane"},
        {"",""},
        {"BRANCHES",""},
        {"  Enter","Checkout selected branch"},
        {"  n","Create new branch (prompt)"},
        {"  D","Delete branch (force)"},
        {"",""},
        {"STASH",""},
        {"  Enter","Apply stash"},
        {"  p","Pop stash (apply + drop)"},
        {"  D","Drop stash"},
        {"",""},
        {"GLOBAL",""},
        {"  c","Commit staged changes"},
        {"  A","Amend last commit"},
        {"  P","Push to remote"},
        {"  f","Fetch + pull"},
        {"  s","Stash working changes"},
        {"  R","Full refresh"},
        {"  T","Cycle theme: Dark+ → VS-Light → Solarized"},
        {"  V","Toggle Vi modal keybindings"},
        {"  ?","Toggle this help"},
        {"  q / Esc","Go back / quit"},
        {"",""},
        {"VI MODE  (toggle with V)",""},
        {"  i","Enter insert/action mode"},
        {"  Esc","Return to normal mode"},
        {"  3j / 5k","Numeric prefix (repeat count)"},
        {"  gg / G","Top / bottom"},
        {"  Ctrl+F/B","Page down/up"},
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
static void draw(void){
    layout();
    buf_clear(&G.back);
    rst();
    draw_tabbar();

    int ct=2, ch=G.rows-2;
    switch(G.current_view){
    case VIEW_STATUS:
        if(G.lh_chg > 2) draw_changes(ct, G.lh_chg);
        if(G.lh_gph > 2) draw_graph(ct+G.lh_chg, G.lh_gph);
        draw_diff(ct, G.rx, G.rw, ch);
        break;
    case VIEW_LOG:{
        int lh=ch*55/100, dh=ch-lh;
        draw_log(ct,lh);
        draw_diff(ct+lh,1,G.cols,dh);
        break;
    }
    case VIEW_BRANCHES: draw_branches(ct,ch); break;
    case VIEW_STASH:    draw_stash(ct,ch);    break;
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
    case 11:k.type=KEY_CTRL_K;return k;
    case 14:k.type=KEY_CTRL_N;return k;
    case 16:k.type=KEY_CTRL_P;return k;
    case 21:k.type=KEY_CTRL_U;return k;
    case 23:k.type=KEY_CTRL_W;return k;
    case 25:k.type=KEY_CTRL_Y;return k;
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
static void msel(int *sel,int *scr,int cnt,int d,int vis){
    int n=G.vi_count>0?G.vi_count:1;
    *sel=iclamp(*sel+d*n,0,cnt>0?cnt-1:0);
    if(*sel<*scr)*scr=*sel;
    if(*sel>=*scr+vis)*scr=*sel-vis+1;
    G.vi_count=0;
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

    if(m.release){G.dragging_v=false; G.dragging_h=false;}

    if(G.menu_active){
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

    if(motion){
        if(G.dragging_v){G.lw_custom=m.col; layout(); return;}
        if(G.dragging_h){G.lh_chg_custom=m.row-ct+1; layout(); return;}
        return;
    }

    if(right_cl){
        menu_reset(m.col, m.row);
        if(G.current_view==VIEW_STATUS){
            if(G.focus==FOCUS_CHANGES){
                menu_add_item("Stage", action_stage);
                menu_add_item("Stage All", action_stage_all);
                menu_add_item("Unstage All", action_unstage_all);
                menu_add_item("Discard", action_discard);
                menu_add_item("Stash", action_stash);
            } else {
                menu_add_item("Commit", action_commit);
                menu_add_item("Amend", action_amend);
                menu_add_item("Push", action_push);
                menu_add_item("Pull", action_pull);
            }
        } else if(G.current_view==VIEW_LOG){
            menu_add_item("Reload", reload_all);
            menu_add_item("Push", action_push);
            menu_add_item("Pull", action_pull);
        } else if(G.current_view==VIEW_BRANCHES){
            menu_add_item("Checkout", action_checkout);
            menu_add_item("New Branch", action_new_branch);
            menu_add_item("Delete Branch", action_delete_branch);
        } else if(G.current_view==VIEW_STASH){
            menu_add_item("Apply", action_apply_stash);
            menu_add_item("Pop", action_pop_stash);
            menu_add_item("Drop", action_drop_stash);
        }
        menu_add_item("Cancel", NULL);
        return;
    }

    if(cl && m.row==1){
        if(m.col>=G.tab_x[0] && m.col<G.tab_x[1]) G.current_view=VIEW_STATUS;
        else if(m.col>=G.tab_x[1] && m.col<G.tab_x[2]) G.current_view=VIEW_LOG;
        else if(m.col>=G.tab_x[2] && m.col<G.tab_x[3]) G.current_view=VIEW_BRANCHES;
        else if(m.col>=G.tab_x[3] && m.col<G.tab_x[4]) G.current_view=VIEW_STASH;
        else if(m.col>=G.tab_x[4] && m.col<G.tab_x[5]) G.current_view=VIEW_HELP;
        return;
    }
    if(cl && m.row==G.rows-1){
        G.focus=FOCUS_CLI;
        return;
    }

    if(G.current_view==VIEW_STATUS){
        layout();
        if(cl && m.col==G.lw){G.dragging_v=true; return;}
        if(cl && m.col<G.lw && m.row==ct+G.lh_chg-1){G.dragging_h=true; return;}

        bool in_l=(m.col>=1&&m.col<=G.lw);
        bool in_r=(m.col>G.lw);
        bool in_top=(m.row>=ct&&m.row<ct+G.lh_chg);
        bool in_bot=(m.row>=ct+G.lh_chg);

        if(in_l&&in_top){
            if(cl)G.focus=FOCUS_CHANGES;
            if(su)G.file_sel=imax(0,G.file_sel-1);
            if(sd)G.file_sel=imin(G.file_count>0?G.file_count-1:0,G.file_sel+1);
            if(cl){
                int row=m.row-(ct+1);
                int vis=0; /* row 0 is Staged header */
                for(int i=0;i<G.file_count;i++){
                    if(G.files[i].staged){vis++; if(vis==row){G.file_sel=i;break;}}
                }
                vis++; /* spacer */
                vis++; /* Unstaged header */
                for(int i=0;i<G.file_count;i++){
                    if(!G.files[i].staged){vis++; if(vis==row){G.file_sel=i;break;}}
                }
            }
            update_diff();
        } else if(in_l&&in_bot){
            if(cl)G.focus=FOCUS_GRAPH;
            int vis=G.lh_gph-2;
            if(su)msel(&G.commit_sel,&G.commit_scroll,G.commit_count,-1,vis);
            if(sd)msel(&G.commit_sel,&G.commit_scroll,G.commit_count,1,vis);
            if(cl){
                int row=m.row-(ct+G.lh_chg+1);
                int t=G.commit_scroll+row;
                if(t>=0&&t<G.commit_count){
                    G.commit_sel=t;
                    snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",G.commits[t].hash,G.commits[t].subject);
                    load_diff_commit(G.commits[t].hash);
                }
            }
        } else if(in_r){
            if(cl)G.focus=FOCUS_DIFF;
            if(su)G.diff_scroll=imax(0,G.diff_scroll-3);
            if(sd)G.diff_scroll+=3;
        }
    } else if(G.current_view==VIEW_LOG){
        int lh=(G.rows-2)*55/100;
        bool in_log=(m.row<ct+lh);
        int vis=lh-2;
        if(in_log){
            if(su)msel(&G.commit_sel,&G.commit_scroll,G.commit_count,-1,vis);
            if(sd)msel(&G.commit_sel,&G.commit_scroll,G.commit_count,1,vis);
            if(cl){int t=G.commit_scroll+(m.row-ct-1);if(t>=0&&t<G.commit_count){G.commit_sel=t;snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",G.commits[t].hash,G.commits[t].subject);load_diff_commit(G.commits[t].hash);}}
        } else {
            if(su)G.diff_scroll=imax(0,G.diff_scroll-3);
            if(sd)G.diff_scroll+=3;
        }
    } else if(G.current_view==VIEW_BRANCHES){
        int vis=G.rows-4;
        if(su)msel(&G.branch_sel,&G.branch_scroll,G.branch_count,-1,vis);
        if(sd)msel(&G.branch_sel,&G.branch_scroll,G.branch_count,1,vis);
        if(cl){int t=G.branch_scroll+(m.row-ct-2);if(t>=0&&t<G.branch_count)G.branch_sel=t;}
    } else if(G.current_view==VIEW_STASH){
        if(su)G.stash_sel=imax(0,G.stash_sel-1);
        if(sd)G.stash_sel=imin(G.stash_count>0?G.stash_count-1:0,G.stash_sel+1);
        if(cl){int t=m.row-ct-1;if(t>=0&&t<G.stash_count)G.stash_sel=t;}
    }
}

/* ================================================================
   VI MODE PREPROCESSOR
================================================================ */
static bool vi_pre(Key *k){
    if(!G.vi_enabled)return false;
    if(G.vi_mode==VIMODE_INSERT){
        if(k->type==KEY_ESC){G.vi_mode=VIMODE_NORMAL;return true;}
        return false;
    }
    /* Normal mode */
    if(k->type==KEY_CHAR){
        if(k->ch>='1'&&k->ch<='9'&&!G.vi_count){G.vi_count=k->ch-'0';return true;}
        if(k->ch>='0'&&k->ch<='9'&&G.vi_count){G.vi_count=G.vi_count*10+(k->ch-'0');return true;}
        if(k->ch=='i'||k->ch=='a'){G.vi_mode=VIMODE_INSERT;return true;}
        if(k->ch=='j'){k->type=KEY_DOWN;return false;}
        if(k->ch=='k'){k->type=KEY_UP;return false;}
        if(k->ch=='G'){k->type=KEY_END;return false;}
        if(k->ch=='g'){
            if(G.vi_gg_pending){k->type=KEY_HOME;G.vi_gg_pending=false;return false;}
            G.vi_gg_pending=true;return true;
        }
        G.vi_gg_pending=false;
    }
    if(k->type==KEY_CTRL_F){k->type=KEY_PGDN;return false;}
    if(k->type==KEY_CTRL_B){k->type=KEY_PGUP;return false;}
    if(k->type==KEY_CTRL_D){k->type=KEY_PGDN;G.vi_count=imax(1,G.vi_count/2)+1;return false;}
    if(k->type==KEY_CTRL_U){k->type=KEY_PGUP;G.vi_count=imax(1,G.vi_count/2)+1;return false;}
    return false;
}

/* ================================================================
   MAIN KEY HANDLER
================================================================ */
static void handle_cli_key(Key k){
    int len=(int)strlen(G.cli_buf);
    if(k.type==KEY_ESC){G.focus=FOCUS_CHANGES;return;}
    if(k.type==KEY_ENTER){
        if(G.cli_buf[0]){
            OK("Executing: %s", G.cli_buf);
            draw();
            int r = system(G.cli_buf);
            if(r==0)OK("Success: %s", G.cli_buf);
            else ERR("Failed (%d): %s", r, G.cli_buf);
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
        }
    }
}

static void handle_key(Key k){
    if(k.type==KEY_MOUSE){handle_mouse(k.mouse);return;}
    if(G.menu_active){G.menu_active=false;return;}
    if(G.in_prompt){handle_prompt_key(k);return;}
    if(G.focus==FOCUS_CLI){handle_cli_key(k);return;}
    if(k.type==KEY_CHAR && k.ch==':'){G.focus=FOCUS_CLI; return;}
    if(vi_pre(&k))return;

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
        case 'T':G.theme_idx=(G.theme_idx+1)%NTHEMES;OK("Theme: %s",TH->name);return;
        case 'V':G.vi_enabled=!G.vi_enabled;G.vi_mode=VIMODE_NORMAL;G.vi_count=0;
                 OK("Vi mode: %s",G.vi_enabled?"ON":"OFF");return;
        }
    }
    if(k.type==KEY_ESC){
        if(G.current_view==VIEW_HELP){G.current_view=VIEW_STATUS;return;}
        if(G.focus==FOCUS_DIFF){G.focus=FOCUS_CHANGES;return;}
        G.vi_count=0;G.vi_gg_pending=false;return;
    }
    if(k.type==KEY_TAB){
        if(G.current_view==VIEW_STATUS)G.focus=(FocusPane)((G.focus+1)%3);
        else G.current_view=(View)((G.current_view+1)%VIEW_COUNT);
        return;
    }
    if(k.type==KEY_SHIFT_TAB){
        if(G.current_view==VIEW_STATUS)G.focus=(FocusPane)((G.focus+2)%3);
        else G.current_view=(View)((G.current_view+VIEW_COUNT-1)%VIEW_COUNT);
        return;
    }

    switch(G.current_view){
    /* ──── STATUS ──── */
    case VIEW_STATUS:{
        if(k.type==KEY_LEFT||(k.type==KEY_CHAR&&k.ch=='h')){
            if(G.focus==FOCUS_DIFF)G.focus=FOCUS_GRAPH;
            else if(G.focus==FOCUS_GRAPH)G.focus=FOCUS_CHANGES;
            return;
        }
        if(k.type==KEY_RIGHT||(k.type==KEY_CHAR&&k.ch=='l')){
            if(G.focus==FOCUS_CHANGES)G.focus=FOCUS_GRAPH;
            else if(G.focus==FOCUS_GRAPH)G.focus=FOCUS_DIFF;
            return;
        }
        if(G.focus==FOCUS_CHANGES){
            int cnt=G.file_count;
            switch(k.type){
            case KEY_UP:   msel(&G.file_sel,&G.file_scroll,cnt,-1,vis);break;
            case KEY_DOWN: msel(&G.file_sel,&G.file_scroll,cnt, 1,vis);break;
            case KEY_PGUP: msel(&G.file_sel,&G.file_scroll,cnt,-vis/2,vis);break;
            case KEY_PGDN: msel(&G.file_sel,&G.file_scroll,cnt, vis/2,vis);break;
            case KEY_HOME: G.file_sel=0;G.file_scroll=0;break;
            case KEY_END:  G.file_sel=cnt>0?cnt-1:0;break;
            case KEY_ENTER:G.focus=FOCUS_DIFF;break;
            case KEY_CHAR:
                if(k.ch==' ')action_stage();
                else if(k.ch=='a')action_stage_all();
                else if(k.ch=='u')action_unstage_all();
                else if(k.ch=='d')action_discard();
                else if(k.ch=='='||k.ch=='>')G.focus=FOCUS_DIFF;
                else if(k.ch=='s')action_stash();
                break;
            default:break;
            }
            update_diff();
        } else if(G.focus==FOCUS_GRAPH){
            int cnt=G.commit_count,gvis=G.lh_gph-2;
            switch(k.type){
            case KEY_UP:   msel(&G.commit_sel,&G.commit_scroll,cnt,-1,gvis);break;
            case KEY_DOWN: msel(&G.commit_sel,&G.commit_scroll,cnt, 1,gvis);break;
            case KEY_PGUP: msel(&G.commit_sel,&G.commit_scroll,cnt,-gvis/2,gvis);break;
            case KEY_PGDN: msel(&G.commit_sel,&G.commit_scroll,cnt, gvis/2,gvis);break;
            case KEY_HOME: G.commit_sel=0;G.commit_scroll=0;break;
            case KEY_END:  G.commit_sel=cnt>0?cnt-1:0;break;
            case KEY_ENTER:
                if(G.commit_count>0){
                    GitCommit *c=&G.commits[G.commit_sel];
                    snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                    G.diff_staged=false;load_diff_commit(c->hash);G.focus=FOCUS_DIFF;
                }
                break;
            default:break;
            }
        } else { /* FOCUS_DIFF */
            int dv=G.rows-4;
            switch(k.type){
            case KEY_UP:   G.diff_scroll=imax(0,G.diff_scroll-1);break;
            case KEY_DOWN: G.diff_scroll++;break;
            case KEY_PGUP: G.diff_scroll=imax(0,G.diff_scroll-dv);break;
            case KEY_PGDN: G.diff_scroll+=dv;break;
            case KEY_HOME: G.diff_scroll=0;break;
            case KEY_END:  G.diff_scroll=G.diff_count;break;
            case KEY_CHAR:
                if(k.ch=='s'||k.ch=='S')G.diff_sidebyside=!G.diff_sidebyside;
                else if(k.ch=='['||k.ch=='h')G.diff_hscroll=imax(0,G.diff_hscroll-4);
                else if(k.ch==']'||k.ch=='l')G.diff_hscroll+=4;
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
            case KEY_UP:   msel(&G.commit_sel,&G.commit_scroll,cnt,-1,lh);break;
            case KEY_DOWN: msel(&G.commit_sel,&G.commit_scroll,cnt, 1,lh);break;
            case KEY_PGUP: msel(&G.commit_sel,&G.commit_scroll,cnt,-lh/2,lh);break;
            case KEY_PGDN: msel(&G.commit_sel,&G.commit_scroll,cnt, lh/2,lh);break;
            case KEY_HOME: G.commit_sel=0;G.commit_scroll=0;break;
            case KEY_END:  G.commit_sel=cnt>0?cnt-1:0;break;
            case KEY_ENTER:
                if(G.commit_count>0){
                    GitCommit *c=&G.commits[G.commit_sel];
                    snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                    load_diff_commit(c->hash);G.focus=FOCUS_DIFF;
                }
                break;
            case KEY_CHAR:if(k.ch=='n')action_new_branch();break;
            default:break;
            }
            /* auto-preview */
            if(G.commit_count>0){
                GitCommit *c=&G.commits[G.commit_sel];
                char exp[24];snprintf(exp,sizeof(exp),"commit %s:",c->hash);
                if(strncmp(G.diff_title,exp,strlen(exp))!=0){
                    snprintf(G.diff_title,sizeof(G.diff_title),"commit %s: %s",c->hash,c->subject);
                    load_diff_commit(c->hash);
                }
            }
        } else {
            int dv=G.rows-4;
            switch(k.type){
            case KEY_UP:   G.diff_scroll=imax(0,G.diff_scroll-1);break;
            case KEY_DOWN: G.diff_scroll++;break;
            case KEY_PGUP: G.diff_scroll=imax(0,G.diff_scroll-dv);break;
            case KEY_PGDN: G.diff_scroll+=dv;break;
            case KEY_CHAR:if(k.ch=='s')G.diff_sidebyside=!G.diff_sidebyside;break;
            default:break;
            }
        }
        break;
    }
    /* ──── BRANCHES ──── */
    case VIEW_BRANCHES:{
        int cnt=G.branch_count;
        switch(k.type){
        case KEY_UP:   msel(&G.branch_sel,&G.branch_scroll,cnt,-1,vis);break;
        case KEY_DOWN: msel(&G.branch_sel,&G.branch_scroll,cnt, 1,vis);break;
        case KEY_PGUP: msel(&G.branch_sel,&G.branch_scroll,cnt,-vis/2,vis);break;
        case KEY_PGDN: msel(&G.branch_sel,&G.branch_scroll,cnt, vis/2,vis);break;
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
        case KEY_UP:   msel(&G.stash_sel,&dummy,cnt,-1,vis);break;
        case KEY_DOWN: msel(&G.stash_sel,&dummy,cnt, 1,vis);break;
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
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)){fprintf(stderr,"gitui: requires a terminal\n");return 1;}
    if(!in_git_repo()){fprintf(stderr,"gitui: not a git repository\n");return 1;}

    memset(&G,0,sizeof(G));
    G.running=true; G.current_view=VIEW_STATUS; G.focus=FOCUS_CHANGES;
    G.theme_idx=0; G.vi_enabled=false; G.vi_mode=VIMODE_NORMAL;
    G.diff_sidebyside=true;

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

    OK("gitui v%s — Tab:focus  T:theme  V:vi-mode  s:side-by-side  ?:help  q:quit", VERSION);

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
    printf("gitui — bye!\n");
    return 0;
}
