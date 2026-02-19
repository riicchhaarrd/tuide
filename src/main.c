#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "git.h"
#include "input.h"
#include "render.h"
#include "state.h"
#include "term.h"
#include "ui.h"
#include "util.h"

static volatile int resize_pending = 0;

static void handle_sigwinch(int signal_num) {
	(void)signal_num;
	resize_pending = 1;
}
static void handle_sigint(int signal_num) {
	(void)signal_num;
	g_app_state.running = false;
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "tuide: requires a terminal\n");
		return 1;
	}
	if (!in_git_repo()) {
		fprintf(stderr, "tuide: not a git repository\n");
		return 1;
	}

	memset(&g_app_state, 0, sizeof(g_app_state));
	g_app_state.running = true;
	g_app_state.current_view = VIEW_STATUS;
	g_app_state.focus = FOCUS_CHANGES;
	g_app_state.theme_idx = 0;
	g_app_state.clipboard = NULL;
	g_app_state.col_hash_w = 9;
	g_app_state.col_author_w = 14;
	g_app_state.col_date_w = 13;
	g_app_state.editor_active = false;
	g_app_state.browser_active = false;
	g_app_state.diff_sidebyside = true;
	g_app_state.diff_continuous = false;
	g_app_state.diff_wrap = false;
	g_app_state.commit_msg_buf[0] = '\0';
	g_app_state.commit_msg_cursor = 0;
	g_app_state.commit_bar_focused = false;

	signal(SIGWINCH, handle_sigwinch);
	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGPIPE, SIG_IGN);

	get_winsize();
	term_raw();
	printf(T_ALT T_HIDE T_MOUSE_ON T_CLEAR);
	fflush(stdout);

	/* Initial load */
	reload_all();

	draw();
	while (g_app_state.running) {
		Key key = read_key();
		if (key.type != KEY_NONE) {
			handle_key(key);
			draw();
		} else if (resize_pending) {
			resize_pending = 0;
			get_winsize();
			printf(T_CLEAR);
			draw();
		}
	}

	printf(T_NORM T_SHOW T_MOUSE_OFF T_RESET);
	term_restore();
	free(g_app_state.front.cells);
	free(g_app_state.back.cells);
	printf("tuide — bye!\n");
	return 0;
}
