#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "state.h"

void term_raw(void) {
	tcgetattr(STDIN_FILENO, &g_app_state.orig_termios);
	struct termios r = g_app_state.orig_termios;
	r.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	r.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	r.c_oflag &= ~OPOST;
	r.c_cflag |= CS8;
	r.c_cc[VMIN] = 0;
	r.c_cc[VTIME] = 1;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
}

void term_restore(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_app_state.orig_termios); }

static void buf_resize(Buffer *b, int w, int h) {
	if (b->w == w && b->h == h) return;
	free(b->cells);
	b->cells = calloc(w * h, sizeof(Cell));
	b->w = w;
	b->h = h;
}

void get_winsize(void) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || !ws.ws_col) {
		g_app_state.cols = 80;
		g_app_state.rows = 24;
	} else {
		g_app_state.cols = ws.ws_col;
		g_app_state.rows = ws.ws_row;
	}
	buf_resize(&g_app_state.front, g_app_state.cols, g_app_state.rows);
	buf_resize(&g_app_state.back, g_app_state.cols, g_app_state.rows);
	if (g_app_state.front.cells)
		memset(g_app_state.front.cells, 0,
			   g_app_state.front.w * g_app_state.front.h * sizeof(Cell));
}

Key read_key(void) {
	Key k = {KEY_NONE, 0, {0, 0, 0, false, false, false}};
	unsigned char buf[64];
	int n = (int)read(STDIN_FILENO, buf, sizeof(buf) - 1);
	if (n <= 0) return k;
	buf[n] = '\0';

	if (buf[0] == 0x1b) {
		if (n == 1) {
			k.type = KEY_ESC;
			return k;
		}
		if (buf[1] == '[') {
			if (n == 3) {
				switch (buf[2]) {
					case 'A':
						k.type = KEY_UP;
						return k;
					case 'B':
						k.type = KEY_DOWN;
						return k;
					case 'C':
						k.type = KEY_RIGHT;
						return k;
					case 'D':
						k.type = KEY_LEFT;
						return k;
					case 'H':
						k.type = KEY_HOME;
						return k;
					case 'F':
						k.type = KEY_END;
						return k;
					case 'Z':
						k.type = KEY_SHIFT_TAB;
						return k;
				}
			}
			if (n == 6 && buf[2] == '1' && buf[3] == ';' && buf[4] == '2') {
				switch (buf[5]) {
					case 'A':
						k.type = KEY_SHIFT_UP;
						return k;
					case 'B':
						k.type = KEY_SHIFT_DOWN;
						return k;
					case 'C':
						k.type = KEY_SHIFT_RIGHT;
						return k;
					case 'D':
						k.type = KEY_SHIFT_LEFT;
						return k;
				}
			}
			if (n >= 4 && buf[n - 1] == '~') {
				switch (buf[2]) {
					case '1':
						k.type = KEY_HOME;
						return k;
					case '3':
						k.type = KEY_DEL;
						return k;
					case '4':
						k.type = KEY_END;
						return k;
					case '5':
						k.type = KEY_PGUP;
						return k;
					case '6':
						k.type = KEY_PGDN;
						return k;
				}
			}
			if (buf[2] == '<') {
				int btn = 0, col = 0, row = 0;
				char end = 'M';
				sscanf((char *)buf + 3, "%d;%d;%d%c", &btn, &col, &row, &end);
				k.type = KEY_MOUSE;
				k.mouse.btn = btn;
				k.mouse.col = col;
				k.mouse.row = row;
				k.mouse.release = (end == 'm');
				k.mouse.shift = (btn & 4) != 0;
				k.mouse.ctrl = (btn & 16) != 0;
				return k;
			}
			if (buf[2] == 'M' && n >= 6) {
				k.type = KEY_MOUSE;
				k.mouse.btn = buf[3] - 32;
				k.mouse.col = buf[4] - 32;
				k.mouse.row = buf[5] - 32;
				return k;
			}
		}
		if (buf[1] == 'O') {
			switch (buf[2]) {
				case 'P':
					k.type = KEY_F1;
					return k;
				case 'Q':
					k.type = KEY_F2;
					return k;
				case 'R':
					k.type = KEY_F3;
					return k;
				case 'S':
					k.type = KEY_F4;
					return k;
			}
		}
		return k;
	}
	switch (buf[0]) {
		case '\r':
		case '\n':
			k.type = KEY_ENTER;
			return k;
		case 127:
		case 8:
			k.type = KEY_BACKSPACE;
			return k;
		case '\t':
			k.type = KEY_TAB;
			return k;
		case 1:
			k.type = KEY_CTRL_A;
			return k;
		case 2:
			k.type = KEY_CTRL_B;
			return k;
		case 3:
			k.type = KEY_CTRL_C;
			return k;
		case 4:
			k.type = KEY_CTRL_D;
			return k;
		case 5:
			k.type = KEY_CTRL_E;
			return k;
		case 6:
			k.type = KEY_CTRL_F;
			return k;
		case 7:
			k.type = KEY_CTRL_G;
			return k;
		case 11:
			k.type = KEY_CTRL_K;
			return k;
		case 12:
			k.type = KEY_CTRL_L;
			return k;
		case 14:
			k.type = KEY_CTRL_N;
			return k;
		case 16:
			k.type = KEY_CTRL_P;
			return k;
		case 17:
			k.type = KEY_CTRL_Q;
			return k;
		case 18:
			k.type = KEY_CTRL_R;
			return k;
		case 19:
			k.type = KEY_CTRL_S;
			return k;
		case 21:
			k.type = KEY_CTRL_U;
			return k;
		case 22:
			k.type = KEY_CTRL_V;
			return k;
		case 23:
			k.type = KEY_CTRL_W;
			return k;
		case 24:
			k.type = KEY_CTRL_X;
			return k;
		case 25:
			k.type = KEY_CTRL_Y;
			return k;
		case 26:
			k.type = KEY_CTRL_Z;
			return k;
	}
	if (buf[0] >= 32 && buf[0] < 127) {
		k.type = KEY_CHAR;
		k.ch = (char)buf[0];
		return k;
	}
	return k;
}
