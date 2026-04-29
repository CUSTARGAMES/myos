/* kernel.c – Complete OS kernel */
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  I/O port inline functions                                          */
/* ------------------------------------------------------------------ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* ------------------------------------------------------------------ */
/*  Global variables (screen, mouse, keyboard, editor)                 */
/* ------------------------------------------------------------------ */
/* Framebuffer */
static uint8_t *fb;            /* start of linear framebuffer */
static int fb_pitch;           /* bytes per line */
static int fb_width, fb_height;

/* Palette indices */
#define COLOR_BLACK   0
#define COLOR_BLUE    1
#define COLOR_WHITE   15
#define COLOR_DARK_GRAY  8
#define COLOR_LIGHT_GRAY 7
#define COLOR_RED     12

/* Mouse state */
static int mouse_x, mouse_y;           /* current screen position */
static int mouse_cycle;
static uint8_t mouse_bytes[3];
static int mouse_dirty = 1;            /* need redraw */

/* Editor window position & size – will be adjusted later */
static int win_x, win_y, win_w, win_h;
#define WIN_BORDER 2

/* Text editor buffer */
#define MAX_CHAR  3200   /* 40 lines * 80 chars */
static char text_buffer[MAX_CHAR];
static int text_len = 0;        /* number of characters (includes '\n') */
static int scroll_line = 0;     /* first visible line (scrolling) */
#define VISIBLE_LINES  (fb_height / 8)   /* will be recalculated */

/* Fake error state */
static int error_active = 0;
static int error_timer;
static int error_duration = 500000;   /* ~0.5 sec at ~1GHz */
static int error_x, error_y, error_w, error_h;
static char error_text[40];

static uint8_t error_bg[250*60];       /* saved background for error box */

/* Simple PRNG */
static uint32_t seed = 0xDEADBEEF;
static uint32_t rand(void) {
    seed = (1103515245 * seed + 12345) & 0x7FFFFFFF;
    return seed;
}

/* ------------------------------------------------------------------ */
/*  VGA mode 13h fallback (320x200, 8bpp)                               */
/* ------------------------------------------------------------------ */
static void set_vga_mode13(void) {
    /* Standard VGA register sequence for 320x200x256 */
    outb(0x3C2, 0x63);                     /* misc output */
    outb(0x3D4, 0x00); outb(0x3D5, 0x5F); /* horiz total */
    outb(0x3D4, 0x01); outb(0x3D5, 0x4F); /* horiz disp end */
    outb(0x3D4, 0x02); outb(0x3D5, 0x50); /* start horiz blank */
    outb(0x3D4, 0x03); outb(0x3D5, 0x82); /* end horiz blank */
    outb(0x3D4, 0x04); outb(0x3D5, 0x54); /* start horiz retrace */
    outb(0x3D4, 0x05); outb(0x3D5, 0x80); /* end horiz retrace */
    outb(0x3D4, 0x06); outb(0x3D5, 0xBF); /* vertical total */
    outb(0x3D4, 0x07); outb(0x3D5, 0x00); /* overflow */
    outb(0x3D4, 0x08); outb(0x3D5, 0x00); /* preset row scan */
    outb(0x3D4, 0x09); outb(0x3D5, 0x41); /* max scan line */
    outb(0x3D4, 0x0A); outb(0x3D5, 0x00); /* cursor start */
    outb(0x3D4, 0x0B); outb(0x3D5, 0x00); /* cursor end */
    outb(0x3D4, 0x0C); outb(0x3D5, 0x00); /* start addr high */
    outb(0x3D4, 0x0D); outb(0x3D5, 0x00); /* start addr low */
    outb(0x3D4, 0x0E); outb(0x3D5, 0x00); /* cursor high */
    outb(0x3D4, 0x0F); outb(0x3D5, 0x00); /* cursor low */
    outb(0x3D4, 0x10); outb(0x3D5, 0x9C); /* vertical retrace start */
    outb(0x3D4, 0x11); outb(0x3D5, 0x8E); /* vertical retrace end */
    outb(0x3D4, 0x12); outb(0x3D5, 0x8F); /* vertical disp end */
    outb(0x3D4, 0x13); outb(0x3D5, 0x28); /* offset */
    outb(0x3D4, 0x14); outb(0x3D5, 0x40); /* underline location */
    outb(0x3D4, 0x15); outb(0x3D5, 0x96); /* start vert blank */
    outb(0x3D4, 0x16); outb(0x3D5, 0xB9); /* end vert blank */
    outb(0x3D4, 0x17); outb(0x3D5, 0xA3); /* mode control */

    /* Sequencer: chain4, no reset */
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);

    /* Graphics controller */
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    /* Attribute controller: palette enable */
    inb(0x3DA);
    outb(0x3C0, 0x30); outb(0x3C0, 0x41);
    outb(0x3C0, 0x33); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20); /* enable video */

    fb = (uint8_t*)0xA0000;
    fb_pitch = 320;
    fb_width = 320;
    fb_height = 200;
}

/* ------------------------------------------------------------------ */
/*  Palette setup (first 16 colors VGA standard)                       */
/* ------------------------------------------------------------------ */
static void set_palette(void) {
    /* Write VGA DAC */
    outb(0x3C8, 0);
    /* First 16 colors from the classic VGA palette */
    static const uint8_t palette[16][3] = {
        {0x00,0x00,0x00}, {0x00,0x00,0xAA}, {0x00,0xAA,0x00}, {0x00,0xAA,0xAA},
        {0xAA,0x00,0x00}, {0xAA,0x00,0xAA}, {0xAA,0x55,0x00}, {0xAA,0xAA,0xAA},
        {0x55,0x55,0x55}, {0x55,0x55,0xFF}, {0x55,0xFF,0x55}, {0x55,0xFF,0xFF},
        {0xFF,0x55,0x55}, {0xFF,0x55,0xFF}, {0xFF,0xFF,0x55}, {0xFF,0xFF,0xFF}
    };
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, palette[i][0] / 4); /* 6-bit DAC */
        outb(0x3C9, palette[i][1] / 4);
        outb(0x3C9, palette[i][2] / 4);
    }
    /* Fill remaining colors with black */
    for (int i = 16; i < 256; i++) {
        outb(0x3C9, 0);
        outb(0x3C9, 0);
        outb(0x3C9, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Pixel drawing                                                       */
/* ------------------------------------------------------------------ */
static inline void putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height)
        return;
    fb[y * fb_pitch + x] = color;
}

/* ------------------------------------------------------------------ */
/*  Mouse cursor (cross)                                                */
/* ------------------------------------------------------------------ */
#define CURSOR_SIZE 5   /* radius of cross arms */
static uint8_t cursor_bg[(CURSOR_SIZE*2+1)*(CURSOR_SIZE*2+1)];

static void cursor_save(void) {
    int cx = mouse_x, cy = mouse_y;
    int idx = 0;
    for (int dy = -CURSOR_SIZE; dy <= CURSOR_SIZE; dy++) {
        for (int dx = -CURSOR_SIZE; dx <= CURSOR_SIZE; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                cursor_bg[idx] = fb[py * fb_pitch + px];
            else
                cursor_bg[idx] = 0;
            idx++;
        }
    }
}

static void cursor_restore(void) {
    int cx = mouse_x, cy = mouse_y;
    int idx = 0;
    for (int dy = -CURSOR_SIZE; dy <= CURSOR_SIZE; dy++) {
        for (int dx = -CURSOR_SIZE; dx <= CURSOR_SIZE; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height)
                fb[py * fb_pitch + px] = cursor_bg[idx];
            idx++;
        }
    }
}

static void cursor_draw(void) {
    int cx = mouse_x, cy = mouse_y;
    for (int i = -CURSOR_SIZE; i <= CURSOR_SIZE; i++) {
        putpixel(cx + i, cy, COLOR_WHITE);
        putpixel(cx, cy + i, COLOR_WHITE);
    }
}

static void move_cursor(int newx, int newy) {
    if (newx < 0) newx = 0;
    if (newy < 0) newy = 0;
    if (newx >= fb_width) newx = fb_width - 1;
    if (newy >= fb_height) newy = fb_height - 1;
    if (newx == mouse_x && newy == mouse_y) return;

    cursor_restore();
    mouse_x = newx;
    mouse_y = newy;
    cursor_save();
    cursor_draw();
    mouse_dirty = 1;
}

/* ------------------------------------------------------------------ */
/*  PS/2 helper functions                                               */
/* ------------------------------------------------------------------ */
static void ps2_wait_write(void) {
    while (inb(0x64) & 2)
        io_wait();
}

static void ps2_wait_data(void) {
    while (!(inb(0x64) & 1))
        io_wait();
}

static uint8_t ps2_read(void) {
    ps2_wait_data();
    return inb(0x60);
}

static void mouse_write(uint8_t data) {
    /* Send command to mouse via controller */
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, data);
    /* Consume ACK (0xFA) */
    uint8_t ack = ps2_read();
    (void)ack;
}

static void keyboard_write(uint8_t data) {
    ps2_wait_write();
    outb(0x60, data);
    /* keyboard may respond with ACK; ignore for simplicity */
}

/* ------------------------------------------------------------------ */
/*  Mouse / keyboard initialisation                                     */
/* ------------------------------------------------------------------ */
static void init_input(void) {
    /* Enable auxiliary device (mouse) */
    ps2_wait_write();
    outb(0x64, 0xA8);

    /* Mouse: set defaults, enable data reporting */
    mouse_write(0xF6);
    mouse_write(0xF4);

    /* Keyboard: set scan code set 1 (already default) */
    keyboard_write(0xF0);
    keyboard_write(0x01);  /* set scancode set 1 */

    mouse_cycle = 0;
}

/* ------------------------------------------------------------------ */
/*  Scan code set 1 → ASCII (US keyboard, unshifted)                    */
/* ------------------------------------------------------------------ */
static char scancode_to_ascii(uint8_t sc) {
    switch (sc) {
        case 0x0E: return '\b';       /* backspace */
        case 0x1C: return '\n';       /* enter */
        case 0x39: return ' ';        /* space */
        case 0x01: return 0;          /* ESC – ignored */
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return '-';
        case 0x0D: return '=';
        case 0x0F: return 0;          /* tab – ignore */
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1A: return '[';
        case 0x1B: return ']';
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
        case 0x2B: return '\\';
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x36: return 0;          /* right shift – ignore */
        case 0x37: return '*';        /* keypad * */
        case 0x38: return 0;          /* alt – ignore */
        default: return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Editor drawing (8x8 font)                                          */
/* ------------------------------------------------------------------ */
/* Simple 8x8 bitmap font (ASCII 32..126) */
static const uint8_t font8x8[95][8] = {
    [0]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* space */
    [1]  = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},  /* ! */
    [2]  = {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},  /* " */
    [3]  = {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},  /* # */
    [4]  = {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},  /* $ */
    [5]  = {0x00,0x66,0xAC,0xD8,0x36,0x6A,0xD6,0x00},  /* % */
    [6]  = {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},  /* & */
    [7]  = {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},  /* ' */
    [8]  = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},  /* ( */
    [9]  = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},  /* ) */
    [10] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},  /* * */
    [11] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},  /* + */
    [12] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},  /* , */
    [13] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},  /* - */
    [14] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},  /* . */
    [15] = {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},  /* / */
    [16] = {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00},  /* 0 */
    [17] = {0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00},  /* 1 */
    [18] = {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},  /* 2 */
    [19] = {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},  /* 3 */
    [20] = {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},  /* 4 */
    [21] = {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},  /* 5 */
    [22] = {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},  /* 6 */
    [23] = {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},  /* 7 */
    [24] = {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},  /* 8 */
    [25] = {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},  /* 9 */
    [26] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},  /* : */
    [27] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},  /* ; */
    [28] = {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},  /* < */
    [29] = {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},  /* = */
    [30] = {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},  /* > */
    [31] = {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},  /* ? */
    [32] = {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},  /* @ */
    [33] = {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},  /* A */
    [34] = {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},  /* B */
    [35] = {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},  /* C */
    [36] = {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},  /* D */
    [37] = {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},  /* E */
    [38] = {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},  /* F */
    [39] = {0x3C,0x66,0xC0,0xDE,0xC6,0x66,0x3A,0x00},  /* G */
    [40] = {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},  /* H */
    [41] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},  /* I */
    [42] = {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},  /* J */
    [43] = {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},  /* K */
    [44] = {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},  /* L */
    [45] = {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},  /* M */
    [46] = {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},  /* N */
    [47] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},  /* O */
    [48] = {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},  /* P */
    [49] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xD6,0x7C,0x06},  /* Q */
    [50] = {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},  /* R */
    [51] = {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00},  /* S */
    [52] = {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00},  /* T */
    [53] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},  /* U */
    [54] = {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},  /* V */
    [55] = {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},  /* W */
    [56] = {0xC6,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00},  /* X */
    [57] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},  /* Y */
    [58] = {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},  /* Z */
    [59] = {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},  /* [ */
    [60] = {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},  /* \ */
    [61] = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},  /* ] */
    [62] = {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},  /* ^ */
    [63] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},  /* _ */
    [64] = {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},  /* ` */
    [65] = {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},  /* a */
    [66] = {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},  /* b */
    [67] = {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},  /* c */
    [68] = {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},  /* d */
    [69] = {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},  /* e */
    [70] = {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00},  /* f */
    [71] = {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78},  /* g */
    [72] = {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},  /* h */
    [73] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},  /* i */
    [74] = {0x0C,0x00,0x0C,0x0C,0x0C,0xCC,0xCC,0x78},  /* j */
    [75] = {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},  /* k */
    [76] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},  /* l */
    [77] = {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},  /* m */
    [78] = {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},  /* n */
    [79] = {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},  /* o */
    [80] = {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},  /* p */
    [81] = {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},  /* q */
    [82] = {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},  /* r */
    [83] = {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},  /* s */
    [84] = {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00},  /* t */
    [85] = {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x76,0x00},  /* u */
    [86] = {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},  /* v */
    [87] = {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},  /* w */
    [88] = {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},  /* x */
    [89] = {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC},  /* y */
    [90] = {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},  /* z */
    [91] = {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},  /* { */
    [92] = {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},  /* | */
    [93] = {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},  /* } */
    [94] = {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}   /* ~ */
};

static void draw_char(int x, int y, char c, uint8_t fgcol, uint8_t bgcol) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= fb_width || py < 0 || py >= fb_height)
                continue;
            fb[py * fb_pitch + px] = (bits & (0x80 >> col)) ? fgcol : bgcol;
        }
    }
}

static void draw_editor_window(void) {
    /* Border */
    for (int x = win_x; x < win_x + win_w; x++) {
        putpixel(x, win_y, COLOR_WHITE);
        putpixel(x, win_y + win_h - 1, COLOR_WHITE);
        putpixel(x, win_y + 1, COLOR_WHITE);
        putpixel(x, win_y + win_h - 2, COLOR_WHITE);
    }
    for (int y = win_y; y < win_y + win_h; y++) {
        putpixel(win_x, y, COLOR_WHITE);
        putpixel(win_x + win_w - 1, y, COLOR_WHITE);
        putpixel(win_x + 1, y, COLOR_WHITE);
        putpixel(win_x + win_w - 2, y, COLOR_WHITE);
    }
}

static void redraw_editor_text(void) {
    int inner_x = win_x + WIN_BORDER;
    int inner_y = win_y + WIN_BORDER;
    int max_cols = (win_w - 2*WIN_BORDER) / 8;
    int max_rows = (win_h - 2*WIN_BORDER) / 8;

    /* Clear inner area */
    for (int y = 0; y < max_rows * 8; y++)
        for (int x = 0; x < max_cols * 8; x++)
            putpixel(inner_x + x, inner_y + y, COLOR_DARK_GRAY);

    /* Parse text into lines */
    int line_start = 0;
    int display_line = 0;
    while (display_line < scroll_line) {
        /* advance one line */
        while (line_start < text_len && text_buffer[line_start] != '\n')
            line_start++;
        if (line_start < text_len)
            line_start++; /* skip '\n' */
        display_line++;
        if (line_start >= text_len) break;
    }
    /* Now render visible lines */
    for (int row = 0; row < max_rows; row++) {
        if (line_start >= text_len) break;
        int col = 0;
        int current = line_start;
        while (current < text_len && text_buffer[current] != '\n' && col < max_cols) {
            draw_char(inner_x + col*8, inner_y + row*8,
                      text_buffer[current], COLOR_WHITE, COLOR_DARK_GRAY);
            current++;
            col++;
        }
        /* Move to next line */
        line_start = current + 1; /* skip '\n' or end */
        if (line_start > text_len) break;
    }
}

/* ------------------------------------------------------------------ */
/*  Fake error pop‑up                                                    */
/* ------------------------------------------------------------------ */
static void show_error(void) {
    const char *msgs[] = {
        "System corrupt - code 0xFFFF",
        "Kernel panic - not syncing",
        "Fatal exception in module KERNEL.EXE",
        "Out of memory - press any key to continue"
    };
    int idx = rand() % 4;
    /* Copy message */
    for (int i = 0; i < 39 && msgs[idx][i]; i++)
        error_text[i] = msgs[idx][i];

    /* Calculate box size (based on text length) */
    int len = 0;
    while (error_text[len]) len++;
    error_w = len * 8 + 10;  /* some padding */
    error_h = 20;
    /* Random position, avoid edges */
    error_x = rand() % (fb_width - error_w);
    error_y = rand() % (fb_height - error_h);

    /* Save background */
    int idx_bg = 0;
    for (int dy = 0; dy < error_h; dy++) {
        for (int dx = 0; dx < error_w; dx++) {
            int px = error_x + dx, py = error_y + dy;
            if (px < fb_width && py < fb_height)
                error_bg[idx_bg++] = fb[py * fb_pitch + px];
            else
                error_bg[idx_bg++] = 0;
        }
    }
    /* Draw error box */
    for (int dy = 0; dy < error_h; dy++) {
        for (int dx = 0; dx < error_w; dx++) {
            putpixel(error_x + dx, error_y + dy, COLOR_RED);
        }
    }
    /* White border */
    for (int dx = 0; dx < error_w; dx++) {
        putpixel(error_x + dx, error_y, COLOR_WHITE);
        putpixel(error_x + dx, error_y + error_h - 1, COLOR_WHITE);
    }
    for (int dy = 0; dy < error_h; dy++) {
        putpixel(error_x, error_y + dy, COLOR_WHITE);
        putpixel(error_x + error_w - 1, error_y + dy, COLOR_WHITE);
    }
    /* Draw text */
    for (int i = 0; i < len; i++) {
        draw_char(error_x + 5 + i*8, error_y + 6, error_text[i],
                  COLOR_WHITE, COLOR_RED);
    }

    error_active = 1;
    error_timer = error_duration;
}

static void hide_error(void) {
    /* Restore background */
    int idx = 0;
    for (int dy = 0; dy < error_h; dy++) {
        for (int dx = 0; dx < error_w; dx++) {
            int px = error_x + dx, py = error_y + dy;
            if (px < fb_width && py < fb_height)
                fb[py * fb_pitch + px] = error_bg[idx];
            idx++;
        }
    }
    error_active = 0;
}

/* ------------------------------------------------------------------ */
/*  Main kernel entry                                                    */
/* ------------------------------------------------------------------ */
void kernel_main(uint32_t magic, uint32_t addr) {
    /* Multiboot info is at physical address addr */
    if (magic != 0x2BADB002) {
        /* Not a Multiboot kernel – hang */
        return;
    }

    uint32_t *mbi = (uint32_t *)addr;
    uint32_t flags = *(mbi); /* mbi->flags */
    fb = 0;
    fb_pitch = 0;
    fb_width = 0;
    fb_height = 0;

    /* Check for framebuffer info (bit 11) */
    if ((flags & (1 << 11)) && *(mbi+22) != 0) {
        /* mbi->framebuffer_addr is at offset 88 (22*4) */
        fb = (uint8_t *)(uint32_t)*(mbi+22);
        fb_pitch = *(mbi+24);       /* mbi->framebuffer_pitch */
        fb_width = *(mbi+25);       /* mbi->framebuffer_width */
        fb_height = *(mbi+26);      /* mbi->framebuffer_height */
        /* Verify depth 8, but we trust GRUB */
    }

    if (!fb) {
        /* Fallback to VGA mode 13h */
        set_vga_mode13();
    }

    /* Set up palette */
    set_palette();

    /* Fill screen blue */
    for (int i = 0; i < fb_height * fb_pitch; i++)
        fb[i] = COLOR_BLUE;

    /* Adjust window size if screen is too small */
    win_w = 400;
    if (win_w > fb_width - 2*WIN_BORDER) win_w = fb_width - 2*WIN_BORDER;
    win_h = 200;
    if (win_h > fb_height - 2*WIN_BORDER) win_h = fb_height - 2*WIN_BORDER;
    win_x = (fb_width - win_w) / 2;
    win_y = (fb_height - win_h) / 2;

    /* Initial editor drawing */
    draw_editor_window();
    redraw_editor_text();

    /* Mouse initial position */
    mouse_x = fb_width / 2;
    mouse_y = fb_height / 2;
    cursor_save();
    cursor_draw();

    /* Initialise PS/2 input */
    init_input();

    /* Main loop */
    int tick_counter = 0;
    while (1) {
        /* Poll PS/2 controller */
        while (inb(0x64) & 1) {
            uint8_t status = inb(0x64);
            uint8_t data = inb(0x60);
            if (status & 0x20) {  /* mouse */
                /* Build packet */
                if (mouse_cycle == 0) {
                    if (data & 0x08) { /* sync bit */
                        mouse_bytes[0] = data;
                        mouse_cycle++;
                    }
                } else {
                    mouse_bytes[mouse_cycle] = data;
                    mouse_cycle++;
                    if (mouse_cycle == 3) {
                        mouse_cycle = 0;
                        /* Extract movement */
                        int dx = mouse_bytes[1];
                        int dy = mouse_bytes[2];
                        if (mouse_bytes[0] & 0x10) dx |= ~0xFF; /* sign extend */
                        if (mouse_bytes[0] & 0x20) dy |= ~0xFF;
                        dy = -dy; /* Y is inverted */
                        int newx = mouse_x + dx;
                        int newy = mouse_y + dy;
                        move_cursor(newx, newy);
                    }
                }
            } else { /* keyboard */
                char c = scancode_to_ascii(data);
                if (c == '\b') {
                    if (text_len > 0) {
                        text_len--;
                        redraw_editor_text();
                    }
                } else if (c == '\n') {
                    if (text_len < MAX_CHAR - 1) {
                        text_buffer[text_len++] = '\n';
                        /* Auto-scroll if cursor beyond visible area */
                        int current_line = 0;
                        for (int i = 0; i < text_len; i++)
                            if (text_buffer[i] == '\n') current_line++;
                        int max_lines = (win_h - 2*WIN_BORDER) / 8;
                        if (current_line - scroll_line >= max_lines)
                            scroll_line = current_line - max_lines + 1;
                        redraw_editor_text();
                    }
                } else if (c >= 32 && c <= 126) {
                    if (text_len < MAX_CHAR - 1) {
                        text_buffer[text_len++] = c;
                        redraw_editor_text();
                    }
                }
            }
        }

        /* Fake error every ~3-7 seconds */
        tick_counter++;
        if (!error_active && (tick_counter % (3000000 + rand() % 4000000)) == 0) {
            show_error();
        }
        if (error_active) {
            error_timer--;
            if (error_timer <= 0)
                hide_error();
        }

        /* Small delay to avoid hammering the CPU */
        for (volatile int d = 0; d < 1000; d++);
    }
}
