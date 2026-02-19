#ifndef GIT_H
#define GIT_H

#include <stdbool.h>

char *git_run(const char *cmd);
int git_exec(const char *fmt, ...);

void load_branch(void);
void load_status(void);
void load_log(void);
void load_branches(void);
void load_stash(void);

void parse_diff(const char *raw);
void load_commit_summary(const char *hash);
void load_diff_file(const char *path, bool staged);
void load_diff_commit(const char *hash);
void update_diff(void);
void fetch_commit_files(int idx);
void sync_graph_preview(void);

void action_stage(void);
void action_stage_all(void);
void action_unstage_all(void);
void action_discard(void);
void action_commit(void);
void action_amend(void);
void action_push(void);
void action_pull(void);
void action_stash(void);
void action_checkout(void);
void action_new_branch(void);
void action_delete_branch(void);
void action_apply_stash(void);
void action_pop_stash(void);
void action_drop_stash(void);
void action_copy_selection(void);
void action_find_file(const char *name);
void action_grep(const char *pattern);

void reload_all(void);

bool in_git_repo(void);

#endif
