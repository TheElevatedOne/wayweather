#define _GNU_SOURCE
#include "ui.h"
#include "util.h"

#include <langinfo.h>
#include <stdarg.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Setup ---------- */

void ui_init(void) {
  initscr();
  cbreak();
  noecho();
  nonl();
  keypad(stdscr, TRUE);
  set_escdelay(25);
  curs_set(0);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(CP_TITLE, COLOR_BLACK, COLOR_CYAN);
    init_pair(CP_SELECTED, COLOR_BLACK, COLOR_CYAN);
    init_pair(CP_ACCENT, COLOR_CYAN, -1);
    init_pair(CP_OK, COLOR_GREEN, -1);
    init_pair(CP_WARN, COLOR_YELLOW, -1);
    init_pair(CP_ERROR, COLOR_RED, -1);
    init_pair(CP_BORDER, COLOR_BLUE, -1);
    init_pair(CP_TAB, COLOR_CYAN, -1);
    init_pair(CP_COLD, COLOR_BLUE, -1);
    init_pair(CP_HOT, COLOR_RED, -1);
    init_pair(CP_INPUT, COLOR_WHITE, COLOR_BLUE);
  }
  mousemask(BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED |
                BUTTON5_PRESSED,
            NULL);
  mouseinterval(250);
}

void ui_shutdown(void) {
  curs_set(1);
  endwin();
}

int ui_getkey(WINDOW *win, wint_t *ch) {
  int rc = wget_wch(win, ch);
  /* Enter arrives as \r because of nonl(); normalise it to KEY_ENTER */
  if (rc == OK && (*ch == '\r' || *ch == '\n')) {
    *ch = KEY_ENTER;
    return KEY_CODE_YES;
  }
  return rc;
}

/* ---------- Text helpers ---------- */

int ui_put(WINDOW *win, int y, int x, int width, attr_t attr,
           const char *text) {
  if (width <= 0 || !text)
    return 0;
  size_t n = utf8_fit(text, width);
  wattron(win, attr);
  mvwaddnstr(win, y, x, text, (int)n);
  wattroff(win, attr);
  char tmp[1024];
  str_copy(tmp, text, n + 1 < sizeof tmp ? n + 1 : sizeof tmp);
  return utf8_width(tmp);
}

int ui_putf(WINDOW *win, int y, int x, int width, attr_t attr,
            const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return ui_put(win, y, x, width, attr, buf);
}

void ui_put_center(WINDOW *win, int y, int width, attr_t attr,
                   const char *text) {
  int w = utf8_width(text);
  int x = w < width ? (width - w) / 2 : 0;
  ui_put(win, y, x, width - x, attr, text);
}

void ui_put_right(WINDOW *win, int y, int right, attr_t attr,
                  const char *text) {
  int w = utf8_width(text);
  int x = right - w;
  if (x < 0)
    x = 0;
  ui_put(win, y, x, right - x, attr, text);
}

void ui_fill(WINDOW *win, int y, int x, int width, attr_t attr) {
  wattron(win, attr);
  mvwhline(win, y, x, ' ', width);
  wattroff(win, attr);
}

static bool utf8_locale(void) {
  static int cached = -1;
  if (cached < 0) {
    const char *cs = nl_langinfo(CODESET);
    cached = cs && (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "utf8") == 0);
  }
  return cached;
}

static void put_glyph(WINDOW *win, int y, int x, const wchar_t *wide,
                      char ascii, attr_t attr) {
  if (utf8_locale()) {
    cchar_t cc;
    setcchar(&cc, wide, attr, 0, NULL);
    mvwadd_wch(win, y, x, &cc);
  } else {
    mvwaddch(win, y, x, (chtype)ascii | attr);
  }
}

static void hline_glyph(WINDOW *win, int y, int x, int n, attr_t attr) {
  for (int i = 0; i < n; i++)
    put_glyph(win, y, x + i, L"─", '-', attr);
}

void ui_box(WINDOW *win, int y, int x, int h, int w, attr_t attr) {
  if (h < 2 || w < 2)
    return;
  hline_glyph(win, y, x + 1, w - 2, attr);
  hline_glyph(win, y + h - 1, x + 1, w - 2, attr);
  for (int i = 1; i < h - 1; i++) {
    put_glyph(win, y + i, x, L"│", '|', attr);
    put_glyph(win, y + i, x + w - 1, L"│", '|', attr);
  }
  put_glyph(win, y, x, L"╭", '+', attr);
  put_glyph(win, y, x + w - 1, L"╮", '+', attr);
  put_glyph(win, y + h - 1, x, L"╰", '+', attr);
  put_glyph(win, y + h - 1, x + w - 1, L"╯", '+', attr);
}

void ui_rule(WINDOW *win, int y, int x, int w, attr_t attr) {
  put_glyph(win, y, x, L"├", '+', attr);
  hline_glyph(win, y, x + 1, w - 2, attr);
  put_glyph(win, y, x + w - 1, L"┤", '+', attr);
}

void ui_frame(WINDOW *win, const char *title, bool focused) {
  attr_t a = COLOR_PAIR(focused ? CP_ACCENT : CP_BORDER);
  ui_box(win, 0, 0, getmaxy(win), getmaxx(win), a);
  if (title && *title) {
    char t[256];
    snprintf(t, sizeof t, " %s ", title);
    ui_put(win, 0, 2, getmaxx(win) - 4, A_BOLD | a, t);
  }
}

/* ---------- Line editor ---------- */

void le_set(line_edit *le, const char *utf8) {
  mbstate_t st = {0};
  const char *src = utf8 ? utf8 : "";
  size_t n = mbsrtowcs(le->buf, &src, LINE_MAX_CHARS, &st);
  if (n == (size_t)-1) {
    /* Not valid in the current locale: keep ASCII only */
    n = 0;
    for (const char *p = utf8; *p && n < LINE_MAX_CHARS; p++)
      if ((unsigned char)*p < 0x80)
        le->buf[n++] = (wchar_t)*p;
  }
  le->buf[n] = L'\0';
  le->len = (int)n;
  le->cursor = le->len;
  le->offset = 0;
}

void le_get(const line_edit *le, char *out, size_t size) {
  mbstate_t st = {0};
  const wchar_t *src = le->buf;
  size_t n = wcsrtombs(out, &src, size - 1, &st);
  if (n == (size_t)-1)
    n = 0;
  out[n < size ? n : size - 1] = '\0';
}

static void le_delete(line_edit *le, int from, int to) {
  if (from < 0)
    from = 0;
  if (to > le->len)
    to = le->len;
  if (from >= to)
    return;
  wmemmove(le->buf + from, le->buf + to, (size_t)(le->len - to + 1));
  le->len -= to - from;
  le->cursor = from;
}

bool le_key(line_edit *le, int type, wint_t ch) {
  if (type == KEY_CODE_YES) {
    switch (ch) {
    case KEY_LEFT:
      if (le->cursor > 0)
        le->cursor--;
      return true;
    case KEY_RIGHT:
      if (le->cursor < le->len)
        le->cursor++;
      return true;
    case KEY_HOME:
      le->cursor = 0;
      return true;
    case KEY_END:
      le->cursor = le->len;
      return true;
    case KEY_BACKSPACE:
      le_delete(le, le->cursor - 1, le->cursor);
      return true;
    case KEY_DC:
      le_delete(le, le->cursor, le->cursor + 1);
      return true;
    }
    return false;
  }
  switch (ch) {
  case 127:
  case CTRL('h'):
    le_delete(le, le->cursor - 1, le->cursor);
    return true;
  case CTRL('a'):
    le->cursor = 0;
    return true;
  case CTRL('e'):
    le->cursor = le->len;
    return true;
  case CTRL('u'):
    le_delete(le, 0, le->cursor);
    return true;
  case CTRL('k'):
    le_delete(le, le->cursor, le->len);
    return true;
  case CTRL('w'): {
    int p = le->cursor;
    while (p > 0 && le->buf[p - 1] == L' ')
      p--;
    while (p > 0 && le->buf[p - 1] != L' ')
      p--;
    le_delete(le, p, le->cursor);
    return true;
  }
  }
  if (ch < 0x20 || ch == 0x7f || le->len >= LINE_MAX_CHARS)
    return false;
  wmemmove(le->buf + le->cursor + 1, le->buf + le->cursor,
           (size_t)(le->len - le->cursor + 1));
  le->buf[le->cursor++] = (wchar_t)ch;
  le->len++;
  return true;
}

static int wc_cols(wchar_t c) {
  int w = wcwidth(c);
  return w < 0 ? 1 : w;
}

void le_draw(line_edit *le, WINDOW *win, int y, int x, int width,
             bool focused) {
  if (width < 2)
    return;
  /* Keep the cursor inside the visible window */
  if (le->cursor < le->offset)
    le->offset = le->cursor;
  for (;;) {
    int w = 0;
    for (int i = le->offset; i < le->cursor; i++)
      w += wc_cols(le->buf[i]);
    if (w < width || le->offset >= le->cursor)
      break;
    le->offset++;
  }

  attr_t a = focused ? COLOR_PAIR(CP_INPUT) : A_UNDERLINE;
  ui_fill(win, y, x, width, a);
  wattron(win, a);
  int col = 0, cursor_col = 0;
  for (int i = le->offset; i <= le->len; i++) {
    if (i == le->cursor)
      cursor_col = col;
    if (i == le->len)
      break;
    int cw = wc_cols(le->buf[i]);
    if (col + cw > width)
      break;
    mvwaddnwstr(win, y, x + col, &le->buf[i], 1);
    col += cw;
  }
  wattroff(win, a);
  if (focused)
    wmove(win, y, x + (cursor_col < width ? cursor_col : width - 1));
}

/* ---------- Dialog helpers ---------- */

static WINDOW *dialog_new(int h, int w) {
  if (h > LINES)
    h = LINES;
  if (w > COLS)
    w = COLS;
  WINDOW *win = newwin(h, w, (LINES - h) / 2, (COLS - w) / 2);
  keypad(win, TRUE);
  return win;
}

static void dialog_close(WINDOW *win) {
  curs_set(0);
  delwin(win);
  /* Repaint what was underneath so stacked dialogs don't leave holes */
  touchwin(stdscr);
  wnoutrefresh(stdscr);
  doupdate();
}

/* Splits `text` into lines no wider than `width` (caller frees) */
static char **wrap_text(const char *text, int width, int *count) {
  int cap = 16, n = 0;
  char **lines = xmalloc(sizeof *lines * (size_t)cap);
  char *copy = xstrdup(text);
  char *save = NULL;
  for (char *para = strtok_r(copy, "\n", &save); para || n == 0;
       para = strtok_r(NULL, "\n", &save)) {
    if (!para)
      break;
    /* strtok_r drops empty lines; "\x01" marks intentional blanks */
    const char *p = strcmp(para, "\x01") == 0 ? "" : para;
    do {
      size_t fit = utf8_fit(p, width);
      size_t cut = fit;
      if (p[fit]) {
        size_t sp = fit;
        while (sp > 0 && p[sp] != ' ')
          sp--;
        if (sp > 0)
          cut = sp;
      }
      if (n == cap)
        lines = xrealloc(lines, sizeof *lines * (size_t)(cap *= 2));
      lines[n] = xmalloc(cut + 1);
      memcpy(lines[n], p, cut);
      lines[n][cut] = '\0';
      n++;
      p += cut;
      while (*p == ' ')
        p++;
    } while (*p);
  }
  free(copy);
  *count = n;
  return lines;
}

/* Replaces empty lines so strtok_r keeps them */
static char *mark_blank_lines(const char *text) {
  strbuf sb;
  sb_init(&sb);
  for (const char *p = text; *p; p++) {
    sb_appendc(&sb, *p);
    if (*p == '\n' && (p[1] == '\n'))
      sb_appendc(&sb, '\x01');
  }
  return sb_steal(&sb);
}

void ui_message(const char *title, const char *text) {
  int w = COLS - 4 < 76 ? COLS - 4 : 76;
  int n;
  char *marked = mark_blank_lines(text);
  char **lines = wrap_text(marked, w - 4, &n);
  free(marked);
  int h = n + 4 < LINES - 2 ? n + 4 : LINES - 2;
  int view = h - 4;
  WINDOW *win = dialog_new(h, w);
  int top = 0;
  for (;;) {
    werase(win);
    ui_frame(win, title, true);
    for (int i = 0; i < view && top + i < n; i++)
      ui_put(win, 1 + i, 2, w - 4, 0, lines[top + i]);
    const char *hint = n > view ? " ↑↓ scroll · any other key closes "
                                : " press any key ";
    ui_put_right(win, h - 2, w - 2, A_DIM, hint);
    wrefresh(win);
    wint_t ch;
    int rc = ui_getkey(win, &ch);
    if (rc == KEY_CODE_YES && ch == KEY_RESIZE)
      break;
    if (rc == KEY_CODE_YES && (ch == KEY_DOWN || ch == KEY_NPAGE)) {
      int step = ch == KEY_DOWN ? 1 : view;
      top = top + step > n - view ? (n > view ? n - view : 0) : top + step;
      continue;
    }
    if (rc == KEY_CODE_YES && (ch == KEY_UP || ch == KEY_PPAGE)) {
      int step = ch == KEY_UP ? 1 : view;
      top = top - step < 0 ? 0 : top - step;
      continue;
    }
    if (rc == OK && (ch == 'j' || ch == 'k')) {
      top += ch == 'j' ? (top + view < n) : -(top > 0);
      continue;
    }
    if (rc != ERR && !(rc == KEY_CODE_YES && ch == KEY_MOUSE))
      break;
  }
  for (int i = 0; i < n; i++)
    free(lines[i]);
  free(lines);
  dialog_close(win);
}

static void draw_buttons(WINDOW *win, int y, int width,
                         const char *const *labels, int n, int focus) {
  int total = 0;
  for (int i = 0; i < n; i++)
    total += utf8_width(labels[i]) + 4 + (i ? 2 : 0);
  int x = (width - total) / 2;
  for (int i = 0; i < n; i++) {
    char b[64];
    snprintf(b, sizeof b, "[ %s ]", labels[i]);
    attr_t a = i == focus ? (COLOR_PAIR(CP_SELECTED) | A_BOLD) : 0;
    x += ui_put(win, y, x, width - x, a, b) + 2;
  }
}

bool ui_confirm(const char *title, const char *question, bool danger) {
  int w = COLS - 4 < 60 ? COLS - 4 : 60;
  int n;
  char **lines = wrap_text(question, w - 4, &n);
  int h = n + 5;
  WINDOW *win = dialog_new(h, w);
  static const char *const labels[] = {"Yes", "No"};
  int focus = danger ? 1 : 0;
  bool result = false;
  for (;;) {
    werase(win);
    ui_frame(win, title, true);
    for (int i = 0; i < n; i++)
      ui_put(win, 1 + i, 2, w - 4, 0, lines[i]);
    draw_buttons(win, h - 2, w, labels, 2, focus);
    wrefresh(win);
    wint_t ch;
    int rc = ui_getkey(win, &ch);
    if (rc == OK && (ch == 'y' || ch == 'Y')) {
      result = true;
      break;
    }
    if ((rc == OK && (ch == 'n' || ch == 'N' || ch == 'q' || ch == KEY_ESC)) ||
        (rc == KEY_CODE_YES && ch == KEY_RESIZE))
      break;
    if (rc == KEY_CODE_YES && ch == KEY_ENTER) {
      result = focus == 0;
      break;
    }
    if ((rc == KEY_CODE_YES &&
         (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_BTAB)) ||
        (rc == OK && (ch == '\t' || ch == 'h' || ch == 'l')))
      focus = !focus;
  }
  for (int i = 0; i < n; i++)
    free(lines[i]);
  free(lines);
  dialog_close(win);
  return result;
}

bool ui_input(const char *title, const char *prompt, char *buf, size_t size) {
  int w = COLS - 4 < 64 ? COLS - 4 : 64;
  int h = 7;
  WINDOW *win = dialog_new(h, w);
  line_edit le;
  le_set(&le, buf);
  bool ok = false;
  curs_set(1);
  for (;;) {
    werase(win);
    ui_frame(win, title, true);
    ui_put(win, 1, 2, w - 4, 0, prompt);
    ui_put_right(win, h - 2, w - 2, A_DIM, "Enter confirm · Esc cancel");
    le_draw(&le, win, 3, 2, w - 4, true);
    wrefresh(win);
    wint_t ch;
    int rc = ui_getkey(win, &ch);
    if (rc == ERR)
      continue;
    if (rc == KEY_CODE_YES && ch == KEY_ENTER) {
      le_get(&le, buf, size);
      ok = true;
      break;
    }
    if ((rc == OK && ch == KEY_ESC) || (rc == KEY_CODE_YES && ch == KEY_RESIZE))
      break;
    le_key(&le, rc, ch);
  }
  dialog_close(win);
  return ok;
}

int ui_pick(const char *title, const char *const *items, int n, int sel) {
  if (n <= 0)
    return -1;
  int maxw = utf8_width(title) + 6;
  for (int i = 0; i < n; i++) {
    int iw = utf8_width(items[i]) + 10;
    if (iw > maxw)
      maxw = iw;
  }
  int w = maxw < COLS - 4 ? maxw : COLS - 4;
  if (w < 30)
    w = COLS - 4 < 30 ? COLS - 4 : 30;
  int h = n + 4 < LINES - 2 ? n + 4 : LINES - 2;
  int view = h - 4;
  WINDOW *win = dialog_new(h, w);
  int top = 0;
  sel = CLAMP(sel, 0, n - 1);
  int result = -1;
  for (;;) {
    if (sel < top)
      top = sel;
    if (sel >= top + view)
      top = sel - view + 1;
    werase(win);
    ui_frame(win, title, true);
    for (int i = 0; i < view && top + i < n; i++) {
      int idx = top + i;
      attr_t a = idx == sel ? (COLOR_PAIR(CP_SELECTED) | A_BOLD) : 0;
      ui_fill(win, 1 + i, 1, w - 2, a);
      ui_putf(win, 1 + i, 2, w - 4, a, "%2d  %s", idx + 1, items[idx]);
    }
    char hint[64];
    snprintf(hint, sizeof hint, " %d/%d · Enter select · Esc cancel ",
             sel + 1, n);
    ui_put_right(win, h - 2, w - 2, A_DIM, hint);
    wrefresh(win);

    wint_t ch;
    int rc = ui_getkey(win, &ch);
    if (rc == KEY_CODE_YES) {
      switch (ch) {
      case KEY_UP:
        sel = sel > 0 ? sel - 1 : n - 1;
        break;
      case KEY_DOWN:
        sel = sel < n - 1 ? sel + 1 : 0;
        break;
      case KEY_PPAGE:
        sel = sel - view < 0 ? 0 : sel - view;
        break;
      case KEY_NPAGE:
        sel = sel + view >= n ? n - 1 : sel + view;
        break;
      case KEY_HOME:
        sel = 0;
        break;
      case KEY_END:
        sel = n - 1;
        break;
      case KEY_ENTER:
        result = sel;
        goto done;
      case KEY_RESIZE:
        goto done;
      }
    } else if (rc == OK) {
      if (ch == KEY_ESC || ch == 'q')
        goto done;
      if (ch == 'k')
        sel = sel > 0 ? sel - 1 : n - 1;
      else if (ch == 'j')
        sel = sel < n - 1 ? sel + 1 : 0;
      else if (ch >= '1' && ch <= '9' && (int)(ch - '1') < n) {
        result = (int)(ch - '1');
        goto done;
      }
    }
  }
done:
  dialog_close(win);
  return result;
}

void ui_busy(const char *text) {
  int w = utf8_width(text) + 8;
  if (w > COLS)
    w = COLS;
  WINDOW *win = dialog_new(3, w);
  ui_frame(win, NULL, true);
  ui_put(win, 1, 4, w - 6, A_BOLD, text);
  wrefresh(win);
  delwin(win);
  touchwin(stdscr);
}

/* ---------- Forms ---------- */

static void form_draw_field(WINDOW *win, form_field *f, int value, bool flag,
                            int y, int labelw, int fieldw, bool focused) {
  attr_t la = focused ? (COLOR_PAIR(CP_ACCENT) | A_BOLD) : 0;
  ui_put_right(win, y, 2 + labelw, la, f->label);
  int x = 4 + labelw;
  switch (f->type) {
  case FF_TEXT:
    le_draw(&f->edit, win, y, x, fieldw, focused);
    break;
  case FF_CHOICE: {
    attr_t a = focused ? COLOR_PAIR(CP_INPUT) : 0;
    char b[128];
    snprintf(b, sizeof b, "‹ %s ›", f->choices[value]);
    ui_fill(win, y, x, fieldw, focused ? a : 0);
    ui_put(win, y, x, fieldw, a | A_BOLD, b);
    if (focused) {
      char idx[16];
      snprintf(idx, sizeof idx, "%d/%d", value + 1, f->n_choices);
      ui_put_right(win, y, x + fieldw, a | A_DIM, idx);
    }
    break;
  }
  case FF_BOOL: {
    attr_t a = focused ? COLOR_PAIR(CP_INPUT) : 0;
    ui_fill(win, y, x, fieldw, focused ? a : 0);
    ui_put(win, y, x, fieldw, a | A_BOLD, flag ? "[x] Yes" : "[ ] No");
    break;
  }
  }
}

bool ui_form(const char *title, form_field *fields, int n,
             form_validate validate, void *user) {
  int labelw = 0;
  for (int i = 0; i < n; i++) {
    int lw = utf8_width(fields[i].label);
    if (lw > labelw)
      labelw = lw;
  }
  int w = COLS - 4 < 72 ? COLS - 4 : 72;
  int fieldw = w - labelw - 6;
  int view = n < LINES - 7 ? n : LINES - 7;
  if (view < 1)
    view = 1;
  int h = view + 6;

  int *values = xcalloc((size_t)n, sizeof *values);
  bool *flags = xcalloc((size_t)n, sizeof *flags);
  for (int i = 0; i < n; i++) {
    if (fields[i].type == FF_TEXT)
      le_set(&fields[i].edit, fields[i].text);
    else if (fields[i].type == FF_CHOICE)
      values[i] = CLAMP(*fields[i].choice, 0, fields[i].n_choices - 1);
    else
      flags[i] = *fields[i].flag;
  }

  WINDOW *win = dialog_new(h, w);
  static const char *const labels[] = {"Save", "Cancel"};
  int focus = 0, top = 0; /* focus n = Save, n + 1 = Cancel */
  char error[256] = "";
  bool saved = false;

  for (;;) {
    if (focus < n) {
      if (focus < top)
        top = focus;
      if (focus >= top + view)
        top = focus - view + 1;
    }
    werase(win);
    ui_frame(win, title, true);
    for (int i = 0; i < view && top + i < n; i++) {
      int idx = top + i;
      if (idx != focus)
        form_draw_field(win, &fields[idx], values[idx], flags[idx], 1 + i,
                        labelw, fieldw, false);
    }
    if (top > 0)
      ui_put(win, 1, w - 3, 1, A_DIM, "↑");
    if (top + view < n)
      ui_put(win, view, w - 3, 1, A_DIM, "↓");

    if (error[0])
      ui_put(win, h - 4, 2, w - 4, COLOR_PAIR(CP_ERROR) | A_BOLD, error);
    else if (focus < n && fields[focus].hint)
      ui_put(win, h - 4, 2, w - 4, A_DIM, fields[focus].hint);
    draw_buttons(win, h - 3, w, labels, 2, focus - n);
    ui_put_right(win, h - 2, w - 2, A_DIM,
                 "Tab/↑↓ move · ←→ change · Enter save · Esc cancel");

    /* The focused field last so the cursor ends up in it */
    bool text_focus = focus < n && fields[focus].type == FF_TEXT;
    curs_set(text_focus ? 1 : 0);
    if (focus < n)
      form_draw_field(win, &fields[focus], values[focus], flags[focus],
                      1 + focus - top, labelw, fieldw, true);
    wrefresh(win);

    wint_t ch;
    int rc = ui_getkey(win, &ch);
    if (rc == ERR)
      continue;
    bool is_key = rc == KEY_CODE_YES;

    if ((!is_key && ch == KEY_ESC) || (is_key && ch == KEY_RESIZE))
      break;
    if ((!is_key && ch == '\t') || (is_key && ch == KEY_DOWN)) {
      focus = (focus + 1) % (n + 2);
      continue;
    }
    if (is_key && (ch == KEY_BTAB || ch == KEY_UP)) {
      focus = (focus + n + 1) % (n + 2);
      continue;
    }
    bool submit = (is_key && (ch == KEY_F(2))) || (!is_key && ch == CTRL('s'));
    if (is_key && ch == KEY_ENTER) {
      if (focus == n + 1)
        break; /* Cancel */
      submit = true;
    }
    if (submit) {
      for (int i = 0; i < n; i++) {
        if (fields[i].type == FF_TEXT)
          le_get(&fields[i].edit, fields[i].text, fields[i].text_size);
        else if (fields[i].type == FF_CHOICE)
          *fields[i].choice = values[i];
        else
          *fields[i].flag = flags[i];
      }
      error[0] = '\0';
      if (!validate || validate(fields, n, user, error, sizeof error)) {
        saved = true;
        break;
      }
      continue;
    }

    if (focus >= n) {
      if ((is_key && (ch == KEY_LEFT || ch == KEY_RIGHT)))
        focus = focus == n ? n + 1 : n;
      continue;
    }
    form_field *f = &fields[focus];
    error[0] = '\0';
    if (f->type == FF_TEXT) {
      le_key(&f->edit, rc, ch);
    } else if (f->type == FF_CHOICE) {
      if ((is_key && ch == KEY_LEFT) || (!is_key && ch == 'h'))
        values[focus] = (values[focus] + f->n_choices - 1) % f->n_choices;
      else if ((is_key && ch == KEY_RIGHT) || (!is_key && (ch == ' ' || ch == 'l')))
        values[focus] = (values[focus] + 1) % f->n_choices;
    } else {
      if ((is_key && (ch == KEY_LEFT || ch == KEY_RIGHT)) ||
          (!is_key && ch == ' '))
        flags[focus] = !flags[focus];
      else if (!is_key && (ch == 'y' || ch == 'Y'))
        flags[focus] = true;
      else if (!is_key && (ch == 'n' || ch == 'N'))
        flags[focus] = false;
    }
  }
  free(values);
  free(flags);
  dialog_close(win);
  return saved;
}
