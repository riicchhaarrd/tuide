#ifndef VIEWS_H
#define VIEWS_H

void draw_changes(int top, int h);
void draw_graph(int top, int h);
void draw_diff(int top, int render_x, int render_width, int h);
void draw_log(int top, int h);
void draw_branches(int top, int h);
void draw_stash(int top, int h);
void draw_help(int top, int h);
void draw_editor(int top, int render_x, int render_width, int h);
void draw_browser(int top, int h);

#endif
