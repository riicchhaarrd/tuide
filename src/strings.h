#ifndef STRINGS_H
#define STRINGS_H

#include <stddef.h>

typedef struct {
	const char *left;
	const char *right;
} UiHelpEntry;

typedef struct {
	const char *app_name;
	const char *tabbar_title;
	const char *exit_message;
	const char *err_requires_terminal;
	const char *err_not_git_repo;
	const char *err_failed_open_fmt;
	const char *branch_unknown;

	const char *tab_changes;
	const char *tab_log;
	const char *tab_branches;
	const char *tab_stash;
	const char *tab_help;

	const char *sidebar_browser_active;
	const char *sidebar_browser_inactive;
	const char *sidebar_git_active;
	const char *sidebar_git_inactive;

	const char *focus_unknown;
	const char *focus_changes;
	const char *focus_graph;
	const char *focus_diff;
	const char *focus_browser;
	const char *focus_editor;
	const char *focus_cli;

	const char *hint_empty;
	const char *hint_status_changes;
	const char *hint_status_graph;
	const char *hint_status_browser;
	const char *hint_status_editor;
	const char *hint_status_diff;
	const char *hint_log;
	const char *hint_branches;
	const char *hint_stash;
	const char *hint_editor_browser;
	const char *hint_editor_editor;
	const char *hint_help;

	const char *menu_title;
	const char *menu_copy;
	const char *menu_cut;
	const char *menu_paste;
	const char *menu_cancel;
	const char *menu_stage;
	const char *menu_unstage;

	const char *prompt_find;
	const char *prompt_go_to_line;
	const char *prompt_go_to_file;
	const char *prompt_global_search;
	const char *prompt_new_file;
	const char *prompt_commit_message;
	const char *prompt_amend_message;
	const char *prompt_stash_message;
	const char *prompt_new_branch;

	const char *cli_prompt;
	const char *msg_cli_executing_fmt;
	const char *msg_cli_success_fmt;
	const char *msg_cli_failed_fmt;
	const char *msg_cancelled;

	const char *msg_nothing_to_undo;
	const char *msg_undo;
	const char *msg_nothing_to_redo;
	const char *msg_redo;
	const char *msg_saved_fmt;
	const char *msg_jumped_to_line_fmt;
	const char *err_invalid_line_number;
	const char *msg_found_fmt;
	const char *msg_found_wrapped_fmt;
	const char *err_not_found_fmt;
	const char *err_no_search_term;
	const char *msg_next_fmt;
	const char *msg_next_wrapped_fmt;
	const char *msg_prev_fmt;
	const char *msg_prev_wrapped_fmt;
	const char *msg_copied_editor_fmt;

	const char *msg_created_file_fmt;
	const char *err_failed_create_file_fmt;
	const char *msg_deleted_file_fmt;
	const char *err_failed_delete_file_fmt;

	const char *msg_returned_from_fmt;
	const char *err_empty_commit_message;
	const char *err_nothing_staged;
	const char *msg_committed_fmt;
	const char *err_commit_failed;
	const char *msg_amended;
	const char *err_amend_failed;
	const char *msg_theme_fmt;
	const char *msg_continuous_diff_fmt;
	const char *diff_continuous_on;
	const char *diff_continuous_off;
	const char *msg_diff_wrap_fmt;
	const char *toggle_on;
	const char *toggle_off;

	const char *msg_refreshed;
	const char *msg_unstaged_fmt;
	const char *msg_staged_fmt;
	const char *msg_staged_all;
	const char *msg_unstaged_all;
	const char *err_select_working_tree_diff;
	const char *err_no_file_selected;
	const char *err_no_diff;
	const char *err_viewing_unstaged_diff;
	const char *err_viewing_staged_diff;
	const char *err_parse_hunks;
	const char *err_no_hunks;
	const char *err_mkstemp_failed;
	const char *err_mkstemp;
	const char *err_out_of_memory;
	const char *err_no_changed_lines_selected;
	const char *err_no_hunk_selected;
	const char *msg_unstaged_selection;
	const char *msg_staged_selection;
	const char *err_unstage_failed;
	const char *err_stage_failed;
	const char *msg_removed_fmt;
	const char *msg_discarded_fmt;
	const char *err_empty_message;
	const char *msg_pushing;
	const char *msg_pushed;
	const char *err_push_failed;
	const char *msg_pulling;
	const char *msg_pulled;
	const char *err_pull_failed;
	const char *msg_stashed;
	const char *err_stash_failed;
	const char *msg_checked_out_fmt;
	const char *err_checkout_failed;
	const char *err_name_required;
	const char *msg_created_branch_fmt;
	const char *err_branch_failed;
	const char *err_cannot_delete_current_branch;
	const char *msg_deleted_branch_fmt;
	const char *err_delete_failed;
	const char *msg_applied_stash_fmt;
	const char *err_apply_failed;
	const char *msg_popped_stash_fmt;
	const char *err_pop_failed;
	const char *msg_dropped_stash_fmt;
	const char *err_drop_failed;
	const char *msg_copied_clipboard_fmt;
	const char *err_no_files_matching_fmt;
	const char *err_no_matches_fmt;

	const char *diff_title_empty;
	const char *diff_empty_msg;
	const char *diff_title_staged_suffix;
	const char *diff_title_files_fmt;
	const char *diff_title_search_fmt;
	const char *diff_title_commit_fmt;
	const char *diff_title_files_prefix;
	const char *diff_title_search_prefix;
	const char *diff_title_commit_prefix;
	const char *diff_view_full_label;
	const char *diff_label_old;
	const char *diff_label_new;
	const char *diff_side_split;
	const char *diff_side_unify;
	const char *diff_ctx_full;
	const char *diff_ctx_hunk;
	const char *diff_wrap_label_on;
	const char *diff_wrap_label_off;

	const char *title_changes;
	const char *title_graph;
	const char *title_branches;
	const char *title_commit_log;
	const char *title_stash_fmt;
	const char *title_help;
	const char *title_files;
	const char *title_editor;
	const char *title_editor_save;
	const char *editor_no_files;
	const char *editor_status_position_fmt;
	const char *editor_status_encoding_lang_fmt;
	const char *commit_placeholder;

	const char *header_commit_graph;
	const char *header_commit_hash;
	const char *header_commit_refs;
	const char *header_commit_author;
	const char *header_commit_date;
	const char *header_commit_subject;
	const char *header_branches_name;
	const char *header_branches_upstream;
	const char *header_branches_delta;

	const char *header_staged_fmt;
	const char *header_unstaged_fmt;
	const char *commit_bar_icon;
	const char *commit_button_label;
	const char *amend_button_label;

	const char *stash_empty_msg;

	const char *lang_plain_text;
	const char *lang_c;
	const char *lang_c_header;
	const char *lang_cpp;
	const char *lang_python;
	const char *lang_javascript;
	const char *lang_typescript;
	const char *lang_markdown;

	const UiHelpEntry *help_entries;
} UiStrings;

const UiStrings *ui_strings(void);
void ui_set_strings(const UiStrings *strings);
extern const UiStrings UI_EN;

#define UI (ui_strings())

#endif
