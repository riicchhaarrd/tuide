#ifndef TERM_H
#define TERM_H

#include "state.h"

/* Key definitions */
typedef struct {
	int btn, col, row;
	bool release, shift, ctrl;
} MouseEvt;
typedef enum {
	KEY_NONE = 0,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_PGUP,
	KEY_PGDN,
	KEY_HOME,
	KEY_END,
	KEY_ENTER,
	KEY_ESC,
	KEY_BACKSPACE,
	KEY_DEL,
	KEY_TAB,
	KEY_SHIFT_TAB,
	KEY_CTRL_A,
	KEY_CTRL_B,
	KEY_CTRL_C,
	KEY_CTRL_D,
	KEY_CTRL_E,
	KEY_CTRL_F,
	KEY_CTRL_G,
	KEY_CTRL_K,
	KEY_CTRL_N,
	KEY_CTRL_P,
	KEY_CTRL_U,
	KEY_CTRL_W,
	KEY_CTRL_Y,
	KEY_CTRL_R,
	KEY_CTRL_S,
	KEY_CTRL_L,
	KEY_CTRL_Q,
	KEY_CTRL_V,
	KEY_CTRL_X,
	KEY_CTRL_Z,
	KEY_SHIFT_UP,
	KEY_SHIFT_DOWN,
	KEY_SHIFT_LEFT,
	KEY_SHIFT_RIGHT,
	KEY_MOUSE,
	KEY_CHAR,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5
} KeyType;
typedef struct {
	KeyType type;
	char ch;
	MouseEvt mouse;
} Key;

/* Constants */
#define ESC "\x1b"
#define CSI ESC "["
#define T_CLEAR CSI "2J" CSI "H"
#define T_HIDE CSI "?25l"
#define T_SHOW CSI "?25h"
#define T_ALT ESC "[?1049h"
#define T_NORM ESC "[?1049l"
#define T_MOUSE_ON ESC "[?1000h" ESC "[?1002h" ESC "[?1006h"
#define T_MOUSE_OFF ESC "[?1000l" ESC "[?1002l" ESC "[?1006l"
#define T_RESET CSI "0m"
#define T_BOLD CSI "1m"
#define T_DIM CSI "2m"
#define T_ITALIC CSI "3m"
#define T_UNDER CSI "4m"
#define T_REVERSE CSI "7m"

void term_raw(void);
void term_restore(void);
void get_winsize(void);
Key read_key(void);

#endif
