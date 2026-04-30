/* gui.c - Window Manager & GUI */
#include <stdint.h>

/* External globals */
extern uint8_t *fb;
extern int fb_pitch, fb_width, fb_height;
extern int mouse_x, mouse_y, mouse_buttons;

/* GFX functions */
extern void fill_rect(int x, int y, int w, int h, uint8_t c);
extern void hline(int x, int y, int w, uint8_t c);
extern void vline(int x, int y, int h, uint8_t c);
extern void putpixel(int x, int y, uint8_t c);
extern void draw_raised_box(int x, int y, int w, int h);
extern void draw_sunken_box(int x, int y, int w, int h);
extern void draw_char(int x, int y, char c, uint8_t fg, uint8_t bg);
extern void draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg);
extern int str_len(const char *s);
extern int str_cmp(const char *a, const char *b);

/* Mouse functions */
extern void mouse_handle_data(uint8_t d);

/* Keyboard functions */
extern char scancode_to_ascii(uint8_t sc);

/* I/O */
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern void io_wait(void);

/* Colors */
#define C_BLACK   0
#define C_BLUE    1
#define C_CYAN    3
#define C_RED     4
#define C_LGRAY   7
#define C_DGRAY   8
#define C_LBLUE   9
#define C_WHITE   15
#define DESKTOP    C_CYAN
#define TASKBAR    C_LGRAY
#define TITLE_ACT  C_LBLUE
#define TITLE_INACT C_DGRAY
#define BTN_FACE   C_LGRAY
#define WIN_BG     C_WHITE

/* Window system */
#define MAX_WIN 5
static int win_x[MAX_WIN], win_y[MAX_WIN], win_w[MAX_WIN], win_h[MAX_WIN];
static int win_vis[MAX_WIN];
static char win_title[MAX_WIN][30];
static int win_count = 0;
static int active_win = -1;
static int dragging = -1;
static int drag_ox, drag_oy;

/* Notepad */
static char notepad_buf[2000];
static int notepad_len = 0;

/* Start menu */
static int start_open = 0;
static const char *start_items[] = {"Notepad", "My PC", "Calculator", "Shut Down"};
static int start_count = 4;
static int start_y;

/* Desktop icons */
static const char *icons[] = {"Notepad", "My PC", "Calc"};
static int icon_x[] = {20, 20, 20};
static int icon_y_pos[] = {20, 100, 180};
static int icon_n = 3;

/* Error popup */
static int err_active = 0, err_timer = 0;
static int err_x, err_y;

/* Cursor */
static uint8_t cursor_bg[22*22];

/* Taskbar height */
static int tbh;

/* Random */
static uint32_t rseed = 0xDEAD;
static uint32_t rand_val(void) {
    rseed = (1103515245 * rseed + 12345) & 0x7FFFFFFF;
    return rseed;
}

static void cursor_save(void) {
    int idx = 0;
    for (int dy = 0; dy < 22; dy++)
        for (int dx = 0; dx < 22; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            cursor_bg[idx++] = (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                               ? fb[py * fb_pitch + px] : 0;
        }
}
static void cursor_restore(void) {
    int idx = 0;
    for (int dy = 0; dy < 22; dy++)
        for (int dx = 0; dx < 22; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = cursor_bg[idx];
            idx++;
        }
}
static void cursor_draw(void) {
    /* Triangle arrow */
    static const int arrow[22][22] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };
    for (int dy = 0; dy < 22; dy++)
        for (int dx = 0; dx < 22; dx++)
            if (arrow[dy][dx]) putpixel(mouse_x + dx, mouse_y + dy, C_WHITE);
}

static void draw_titlebar(int x, int y, int w, const char *t, uint8_t c) {
    fill_rect(x, y, w, 18, c);
    draw_string(x + 4, y + 2, t, C_WHITE, c);
    int bx = x + w - 18;
    fill_rect(bx, y, 16, 16, BTN_FACE);
    draw_raised_box(bx, y, 16, 16);
    draw_char(bx + 4, y + 2, 'X', C_BLACK, BTN_FACE);
}

static void draw_window_frame(int i) {
    if (!win_vis[i]) return;
    int x = win_x[i], y = win_y[i], w = win_w[i], h = win_h[i];
    uint8_t tc = (i == active_win) ? TITLE_ACT : TITLE_INACT;
    fill_rect(x, y, w, h, WIN_BG);
    draw_titlebar(x, y, w, win_title[i], tc);
    draw_raised_box(x, y + 18, w, h - 18);
}

static void draw_notepad(int i) {
    int cx = win_x[i] + 4, cy = win_y[i] + 22;
    int mc = (win_w[i] - 8) / 8, mr = (win_h[i] - 40) / 8;
    fill_rect(cx, cy, mc * 8, mr * 8, C_WHITE);
    draw_sunken_box(cx - 1, cy - 1, mc * 8 + 2, mr * 8 + 2);
    int pos = 0, row = 0;
    while (row < mr && pos < notepad_len) {
        int col = 0;
        while (col < mc && pos < notepad_len && notepad_buf[pos] != '\n') {
            draw_char(cx + col * 8, cy + row * 8, notepad_buf[pos], C_BLACK, C_WHITE);
            col++; pos++;
        }
        if (pos < notepad_len && notepad_buf[pos] == '\n') pos++;
        row++;
    }
}

static void draw_icon(int x, int y, const char *label) {
    fill_rect(x, y, 32, 40, DESKTOP);
    fill_rect(x + 2, y + 2, 28, 28, C_WHITE);
    draw_raised_box(x + 2, y + 2, 28, 28);
    fill_rect(x + 4, y + 4, 24, 8, C_BLUE);
    hline(x + 6, y + 16, 20, C_DGRAY);
    hline(x + 6, y + 19, 14, C_DGRAY);
    hline(x + 6, y + 22, 16, C_DGRAY);
    int len = str_len(label);
    int tx = x + (32 - len * 8) / 2;
    for (int i = 0; i < len; i++)
        draw_char(tx + i * 8, y + 34, label[i], C_WHITE, DESKTOP);
}

static void draw_taskbar(void) {
    fill_rect(0, fb_height - tbh, fb_width, tbh, TASKBAR);
    hline(0, fb_height - tbh, fb_width, C_WHITE);
    /* Start button */
    int sx = 2, sy = fb_height - tbh + 2;
    fill_rect(sx, sy, 56, tbh - 4, BTN_FACE);
    draw_raised_box(sx, sy, 56, tbh - 4);
    draw_string(sx + 8, sy + 3, "Start", C_BLACK, BTN_FACE);
    /* Clock */
    int cx = fb_width - 56;
    draw_sunken_box(cx, fb_height - tbh + 2, 52, tbh - 4);
    draw_string(cx + 10, fb_height - tbh + 4, "12:00", C_BLACK, BTN_FACE);
}

static void draw_start_menu(void) {
    int sx = 2, sy = fb_height - tbh - start_count * 24 - 4;
    start_y = sy;
    fill_rect(sx, sy, 160, start_count * 24 + 4, BTN_FACE);
    draw_raised_box(sx, sy, 160, start_count * 24 + 4);
    for (int i = 0; i < start_count; i++) {
        int iy = sy + 2 + i * 24;
        draw_string(sx + 8, iy + 4, start_items[i], C_BLACK, BTN_FACE);
    }
}

static void show_error_popup(void) {
    err_x = (fb_width - 200) / 2;
    err_y = (fb_height - 70) / 2;
    fill_rect(err_x, err_y, 200, 70, BTN_FACE);
    draw_raised_box(err_x, err_y, 200, 70);
    draw_titlebar(err_x, err_y, 200, "Error", C_RED);
    draw_string(err_x + 20, err_y + 30, "No filesystem found!", C_BLACK, BTN_FACE);
    /* OK button */
    int bx = err_x + 80;
    fill_rect(bx, err_y + 48, 40, 16, BTN_FACE);
    draw_raised_box(bx, err_y + 48, 40, 16);
    draw_string(bx + 10, err_y + 50, "OK", C_BLACK, BTN_FACE);
    err_active = 1; err_timer = 400000;
}

static int in_rect(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

static void add_window(int x, int y, int w, int h, const char *t) {
    if (win_count >= MAX_WIN) return;
    int i = win_count++;
    win_x[i] = x; win_y[i] = y; win_w[i] = w; win_h[i] = h;
    for (int j = 0; j < 30 && t[j]; j++) win_title[i][j] = t[j];
    win_vis[i] = 1;
    active_win = i;
}

void gui_init(void) {
    tbh = (fb_height > 200) ? 28 : 20;
    cursor_save();
}

void gui_main_loop(void) {
    int tick = 0, prev_btn = 0;
    while (1) {
        /* ==== POLL INPUT ==== */
        while (inb(0x64) & 1) {
            uint8_t st = inb(0x64), d = inb(0x60);
            if (st & 0x20) {
                mouse_handle_data(d);
                int click = (prev_btn == 0 && mouse_buttons == 1);
                prev_btn = mouse_buttons;
                
                if (click) {
                    /* Start button */
                    if (in_rect(mouse_x, mouse_y, 2, fb_height - tbh + 2, 56, tbh - 4))
                        start_open = !start_open;
                    /* Desktop icons */
                    for (int i = 0; i < icon_n; i++) {
                        if (in_rect(mouse_x, mouse_y, icon_x[i], icon_y_pos[i], 32, 40)) {
                            start_open = 0;
                            if (i == 0) add_window(100, 50, 350, 250, "Notepad - Untitled");
                            else show_error_popup();
                        }
                    }
                    /* Window close buttons */
                    for (int i = win_count - 1; i >= 0; i--) {
                        if (!win_vis[i]) continue;
                        int wx = win_x[i], wy = win_y[i], ww = win_w[i];
                        if (in_rect(mouse_x, mouse_y, wx + ww - 18, wy, 16, 16)) {
                            win_vis[i] = 0;
                            if (active_win == i) {
                                active_win = -1;
                                for (int j = win_count - 1; j >= 0; j--)
                                    if (win_vis[j]) { active_win = j; break; }
                            }
                        }
                    }
                    /* Error OK */
                    if (err_active && in_rect(mouse_x, mouse_y, err_x + 80, err_y + 48, 40, 16))
                        err_active = 0;
                    /* Start menu items */
                    if (start_open) {
                        for (int i = 0; i < start_count; i++) {
                            int iy = start_y + 2 + i * 24;
                            if (in_rect(mouse_x, mouse_y, 2, iy, 156, 22)) {
                                start_open = 0;
                                if (i == 0) add_window(100, 50, 350, 250, "Notepad - Untitled");
                                else if (i == 3) show_error_popup();
                                else show_error_popup();
                            }
                        }
                    }
                    /* Window drag */
                    dragging = -1;
                    for (int i = win_count - 1; i >= 0; i--) {
                        if (!win_vis[i]) continue;
                        int wx = win_x[i], wy = win_y[i], ww = win_w[i];
                        if (in_rect(mouse_x, mouse_y, wx, wy, ww, 18)) {
                            active_win = i;
                            dragging = i;
                            drag_ox = mouse_x - wx;
                            drag_oy = mouse_y - wy;
                            break;
                        }
                    }
                }
                if (mouse_buttons == 1 && dragging >= 0) {
                    win_x[dragging] = mouse_x - drag_ox;
                    win_y[dragging] = mouse_y - drag_oy;
                }
                if (mouse_buttons == 0) dragging = -1;
            } else {
                /* Keyboard */
                char c = scancode_to_ascii(d);
                if (c == '\b') { if (notepad_len > 0) notepad_len--; }
                else if (c == '\n') { if (notepad_len < 1999) notepad_buf[notepad_len++] = '\n'; }
                else if (c >= 32 && c <= 126) { if (notepad_len < 1999) notepad_buf[notepad_len++] = c; }
            }
        }

        /* ==== REDRAW ==== */
        cursor_restore();
        fill_rect(0, 0, fb_width, fb_height - tbh, DESKTOP);
        for (int i = 0; i < icon_n; i++) draw_icon(icon_x[i], icon_y_pos[i], icons[i]);
        draw_taskbar();
        if (start_open) draw_start_menu();
        for (int i = 0; i < win_count; i++) {
            if (win_vis[i]) {
                draw_window_frame(i);
                if (str_cmp(win_title[i], "Notepad - Untitled") == 0)
                    draw_notepad(i);
            }
        }
        if (err_active) {
            fill_rect(err_x, err_y, 200, 70, BTN_FACE);
            draw_raised_box(err_x, err_y, 200, 70);
            draw_titlebar(err_x, err_y, 200, "Error", C_RED);
            draw_string(err_x + 20, err_y + 30, "No filesystem found!", C_BLACK, BTN_FACE);
            int bx = err_x + 80;
            fill_rect(bx, err_y + 48, 40, 16, BTN_FACE);
            draw_raised_box(bx, err_y + 48, 40, 16);
            draw_string(bx + 10, err_y + 50, "OK", C_BLACK, BTN_FACE);
        }
        cursor_save();
        cursor_draw();

        /* Random errors */
        tick++;
        if (!err_active && (tick % (5000000 + rand_val() % 3000000)) == 0) show_error_popup();
        if (err_active) { err_timer--; if (err_timer <= 0) err_active = 0; }

        for (volatile int dly = 0; dly < 300; dly++);
    }
}
