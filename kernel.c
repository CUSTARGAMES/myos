#include <stdint.h>

/* VGA Text Mode */
static volatile uint16_t *const vga = (uint16_t *)0xB8000;
#define COLS 80
#define ROWS 25
static int cx = 0, cy = 0;
static uint8_t color = 0x0F;

/* Colors */
#define BLACK   0x00
#define BLUE    0x01
#define GREEN   0x02
#define CYAN    0x03
#define RED     0x04
#define MAGENTA 0x05
#define BROWN   0x06
#define LGRAY   0x07
#define DGRAY   0x08
#define LBLUE   0x09
#define LGREEN  0x0A
#define LCYAN   0x0B
#define LRED    0x0C
#define LMAGENTA 0x0D
#define YELLOW  0x0E
#define WHITE   0x0F

/* I/O - FIXED: proper functions, not macros */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* User info */
static char username[20] = "user";
static char pcname[20] = "TFD-PC";
static char cmd[256];
static int cmd_len = 0;
static char history[10][256];
static int hist_count = 0;
static char notepad_text[20][80];
static int notepad_row = 0, notepad_col = 0;
static int app_mode = 0;

/* Snake */
static int sx[200], sy[200], sn_len = 3, sdir = 0, sfx, sfy, sscore = 0, sgameover = 0;
/* Tetris */
static int board[20][10];
static int tpiece[4][2], tpiecetype = 0, tx = 4, ty = 0, tscore = 0;
static const int pieces[7][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}},{{0,0},{0,1},{1,1},{2,1}},{{0,1},{1,1},{2,1},{2,0}},
    {{0,0},{1,0},{1,1},{2,1}},{{0,1},{1,1},{1,0},{2,0}},{{0,0},{1,0},{2,0},{2,1}},
    {{0,0},{1,0},{2,0},{1,1}}
};
/* Pong */
static int p1y = 10, p2y = 10, bpx = 40, bpy = 12, bdx = 1, bdy = 1, p1score = 0, p2score = 0;

/* ============ STRING HELPERS ============ */
static int str_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) { while(*a&&*b&&*a==*b){a++;b++;} return *a-*b; }
static void str_cpy(char *d, const char *s) { while(*s) *d++=*s++; *d=0; }
static int str_has(const char *h, const char *n) {
    int nl=str_len(n); if(nl==0) return 1;
    while(*h){int m=1;for(int i=0;i<nl;i++)if(h[i]!=n[i]){m=0;break;}if(m)return 1;h++;}
    return 0;
}

/* ============ SCREEN ============ */
static void set_cursor(int x, int y) {
    uint16_t pos = y * COLS + x;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    cx = x; cy = y;
}
static void hide_cursor(void) { outb(0x3D4, 0x0A); outb(0x3D5, 0x20); }
static void show_cursor(void) { outb(0x3D4, 0x0A); outb(0x3D5, 0x0E); outb(0x3D4, 0x0B); outb(0x3D5, 0x0F); }
static void clear_screen(void) {
    for (int i = 0; i < COLS * ROWS; i++) vga[i] = (uint16_t)' ' | ((uint16_t)LGRAY << 8);
    cx = 0; cy = 0;
}
static void scroll_up(void) {
    for (int y = 0; y < ROWS - 1; y++)
        for (int x = 0; x < COLS; x++)
            vga[y * COLS + x] = vga[(y + 1) * COLS + x];
    for (int x = 0; x < COLS; x++)
        vga[(ROWS - 1) * COLS + x] = (uint16_t)' ' | ((uint16_t)LGRAY << 8);
}
static void putc(char c) {
    if (c == '\n') { cx = 0; cy++; if (cy >= ROWS) { scroll_up(); cy = ROWS - 1; } }
    else if (c == '\b') { if (cx > 0) { cx--; vga[cy * COLS + cx] = (uint16_t)' ' | ((uint16_t)color << 8); } }
    else if (c == '\r') { cx = 0; }
    else {
        vga[cy * COLS + cx] = (uint16_t)c | ((uint16_t)color << 8);
        cx++;
        if (cx >= COLS) { cx = 0; cy++; if (cy >= ROWS) { scroll_up(); cy = ROWS - 1; } }
    }
    set_cursor(cx, cy);
}
static void print(const char *s, uint8_t col) { color = col; while (*s) putc(*s++); }
static void println(const char *s, uint8_t col) { print(s, col); putc('\n'); }
static void putc_at(int x, int y, char c, uint8_t col) {
    if (x >= 0 && x < COLS && y >= 0 && y < ROWS)
        vga[y * COLS + x] = (uint16_t)c | ((uint16_t)col << 8);
}
static void print_at(int x, int y, const char *s, uint8_t col) { while (*s) putc_at(x++, y, *s++, col); }

/* ============ KEYBOARD ============ */
static void ps2_wait_write(void) { while (inb(0x64) & 2) io_wait(); }
static void ps2_wait_data(void) { while (!(inb(0x64) & 1)) io_wait(); }
static char scancode_to_ascii(uint8_t sc) {
    switch (sc) {
        case 0x0E: return '\b'; case 0x1C: return '\n'; case 0x39: return ' ';
        case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3'; case 0x05: return '4';
        case 0x06: return '5'; case 0x07: return '6'; case 0x08: return '7'; case 0x09: return '8';
        case 0x0A: return '9'; case 0x0B: return '0'; case 0x0C: return '-'; case 0x0D: return '=';
        case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e'; case 0x13: return 'r';
        case 0x14: return 't'; case 0x15: return 'y'; case 0x16: return 'u'; case 0x17: return 'i';
        case 0x18: return 'o'; case 0x19: return 'p'; case 0x1A: return '['; case 0x1B: return ']';
        case 0x1E: return 'a'; case 0x1F: return 's'; case 0x20: return 'd'; case 0x21: return 'f';
        case 0x22: return 'g'; case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k';
        case 0x26: return 'l'; case 0x27: return ';'; case 0x28: return '\''; case 0x29: return '`';
        case 0x2B: return '\\'; case 0x2C: return 'z'; case 0x2D: return 'x'; case 0x2E: return 'c';
        case 0x2F: return 'v'; case 0x30: return 'b'; case 0x31: return 'n'; case 0x32: return 'm';
        case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/'; case 0x37: return '*';
        case 0x48: return 'U'; case 0x50: return 'D'; case 0x4B: return 'L'; case 0x4D: return 'R';
        default: return 0;
    }
}
static char read_key(void) { ps2_wait_data(); return scancode_to_ascii(inb(0x60)); }
static int key_available(void) { return inb(0x64) & 1; }

/* ============ BOOT MENU ============ */
static void boot_menu(void) {
    clear_screen();
    print_at(25,2,"╔══════════════════════════════╗",CYAN);
    print_at(25,3,"║                              ║",CYAN);
    print_at(25,4,"║   ████████╗███████╗██████╗   ║",RED);
    print_at(25,5,"║   ╚══██╔══╝██╔════╝██╔══██╗  ║",RED);
    print_at(25,6,"║      ██║   █████╗  ██║  ██║  ║",GREEN);
    print_at(25,7,"║      ██║   ██╔══╝  ██║  ██║  ║",GREEN);
    print_at(25,8,"║      ██║   ██║     ██████╔╝  ║",BLUE);
    print_at(25,9,"║      ╚═╝   ╚═╝     ╚═════╝   ║",BLUE);
    print_at(25,10,"║                              ║",CYAN);
    print_at(25,11,"║     TFD~by sadman            ║",CYAN);
    print_at(25,12,"║     Version beta             ║",CYAN);
    print_at(25,13,"║                              ║",CYAN);
    print_at(25,14,"╚══════════════════════════════╝",CYAN);
    print_at(28,16,"[ PRESS ENTER TO BOOT ]",YELLOW);
    while (1) { if (key_available() && read_key() == '\n') break; }
}

/* ============ SETUP ============ */
static void setup_wizard(void) {
    clear_screen();
    println("=== SETUP WIZARD ===", CYAN);
    println("", 0);
    print("Enter username: ", YELLOW); show_cursor();
    int i = 0; username[0] = 0;
    while (1) {
        char c = read_key();
        if (c == '\n') { username[i] = 0; break; }
        else if (c == '\b' && i > 0) { i--; putc('\b'); }
        else if (c >= 32 && i < 19) { username[i++] = c; putc(c); }
    }
    println("", 0);
    print("Enter PC name: ", YELLOW);
    i = 0; pcname[0] = 0;
    while (1) {
        char c = read_key();
        if (c == '\n') { pcname[i] = 0; break; }
        else if (c == '\b' && i > 0) { i--; putc('\b'); }
        else if (c >= 32 && i < 19) { pcname[i++] = c; putc(c); }
    }
    println("", 0); println("", 0);
    print("Setup complete! Welcome, ", GREEN); println(username, WHITE);
    for (volatile int d = 0; d < 300000; d++);
    hide_cursor();
}

/* ============ APPS ============ */
static void app_notepad(void) {
    app_mode = 1; notepad_row = 0; notepad_col = 0;
    for (int i = 0; i < 20; i++) for (int j = 0; j < 80; j++) notepad_text[i][j] = ' ';
    clear_screen();
    println("=== FXC NOTEPAD ===", CYAN);
    println("Type text. Press ESC to exit.", YELLOW);
    println("------------------------------------------", CYAN);
    cy = 3; cx = 0;
}

static void app_games_menu(void) {
    app_mode = 2;
    clear_screen();
    println("=== FXC GAMES ===", CYAN);
    println("", 0);
    println("  1. Snake  (>>> eats . )", GREEN);
    println("  2. Tetris (|||| blocks)", LGREEN);
    println("  3. Pong   (| o |)", LCYAN);
    println("", 0);
    println("  Press 1/2/3 to choose, Q to quit", YELLOW);
}

static void app_snake_init(void) {
    app_mode = 3; sn_len = 3; sdir = 0; sscore = 0; sgameover = 0;
    sx[0] = 40; sy[0] = 12; sx[1] = 38; sy[1] = 12; sx[2] = 36; sy[2] = 12;
    sfx = 50; sfy = 12;
    clear_screen();
    for (int x = 0; x < COLS; x++) { putc_at(x, 0, '#', GREEN); putc_at(x, ROWS - 1, '#', GREEN); }
    for (int y = 0; y < ROWS; y++) { putc_at(0, y, '#', GREEN); putc_at(COLS - 1, y, '#', GREEN); }
    print_at(2, 0, " SNAKE | Score: 0 | WASD=Move | Q=Quit ", WHITE);
    putc_at(sfx, sfy, '.', RED);
    for (int i = 0; i < sn_len; i++) { putc_at(sx[i], sy[i], '>', YELLOW); putc_at(sx[i] + 1, sy[i], '>', YELLOW); }
}

static void snake_update(void) {
    int nx = sx[0], ny = sy[0];
    if (sdir == 0) nx += 2; else if (sdir == 1) ny++; else if (sdir == 2) nx -= 2; else ny--;
    if (nx <= 0 || nx >= COLS - 2 || ny <= 0 || ny >= ROWS - 1) { sgameover = 1; return; }
    for (int i = 0; i < sn_len; i++) if (sx[i] == nx && sy[i] == ny) { sgameover = 1; return; }
    putc_at(sx[sn_len - 1], sy[sn_len - 1], ' ', BLACK);
    putc_at(sx[sn_len - 1] + 1, sy[sn_len - 1], ' ', BLACK);
    for (int i = sn_len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    putc_at(sx[0], sy[0], '>', YELLOW); putc_at(sx[0] + 1, sy[0], '>', YELLOW);
    if (sx[0] == sfx && sy[0] == sfy || sx[0] + 1 == sfx && sy[0] == sfy) {
        sn_len++; sscore += 10;
        sx[sn_len - 1] = sx[sn_len - 2]; sy[sn_len - 1] = sy[sn_len - 2];
        putc_at(sx[sn_len - 1], sy[sn_len - 1], '>', YELLOW);
        putc_at(sx[sn_len - 1] + 1, sy[sn_len - 1], '>', YELLOW);
        sfx = ((inb(0x40) * 123 + 456) % 35) * 2 + 5;
        sfy = ((inb(0x40) * 789 + 123) % 20) + 3;
        putc_at(sfx, sfy, '.', RED);
        char buf[10]; int sc = sscore, idx = 9; buf[9] = 0;
        if (sc == 0) buf[8] = '0'; else while (sc) { buf[--idx] = '0' + sc % 10; sc /= 10; }
        print_at(16, 0, buf + idx, YELLOW);
    }
}

static void app_tetris_init(void) {
    app_mode = 4; tscore = 0; tx = 4; ty = 0;
    for (int y = 0; y < 20; y++) for (int x = 0; x < 10; x++) board[y][x] = 0;
    tpiecetype = inb(0x40) % 7;
    for (int i = 0; i < 4; i++) { tpiece[i][0] = pieces[tpiecetype][i][0]; tpiece[i][1] = pieces[tpiecetype][i][1]; }
    clear_screen();
    for (int y = 0; y < 22; y++) { putc_at(20, y, '#', GREEN); putc_at(43, y, '#', GREEN); }
    for (int x = 0; x < 12; x++) { putc_at(x * 2 + 20, 0, '#', GREEN); putc_at(x * 2 + 20, 21, '#', GREEN); }
    print_at(2, 0, "TETRIS | Score: 0 | AD=Move S=Down W=Rotate Q=Quit", WHITE);
}

static void tetris_draw(int clr) {
    for (int i = 0; i < 4; i++) {
        int px = 20 + (tx + tpiece[i][0]) * 2, py = ty + tpiece[i][1] + 1;
        if (py > 0) {
            if (clr) { putc_at(px, py, ' ', BLACK); putc_at(px + 1, py, ' ', BLACK); }
            else { putc_at(px, py, '|', LCYAN); putc_at(px + 1, py, '|', LCYAN); }
        }
    }
}

static int tetris_hit(int nx, int ny, int *p) {
    for (int i = 0; i < 4; i++) {
        int x = nx + p[i * 2], y = ny + p[i * 2 + 1];
        if (x < 0 || x >= 10 || y >= 20) return 1;
        if (y >= 0 && board[y][x]) return 1;
    }
    return 0;
}

static void tetris_update(void) {
    tetris_draw(1);
    int f[8]; for (int i = 0; i < 4; i++) { f[i * 2] = tpiece[i][0]; f[i * 2 + 1] = tpiece[i][1]; }
    if (!tetris_hit(tx, ty + 1, f)) { ty++; }
    else {
        for (int i = 0; i < 4; i++) { int bx = tx + tpiece[i][0], by = ty + tpiece[i][1]; if (by >= 0 && by < 20 && bx >= 0 && bx < 10) board[by][bx] = 1; }
        for (int y = 19; y >= 0; y--) {
            int full = 1;
            for (int x = 0; x < 10; x++) if (!board[y][x]) { full = 0; break; }
            if (full) {
                tscore += 100;
                for (int yy = y; yy > 0; yy--) for (int x = 0; x < 10; x++) board[yy][x] = board[yy - 1][x];
                for (int x = 0; x < 10; x++) board[0][x] = 0;
                y++;
            }
        }
        for (int y = 0; y < 20; y++) for (int x = 0; x < 10; x++) {
            if (board[y][x]) { putc_at(20 + x * 2, y + 1, '|', WHITE); putc_at(21 + x * 2, y + 1, '|', WHITE); }
            else { putc_at(20 + x * 2, y + 1, ' ', BLACK); putc_at(21 + x * 2, y + 1, ' ', BLACK); }
        }
        tx = 4; ty = 0; tpiecetype = inb(0x40) % 7;
        for (int i = 0; i < 4; i++) { tpiece[i][0] = pieces[tpiecetype][i][0]; tpiece[i][1] = pieces[tpiecetype][i][1]; }
        if (tetris_hit(tx, ty, f)) { app_mode = 8; return; }
        char buf[10]; int sc = tscore, idx = 9; buf[9] = 0;
        if (sc == 0) buf[8] = '0'; else while (sc) { buf[--idx] = '0' + sc % 10; sc /= 10; }
        print_at(16, 0, buf + idx, YELLOW);
    }
    tetris_draw(0);
}

static void app_pong_init(void) {
    app_mode = 5; p1y = 9; p2y = 9; bpx = 40; bpy = 12; bdx = 1; bdy = 1; p1score = 0; p2score = 0;
    clear_screen();
    for (int x = 0; x < COLS; x++) { putc_at(x, 0, '#', GREEN); putc_at(x, ROWS - 1, '#', GREEN); }
    print_at(2, 0, "PONG | W/S=Player1 | O/L=Player2 | Q=Quit", WHITE);
    print_at(30, 0, "0", LRED); print_at(48, 0, "0", LBLUE);
    for (int i = -2; i <= 2; i++) { putc_at(2, p1y + i, '|', LRED); putc_at(77, p2y + i, '|', LBLUE); }
    putc_at(bpx, bpy, 'o', WHITE);
}

static void pong_update(void) {
    putc_at(bpx, bpy, ' ', BLACK);
    bpx += bdx; bpy += bdy;
    if (bpy <= 1) bdy = 1; if (bpy >= ROWS - 2) bdy = -1;
    if (bpx <= 4 && bpy >= p1y - 2 && bpy <= p1y + 2) bdx = 1;
    if (bpx >= 74 && bpy >= p2y - 2 && bpy <= p2y + 2) bdx = -1;
    if (bpx <= 0) { p2score++; bpx = 40; bpy = 12; bdx = 1; char buf[5]; int s = p2score, i = 4; buf[4] = 0; while (s || i == 4) { buf[--i] = '0' + s % 10; s /= 10; } print_at(48, 0, buf + i, LBLUE); }
    if (bpx >= 79) { p1score++; bpx = 40; bpy = 12; bdx = -1; char buf[5]; int s = p1score, i = 4; buf[4] = 0; while (s || i == 4) { buf[--i] = '0' + s % 10; s /= 10; } print_at(30, 0, buf + i, LRED); }
    putc_at(bpx, bpy, 'o', WHITE);
    for (int i = -3; i <= 3; i++) { putc_at(2, p1y + i, ' ', BLACK); putc_at(77, p2y + i, ' ', BLACK); }
    for (int i = -2; i <= 2; i++) { putc_at(2, p1y + i, '|', LRED); putc_at(77, p2y + i, '|', LBLUE); }
}

/* ============ COMMANDS ============ */
static void process_command(const char *c) {
    if (c[0] == 0) return;
    if (hist_count < 10) str_cpy(history[hist_count++], c);
    if (str_cmp(c, "help") == 0) {
        println("", 0);
        println("══════ COMMANDS ══════", CYAN);
        println("FXC NOTEPAD  - Text editor", WHITE);
        println("FXC GAMES    - Play games", GREEN);
        println("help         - Commands", WHITE);
        println("clear        - Clear", WHITE);
        println("about        - Info", WHITE);
        println("history      - History", WHITE);
        println("shutdown     - Off", RED);
        println("══════════════════════", CYAN);
    } else if (str_cmp(c, "clear") == 0) {
        clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN);
    } else if (str_has(c, "FXC NOTEPAD") || str_has(c, "fxc notepad")) {
        app_notepad();
    } else if (str_has(c, "FXC GAMES") || str_has(c, "fxc games")) {
        app_games_menu();
    } else if (str_cmp(c, "about") == 0) {
        println("", 0); println("══ ABOUT ══", CYAN); println("TFD OS v2.0", WHITE); println("by Sadman", YELLOW); println("Snake|Tetris|Pong", GREEN); println("═══════════", CYAN);
    } else if (str_cmp(c, "history") == 0) {
        for (int i = 0; i < hist_count; i++) { char b[5]; b[0] = ' '; if (i < 9) b[1] = '1' + i; else { b[0] = '1'; b[1] = '0' + i - 9; } b[2] = ' '; b[3] = 0; print(b, WHITE); println(history[i], LGRAY); }
    } else if (str_cmp(c, "shutdown") == 0) {
        println("Shutdown...", RED); for (int t = 3; t > 0; t--) { putc('0' + t); putc('.'); putc('.'); for (volatile int d = 0; d < 300000; d++); } println("", 0); println("Off.", GREEN); while (1) __asm__ volatile("hlt");
    } else { print("?: ", RED); println(c, RED); }
    println("", 0);
}

/* ============ MAIN ============ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic; (void)addr;
    ps2_wait_write(); outb(0x64, 0xA8);
    ps2_wait_write(); outb(0x60, 0xF0);
    ps2_wait_write(); outb(0x60, 0x01);
    hide_cursor();
    boot_menu();
    setup_wizard();
    clear_screen();
    print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN);
    println("Type 'help' | 'FXC GAMES' to play!", YELLOW);
    println("", 0); cmd_len = 0;

    while (1) {
        if (app_mode == 0) {
            print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); print("/type~", CYAN); print("):$ ", GREEN);
            show_cursor();
        }
        cmd_len = 0;
        while (1) {
            char c = read_key();
            if (app_mode == 0) {
                if (c == '\n') { putc('\n'); break; }
                else if (c == '\b') { if (cmd_len > 0) { cmd_len--; putc('\b'); } }
                else if (c >= 32 && c <= 126 && cmd_len < 255) { cmd[cmd_len++] = c; putc(c); }
            } else if (app_mode == 1) {
                if (c == 0x01) { app_mode = 0; clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN); break; }
                else if (c == '\n') { notepad_row++; notepad_col = 0; if (notepad_row >= 20) notepad_row = 0; putc('\n'); }
                else if (c == '\b') { if (notepad_col > 0) { notepad_col--; notepad_text[notepad_row][notepad_col] = ' '; putc('\b'); } }
                else if (c >= 32 && c <= 126) { if (notepad_col < 79) { notepad_text[notepad_row][notepad_col++] = c; putc(c); } }
            } else if (app_mode == 2) {
                if (c == '1') { app_snake_init(); break; }
                else if (c == '2') { app_tetris_init(); break; }
                else if (c == '3') { app_pong_init(); break; }
                else if (c == 'q' || c == 'Q') { app_mode = 0; clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN); break; }
            } else if (app_mode == 3) {
                if (c == 'w' || c == 'W') sdir = 3; else if (c == 's' || c == 'S') sdir = 1;
                else if (c == 'a' || c == 'A') sdir = 2; else if (c == 'd' || c == 'D') sdir = 0;
                else if (c == 'q' || c == 'Q') { app_mode = 0; clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN); print("Snake: ", YELLOW); char b[5]; int sc = sscore, i = 4; b[4] = 0; while (sc || i == 4) { b[--i] = '0' + sc % 10; sc /= 10; } println(b + i, WHITE); break; }
                if (!sgameover) { snake_update(); if (sgameover) { print_at(30, 12, "GAME OVER!", RED); print_at(28, 13, "Press Q to quit", WHITE); } }
                for (volatile int d = 0; d < 150000; d++);
            } else if (app_mode == 4) {
                if (c == 'a' || c == 'A') { tetris_draw(1); int f[8]; for (int i = 0; i < 4; i++) { f[i * 2] = tpiece[i][0]; f[i * 2 + 1] = tpiece[i][1]; } if (!tetris_hit(tx - 1, ty, f)) tx--; tetris_draw(0); }
                else if (c == 'd' || c == 'D') { tetris_draw(1); int f[8]; for (int i = 0; i < 4; i++) { f[i * 2] = tpiece[i][0]; f[i * 2 + 1] = tpiece[i][1]; } if (!tetris_hit(tx + 1, ty, f)) tx++; tetris_draw(0); }
                else if (c == 's' || c == 'S') tetris_update();
                else if (c == 'w' || c == 'W') { tetris_draw(1); int np[4][2]; for (int i = 0; i < 4; i++) { np[i][0] = -tpiece[i][1]; np[i][1] = tpiece[i][0]; } int f[8]; for (int i = 0; i < 4; i++) { f[i * 2] = np[i][0]; f[i * 2 + 1] = np[i][1]; } if (!tetris_hit(tx, ty, f)) for (int i = 0; i < 4; i++) { tpiece[i][0] = np[i][0]; tpiece[i][1] = np[i][1]; } tetris_draw(0); }
                else if (c == 'q' || c == 'Q') { app_mode = 0; clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN); print("Tetris: ", YELLOW); char b[5]; int sc = tscore, i = 4; b[4] = 0; while (sc || i == 4) { b[--i] = '0' + sc % 10; sc /= 10; } println(b + i, WHITE); break; }
                if (app_mode == 8) { print_at(30, 12, "GAME OVER!", RED); print_at(28, 13, "Press Q to quit", WHITE); app_mode = 4; }
                for (volatile int d = 0; d < 80000; d++);
            } else if (app_mode == 5) {
                if (c == 'w' || c == 'W') { if (p1y > 4) p1y--; } else if (c == 's' || c == 'S') { if (p1y < 17) p1y++; }
                else if (c == 'o' || c == 'O') { if (p2y > 4) p2y--; } else if (c == 'l' || c == 'L') { if (p2y < 17) p2y++; }
                else if (c == 'q' || c == 'Q') { app_mode = 0; clear_screen(); print("TFD~(", GREEN); print(username, WHITE); print("/", LGRAY); print(pcname, WHITE); println("/home):$", GREEN); print("P1:", LRED); char b[3]; int s1 = p1score; if (s1 >= 10) { b[0] = '0' + s1 / 10; b[1] = '0' + s1 % 10; b[2] = 0; print(b, WHITE); } else putc('0' + s1); print(" P2:", LBLUE); int s2 = p2score; if (s2 >= 10) { b[0] = '0' + s2 / 10; b[1] = '0' + s2 % 10; b[2] = 0; print(b, WHITE); } else putc('0' + s2); println("", 0); break; }
                pong_update();
                for (volatile int d = 0; d < 50000; d++);
            }
        }
        if (app_mode == 0) { hide_cursor(); cmd[cmd_len] = 0; process_command(cmd); }
    }
}
