#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../render.h"
#include "../state.h"
#include "../util.h"
#include "../views.h"

void draw_branches(int top, int h) {
	int w = g_app_state.cols;
	box_top(top, 1, w, "Branches", true, NULL);
	box_sides(top, 1, w, h, true);
	box_fill(top, 1, w, h, TH->bg_panel);
	box_bot(top + h - 1, 1, w, true);

	int row = top + 1, lim = top + h - 1, vis = lim - row;
	if (g_app_state.branch_sel < g_app_state.branch_scroll)
		g_app_state.branch_scroll = g_app_state.branch_sel;
	if (g_app_state.branch_sel >= g_app_state.branch_scroll + vis)
		g_app_state.branch_scroll = g_app_state.branch_sel - vis + 1;

	at(row, 2);
	cbg(TH->bg_header);
	cfg(TH->fg_accent3);
	g_app_state.cur_bold = true;
	char hdb[256];
	snprintf(hdb, sizeof(hdb), "  %-2s %-40s %-28s %s", "★", "Name", "Upstream", "±");
	ppad(hdb, w - 3);
	rst();
	row++;

	for (int i = g_app_state.branch_scroll; i < g_app_state.branch_count && row < lim; i++, row++) {
		GitBranch *b = &g_app_state.branches[i];
		bool sel = (g_app_state.branch_sel == i);
		at(row, 2);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
			g_app_state.cur_bold = true;
		} else
			cbg(TH->bg_panel);
		if (b->is_current) {
			cfg(sel ? TH->fg_sel : TH->fg_staged);
			g_app_state.cur_bold = true;
			ppad("✱ ", 2);
		} else
			ppad("  ", 2);
		if (sel) {
			cbg(TH->bg_sel);
			cfg(TH->fg_sel);
		} else
			cbg(TH->bg_panel);
		if (b->is_remote) {
			cfg(sel ? TH->fg_sel : TH->fg_ref_remote);
			ppad("⬡ ", 2);
		} else {
			cfg(sel ? TH->fg_sel : TH->fg_ref_local);
			ppad("⬢ ", 2);
		}
		if (sel) {
			cfg(TH->fg_sel);
		} else
			cfg(TH->fg_normal);
		char nb[42];
		snprintf(nb, sizeof(nb), "%-40s", b->name);
		ppad(nb, 40);
		ppad(" ", 1);
		ppad(b->upstream, 28);
		ppad(" ", 1);
		if (b->ahead || b->behind) {
			cfg(sel ? TH->fg_sel : TH->fg_accent1);
			char ab[32];
			snprintf(ab, sizeof(ab), "↑%d ↓%d", b->ahead, b->behind);
			ppad(ab, (int)strlen(ab));
		} else if (b->upstream[0]) {
			cfg(sel ? TH->fg_sel : TH->fg_staged);
			ppad("✓", 1);
		}
		rst();
	}
	while (row < lim) {
		at(row, 2);
		cbg(TH->bg_panel);
		for (int i = 0; i < w - 3; i++) put_cell(row, 2 + i, " ");
		rst();
		row++;
	}
}
