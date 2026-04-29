/* kernel.c - Windows-like UI OS */
#include <stdint.h>

/* I/O ports */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* Colors */
#define COLOR_BLACK       0
#define COLOR_BLUE        1
#define COLOR_WHITE       15
#define COLOR_DARKGRAY    8
#define COLOR_LIGHTGRAY   7
#define COLOR_RED         4
#define COLOR_LIGHTBLUE   9
#define COLOR_YELLOW      14

/* Windows theme colors */
#define WIN_DESKTOP        COLOR_BLUE
#define WIN_TITLEBAR       COLOR_DARKGRAY
#define WIN_TITLEBAR_ACTIVE COLOR_LIGHTBLUE
#define WIN_BORDER_LIGHT   COLOR_WHITE
#define WIN_BORDER_DARK    COLOR_DARKGRAY
#define WIN_BUTTON_FACE    COLOR_LIGHTGRAY
#define WIN_WINDOW_BG      COLOR_WHITE
#define WIN_TASKBAR        COLOR_LIGHTGRAY

static uint8_t *fb;
static int fb_pitch, fb_width, fb_height;

/* Mouse */
static int mouse_x, mouse_y, mouse_cycle;
static uint8_t mouse_bytes[3];
#define CURSOR_SIZE 6
static uint8_t cursor_bg[(CURSOR_SIZE*2+1)*(CURSOR_SIZE*2+1)];

/* Editor */
static char text_buffer[3200];
static int text_len = 0, scroll_line = 0;

/* Window */
static int win_x = 120, win_y = 80, win_w = 400, win_h = 280;
static int taskbar_h = 24;

/* Rand */
static uint32_t seed = 0xDEADBEEF;
static uint32_t rand(void) {
    seed = (1103515245 * seed + 12345) & 0x7FFFFFFF;
    return seed;
}

/* Error popup */
static int error_active = 0, error_timer = 0;
static int error_x, error_y, error_w, error_h;
static char error_text[40];
static uint8_t error_bg[300*80];

/* ----- Drawing Primitives ----- */
static inline void putpixel(int x, int y, uint8_t c) {
    if (x >= 0 && x < fb_width && y >= 0 && y < fb_height)
        fb[y * fb_pitch + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            putpixel(x + dx, y + dy, c);
}

static void hline(int x, int y, int w, uint8_t c) {
    for (int i = 0; i < w; i++) putpixel(x + i, y, c);
}
static void vline(int x, int y, int h, uint8_t c) {
    for (int i = 0; i < h; i++) putpixel(x, y + i, c);
}

/* ----- 3D Effects ----- */
static void draw_raised_box(int x, int y, int w, int h) {
    hline(x, y, w, WIN_BORDER_LIGHT);
    vline(x, y, h, WIN_BORDER_LIGHT);
    hline(x, y + h - 1, w, WIN_BORDER_DARK);
    vline(x + w - 1, y, h, WIN_BORDER_DARK);
    hline(x, y + 1, w - 2, WIN_BORDER_LIGHT);
    vline(x + 1, y, h - 2, WIN_BORDER_LIGHT);
    hline(x + 1, y + h - 2, w - 2, COLOR_DARKGRAY);
    vline(x + w - 2, y + 1, h - 2, COLOR_DARKGRAY);
}

static void draw_sunken_box(int x, int y, int w, int h) {
    hline(x, y, w, COLOR_DARKGRAY);
    vline(x, y, h, COLOR_DARKGRAY);
    hline(x, y + h - 1, w, WIN_BORDER_LIGHT);
    vline(x + w - 1, y, h, WIN_BORDER_LIGHT);
}

/* ----- Font 8x8 ----- */
static const uint8_t font8x8[95][8] = {
    [0]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [1]={0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    [2]={0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},
    [3]={0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    [4]={0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    [5]={0x00,0x66,0xAC,0xD8,0x36,0x6A,0xD6,0x00},
    [6]={0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    [7]={0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    [8]={0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    [9]={0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    [10]={0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    [11]={0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    [12]={0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    [13]={0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    [14]={0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    [15]={0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    [16]={0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00},
    [17]={0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00},
    [18]={0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},
    [19]={0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    [20]={0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},
    [21]={0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    [22]={0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    [23]={0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    [24]={0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    [25]={0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    [26]={0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    [27]={0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    [28]={0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    [29]={0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},
    [30]={0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    [31]={0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    [32]={0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},
    [33]={0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    [34]={0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
    [35]={0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    [36]={0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
    [37]={0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    [38]={0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
    [39]={0x3C,0x66,0xC0,0xDE,0xC6,0x66,0x3A,0x00},
    [40]={0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    [41]={0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    [42]={0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
    [43]={0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    [44]={0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
    [45]={0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    [46]={0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    [47]={0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    [48]={0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    [49]={0x7C,0xC6,0xC6,0xC6,0xC6,0xD6,0x7C,0x06},
    [50]={0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
    [51]={0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00},
    [52]={0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00},
    [53]={0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    [54]={0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    [55]={0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    [56]={0xC6,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00},
    [57]={0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
    [58]={0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
    [59]={0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    [60]={0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    [61]={0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    [62]={0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    [63]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    [64]={0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    [65]={0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    [66]={0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},
    [67]={0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    [68]={0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},
    [69]={0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    [70]={0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00},
    [71]={0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78},
    [72]={0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
    [73]={0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    [74]={0x0C,0x00,0x0C,0x0C,0x0C,0xCC,0xCC,0x78},
    [75]={0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    [76]={0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    [77]={0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
    [78]={0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},
    [79]={0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    [80]={0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    [81]={0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    [82]={0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},
    [83]={0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},
    [84]={0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00},
    [85]={0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x76,0x00},
    [86]={0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
    [87]={0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
    [88]={0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    [89]={0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC},
    [90]={0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},
    [91]={0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    [92]={0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    [93]={0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    [94]={0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}
};

/* ----- Character Drawing (declared before use) ----- */
static void draw_char(int x, int y, char c, uint8_t fg, uint8_t bg) {
    if (c < 32 || c > 126) return;
    const uint8_t *g = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col, py = y + row;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

/* ----- Button ----- */
static void draw_button(int x, int y, int w, int h, const char *text) {
    fill_rect(x, y, w, h, WIN_BUTTON_FACE);
    draw_raised_box(x, y, w, h);
    int len = 0; while (text[len]) len++;
    int tx = x + (w - len * 8) / 2;
    int ty = y + (h - 8) / 2;
    for (int i = 0; text[i]; i++)
        draw_char(tx + i * 8, ty, text[i], COLOR_BLACK, WIN_BUTTON_FACE);
}

/* ----- Title Bar ----- */
static void draw_titlebar(int x, int y, int w, const char *title, uint8_t color) {
    fill_rect(x, y, w, 18, color);
    int len = 0; while (title[len]) len++;
    int tx = x + 4, ty = y + 2;
    for (int i = 0; title[i]; i++)
        draw_char(tx + i * 8, ty, title[i], COLOR_WHITE, color);
    int bx = x + w - 18;
    fill_rect(bx, y, 16, 16, WIN_BUTTON_FACE);
    draw_raised_box(bx, y, 16, 16);
    draw_char(bx + 4, y + 2, 'X', COLOR_BLACK, WIN_BUTTON_FACE);
}

/* ----- Desktop Icon ----- */
static void draw_desktop_icon(int x, int y, const char *label) {
    fill_rect(x, y, 32, 32, WIN_DESKTOP);
    fill_rect(x + 4, y + 4, 24, 28, COLOR_WHITE);
    draw_raised_box(x + 4, y + 4, 24, 28);
    fill_rect(x + 6, y + 6, 20, 8, COLOR_BLUE);
    hline(x + 8, y + 18, 16, COLOR_DARKGRAY);
    hline(x + 8, y + 21, 12, COLOR_DARKGRAY);
    hline(x + 8, y + 24, 14, COLOR_DARKGRAY);
    int len = 0; while (label[len]) len++;
    int tx = x + (32 - len * 8) / 2;
    for (int i = 0; label[i]; i++)
        draw_char(tx + i * 8, y + 34, label[i], COLOR_WHITE, WIN_DESKTOP);
}

/* ----- Desktop (declared before use) ----- */
static void draw_desktop(void) {
    fill_rect(0, 0, fb_width, fb_height - taskbar_h, WIN_DESKTOP);
    draw_desktop_icon(20, 20, "My OS");
    draw_desktop_icon(20, 80, "Readme");
    draw_desktop_icon(20, 140, "System");
    fill_rect(0, fb_height - taskbar_h, fb_width, taskbar_h, WIN_TASKBAR);
    hline(0, fb_height - taskbar_h, fb_width, COLOR_WHITE);
    draw_button(2, fb_height - taskbar_h + 2, 56, 20, "Start");
    int cx = fb_width - 60;
    draw_sunken_box(cx, fb_height - taskbar_h + 2, 56, 20);
    draw_char(cx + 12, fb_height - taskbar_h + 4, '1', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(cx + 20, fb_height - taskbar_h + 4, '2', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(cx + 28, fb_height - taskbar_h + 4, ':', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(cx + 36, fb_height - taskbar_h + 4, '0', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(cx + 44, fb_height - taskbar_h + 4, '0', COLOR_BLACK, WIN_BUTTON_FACE);
}

/* ----- Window ----- */
static void draw_window(void) {
    fill_rect(win_x, win_y, win_w, win_h, WIN_WINDOW_BG);
    draw_titlebar(win_x, win_y, win_w, "Notepad - Untitled", WIN_TITLEBAR_ACTIVE);
    draw_raised_box(win_x, win_y + 18, win_w, win_h - 18);
    int ex = win_x + 4, ey = win_y + 22;
    int ew = win_w - 8, eh = win_h - 44;
    draw_sunken_box(ex, ey, ew, eh);
    fill_rect(win_x + 2, win_y + win_h - 18, win_w - 4, 14, WIN_BUTTON_FACE);
    hline(win_x + 2, win_y + win_h - 18, win_w - 4, COLOR_WHITE);
    hline(win_x + 2, win_y + win_h - 19, win_w - 4, COLOR_DARKGRAY);
    draw_char(win_x + 6, win_y + win_h - 16, 'L', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(win_x + 14, win_y + win_h - 16, 'n', COLOR_BLACK, WIN_BUTTON_FACE);
    draw_char(win_x + 22, win_y + win_h - 16, ':', COLOR_BLACK, WIN_BUTTON_FACE);
}

/* ----- Redraw Text ----- */
static void redraw_text(void) {
    int ex = win_x + 6, ey = win_y + 24;
    int max_cols = (win_w - 14) / 8;
    int max_rows = (win_h - 48) / 8;
    fill_rect(ex, ey, max_cols * 8, max_rows * 8, COLOR_WHITE);
    int line_start = 0, dl = 0;
    while (dl < scroll_line && line_start < text_len) {
        while (line_start < text_len && text_buffer[line_start] != '\n') line_start++;
        if (line_start < text_len) line_start++;
        dl++;
    }
    for (int row = 0; row < max_rows && line_start < text_len; row++) {
        int col = 0, cur = line_start;
        while (cur < text_len && text_buffer[cur] != '\n' && col < max_cols) {
            draw_char(ex + col*8, ey + row*8, text_buffer[cur], COLOR_BLACK, COLOR_WHITE);
            cur++; col++;
        }
        line_start = cur + 1;
    }
}

/* ----- Mouse Cursor ----- */
static void cursor_save(void) {
    int idx = 0;
    for (int dy = -CURSOR_SIZE; dy <= CURSOR_SIZE; dy++)
        for (int dx = -CURSOR_SIZE; dx <= CURSOR_SIZE; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            cursor_bg[idx++] = (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                               ? fb[py * fb_pitch + px] : 0;
        }
}
static void cursor_restore(void) {
    int idx = 0;
    for (int dy = -CURSOR_SIZE; dy <= CURSOR_SIZE; dy++)
        for (int dx = -CURSOR_SIZE; dx <= CURSOR_SIZE; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = cursor_bg[idx];
            idx++;
        }
}
static void cursor_draw(void) {
    for (int i = 0; i <= 8; i++) {
        putpixel(mouse_x + i, mouse_y + i, COLOR_BLACK);
        putpixel(mouse_x + i, mouse_y + i + 1, COLOR_BLACK);
    }
    for (int i = 0; i <= 4; i++) {
        putpixel(mouse_x, mouse_y + i, COLOR_BLACK);
        putpixel(mouse_x + 1, mouse_y + i, COLOR_BLACK);
    }
    putpixel(mouse_x + 2, mouse_y, COLOR_BLACK);
    putpixel(mouse_x + 3, mouse_y + 1, COLOR_BLACK);
    for (int i = 1; i <= 7; i++) {
        putpixel(mouse_x + i + 1, mouse_y + i, COLOR_WHITE);
        putpixel(mouse_x + i + 1, mouse_y + i + 1, COLOR_WHITE);
    }
}
static void move_cursor(int nx, int ny) {
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= fb_width) nx = fb_width - 1;
    if (ny >= fb_height) ny = fb_height - 1;
    if (nx == mouse_x && ny == mouse_y) return;
    cursor_restore();
    mouse_x = nx; mouse_y = ny;
    cursor_save();
    cursor_draw();
}

/* ----- Error Popup ----- */
static void show_error(void) {
    const char *msgs[] = {"Error: System unstable","WARNING: Memory corruption",
                          "Fatal: Kernel exception","Critical: Stack overflow"};
    int idx = rand() % 4;
    for (int i = 0; i < 39 && msgs[idx][i]; i++) error_text[i] = msgs[idx][i];
    int len = 0; while (error_text[len]) len++;
    error_w = len * 8 + 24; error_h = 52;
    error_x = (fb_width - error_w) / 2 + rand() % 40 - 20;
    error_y = (fb_height - error_h) / 2 + rand() % 40 - 20;
    int bi = 0;
    for (int dy = 0; dy < error_h; dy++)
        for (int dx = 0; dx < error_w; dx++) {
            int px = error_x + dx, py = error_y + dy;
            error_bg[bi++] = (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                             ? fb[py * fb_pitch + px] : 0;
        }
    fill_rect(error_x, error_y, error_w, error_h, WIN_BUTTON_FACE);
    draw_raised_box(error_x, error_y, error_w, error_h);
    draw_titlebar(error_x, error_y, error_w, "Error", COLOR_RED);
    for (int i = 0; i < len; i++)
        draw_char(error_x + 12 + i * 8, error_y + 26, error_text[i], COLOR_BLACK, WIN_BUTTON_FACE);
    int bx = error_x + error_w / 2 - 20;
    draw_button(bx, error_y + 38, 40, 12, "OK");
    error_active = 1;
    error_timer = 300000;
}
static void hide_error(void) {
    int bi = 0;
    for (int dy = 0; dy < error_h; dy++)
        for (int dx = 0; dx < error_w; dx++) {
            int px = error_x + dx, py = error_y + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = error_bg[bi];
            bi++;
        }
    error_active = 0;
    draw_desktop();
    draw_window();
    redraw_text();
}

/* ----- PS/2 ----- */
static void ps2_wait_write(void) { while (inb(0x64) & 2) io_wait(); }
static void ps2_wait_data(void) { while (!(inb(0x64) & 1)) io_wait(); }
static uint8_t ps2_read(void) { ps2_wait_data(); return inb(0x60); }
static void mouse_write(uint8_t d) {
    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, d);
    ps2_read();
}
static void keyboard_write(uint8_t d) { ps2_wait_write(); outb(0x60, d); }

static void init_input(void) {
    ps2_wait_write(); outb(0x64, 0xA8);
    mouse_write(0xF6);
    mouse_write(0xF4);
    keyboard_write(0xF0);
    keyboard_write(0x01);
    mouse_cycle = 0;
}

static char scancode_to_ascii(uint8_t sc) {
    switch (sc) {
        case 0x0E: return '\b'; case 0x1C: return '\n'; case 0x39: return ' ';
        case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
        case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
        case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
        case 0x0B: return '0'; case 0x0C: return '-'; case 0x0D: return '=';
        case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
        case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
        case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
        case 0x19: return 'p'; case 0x1A: return '['; case 0x1B: return ']';
        case 0x1E: return 'a'; case 0x1F: return 's'; case 0x20: return 'd';
        case 0x21: return 'f'; case 0x22: return 'g'; case 0x23: return 'h';
        case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
        case 0x27: return ';'; case 0x28: return '\''; case 0x29: return '`';
        case 0x2B: return '\\'; case 0x2C: return 'z'; case 0x2D: return 'x';
        case 0x2E: return 'c'; case 0x2F: return 'v'; case 0x30: return 'b';
        case 0x31: return 'n'; case 0x32: return 'm'; case 0x33: return ',';
        case 0x34: return '.'; case 0x35: return '/'; case 0x37: return '*';
        default: return 0;
    }
}

/* ----- VGA mode 13h ----- */
static void set_vga_mode13(void) {
    outb(0x3C2, 0x63);
    outb(0x3D4, 0x00); outb(0x3D5, 0x5F); outb(0x3D4, 0x01); outb(0x3D5, 0x4F);
    outb(0x3D4, 0x02); outb(0x3D5, 0x50); outb(0x3D4, 0x03); outb(0x3D5, 0x82);
    outb(0x3D4, 0x04); outb(0x3D5, 0x54); outb(0x3D4, 0x05); outb(0x3D5, 0x80);
    outb(0x3D4, 0x06); outb(0x3D5, 0xBF); outb(0x3D4, 0x07); outb(0x3D5, 0x00);
    outb(0x3D4, 0x08); outb(0x3D5, 0x00); outb(0x3D4, 0x09); outb(0x3D5, 0x41);
    outb(0x3D4, 0x0A); outb(0x3D5, 0x00); outb(0x3D4, 0x0B); outb(0x3D5, 0x00);
    outb(0x3D4, 0x0C); outb(0x3D5, 0x00); outb(0x3D4, 0x0D); outb(0x3D5, 0x00);
    outb(0x3D4, 0x0E); outb(0x3D5, 0x00); outb(0x3D4, 0x0F); outb(0x3D5, 0x00);
    outb(0x3D4, 0x10); outb(0x3D5, 0x9C); outb(0x3D4, 0x11); outb(0x3D5, 0x8E);
    outb(0x3D4, 0x12); outb(0x3D5, 0x8F); outb(0x3D4, 0x13); outb(0x3D5, 0x28);
    outb(0x3D4, 0x14); outb(0x3D5, 0x40); outb(0x3D4, 0x15); outb(0x3D5, 0x96);
    outb(0x3D4, 0x16); outb(0x3D5, 0xB9); outb(0x3D4, 0x17); outb(0x3D5, 0xA3);
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F); outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E); outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00); outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00); outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40); outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F); outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    inb(0x3DA);
    outb(0x3C0, 0x30); outb(0x3C0, 0x41);
    outb(0x3C0, 0x33); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20);
    fb = (uint8_t*)0xA0000;
    fb_pitch = 320; fb_width = 320; fb_height = 200;
    win_x = 60; win_y = 30; win_w = 200; win_h = 140;
    taskbar_h = 20;
}

static void set_palette(void) {
    outb(0x3C8, 0);
    static const uint8_t pal[16][3] = {
        {0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
        {0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
        {0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
        {0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF}
    };
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, pal[i][0]/4);
        outb(0x3C9, pal[i][1]/4);
        outb(0x3C9, pal[i][2]/4);
    }
    for (int i = 16; i < 256; i++) { outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9, 0); }
}

/* ----- Main ----- */
void kernel_main(uint32_t magic, uint32_t addr) {
    if (magic != 0x2BADB002) return;
    uint32_t *mbi = (uint32_t *)addr;
    uint32_t flags = *mbi;
    fb = 0;

    if ((flags & (1 << 11)) && *(mbi+22) != 0) {
        fb = (uint8_t *)(uint32_t)*(mbi+22);
        fb_pitch = *(mbi+24);
        fb_width = *(mbi+25);
        fb_height = *(mbi+26);
        if (fb_width == 0 || fb_height == 0) fb = 0;
    }
    if (!fb) set_vga_mode13();

    set_palette();
    draw_desktop();
    draw_window();
    redraw_text();

    mouse_x = fb_width/2; mouse_y = fb_height/2;
    cursor_save(); cursor_draw();
    init_input();

    int tick = 0;
    while (1) {
        while (inb(0x64) & 1) {
            uint8_t st = inb(0x64), d = inb(0x60);
            if (st & 0x20) {
                if (mouse_cycle == 0) {
                    if (d & 0x08) { mouse_bytes[0] = d; mouse_cycle++; }
                } else {
                    mouse_bytes[mouse_cycle++] = d;
                    if (mouse_cycle == 3) {
                        mouse_cycle = 0;
                        int dx = mouse_bytes[1], dy = mouse_bytes[2];
                        if (mouse_bytes[0] & 0x10) dx |= ~0xFF;
                        if (mouse_bytes[0] & 0x20) dy |= ~0xFF;
                        dy = -dy;
                        move_cursor(mouse_x + dx, mouse_y + dy);
                    }
                }
            } else {
                char c = scancode_to_ascii(d);
                if (c == '\b') { if (text_len > 0) { text_len--; redraw_text(); } }
                else if (c == '\n') {
                    if (text_len < 3199) {
                        text_buffer[text_len++] = '\n';
                        int lines = 0;
                        for (int i = 0; i < text_len; i++) if (text_buffer[i] == '\n') lines++;
                        int maxr = (win_h - 48) / 8;
                        if (lines - scroll_line >= maxr) scroll_line = lines - maxr + 1;
                        redraw_text();
                    }
                } else if (c >= 32 && c <= 126) {
                    if (text_len < 3199) { text_buffer[text_len++] = c; redraw_text(); }
                }
            }
        }
        tick++;
        if (!error_active && (tick % (3000000 + rand() % 4000000)) == 0) show_error();
        if (error_active) { error_timer--; if (error_timer <= 0) hide_error(); }
        for (volatile int d = 0; d < 1000; d++);
    }
}
