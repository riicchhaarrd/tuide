#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "editor.h"
#include "git.h"
#include "input.h"
#include "render.h"
#include "state.h"
#include "term.h"
#include "ui.h"
#include "util.h"
#include "strings.h"

static volatile int resize_pending = 0;

static const char *find_editor_path(int argc, char **argv) {
	const char *path = NULL;
	bool after_dashdash = false;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!after_dashdash && strcmp(arg, "--") == 0) {
			after_dashdash = true;
			continue;
		}
		if (!after_dashdash && arg[0] == '-') continue;
		path = arg;
		if (after_dashdash) break;
	}
	return path;
}

static bool ensure_tty(void) {
	if (!isatty(STDIN_FILENO)) {
		int fd = open("/dev/tty", O_RDONLY);
		if (fd >= 0) {
			dup2(fd, STDIN_FILENO);
			close(fd);
		}
	}
	return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

static bool ensure_file_exists(const char *path) {
	FILE *fp = fopen(path, "a");
	if (!fp) return false;
	fclose(fp);
	return true;
}

static void parent_dir(const char *path, char *out, size_t out_len) {
	const char *slash = strrchr(path, '/');
	if (!slash) {
		snprintf(out, out_len, ".");
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len == 0) len = 1;
	if (len >= out_len) len = out_len - 1;
	memcpy(out, path, len);
	out[len] = '\0';
}

static bool is_git_internal_path(const char *path) {
	if (!path || !path[0]) return false;
	if (strcmp(path, ".git") == 0) return true;
	if (strncmp(path, ".git/", 5) == 0) return true;
	if (strstr(path, "/.git/") != NULL) return true;
	size_t len = strlen(path);
	return (len >= 5 && strcmp(path + len - 5, "/.git") == 0);
}

static void handle_sigwinch(int signal_num) {
	(void)signal_num;
	resize_pending = 1;
}
static void handle_sigint(int signal_num) {
	(void)signal_num;
	g_app_state.running = false;
}

int main(int argc, char **argv) {
	const char *edit_path = find_editor_path(argc, argv);
	bool editor_mode = (edit_path != NULL);
	if (!ensure_tty()) {
		fprintf(stderr, "%s\n", UI->err_requires_terminal);
		return 1;
	}
	bool in_repo = in_git_repo();
	if (!editor_mode && !in_repo) {
		fprintf(stderr, "%s\n", UI->err_not_git_repo);
		return 1;
	}
	if (editor_mode && !ensure_file_exists(edit_path)) {
		fprintf(stderr, UI->err_failed_open_fmt, edit_path);
		fputc('\n', stderr);
		return 1;
	}

	memset(&g_app_state, 0, sizeof(g_app_state));
	g_app_state.running = true;
	g_app_state.current_view = VIEW_STATUS;
	g_app_state.focus = editor_mode ? FOCUS_EDITOR : FOCUS_CHANGES;
	g_app_state.theme_idx = 0;
	g_app_state.clipboard = NULL;
	g_app_state.col_hash_w = 9;
	g_app_state.col_author_w = 14;
	g_app_state.col_date_w = 13;
	g_app_state.editor_active = editor_mode;
	g_app_state.editor_diff_tab = false;
	g_app_state.browser_active = false;
	g_app_state.diff_sidebyside = true;
	g_app_state.diff_continuous = false;
	g_app_state.diff_wrap = false;
	g_app_state.commit_msg_buf[0] = '\0';
	g_app_state.commit_msg_cursor = 0;
	g_app_state.commit_bar_focused = false;

	if (editor_mode) {
		editor_load(edit_path);
		if (!g_app_state.tabs[g_app_state.tab_current].path[0]) {
			fprintf(stderr, UI->err_failed_open_fmt, edit_path);
			fputc('\n', stderr);
			return 1;
		}
		if (!is_git_internal_path(edit_path)) {
			char dir[1024];
			g_app_state.browser_active = true;
			parent_dir(edit_path, dir, sizeof(dir));
			load_browser(dir);
		}
	}

	signal(SIGWINCH, handle_sigwinch);
	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGPIPE, SIG_IGN);

	get_winsize();
	term_raw();
	printf(T_ALT T_HIDE T_MOUSE_ON T_CLEAR);
	fflush(stdout);

	/* Initial load */
	if (!editor_mode || in_repo) reload_all();

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
	printf("%s\n", UI->exit_message);
	return 0;
}
