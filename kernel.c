/* kernel.c - TinyFoxyDOS Windows 95-like Graphical OS */
#include <stdint.h>

/* ============ I/O PORTS ============ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ============ COLORS (VGA Palette) ============ */
#define COLOR_BLACK       0
#define COLOR_BLUE        1
#define COLOR_GREEN       2
#define COLOR_CYAN        3
#define COLOR_RED         4
#define COLOR_MAGENTA     5
#define COLOR_BROWN       6
#define COLOR_LIGHTGRAY   7
#define COLOR_DARKGRAY    8
#define COLOR_LIGHTBLUE   9
#define COLOR_LIGHTGREEN  10
#define COLOR_LIGHTCYAN   11
#define COLOR_LIGHTRED    12
#define COLOR_LIGHTMAGENTA 13
#define COLOR_YELLOW      14
#define COLOR_WHITE       15

/* Windows 95 theme */
#define WIN_DESKTOP       0x02  /* Dark teal */
#define WIN_TASKBAR       0x07  /* Light gray */
#define WIN_TITLE_ACTIVE  0x09  /* Blue */
#define WIN_TITLE_INACTIVE 0x08 /* Dark gray */
#define WIN_BUTTON_FACE   0x07  /* Light gray */
#define WIN_BUTTON_SHADOW 0x08  /* Dark gray */
#define WIN_WINDOW_BG     0x0F  /* White */
#define WIN_BORDER_LIGHT  0x0F  /* White */
#define WIN_BORDER_DARK   0x08  /* Dark gray */
#define WIN_TEXT          0x00  /* Black */
#define WIN_TEXT_WHITE    0x0F  /* White */
#define WIN_TASKBAR_TEXT  0x00  /* Black */
#define WIN_HIGHLIGHT     0x09  /* Blue highlight */
#define WIN_ERROR_RED     0x04  /* Red for error title */

static void set_teal_palette(void) {
    outb(0x3C8, 0);
    static const uint8_t pal[16][3] = {
        {0x00,0x00,0x00}, {0x00,0x00,0xAA}, {0x00,0x80,0x80}, {0x00,0xAA,0xAA},
        {0xAA,0x00,0x00}, {0xAA,0x00,0xAA}, {0xAA,0x55,0x00}, {0xC0,0xC0,0xC0},
        {0x80,0x80,0x80}, {0x55,0x55,0xFF}, {0x55,0xFF,0x55}, {0x55,0xFF,0xFF},
        {0xFF,0x55,0x55}, {0xFF,0x55,0xFF}, {0xFF,0xFF,0x55}, {0xFF,0xFF,0xFF}
    };
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, pal[i][0]/4); outb(0x3C9, pal[i][1]/4); outb(0x3C9, pal[i][2]/4);
    }
    for (int i = 16; i < 256; i++) { outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9, 0); }
}

/* ============ FRAMEBUFFER ============ */
static uint8_t *fb;
static int fb_pitch, fb_width, fb_height;

/* ============ MOUSE STATE ============ */
static int mouse_x, mouse_y, mouse_cycle;
static uint8_t mouse_bytes[3];
static int mouse_buttons = 0; /* 1=left, 2=right, 3=both */
static int mouse_clicked = 0;

/* ============ KEYBOARD STATE ============ */
static int kb_shift = 0;
static int kb_tab_select = 0; /* Tab to switch between clickable items */

/* ============ WINDOWS ============ */
#define MAX_WINDOWS 10
static int window_count = 0;
static int window_x[MAX_WINDOWS], window_y[MAX_WINDOWS];
static int window_w[MAX_WINDOWS], window_h[MAX_WINDOWS];
static const char *window_title[MAX_WINDOWS];
static int window_visible[MAX_WINDOWS];
static int window_active = -1;
static int dragging = -1;
static int drag_off_x, drag_off_y;

/* Notepad text */
static char notepad_text[3200];
static int notepad_len = 0;
static int notepad_scroll = 0;

/* Start menu */
static int start_menu_open = 0;
static int start_menu_x = 2, start_menu_y;
static const char *start_items[] = {"Programs", "Documents", "Settings", "Run...", "Shut Down..."};
static int start_item_count = 5;

/* Desktop icons */
static const char *icon_labels[] = {"My PC", "Notepad", "Calc"};
static int icon_x[] = {20, 20, 20};
static int icon_y[] = {20, 100, 180};
static int icon_count = 3;

/* Error popup */
static int error_active = 0, error_timer = 0;
static int error_x, error_y, error_w, error_h;
static uint8_t error_bg[300*80];

/* Taskbar */
static int taskbar_h = 24;
static int clock_visible = 1;

/* ============ RAND ============ */
static uint32_t seed = 0xDEADBEEF;
static uint32_t rand(void) {
    seed = (1103515245 * seed + 12345) & 0x7FFFFFFF;
    return seed;
}

/* ============ PIXEL DRAWING ============ */
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

/* ============ 3D BORDERS ============ */
static void draw_raised_box(int x, int y, int w, int h) {
    hline(x, y, w, WIN_BORDER_LIGHT);
    vline(x, y, h, WIN_BORDER_LIGHT);
    hline(x, y + h - 1, w, WIN_BORDER_DARK);
    vline(x + w - 1, y, h, WIN_BORDER_DARK);
    if (w > 2 && h > 2) {
        hline(x + 1, y + 1, w - 2, WIN_BORDER_LIGHT);
        vline(x + 1, y + 1, h - 2, WIN_BORDER_LIGHT);
        hline(x + 1, y + h - 2, w - 2, WIN_BORDER_DARK);
        vline(x + w - 2, y + 1, h - 2, WIN_BORDER_DARK);
    }
}

static void draw_sunken_box(int x, int y, int w, int h) {
    hline(x, y, w, WIN_BORDER_DARK);
    vline(x, y, h, WIN_BORDER_DARK);
    hline(x, y + h - 1, w, WIN_BORDER_LIGHT);
    vline(x + w - 1, y, h, WIN_BORDER_LIGHT);
}

/* ============ 8x8 FONT ============ */
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

static void draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) { draw_char(x, y, *s++, fg, bg); x += 8; }
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ TITLE BAR ============ */
static void draw_titlebar(int x, int y, int w, const char *title, uint8_t color) {
    fill_rect(x, y, w, 18, color);
    draw_string(x + 4, y + 2, title, WIN_TEXT_WHITE, color);
    /* Close button */
    int bx = x + w - 18;
    fill_rect(bx, y, 16, 16, WIN_BUTTON_FACE);
    draw_raised_box(bx, y, 16, 16);
    draw_char(bx + 4, y + 2, 'X', WIN_TEXT, WIN_BUTTON_FACE);
    /* Minimize */
    bx -= 18;
    fill_rect(bx, y, 16, 16, WIN_BUTTON_FACE);
    draw_raised_box(bx, y, 16, 16);
    draw_char(bx + 5, y + 8, '_', WIN_TEXT, WIN_BUTTON_FACE);
    /* Maximize */
    bx -= 18;
    fill_rect(bx, y, 16, 16, WIN_BUTTON_FACE);
    draw_raised_box(bx, y, 16, 16);
    draw_char(bx + 4, y + 2, 127, WIN_TEXT, WIN_BUTTON_FACE);
}

/* ============ MOUSE CURSOR (Triangle Arrow) ============ */
static uint8_t cursor_bg[20*20];

static void cursor_save(void) {
    int idx = 0;
    for (int dy = 0; dy < 20; dy++)
        for (int dx = 0; dx < 20; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            cursor_bg[idx++] = (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                               ? fb[py * fb_pitch + px] : 0;
        }
}

static void cursor_restore(void) {
    int idx = 0;
    for (int dy = 0; dy < 20; dy++)
        for (int dx = 0; dx < 20; dx++) {
            int px = mouse_x + dx, py = mouse_y + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = cursor_bg[idx];
            idx++;
        }
}

static void cursor_draw(void) {
    /* White triangle arrow */
    static const int arrow[20][20] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };
    for (int dy = 0; dy < 20; dy++)
        for (int dx = 0; dx < 20; dx++)
            if (arrow[dy][dx])
                putpixel(mouse_x + dx, mouse_y + dy, COLOR_WHITE);
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

/* ============ CHECK MOUSE CLICK ============ */
static int in_rect(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

/* ============ ERROR POPUP ============ */
static void show_error(void) {
    const char *msgs[] = {"No filesystem found!", "Cannot read drive C:",
                          "Access denied!", "File not found!"};
    int idx = rand() % 4;
    error_w = str_len(msgs[idx]) * 8 + 40;
    error_h = 60;
    error_x = (fb_width - error_w) / 2;
    error_y = (fb_height - error_h) / 2;
    int bi = 0;
    for (int dy = 0; dy < error_h; dy++)
        for (int dx = 0; dx < error_w; dx++) {
            int px = error_x + dx, py = error_y + dy;
            error_bg[bi++] = (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                             ? fb[py * fb_pitch + px] : 0;
        }
    fill_rect(error_x, error_y, error_w, error_h, WIN_BUTTON_FACE);
    draw_raised_box(error_x, error_y, error_w, error_h);
    draw_titlebar(error_x, error_y, error_w, "Error", WIN_ERROR_RED);
    draw_string(error_x + 20, error_y + 26, msgs[idx], WIN_TEXT, WIN_BUTTON_FACE);
    int bx = error_x + error_w / 2 - 20;
    fill_rect(bx, error_y + 42, 40, 14, WIN_BUTTON_FACE);
    draw_raised_box(bx, error_y + 42, 40, 14);
    draw_string(bx + 8, error_y + 44, "OK", WIN_TEXT, WIN_BUTTON_FACE);
    error_active = 1; error_timer = 300000;
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
}

/* ============ DESKTOP ============ */
static void draw_desktop(void) {
    fill_rect(0, 0, fb_width, fb_height - taskbar_h, WIN_DESKTOP);
}

static void draw_desktop_icon(int x, int y, const char *label) {
    /* Simple icon box */
    fill_rect(x, y, 32, 32, WIN_DESKTOP);
    fill_rect(x + 2, y + 2, 28, 28, COLOR_WHITE);
    draw_raised_box(x + 2, y + 2, 28, 28);
    /* Blue header on icon */
    fill_rect(x + 4, y + 4, 24, 8, COLOR_BLUE);
    /* Lines */
    hline(x + 6, y + 16, 20, COLOR_DARKGRAY);
    hline(x + 6, y + 19, 14, COLOR_DARKGRAY);
    hline(x + 6, y + 22, 16, COLOR_DARKGRAY);
    /* Label */
    int len = str_len(label);
    int tx = x + (32 - len * 8) / 2;
    for (int i = 0; i < len; i++)
        draw_char(tx + i * 8, y + 34, label[i], COLOR_WHITE, WIN_DESKTOP);
}

static void draw_taskbar(void) {
    fill_rect(0, fb_height - taskbar_h, fb_width, taskbar_h, WIN_TASKBAR);
    hline(0, fb_height - taskbar_h, fb_width, COLOR_WHITE);
    /* Start button */
    int sx = 2, sy = fb_height - taskbar_h + 2, sw = 56, sh = 20;
    fill_rect(sx, sy, sw, sh, WIN_BUTTON_FACE);
    draw_raised_box(sx, sy, sw, sh);
    draw_string(sx + 8, sy + 2, "Start", WIN_TEXT, WIN_BUTTON_FACE);
    /* Clock */
    int cx = fb_width - 56;
    draw_sunken_box(cx, fb_height - taskbar_h + 2, 52, 20);
    draw_string(cx + 8, fb_height - taskbar_h + 4, "12:00", WIN_TEXT, WIN_BUTTON_FACE);
}

/* ============ START MENU ============ */
static void draw_start_menu(void) {
    int sx = start_menu_x, sy = fb_height - taskbar_h - start_item_count * 24 - 4;
    start_menu_y = sy;
    int sw = 160, sh = start_item_count * 24 + 4;
    fill_rect(sx, sy, sw, sh, WIN_BUTTON_FACE);
    draw_raised_box(sx, sy, sw, sh);
    for (int i = 0; i < start_item_count; i++) {
        int iy = sy + 2 + i * 24;
        fill_rect(sx + 2, iy, sw - 4, 22, WIN_BUTTON_FACE);
        draw_string(sx + 8, iy + 3, start_items[i], WIN_TEXT, WIN_BUTTON_FACE);
        if (i == kb_tab_select && !start_menu_open) {}
    }
}

/* ============ WINDOW MANAGEMENT ============ */
static int create_window(int x, int y, int w, int h, const char *title) {
    if (window_count >= MAX_WINDOWS) return -1;
    int i = window_count++;
    window_x[i] = x; window_y[i] = y;
    window_w[i] = w; window_h[i] = h;
    window_title[i] = title;
    window_visible[i] = 1;
    window_active = i;
    return i;
}

static void draw_window_frame(int idx) {
    if (!window_visible[idx]) return;
    int x = window_x[idx], y = window_y[idx];
    int w = window_w[idx], h = window_h[idx];
    uint8_t title_color = (idx == window_active) ? WIN_TITLE_ACTIVE : WIN_TITLE_INACTIVE;
    fill_rect(x, y, w, h, WIN_WINDOW_BG);
    draw_titlebar(x, y, w, window_title[idx], title_color);
    draw_raised_box(x, y + 18, w, h - 18);
}

static void draw_notepad_content(int idx) {
    int x = window_x[idx] + 4, y = window_y[idx] + 22;
    int max_cols = (window_w[idx] - 8) / 8;
    int max_rows = (window_h[idx] - 40) / 8;
    fill_rect(x, y, max_cols * 8, max_rows * 8, COLOR_WHITE);
    int line_start = 0, dl = 0;
    while (dl < notepad_scroll && line_start < notepad_len) {
        while (line_start < notepad_len && notepad_text[line_start] != '\n') line_start++;
        if (line_start < notepad_len) line_start++;
        dl++;
    }
    for (int row = 0; row < max_rows && line_start < notepad_len; row++) {
        int col = 0, cur = line_start;
        while (cur < notepad_len && notepad_text[cur] != '\n' && col < max_cols) {
            draw_char(x + col * 8, y + row * 8, notepad_text[cur], WIN_TEXT, COLOR_WHITE);
            cur++; col++;
        }
        line_start = cur + 1;
    }
}

static void close_window(int idx) {
    window_visible[idx] = 0;
    if (window_active == idx) {
        window_active = -1;
        for (int i = 0; i < window_count; i++)
            if (window_visible[i]) window_active = i;
    }
}

/* ============ INPUT DEVICES ============ */
static void ps2_wait_write(void) { while (inb(0x64) & 2) io_wait(); }
static void ps2_wait_data(void) { while (!(inb(0x64) & 1)) io_wait(); }
static uint8_t ps2_read(void) { ps2_wait_data(); return inb(0x60); }

static void mouse_write(uint8_t d) {
    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, d);
    ps2_read();
}
static void keyboard_write(uint8_t d) { ps2_wait_write(); outb(0x60, d); }

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

/* ============ USB MOUSE SUPPORT via Legacy Emulation ============ */
static void enable_usb_legacy(void) {
    /* Some BIOSes translate USB mouse to PS/2 when legacy support is enabled.
       Try to enable the auxiliary PS/2 port and send mouse init sequence. */
    ps2_wait_write(); outb(0x64, 0xA8); /* Enable auxiliary device */
    /* Send reset */
    mouse_write(0xFF);
    ps2_read(); /* ACK */
    ps2_read(); /* AA (self-test OK) */
    ps2_read(); /* Device ID (0x00 for standard mouse) */
    /* Enable data reporting */
    mouse_write(0xF4);
    ps2_read(); /* ACK */
}

/* ============ KEYBOARD NAVIGATION ============ */
static int kb_nav_mode = 0; /* 0=mouse, 1=keyboard nav */
static int kb_sel_icon = 0;

static void kb_handle_arrow(char key) {
    static int kb_arrow_timer = 0;
    if (key == 'U') { /* Up */
        if (start_menu_open) {
            kb_tab_select = (kb_tab_select - 1 + start_item_count) % start_item_count;
        } else {
            kb_sel_icon = (kb_sel_icon - 1 + icon_count) % icon_count;
            mouse_x = icon_x[kb_sel_icon] + 16;
            mouse_y = icon_y[kb_sel_icon] + 16;
        }
    } else if (key == 'D') { /* Down */
        if (start_menu_open) {
            kb_tab_select = (kb_tab_select + 1) % start_item_count;
        } else {
            kb_sel_icon = (kb_sel_icon + 1) % icon_count;
            mouse_x = icon_x[kb_sel_icon] + 16;
            mouse_y = icon_y[kb_sel_icon] + 16;
        }
    }
    kb_nav_mode = 1;
}

static void kb_handle_enter(void) {
    if (start_menu_open) {
        if (kb_tab_select == 4) { /* Shut Down */
            start_menu_open = 0;
            show_error();
        } else {
            show_error();
        }
        start_menu_open = 0;
    } else {
        /* Open icon based on selection */
        if (kb_sel_icon == 0) show_error(); /* My PC */
        else if (kb_sel_icon == 1) create_window(80, 40, 320, 220, "Notepad - Untitled");
        else if (kb_sel_icon == 2) show_error(); /* Calc */
    }
}

/* ============ REDRAW EVERYTHING ============ */
static void redraw_all(void) {
    draw_desktop();
    for (int i = 0; i < icon_count; i++)
        draw_desktop_icon(icon_x[i], icon_y[i], icon_labels[i]);
    draw_taskbar();
    if (start_menu_open) draw_start_menu();
    for (int i = 0; i < window_count; i++) {
        if (window_visible[i]) {
            draw_window_frame(i);
            if (str_cmp(window_title[i], "Notepad - Untitled") == 0)
                draw_notepad_content(i);
        }
    }
    if (error_active) {
        /* Redraw error on top */
        show_error();
    }
}

/* ============ MAIN ============ */
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

    if (!fb) {
        /* Fallback VGA 320x200 */
        outb(0x3C2, 0x63); outb(0x3D4, 0x00); outb(0x3D5, 0x5F);
        outb(0x3D4, 0x01); outb(0x3D5, 0x4F); outb(0x3D4, 0x02); outb(0x3D5, 0x50);
        outb(0x3D4, 0x03); outb(0x3D5, 0x82); outb(0x3D4, 0x04); outb(0x3D5, 0x54);
        outb(0x3D4, 0x05); outb(0x3D5, 0x80); outb(0x3D4, 0x06); outb(0x3D5, 0xBF);
        outb(0x3D4, 0x07); outb(0x3D5, 0x00); outb(0x3D4, 0x08); outb(0x3D5, 0x00);
        outb(0x3D4, 0x09); outb(0x3D5, 0x41); outb(0x3D4, 0x0A); outb(0x3D5, 0x00);
        outb(0x3D4, 0x0B); outb(0x3D5, 0x00); outb(0x3D4, 0x0C); outb(0x3D5, 0x00);
        outb(0x3D4, 0x0D); outb(0x3D5, 0x00); outb(0x3D4, 0x0E); outb(0x3D5, 0x00);
        outb(0x3D4, 0x0F); outb(0x3D5, 0x00); outb(0x3D4, 0x10); outb(0x3D5, 0x9C);
        outb(0x3D4, 0x11); outb(0x3D5, 0x8E); outb(0x3D4, 0x12); outb(0x3D5, 0x8F);
        outb(0x3D4, 0x13); outb(0x3D5, 0x28); outb(0x3D4, 0x14); outb(0x3D5, 0x40);
        outb(0x3D4, 0x15); outb(0x3D5, 0x96); outb(0x3D4, 0x16); outb(0x3D5, 0xB9);
        outb(0x3D4, 0x17); outb(0x3D5, 0xA3); outb(0x3C4, 0x00); outb(0x3C5, 0x03);
        outb(0x3C4, 0x01); outb(0x3C5, 0x01); outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
        outb(0x3C4, 0x03); outb(0x3C5, 0x00); outb(0x3C4, 0x04); outb(0x3C5, 0x0E);
        outb(0x3CE, 0x00); outb(0x3CF, 0x00); outb(0x3CE, 0x01); outb(0x3CF, 0x00);
        outb(0x3CE, 0x02); outb(0x3CF, 0x00); outb(0x3CE, 0x03); outb(0x3CF, 0x00);
        outb(0x3CE, 0x04); outb(0x3CF, 0x00); outb(0x3CE, 0x05); outb(0x3CF, 0x40);
        outb(0x3CE, 0x06); outb(0x3CF, 0x05); outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF); inb(0x3DA);
        outb(0x3C0, 0x30); outb(0x3C0, 0x41); outb(0x3C0, 0x33); outb(0x3C0, 0x00);
        outb(0x3C0, 0x20);
        fb = (uint8_t*)0xA0000;
        fb_pitch = 320; fb_width = 320; fb_height = 200;
        taskbar_h = 18;
        icon_y[1] = 80; icon_y[2] = 140;
    }

    set_teal_palette();
    mouse_x = fb_width / 2;
    mouse_y = fb_height / 2;
    cursor_save();

    /* Init PS/2 mouse */
    enable_usb_legacy();

    /* Init keyboard */
    keyboard_write(0xF0);
    keyboard_write(0x01);
    mouse_cycle = 0;

    redraw_all();
    cursor_draw();

    int tick = 0;
    while (1) {
        /* ==== POLL I/O ==== */
        while (inb(0x64) & 1) {
            uint8_t st = inb(0x64);
            uint8_t d = inb(0x60);

            if (st & 0x20) {
                /* ==== MOUSE DATA ==== */
                if (mouse_cycle == 0) {
                    if (d & 0x08) { mouse_bytes[0] = d; mouse_cycle++; kb_nav_mode = 0; }
                } else {
                    mouse_bytes[mouse_cycle++] = d;
                    if (mouse_cycle == 3) {
                        mouse_cycle = 0;
                        int dx = mouse_bytes[1], dy = mouse_bytes[2];
                        if (mouse_bytes[0] & 0x10) dx |= ~0xFF;
                        if (mouse_bytes[0] & 0x20) dy |= ~0xFF;
                        dy = -dy;
                        int btn = mouse_bytes[0] & 0x07;
                        int was_click = mouse_buttons == 0 && btn != 0;
                        mouse_buttons = btn;
                        move_cursor(mouse_x + dx, mouse_y + dy);

                        if (was_click && (btn & 1)) {
                            /* ==== LEFT CLICK ==== */
                            /* Check Start button */
                            if (in_rect(mouse_x, mouse_y, 2, fb_height - taskbar_h + 2, 56, 20)) {
                                start_menu_open = !start_menu_open;
                                kb_tab_select = 0;
                            }
                            /* Check taskbar clock for shutdown */
                            if (in_rect(mouse_x, mouse_y, fb_width - 56, fb_height - taskbar_h + 2, 52, 20)) {
                                show_error();
                            }
                            /* Check desktop icons */
                            for (int i = 0; i < icon_count; i++) {
                                if (in_rect(mouse_x, mouse_y, icon_x[i], icon_y[i], 32, 40)) {
                                    if (i == 0) show_error();
                                    else if (i == 1) {
                                        int nw = create_window(80, 40, 320, 220, "Notepad - Untitled");
                                        draw_window_frame(nw);
                                        draw_notepad_content(nw);
                                    }
                                    else if (i == 2) show_error();
                                    start_menu_open = 0;
                                }
                            }
                            /* Check window close buttons */
                            for (int i = 0; i < window_count; i++) {
                                if (!window_visible[i]) continue;
                                int wx = window_x[i], wy = window_y[i], ww = window_w[i];
                                if (in_rect(mouse_x, mouse_y, wx + ww - 18, wy, 16, 16)) {
                                    close_window(i);
                                }
                            }
                            /* Check error OK button */
                            if (error_active && in_rect(mouse_x, mouse_y, error_x + error_w/2 - 20, error_y + 42, 40, 14)) {
                                hide_error();
                            }
                            /* Check start menu items */
                            if (start_menu_open) {
                                for (int i = 0; i < start_item_count; i++) {
                                    int iy = start_menu_y + 2 + i * 24;
                                    if (in_rect(mouse_x, mouse_y, start_menu_x + 2, iy, 156, 22)) {
                                        if (i == 4) { start_menu_open = 0; show_error(); }
                                        else { show_error(); start_menu_open = 0; }
                                    }
                                }
                            }
                            /* Check window drag (title bar) */
                            dragging = -1;
                            for (int i = window_count - 1; i >= 0; i--) {
                                if (!window_visible[i]) continue;
                                int wx = window_x[i], wy = window_y[i], ww = window_w[i];
                                if (in_rect(mouse_x, mouse_y, wx, wy, ww, 18)) {
                                    window_active = i;
                                    dragging = i;
                                    drag_off_x = mouse_x - wx;
                                    drag_off_y = mouse_y - wy;
                                    break;
                                }
                            }
                        }
                        /* ==== DRAG ==== */
                        if ((btn & 1) && dragging >= 0) {
                            window_x[dragging] = mouse_x - drag_off_x;
                            window_y[dragging] = mouse_y - drag_off_y;
                        }
                        if (!(btn & 1)) dragging = -1;
                    }
                }
            } else {
                /* ==== KEYBOARD DATA ==== */
                char c = scancode_to_ascii(d);
                if (c == '\b') {
                    if (notepad_len > 0) { notepad_len--; }
                } else if (c == '\n') {
                    if (notepad_len < 3199) { notepad_text[notepad_len++] = '\n'; }
                } else if (c >= 32 && c <= 126) {
                    if (notepad_len < 3199) { notepad_text[notepad_len++] = c; }
                }

                /* Keyboard shortcuts */
                if (d == 0x48) kb_handle_arrow('U'); /* Up */
                if (d == 0x50) kb_handle_arrow('D'); /* Down */
                if (d == 0x1C) kb_handle_enter();     /* Enter */
                if (d == 0x01) { start_menu_open = 0; } /* Esc */
                if (d == 0x3B) { /* F1 = Start menu */
                    start_menu_open = !start_menu_open;
                    kb_tab_select = 0;
                }
            }
        }

        /* ==== REDRAW ==== */
        cursor_restore();
        redraw_all();
        cursor_save();
        cursor_draw();

        /* ==== RANDOM ERRORS ==== */
        tick++;
        if (!error_active && (tick % (4000000 + rand() % 3000000)) == 0) {
            cursor_restore();
            show_error();
            cursor_save();
            cursor_draw();
        }
        if (error_active) {
            error_timer--;
            if (error_timer <= 0) {
                cursor_restore();
                hide_error();
                cursor_save();
                cursor_draw();
            }
        }

        for (volatile int dly = 0; dly < 500; dly++);
    }
}
