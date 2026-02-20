#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "git.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "render.h" /* For draw_flush if needed, though we use draw() from ui.h now */
#include "state.h"
#include "ui.h"
#include "util.h"

char *git_run(const char *cmd) {
	FILE *fp = popen(cmd, "r");
	if (!fp) return NULL;
	size_t cap = 8192, len = 0;
	char *buf = malloc(cap);
	if (!buf) {
		pclose(fp);
		return NULL;
	}
	int c;
	while ((c = fgetc(fp)) != EOF) {
		if (len + 2 >= cap) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb) {
				free(buf);
				pclose(fp);
				return NULL;
			}
			buf = nb;
		}
		buf[len++] = (char)c;
	}
	buf[len] = '\0';
	pclose(fp);
	return buf;
}

int git_exec(const char *fmt, ...) {
	char cmd[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	char full[2200];
	snprintf(full, sizeof(full), "%s >/dev/null 2>&1", cmd);
	return system(full);
}

void load_branch(void) {
	char *o = git_run("git rev-parse --abbrev-ref HEAD 2>/dev/null");
	if (o) {
		strtrim(o);
		snprintf(g_app_state.branch_name, sizeof(g_app_state.branch_name), "%s", o);
		free(o);
	} else {
		snprintf(g_app_state.branch_name, sizeof(g_app_state.branch_name), "(unknown)");
	}
}

static FileStatus parse_xy(char x, char y) {
	if (x == '?' && y == '?') return FS_UNTRACKED;
	if (x == 'A') return FS_STAGED_NEW;
	if (x == 'D') return FS_STAGED_DEL;
	if (x == 'R') return FS_RENAMED;
	if (x == 'C') return FS_COPIED;
	if (x == 'M') return FS_STAGED_MODIFY;
	if (y == 'M') return FS_MODIFIED;
	if (y == 'D') return FS_DELETED;
	if (x == 'U' || y == 'U') return FS_CONFLICT;
	return FS_MODIFIED;
}

void load_status(void) {
	g_app_state.file_count = 0;
	char *o = git_run("git status --porcelain=v1 -u 2>/dev/null");
	if (!o) return;
	char *line = o;
	while (*line && g_app_state.file_count < MAX_FILES) {
		if (strlen(line) < 4) {
			char *nl = strchr(line, '\n');
			line = nl ? nl + 1 : line + strlen(line);
			continue;
		}
		char x = line[0], y = line[1];
		char *ps = line + 3;
		char *nl = strchr(ps, '\n');
		size_t plen = nl ? (size_t)(nl - ps) : strlen(ps);
		char pb[512];
		if (plen >= sizeof(pb)) plen = sizeof(pb) - 1;
		memcpy(pb, ps, plen);
		pb[plen] = '\0';

		GitFile *f = &g_app_state.files[g_app_state.file_count++];
		memset(f, 0, sizeof(*f));

		char *arrow = strstr(pb, " -> ");
		if (arrow) {
			*arrow = '\0';
			snprintf(f->original_path, sizeof(f->original_path), "%s", pb);
			snprintf(f->path, sizeof(f->path), "%s", arrow + 4);
		} else {
			snprintf(f->path, sizeof(f->path), "%s", pb);
		}
		f->status = parse_xy(x, y);
		f->staged = (x != ' ' && x != '?' && x != '!');
		line = nl ? nl + 1 : line + strlen(line);
	}
	free(o);
	g_app_state.file_sel = iclamp(g_app_state.file_sel, 0,
								  g_app_state.file_count > 0 ? g_app_state.file_count - 1 : 0);
}

void load_log(void) {
	g_app_state.commit_count = 0;
	char *graph_o = git_run("git log --oneline --graph --decorate=short -n 300 2>/dev/null");
	char *data_o =
		git_run("git log --format='%h\x01%an\x01%ae\x01%ar\x01%D\x01%s' -n 300 2>/dev/null");

	if (data_o) {
		char *line = data_o;
		while (*line && g_app_state.commit_count < MAX_COMMITS) {
			char *nl = strchr(line, '\n');
			size_t len = nl ? (size_t)(nl - line) : strlen(line);
			if (!len) {
				line = nl ? nl + 1 : line + strlen(line);
				continue;
			}

			char buf[1024];
			if (len >= sizeof(buf)) len = sizeof(buf) - 1;
			memcpy(buf, line, len);
			buf[len] = '\0';

			GitCommit *c = &g_app_state.commits[g_app_state.commit_count];
			memset(c, 0, sizeof(*c));

			char *fields[6] = {"", "", "", "", "", ""};
			char *p = buf;
			for (int f = 0; f < 5; f++) {
				if (!p) {
					fields[f] = "";
					continue;
				}
				char *sep = strchr(p, '\x01');
				if (sep) {
					*sep = '\0';
					fields[f] = p;
					p = sep + 1;
				} else {
					fields[f] = p;
					p = NULL;
				}
			}
			fields[5] = p ? p : "";

			snprintf(c->hash, sizeof(c->hash), "%s", fields[0]);
			snprintf(c->author, sizeof(c->author), "%s", fields[1]);
			snprintf(c->email, sizeof(c->email), "%s", fields[2]);
			snprintf(c->date, sizeof(c->date), "%s", fields[3]);
			snprintf(c->refs, sizeof(c->refs), "%s", fields[4]);
			if (fields[5][0]) {
				strtrim(fields[5]);
				snprintf(c->subject, sizeof(c->subject), "%s", fields[5]);
			}

			g_app_state.commit_count++;
			line = nl ? nl + 1 : line + strlen(line);
		}
		free(data_o);
	}

	if (graph_o) {
		int ci = 0;
		char *line = graph_o;
		while (*line && ci < g_app_state.commit_count) {
			char *nl = strchr(line, '\n');
			size_t len = nl ? (size_t)(nl - line) : strlen(line);
			char buf[256];
			if (len >= sizeof(buf)) len = sizeof(buf) - 1;
			memcpy(buf, line, len);
			buf[len] = '\0';

			char *star = strchr(buf, '*');
			if (star) {
				int gc = (int)(star - buf);
				int gn = imin(GRAPH_COLS, gc + 3);
				snprintf(g_app_state.commits[ci].graph, sizeof(g_app_state.commits[ci].graph),
						 "%.*s", gn, buf);
				g_app_state.commits[ci].graph_col = gc;
				ci++;
			}
			line = nl ? nl + 1 : line + strlen(line);
		}
		free(graph_o);
	}
	g_app_state.commit_sel = iclamp(
		g_app_state.commit_sel, 0, g_app_state.commit_count > 0 ? g_app_state.commit_count - 1 : 0);
}

void load_branches(void) {
	g_app_state.branch_count = 0;
	char *o = git_run(
		"git branch -vv --format='%(HEAD)|%(refname:short)|%(upstream:short)|%(upstream:track)' "
		"2>/dev/null");
	if (o) {
		char *line = o;
		while (*line && g_app_state.branch_count < MAX_BRANCHES) {
			char *nl = strchr(line, '\n');
			size_t len = nl ? (size_t)(nl - line) : strlen(line);
			if (!len) {
				line = nl ? nl + 1 : line + strlen(line);
				continue;
			}

			char buf[512];
			if (len >= sizeof(buf)) len = sizeof(buf) - 1;
			memcpy(buf, line, len);
			buf[len] = '\0';

			GitBranch *b = &g_app_state.branches[g_app_state.branch_count++];
			memset(b, 0, sizeof(*b));

			char *tok = strtok(buf, "|");
			if (tok) b->is_current = (tok[0] == '*');
			tok = strtok(NULL, "|");
			if (tok) snprintf(b->name, sizeof(b->name), "%s", tok);
			tok = strtok(NULL, "|");
			if (tok) snprintf(b->upstream, sizeof(b->upstream), "%s", tok);
			tok = strtok(NULL, "");
			if (tok) {
				char *ah = strstr(tok, "ahead ");
				if (ah) b->ahead = atoi(ah + 6);
				char *bh = strstr(tok, "behind ");
				if (bh) b->behind = atoi(bh + 7);
			}
			line = nl ? nl + 1 : line + strlen(line);
		}
		free(o);
	}

	char *o2 = git_run("git branch -r --format='%(refname:short)' 2>/dev/null");
	if (o2) {
		char *line = o2;
		while (*line && g_app_state.branch_count < MAX_BRANCHES) {
			char *nl = strchr(line, '\n');
			size_t len = nl ? (size_t)(nl - line) : strlen(line);
			if (!len) {
				line = nl ? nl + 1 : line + strlen(line);
				continue;
			}

			char buf[256];
			if (len >= sizeof(buf)) len = sizeof(buf) - 1;
			memcpy(buf, line, len);
			buf[len] = '\0';
			strtrim(buf);

			if (strstr(buf, "HEAD")) {
				line = nl ? nl + 1 : line + strlen(line);
				continue;
			}
			bool found = false;
			for (int i = 0; i < g_app_state.branch_count; i++)
				if (strcmp(g_app_state.branches[i].name, buf) == 0) {
					found = true;
					break;
				}
			if (!found) {
				GitBranch *b = &g_app_state.branches[g_app_state.branch_count++];
				memset(b, 0, sizeof(*b));
				snprintf(b->name, sizeof(b->name), "%s", buf);
				b->is_remote = true;
			}
			line = nl ? nl + 1 : line + strlen(line);
		}
		free(o2);
	}
	g_app_state.branch_sel = iclamp(
		g_app_state.branch_sel, 0, g_app_state.branch_count > 0 ? g_app_state.branch_count - 1 : 0);
}

void load_stash(void) {
	g_app_state.stash_count = 0;
	char *o = git_run("git stash list --format='%gd|%h|%s' 2>/dev/null");
	if (!o) return;
	char *line = o;
	while (*line && g_app_state.stash_count < MAX_STASHES) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		if (!len) {
			line = nl ? nl + 1 : line + strlen(line);
			continue;
		}

		char buf[512];
		if (len >= sizeof(buf)) len = sizeof(buf) - 1;
		memcpy(buf, line, len);
		buf[len] = '\0';

		GitStash *s = &g_app_state.stashes[g_app_state.stash_count++];
		memset(s, 0, sizeof(*s));

		char *tok = strtok(buf, "|");
		if (tok) {
			char *lb = strchr(tok, '{');
			s->index = lb ? atoi(lb + 1) : 0;
		}
		tok = strtok(NULL, "|");
		if (tok) snprintf(s->hash, sizeof(s->hash), "%s", tok);
		tok = strtok(NULL, "");
		if (tok) snprintf(s->message, sizeof(s->message), "%s", tok);
		line = nl ? nl + 1 : line + strlen(line);
	}
	free(o);
	g_app_state.stash_sel = iclamp(g_app_state.stash_sel, 0,
								   g_app_state.stash_count > 0 ? g_app_state.stash_count - 1 : 0);
}

void parse_diff(const char *raw) {
	g_app_state.diff_count = 0;
	if (!raw || !raw[0]) return;
	const char *line = raw;
	int old_lno = 0, new_lno = 0;

	while (*line && g_app_state.diff_count < MAX_DIFF_LINES) {
		const char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		char lb[LINE_MAX_LEN];
		if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN - 1;
		memcpy(lb, line, len);
		lb[len] = '\0';

		int type = -1;
		if (lb[0] == '+') {
			if (lb[1] == '+' && lb[2] == '+')
				type = 4;
			else
				type = 1;
		} else if (lb[0] == '-') {
			if (lb[1] == '-' && lb[2] == '-')
				type = 4;
			else
				type = 2;
		} else if (lb[0] == '@' && lb[1] == '@') {
			type = 3;
		} else if (lb[0] == 'd' && strncmp(lb, "diff --git ", 11) == 0) {
			type = 4;
		} else if (lb[0] == ' ' || lb[0] == '\0') {
			type = 0;
		} else {
			type = 6;
		}

		bool skip = false;
		if (type == 4) {
			if (lb[0] == '+' || lb[0] == '-') skip = true;
		}

		if (skip) {
			line = nl ? nl + 1 : line + len;
			continue;
		}

		if (type == 3) {
			int om = 0, nm = 0;
			sscanf(lb, "@@ -%d", &om);
			sscanf(lb, "@@ -%*d,%*d +%d", &nm);
			if (!om) sscanf(lb, "@@ -%d,", &om);
			if (!nm) sscanf(lb, "@@ -%*d +%d", &nm);
			old_lno = om > 0 ? om : 1;
			new_lno = nm > 0 ? nm : 1;
		}

		DiffLine *dl = &g_app_state.diff_lines[g_app_state.diff_count];
		memset(dl, 0, sizeof(*dl));
		dl->type = type;
		if (type == 3) {
			snprintf(dl->new_line, sizeof(dl->new_line), "%s", lb);
			snprintf(dl->old_line, sizeof(dl->old_line), "%s", lb);
		} else if (type == 1) {
			dl->new_lno = new_lno++;
			snprintf(dl->new_line, sizeof(dl->new_line), "%s", lb + 1);
		} else if (type == 2) {
			dl->old_lno = old_lno++;
			snprintf(dl->old_line, sizeof(dl->old_line), "%s", lb + 1);
		} else if (type == 6) {
			snprintf(dl->new_line, sizeof(dl->new_line), "%s", lb);
			snprintf(dl->old_line, sizeof(dl->old_line), "%s", lb);
		} else {
			dl->old_lno = old_lno++;
			dl->new_lno = new_lno++;
			const char *src = (lb[0] == ' ') ? lb + 1 : lb;
			snprintf(dl->old_line, sizeof(dl->old_line), "%s", src);
			snprintf(dl->new_line, sizeof(dl->new_line), "%s", src);
		}
		g_app_state.diff_count++;
		line = nl ? nl + 1 : line + len;
	}
	g_app_state.diff_scroll = 0;
	g_app_state.diff_hscroll = 0;
}

void load_commit_summary(const char *hash) {
	char h[64];
	snprintf(h, sizeof(h), "%s", hash);
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "git show --name-only --format='%%s%%n%%b' %s 2>/dev/null", h);
	char *o = git_run(cmd);
	if (!o) return;
	g_app_state.diff_count = 0;
	g_app_state.diff_scroll = 0;
	g_app_state.diff_sel = 0;
	g_app_state.diff_is_summary = true;
	snprintf(g_app_state.diff_commit, sizeof(g_app_state.diff_commit), "%s", h);

	char *line = o;
	bool in_files = false;
	while (*line && g_app_state.diff_count < MAX_DIFF_LINES) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		if (len == 0) {
			in_files = true;
			line = nl ? nl + 1 : line + len;
			continue;
		}

		DiffLine *dl = &g_app_state.diff_lines[g_app_state.diff_count++];
		memset(dl, 0, sizeof(*dl));
		if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN - 1;
		memcpy(dl->new_line, line, len);
		dl->new_line[len] = '\0';
		dl->type = in_files ? 5 : 4;
		line = nl ? nl + 1 : line + len;
	}
	free(o);
}

void load_diff_file(const char *path, bool staged) {
	g_app_state.diff_is_summary = false;
	g_app_state.diff_commit[0] = '\0';
	char cmd[1024];
	const char *ctx = g_app_state.diff_continuous ? "-U1000" : "-U3";
	if (staged)
		snprintf(cmd, sizeof(cmd), "git diff --cached %s -- '%s' 2>/dev/null", ctx, path);
	else
		snprintf(cmd, sizeof(cmd), "git diff %s -- '%s' 2>/dev/null", ctx, path);
	char *o = git_run(cmd);
	if (!o || !o[0]) {
		free(o);
		snprintf(cmd, sizeof(cmd), "cat '%s' 2>/dev/null", path);
		o = git_run(cmd);
		if (o && o[0]) {
			g_app_state.diff_count = 0;
			const char *line = o;
			int lno = 1;
			while (*line && g_app_state.diff_count < MAX_DIFF_LINES) {
				const char *nl = strchr(line, '\n');
				size_t len = nl ? (size_t)(nl - line) : strlen(line);
				DiffLine *dl = &g_app_state.diff_lines[g_app_state.diff_count++];
				memset(dl, 0, sizeof(*dl));
				dl->type = 1;
				dl->new_lno = lno++;
				if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN - 1;
				memcpy(dl->new_line, line, len);
				dl->new_line[len] = '\0';
				line = nl ? nl + 1 : line + strlen(line);
			}
			g_app_state.diff_scroll = 0;
			g_app_state.diff_hscroll = 0;
		} else
			g_app_state.diff_count = 0;
		free(o);
		return;
	}
	parse_diff(o);
	free(o);
}

void load_diff_commit(const char *hash) {
	char cmd[256];
	const char *ctx = g_app_state.diff_continuous ? "-U1000" : "-U3";
	snprintf(cmd, sizeof(cmd), "git show %s %s 2>/dev/null", ctx, hash);
	char *o = git_run(cmd);
	parse_diff(o ? o : "");
	free(o);
}

void update_diff(void) {
	if (g_app_state.file_count > 0 && g_app_state.file_sel < g_app_state.file_count) {
		GitFile *f = &g_app_state.files[g_app_state.file_sel];
		snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), "%s", f->path);
		g_app_state.diff_staged = f->staged;
		load_diff_file(f->path, f->staged);
	}
}

void fetch_commit_files(int idx) {
	if (idx < 0 || idx >= g_app_state.commit_count) return;
	GitCommit *c = &g_app_state.commits[idx];
	if (c->file_count > 0) return;
	snprintf(c->files[0], 128, "[View Full Diff]");
	c->file_count = 1;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "git show --name-only --format='' %s 2>/dev/null | head -n 15",
			 c->hash);
	char *o = git_run(cmd);
	if (!o) return;
	char *line = o;
	while (*line && c->file_count < 16) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		if (len > 0) {
			if (len >= 128) len = 127;
			memcpy(c->files[c->file_count], line, len);
			c->files[c->file_count][len] = '\0';
			c->file_count++;
		}
		line = nl ? nl + 1 : line + len;
	}
	free(o);
}

void sync_graph_preview(void) {
	if (g_app_state.commit_count <= 0 || g_app_state.commit_sel < 0 ||
		g_app_state.commit_sel >= g_app_state.commit_count)
		return;
	GitCommit *c = &g_app_state.commits[g_app_state.commit_sel];
	if (g_app_state.graph_file_sel < 0 || g_app_state.graph_file_sel == 0) {
		snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), "commit %s: %s", c->hash,
				 c->subject);
		g_app_state.diff_is_summary = false;
		snprintf(g_app_state.diff_commit, sizeof(g_app_state.diff_commit), "%s", c->hash);
		load_diff_commit(c->hash);
	} else {
		if (g_app_state.graph_file_sel >= c->file_count)
			g_app_state.graph_file_sel = c->file_count - 1;
		char *fpath = c->files[g_app_state.graph_file_sel];
		const char *ctx_ = g_app_state.diff_continuous ? "-U1000" : "-U3";
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "git show %s %s -- '%s' 2>/dev/null", ctx_, c->hash, fpath);
		char *o = git_run(cmd);
		snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), "commit %s: %s", c->hash,
				 fpath);
		g_app_state.diff_is_summary = false;
		snprintf(g_app_state.diff_commit, sizeof(g_app_state.diff_commit), "%s", c->hash);
		parse_diff(o ? o : "");
		free(o);
	}
}

void reload_all(void) {
	load_branch();
	load_status();
	load_log();
	load_branches();
	load_stash();
	update_diff();
	OK("Refreshed");
}

bool in_git_repo(void) {
	char *o = git_run("git rev-parse --git-dir 2>/dev/null");
	bool ok = o && o[0];
	free(o);
	return ok;
}

void action_stage(void) {
	if (!g_app_state.file_count) return;
	GitFile *f = &g_app_state.files[g_app_state.file_sel];
	if (f->staged) {
		git_exec("git reset HEAD -- '%s'", f->path);
		OK("Unstaged: %s", f->path);
	} else {
		if (f->status == FS_DELETED)
			git_exec("git rm -- '%s'", f->path);
		else
			git_exec("git add -- '%s'", f->path);
		OK("Staged: %s", f->path);
	}
	load_status();
	update_diff();
}
void action_stage_all(void) {
	git_exec("git add -A");
	OK("Staged all");
	load_status();
	update_diff();
}
void action_unstage_all(void) {
	git_exec("git reset HEAD");
	OK("Unstaged all");
	load_status();
	update_diff();
}

typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} StrBuf;

typedef struct {
	int header_idx, start_idx, end_idx;
	int old_start, old_count, new_start, new_count;
	int orig_adds, orig_dels;
} HunkInfo;

static bool sb_reserve(StrBuf *sb, size_t add) {
	size_t need = sb->len + add + 1;
	if (need <= sb->cap) return true;
	size_t ncap = sb->cap ? sb->cap : 1024;
	while (ncap < need) ncap *= 2;
	char *nb = realloc(sb->buf, ncap);
	if (!nb) return false;
	sb->buf = nb;
	sb->cap = ncap;
	return true;
}

static bool sb_append_line(StrBuf *sb, char prefix, const char *text) {
	size_t tlen = strlen(text);
	if (!sb_reserve(sb, tlen + 2)) return false;
	sb->buf[sb->len++] = prefix;
	memcpy(sb->buf + sb->len, text, tlen);
	sb->len += tlen;
	sb->buf[sb->len++] = '\n';
	sb->buf[sb->len] = '\0';
	return true;
}

static void sb_free(StrBuf *sb) {
	free(sb->buf);
	sb->buf = NULL;
	sb->len = 0;
	sb->cap = 0;
}

static bool parse_hunk_header(const char *s, int *o_start, int *o_count, int *n_start,
							  int *n_count) {
	int os = 0, oc = 0, ns = 0, nc = 0;
	if (sscanf(s, "@@ -%d,%d +%d,%d", &os, &oc, &ns, &nc) == 4) {
		/* ok */
	} else if (sscanf(s, "@@ -%d +%d,%d", &os, &ns, &nc) == 3) {
		oc = 1;
	} else if (sscanf(s, "@@ -%d,%d +%d", &os, &oc, &ns) == 3) {
		nc = 1;
	} else if (sscanf(s, "@@ -%d +%d", &os, &ns) == 2) {
		oc = 1;
		nc = 1;
	} else {
		return false;
	}
	*o_start = os;
	*o_count = oc;
	*n_start = ns;
	*n_count = nc;
	return true;
}

static int collect_hunks(HunkInfo **out_hunks) {
	int max = g_app_state.diff_count;
	HunkInfo *hunks = calloc(max, sizeof(HunkInfo));
	if (!hunks) return -1;
	int count = 0;
	for (int i = 0; i < g_app_state.diff_count; i++) {
		DiffLine *dl = &g_app_state.diff_lines[i];
		if (dl->type != 3) continue;
		if (count >= max) break;
		HunkInfo *h = &hunks[count];
		memset(h, 0, sizeof(*h));
		h->header_idx = i;
		h->start_idx = i + 1;
		if (!parse_hunk_header(dl->new_line, &h->old_start, &h->old_count, &h->new_start,
							   &h->new_count)) {
			free(hunks);
			return -1;
		}
		int j = i + 1;
		for (; j < g_app_state.diff_count; j++) {
			int t = g_app_state.diff_lines[j].type;
			if (t == 3 || t == 4) break;
		}
		h->end_idx = j;
		for (int k = h->start_idx; k < h->end_idx; k++) {
			int t = g_app_state.diff_lines[k].type;
			if (t == 1)
				h->orig_adds++;
			else if (t == 2)
				h->orig_dels++;
		}
		count++;
		i = j - 1;
	}
	*out_hunks = hunks;
	return count;
}

static void format_patch_path(const char *prefix, const char *path, char *out, size_t out_sz) {
	char full[1024];
	snprintf(full, sizeof(full), "%s%s", prefix, path);
	bool need_quote = false;
	for (const char *p = full; *p; p++) {
		if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\\') {
			need_quote = true;
			break;
		}
	}
	if (!need_quote) {
		snprintf(out, out_sz, "%s", full);
		return;
	}
	char tmp[2048];
	size_t di = 0;
	tmp[di++] = '"';
	for (const char *p = full; *p && di + 2 < sizeof(tmp); p++) {
		if (*p == '"' || *p == '\\') tmp[di++] = '\\';
		tmp[di++] = *p;
	}
	tmp[di++] = '"';
	tmp[di] = '\0';
	snprintf(out, out_sz, "%s", tmp);
}

static int apply_patch_selection(bool reverse) {
	if (g_app_state.diff_is_summary || g_app_state.diff_commit[0]) {
		ERR("Select a working tree diff");
		return -1;
	}
	if (!g_app_state.file_count || g_app_state.file_sel < 0 ||
		g_app_state.file_sel >= g_app_state.file_count) {
		ERR("No file selected");
		return -1;
	}
	if (g_app_state.diff_count <= 0) {
		ERR("No diff");
		return -1;
	}
	if (reverse && !g_app_state.diff_staged) {
		ERR("Viewing unstaged diff");
		return -1;
	}
	if (!reverse && g_app_state.diff_staged) {
		ERR("Viewing staged diff");
		return -1;
	}

	HunkInfo *hunks = NULL;
	int hunk_count = collect_hunks(&hunks);
	if (hunk_count < 0) {
		ERR("Failed to parse hunks");
		return -1;
	}
	if (hunk_count == 0) {
		free(hunks);
		ERR("No hunks to stage");
		return -1;
	}

	bool has_sel = g_app_state.selecting;
	int sel_sy = 0, sel_ey = 0;
	bool sel_left = true, sel_right = true;
	if (has_sel) {
		sel_sy = g_app_state.sel_start_y;
		sel_ey = g_app_state.sel_end_y;
		if (sel_sy > sel_ey) {
			int t = sel_sy;
			sel_sy = sel_ey;
			sel_ey = t;
		}
		sel_sy = iclamp(sel_sy, 0, g_app_state.diff_count - 1);
		sel_ey = iclamp(sel_ey, 0, g_app_state.diff_count - 1);
		if (g_app_state.diff_sidebyside) {
			int sx = g_app_state.sel_start_x;
			int ex = g_app_state.sel_end_x;
			if (sx > ex) {
				int t = sx;
				sx = ex;
				ex = t;
			}
			int split = g_app_state.diff_split;
			if (ex < split) {
				sel_right = false;
			} else if (sx >= split) {
				sel_left = false;
			}
		}
	}

	int target_hunk = -1;
	if (!has_sel) {
		int line = iclamp(g_app_state.diff_scroll, 0, g_app_state.diff_count - 1);
		for (int i = 0; i < hunk_count; i++) {
			HunkInfo *h = &hunks[i];
			if (line >= h->header_idx && line < h->end_idx) {
				target_hunk = i;
				break;
			}
			if (line < h->header_idx) {
				target_hunk = i;
				break;
			}
		}
		if (target_hunk < 0) target_hunk = hunk_count - 1;
	}

	char tmp[] = "/tmp/tuide_patch_XXXXXX";
	int fd = mkstemp(tmp);
	if (fd < 0) {
		free(hunks);
		ERR("mkstemp failed");
		return -1;
	}
	FILE *fp = fdopen(fd, "w");
	if (!fp) {
		close(fd);
		unlink(tmp);
		free(hunks);
		ERR("mkstemp failed");
		return -1;
	}

	bool wrote_header = false;
	int delta_orig = 0, delta_sel = 0;
	int total_sel = 0;
	bool oom = false;

	for (int hi = 0; hi < hunk_count; hi++) {
		HunkInfo *h = &hunks[hi];
		int orig_delta = h->orig_adds - h->orig_dels;

		if (!has_sel && hi != target_hunk) {
			delta_orig += orig_delta;
			continue;
		}

		StrBuf hb = {0};
		int old_count = 0, new_count = 0;
		int sel_adds = 0, sel_dels = 0;

		for (int i = h->start_idx; i < h->end_idx; i++) {
			DiffLine *dl = &g_app_state.diff_lines[i];
			if (dl->type == 0) {
				if (!sb_append_line(&hb, ' ', dl->old_line)) {
					oom = true;
					break;
				}
				old_count++;
				new_count++;
			} else if (dl->type == 1) {
				bool sel = !has_sel || (i >= sel_sy && i <= sel_ey && sel_right);
				if (sel) {
					if (!sb_append_line(&hb, '+', dl->new_line)) {
						oom = true;
						break;
					}
					new_count++;
					sel_adds++;
				}
			} else if (dl->type == 2) {
				bool sel = !has_sel || (i >= sel_sy && i <= sel_ey && sel_left);
				if (sel) {
					if (!sb_append_line(&hb, '-', dl->old_line)) {
						oom = true;
						break;
					}
					old_count++;
					sel_dels++;
				} else {
					if (!sb_append_line(&hb, ' ', dl->old_line)) {
						oom = true;
						break;
					}
					old_count++;
					new_count++;
				}
			}
		}

		if (!oom && sel_adds + sel_dels > 0) {
			if (!wrote_header) {
				GitFile *f = &g_app_state.files[g_app_state.file_sel];
				char a_path[1024], b_path[1024];
				format_patch_path("a/", f->path, a_path, sizeof(a_path));
				format_patch_path("b/", f->path, b_path, sizeof(b_path));
				fprintf(fp, "diff --git %s %s\n", a_path, b_path);
				fprintf(fp, "--- %s\n+++ %s\n", a_path, b_path);
				wrote_header = true;
			}
			int new_start = h->new_start + (delta_sel - delta_orig);
			fprintf(fp, "@@ -%d,%d +%d,%d @@\n", h->old_start, old_count, new_start,
					new_count);
			if (hb.len) fwrite(hb.buf, 1, hb.len, fp);
			delta_sel += sel_adds - sel_dels;
			total_sel += sel_adds + sel_dels;
		}

		sb_free(&hb);
		if (oom) break;
		delta_orig += orig_delta;
	}

	fclose(fp);

	if (oom) {
		unlink(tmp);
		free(hunks);
		ERR("Out of memory");
		return -1;
	}
	if (!wrote_header || total_sel == 0) {
		unlink(tmp);
		free(hunks);
		ERR(has_sel ? "No changed lines selected" : "No hunk selected");
		return -1;
	}

	int r = git_exec("git apply --cached %s '%s'", reverse ? "-R" : "", tmp);
	unlink(tmp);
	free(hunks);
	if (r == 0) {
		OK(reverse ? "Unstaged selection" : "Staged selection");
		load_status();
		update_diff();
		g_app_state.selecting = false;
		return 0;
	}
	ERR(reverse ? "Unstage failed" : "Stage failed");
	return -1;
}

void action_stage_selection(void) { apply_patch_selection(false); }

void action_unstage_selection(void) { apply_patch_selection(true); }
void action_discard(void) {
	if (!g_app_state.file_count) return;
	GitFile *f = &g_app_state.files[g_app_state.file_sel];
	if (f->status == FS_UNTRACKED) {
		git_exec("rm -f -- '%s'", f->path);
		OK("Removed: %s", f->path);
	} else {
		git_exec("git checkout -- '%s'", f->path);
		OK("Discarded: %s", f->path);
	}
	load_status();
	update_diff();
}

static void do_commit(const char *msg) {
	if (!msg || !msg[0]) {
		ERR("Empty message");
		return;
	}
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "/tmp/gitui_msg_XXXXXX");
	int fd = mkstemp(tmp);
	if (fd < 0) {
		ERR("mkstemp failed");
		return;
	}
	ssize_t w = write(fd, msg, strlen(msg));
	(void)w;
	close(fd);
	int r = git_exec("git commit -F '%s'", tmp);
	unlink(tmp);
	if (r == 0) {
		OK("Committed: %.60s", msg);
		load_status();
		load_log();
	} else
		ERR("Commit failed");
}
void action_commit(void) {
	int s = 0;
	for (int i = 0; i < g_app_state.file_count; i++)
		if (g_app_state.files[i].staged) s++;
	if (!s) {
		ERR("Nothing staged");
		return;
	}
	prompt_start("Commit message:", do_commit, false);
}
static void do_amend(const char *msg) {
	int r;
	if (!msg || !msg[0]) {
		r = git_exec("git commit --amend --no-edit");
	} else {
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "/tmp/gitui_amend_XXXXXX");
		int fd = mkstemp(tmp);
		if (fd < 0) {
			ERR("mkstemp");
			return;
		}
		ssize_t w = write(fd, msg, strlen(msg));
		(void)w;
		close(fd);
		r = git_exec("git commit --amend -F '%s'", tmp);
		unlink(tmp);
	}
	if (r == 0) {
		OK("Amended");
		load_log();
	} else
		ERR("Amend failed");
}
void action_amend(void) { prompt_start("Amend message (empty=keep):", do_amend, false); }
void action_push(void) {
	OK("Pushing...");
	draw();
	int r = git_exec("git push");
	if (r == 0)
		OK("Pushed");
	else
		ERR("Push failed");
	load_log();
}
void action_pull(void) {
	OK("Pulling...");
	draw();
	int r = git_exec("git pull");
	if (r == 0)
		OK("Pulled");
	else
		ERR("Pull failed");
	reload_all();
}

static void do_stash(const char *msg) {
	char cmd[512];
	if (msg && msg[0])
		snprintf(cmd, sizeof(cmd), "git stash push -m '%s'", msg);
	else
		snprintf(cmd, sizeof(cmd), "git stash push");
	int r = git_exec("%s", cmd);
	if (r == 0) {
		OK("Stashed");
		load_status();
		load_stash();
	} else
		ERR("Stash failed");
}
void action_stash(void) { prompt_start("Stash message (optional):", do_stash, false); }

void action_checkout(void) {
	if (!g_app_state.branch_count) return;
	GitBranch *b = &g_app_state.branches[g_app_state.branch_sel];
	int r = b->is_remote ? git_exec("git checkout -t '%s'", b->name)
						 : git_exec("git checkout '%s'", b->name);
	if (r == 0) {
		OK("Checked out: %s", b->name);
		load_branch();
		reload_all();
	} else
		ERR("Checkout failed");
}
static void do_new_branch(const char *name) {
	if (!name || !name[0]) {
		ERR("Name required");
		return;
	}
	int r = git_exec("git checkout -b '%s'", name);
	if (r == 0) {
		OK("Created: %s", name);
		load_branch();
		load_branches();
	} else
		ERR("Branch failed");
}
void action_new_branch(void) { prompt_start("New branch name:", do_new_branch, false); }
void action_delete_branch(void) {
	if (!g_app_state.branch_count) return;
	GitBranch *b = &g_app_state.branches[g_app_state.branch_sel];
	if (b->is_current) {
		ERR("Cannot delete current branch");
		return;
	}
	int r;
	if (b->is_remote) {
		char rn[64] = "origin", bn[128];
		snprintf(bn, sizeof(bn), "%s", b->name);
		char *sl = strchr(bn, '/');
		if (sl) {
			*sl = '\0';
			snprintf(rn, sizeof(rn), "%s", bn);
			r = git_exec("git push '%s' --delete '%s'", rn, sl + 1);
		} else
			r = git_exec("git push origin --delete '%s'", bn);
	} else
		r = git_exec("git branch -D '%s'", b->name);
	if (r == 0) {
		OK("Deleted: %s", b->name);
		load_branches();
	} else
		ERR("Delete failed");
}
void action_apply_stash(void) {
	if (!g_app_state.stash_count) return;
	int r =
		git_exec("git stash apply stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
	if (r == 0) {
		OK("Applied stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
		load_status();
	} else
		ERR("Apply failed");
}
void action_pop_stash(void) {
	if (!g_app_state.stash_count) return;
	int r = git_exec("git stash pop stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
	if (r == 0) {
		OK("Popped stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
		load_status();
		load_stash();
	} else
		ERR("Pop failed");
}
void action_drop_stash(void) {
	if (!g_app_state.stash_count) return;
	int r = git_exec("git stash drop stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
	if (r == 0) {
		OK("Dropped stash@{%d}", g_app_state.stashes[g_app_state.stash_sel].index);
		load_stash();
	} else
		ERR("Drop failed");
}

void action_copy_selection(void) {
	if (!g_app_state.selecting) return;
	int sy = g_app_state.sel_start_y, sx = g_app_state.sel_start_x;
	int ey = g_app_state.sel_end_y, ex = g_app_state.sel_end_x;
	if (sy > ey || (sy == ey && sx > ex)) {
		int t = sy;
		sy = ey;
		ey = t;
		t = sx;
		sx = ex;
		ex = t;
	}
	if (g_app_state.clipboard) free(g_app_state.clipboard);
	size_t cap = 1024, len = 0;
	g_app_state.clipboard = malloc(cap);

	int lnum_w = 4;
	int half = g_app_state.diff_split;

	for (int y = sy; y <= ey; y++) {
		if (y < 0 || y >= g_app_state.diff_count) continue;
		DiffLine *dl = &g_app_state.diff_lines[y];

		bool line_added = false;
		if (!g_app_state.diff_sidebyside) {
			const char *s = (dl->type == 2) ? dl->old_line : dl->new_line;
			int code_start = lnum_w + 2;
			for (int i = 0; s[i]; i++) {
				int x = code_start + i;
				if (is_selected(y, x)) {
					if (len + 2 >= cap) {
						cap *= 2;
						g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
					}
					g_app_state.clipboard[len++] = s[i];
					line_added = true;
				}
			}
		} else {
			const char *s_old = dl->old_line;
			int code_start_old = lnum_w + 2;
			for (int i = 0; s_old[i]; i++) {
				int x = code_start_old + i;
				if (is_selected(y, x)) {
					if (len + 2 >= cap) {
						cap *= 2;
						g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
					}
					g_app_state.clipboard[len++] = s_old[i];
					line_added = true;
				}
			}
			if (is_selected(y, half - 1)) {
				if (len + 2 >= cap) {
					cap *= 2;
					g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
				}
				g_app_state.clipboard[len++] = '|';
				line_added = true;
			}
			const char *s_new = dl->new_line;
			int code_start_new = half + lnum_w + 2;
			for (int i = 0; s_new[i]; i++) {
				int x = code_start_new + i;
				if (is_selected(y, x)) {
					if (len + 2 >= cap) {
						cap *= 2;
						g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
					}
					g_app_state.clipboard[len++] = s_new[i];
					line_added = true;
				}
			}
		}
		if (y < ey && line_added) {
			if (len + 2 >= cap) {
				cap *= 2;
				g_app_state.clipboard = realloc(g_app_state.clipboard, cap);
			}
			g_app_state.clipboard[len++] = '\n';
		}
	}
	g_app_state.clipboard[len] = '\0';
	copy_to_sys_clipboard(g_app_state.clipboard);
	OK("Copied %d chars to system clipboard", (int)len);
}

void action_find_file(const char *name) {
	if (!name || !name[0]) return;
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "find . -maxdepth 4 -name '*%s*' -not -path '*/.*' | head -n 100",
			 name);
	char *o = git_run(cmd);
	if (!o || !o[0]) {
		ERR("No files matching %s", name);
		free(o);
		return;
	}

	g_app_state.diff_count = 0;
	g_app_state.diff_scroll = 0;
	g_app_state.diff_sel = 0;
	g_app_state.diff_is_summary = true;
	snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), "Files: %s", name);
	g_app_state.diff_commit[0] = '\0';

	char *line = o;
	while (*line && g_app_state.diff_count < MAX_DIFF_LINES) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		DiffLine *dl = &g_app_state.diff_lines[g_app_state.diff_count++];
		memset(dl, 0, sizeof(*dl));
		if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN - 1;
		memcpy(dl->new_line, line, len);
		dl->new_line[len] = '\0';
		dl->type = 5;
		line = nl ? nl + 1 : line + len;
	}
	free(o);
	g_app_state.focus = FOCUS_DIFF;
}

void action_grep(const char *pattern) {
	if (!pattern || !pattern[0]) return;
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "grep -rn --exclude-dir=.git --exclude=gitui '%s' . 2>/dev/null | head -n 100",
			 pattern);
	char *o = git_run(cmd);
	if (!o || !o[0]) {
		ERR("No matches for %s", pattern);
		free(o);
		return;
	}

	g_app_state.diff_count = 0;
	g_app_state.diff_scroll = 0;
	g_app_state.diff_sel = 0;
	g_app_state.diff_is_summary = true;
	snprintf(g_app_state.diff_title, sizeof(g_app_state.diff_title), "Search: %s", pattern);
	g_app_state.diff_commit[0] = '\0';

	char *line = o;
	while (*line && g_app_state.diff_count < MAX_DIFF_LINES) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		DiffLine *dl = &g_app_state.diff_lines[g_app_state.diff_count++];
		memset(dl, 0, sizeof(*dl));
		if (len >= LINE_MAX_LEN) len = LINE_MAX_LEN - 1;
		memcpy(dl->new_line, line, len);
		dl->new_line[len] = '\0';
		dl->type = 5;
		line = nl ? nl + 1 : line + len;
	}
	free(o);
	g_app_state.focus = FOCUS_DIFF;
}
