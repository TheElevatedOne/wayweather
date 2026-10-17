#ifndef WW_UI_H_
#define WW_UI_H_

/* Reusable ncurses widgets: colors, text helpers, line editor, dialogs */

#include <ncurses.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

enum {
  CP_NORMAL = 0,
  CP_TITLE = 1,
  CP_SELECTED,
  CP_ACCENT,
  CP_OK,
  CP_WARN,
  CP_ERROR,
  CP_BORDER,
  CP_TAB,
  CP_COLD,
  CP_HOT,
  CP_INPUT,
};

#define KEY_ESC 27
#define CTRL(c) ((c) & 0x1f)

void ui_init(void);
void ui_shutdown(void);

/* Reads one key; returns OK/KEY_CODE_YES like wget_wch, ERR on timeout */
int ui_getkey(WINDOW *win, wint_t *ch);

/* Prints `text` at (y, x) clipped to `width` columns; returns columns used */
int ui_put(WINDOW *win, int y, int x, int width, attr_t attr, const char *text);
int ui_putf(WINDOW *win, int y, int x, int width, attr_t attr,
            const char *fmt, ...) __attribute__((format(printf, 6, 7)));
/* Centered / right-aligned variants */
void ui_put_center(WINDOW *win, int y, int width, attr_t attr,
                   const char *text);
void ui_put_right(WINDOW *win, int y, int right, attr_t attr,
                  const char *text);
/* Fills a row with spaces using `attr` */
void ui_fill(WINDOW *win, int y, int x, int width, attr_t attr);
/* Rounded Unicode box (falls back to ASCII outside UTF-8 locales) */
void ui_box(WINDOW *win, int y, int x, int h, int w, attr_t attr);
/* Horizontal rule inside a box, joined to its borders */
void ui_rule(WINDOW *win, int y, int x, int w, attr_t attr);
/* Box around the whole window with a title in the top border */
void ui_frame(WINDOW *win, const char *title, bool focused);

/* ---------- Line editor ---------- */

#define LINE_MAX_CHARS 255

typedef struct {
  wchar_t buf[LINE_MAX_CHARS + 1];
  int len;
  int cursor;
  int offset; /* first visible character */
} line_edit;

void le_set(line_edit *le, const char *utf8);
void le_get(const line_edit *le, char *out, size_t size);
/* Handles editing keys; returns true if the key was consumed */
bool le_key(line_edit *le, int type, wint_t ch);
/* Draws the field; places the cursor when `focused` */
void le_draw(line_edit *le, WINDOW *win, int y, int x, int width,
             bool focused);

/* ---------- Dialogs ---------- */

/* Scrollable message box, any key closes */
void ui_message(const char *title, const char *text);
/* Yes/No question; `danger` makes "No" the default */
bool ui_confirm(const char *title, const char *question, bool danger);
/* Single line prompt; returns false on Esc */
bool ui_input(const char *title, const char *prompt, char *buf, size_t size);
/* Pick one of `n` items; returns index or -1 on cancel */
int ui_pick(const char *title, const char *const *items, int n, int sel);
/* Small "please wait" popup (stays until the next redraw) */
void ui_busy(const char *text);

/* ---------- Forms ---------- */

typedef enum { FF_TEXT, FF_CHOICE, FF_BOOL } ff_type;

typedef struct {
  const char *label;
  ff_type type;
  const char *hint;
  /* FF_TEXT: UTF-8 buffer edited in place */
  char *text;
  size_t text_size;
  /* FF_CHOICE */
  const char *const *choices;
  int n_choices;
  int *choice;
  /* FF_BOOL */
  bool *flag;
  /* internal */
  line_edit edit;
} form_field;

typedef bool (*form_validate)(form_field *fields, int n, void *user,
                              char *err, size_t errlen);

/* Runs a modal form. Values are written to the targets on submit; callers
 * should pass copies and discard them when this returns false. */
bool ui_form(const char *title, form_field *fields, int n,
             form_validate validate, void *user);

#endif // !WW_UI_H_
