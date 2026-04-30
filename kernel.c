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

/* I/O */
#define outb(p,v) __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p))
#define inb(p) ({uint8_t r;__asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p));r;})
#define io_wait() outb(0x80,0)

/* Command buffer */
static char cmd[256];
static int cmd_len = 0;
static char history[10][256];
static int hist_count = 0;

/* Snake game */
static int snake_x[100], snake_y[100];
static int snake_len = 3;
static int snake_dir = 0; /* 0=right, 1=down, 2=left, 3=up */
static int food_x, food_y;
static int game_score = 0;
static int game_over = 0;

/* Apps */
static int notepad_open = 0;
static char notepad_text[20][80];
static int notepad_row = 0, notepad_col = 0;
static int calc_open = 0;
static int snake_open = 0;

/* ============ SCREEN ============ */
static void set_cursor(int x, int y) {
    uint16_t pos = y * COLS + x;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    cx = x; cy = y;
}

static void hide_cursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);
}

static void show_cursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
}

static void clear_screen(void) {
    for (int i = 0; i < COLS * ROWS; i++)
        vga[i] = (uint16_t)' ' | ((uint16_t)LGRAY << 8);
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
    if (c == '\n') {
        cx = 0; cy++;
        if (cy >= ROWS) { scroll_up(); cy = ROWS - 1; }
    } else if (c == '\b') {
        if (cx > 0) { cx--; vga[cy * COLS + cx] = (uint16_t)' ' | ((uint16_t)color << 8); }
    } else if (c == '\r') {
        cx = 0;
    } else {
        vga[cy * COLS + cx] = (uint16_t)c | ((uint16_t)color << 8);
        cx++;
        if (cx >= COLS) { cx = 0; cy++;
            if (cy >= ROWS) { scroll_up(); cy = ROWS - 1; } }
    }
    set_cursor(cx, cy);
}

static void print(const char *s, uint8_t col) {
    color = col;
    while (*s) putc(*s++);
}

static void println(const char *s, uint8_t col) { print(s, col); putc('\n'); }

static void putc_at(int x, int y, char c, uint8_t col) {
    if (x >= 0 && x < COLS && y >= 0 && y < ROWS)
        vga[y * COLS + x] = (uint16_t)c | ((uint16_t)col << 8);
}

static void print_at(int x, int y, const char *s, uint8_t col) {
    while (*s) putc_at(x++, y, *s++, col);
}

/* ============ STRINGS ============ */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int scmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}
static void scpy(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; }
static int contains(const char *h, const char *n) {
    int nl = slen(n);
    if (nl == 0) return 1;
    while (*h) {
        int match = 1;
        for (int i = 0; i < nl; i++) if (h[i] != n[i]) { match = 0; break; }
        if (match) return 1;
        h++;
    }
    return 0;
}

/* ============ KEYBOARD ============ */
static void ps2_wait_write(void) { while (inb(0x64) & 2) io_wait(); }
static void ps2_wait_data(void) { while (!(inb(0x64) & 1)) io_wait(); }

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
        case 0x4B: return 'L'; case 0x4D: return 'R'; case 0x48: return 'U';
        case 0x50: return 'D'; /* Arrow keys */
        default: return 0;
    }
}

static char read_key(void) {
    ps2_wait_data();
    return scancode_to_ascii(inb(0x60));
}

/* ============ APPS ============ */
static void app_notepad(void) {
    notepad_open = 1;
    notepad_row = 0; notepad_col = 0;
    for (int i = 0; i < 20; i++) for (int j = 0; j < 80; j++) notepad_text[i][j] = ' ';
    clear_screen();
    println("=== NOTEPAD ===", CYAN);
    println("Type text. Press ESC to exit.", YELLOW);
    println("------------------------------------------", CYAN);
    cy = 3;
}

static void app_calculator(void) {
    calc_open = 1;
    clear_screen();
    println("=== CALCULATOR ===", CYAN);
    println("Enter first number:", WHITE);
}

static int calc_num1 = 0, calc_num2 = 0, calc_state = 0;
static char calc_op = 0;

static void app_snake(void) {
    snake_open = 1;
    snake_len = 3;
    snake_dir = 0;
    game_score = 0;
    game_over = 0;
    snake_x[0] = 40; snake_y[0] = 12;
    snake_x[1] = 39; snake_y[1] = 12;
    snake_x[2] = 38; snake_y[2] = 12;
    /* Place food */
    food_x = (inb(0x40) % 70) + 5;
    food_y = (inb(0x40) % 18) + 3;
    clear_screen();
    /* Draw border */
    for (int x = 0; x < COLS; x++) {
        putc_at(x, 0, '#', GREEN);
        putc_at(x, ROWS - 1, '#', GREEN);
    }
    for (int y = 0; y < ROWS; y++) {
        putc_at(0, y, '#', GREEN);
        putc_at(COLS - 1, y, '#', GREEN);
    }
    print_at(2, 0, " SNAKE | Score: 0 | WASD=Move | Q=Quit ", WHITE);
    putc_at(food_x, food_y, '*', RED);
    for (int i = 0; i < snake_len; i++)
        putc_at(snake_x[i], snake_y[i], 'O', YELLOW);
}

static void update_snake(void) {
    /* Move snake */
    int new_x = snake_x[0], new_y = snake_y[0];
    if (snake_dir == 0) new_x++;
    else if (snake_dir == 1) new_y++;
    else if (snake_dir == 2) new_x--;
    else if (snake_dir == 3) new_y--;
    
    /* Check wall collision */
    if (new_x <= 0 || new_x >= COLS - 1 || new_y <= 0 || new_y >= ROWS - 1) {
        game_over = 1;
        print_at(COLS / 2 - 5, ROWS / 2, "GAME OVER!", RED);
        print_at(COLS / 2 - 8, ROWS / 2 + 1, "Press Q to quit", WHITE);
        return;
    }
    
    /* Check self collision */
    for (int i = 0; i < snake_len; i++)
        if (snake_x[i] == new_x && snake_y[i] == new_y) {
            game_over = 1;
            print_at(COLS / 2 - 5, ROWS / 2, "GAME OVER!", RED);
            print_at(COLS / 2 - 8, ROWS / 2 + 1, "Press Q to quit", WHITE);
            return;
        }
    
    /* Erase tail */
    putc_at(snake_x[snake_len - 1], snake_y[snake_len - 1], ' ', BLACK);
    /* Move body */
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = new_x;
    snake_y[0] = new_y;
    /* Draw head */
    putc_at(snake_x[0], snake_y[0], 'O', YELLOW);
    /* Check food */
    if (snake_x[0] == food_x && snake_y[0] == food_y) {
        snake_len++;
        game_score += 10;
        snake_x[snake_len - 1] = snake_x[snake_len - 2];
        snake_y[snake_len - 1] = snake_y[snake_len - 2];
        /* New food */
        food_x = ((inb(0x40) * 123 + 456) % 70) + 5;
        food_y = ((inb(0x40) * 789 + 123) % 18) + 3;
        putc_at(food_x, food_y, '*', RED);
        /* Update score */
        char score[20];
        print_at(2, 0, " SNAKE | Score:    | WASD=Move | Q=Quit ", WHITE);
        int sc = game_score;
        char buf[5]; int idx = 3;
        if (sc == 0) { buf[0] = '0'; idx = 1; }
        else { while (sc) { buf[--idx] = '0' + sc % 10; sc /= 10; } }
        print_at(17, 0, buf, YELLOW);
    }
}

/* ============ COMMANDS ============ */
static void process_command(const char *c) {
    if (c[0] == 0) return;
    
    /* Save history */
    if (hist_count < 10) scpy(history[hist_count++], c);
    
    if (scmp(c, "help") == 0) {
        println("", 0);
        println("====== COMMANDS ======", CYAN);
        println("help      - Show this", WHITE);
        println("clear     - Clear screen", WHITE);
        println("notepad   - Open text editor", WHITE);
        println("calc      - Open calculator", WHITE);
        println("snake     - Play snake game", WHITE);
        println("about     - About this OS", WHITE);
        println("reboot    - Restart system", WHITE);
        println("shutdown  - Power off", WHITE);
        println("history   - Command history", WHITE);
        println("======================", CYAN);
    } else if (scmp(c, "clear") == 0) {
        clear_screen();
        println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
        println("Type 'help' for commands", YELLOW);
    } else if (scmp(c, "notepad") == 0) {
        app_notepad();
    } else if (scmp(c, "calc") == 0) {
        app_calculator();
    } else if (scmp(c, "snake") == 0) {
        app_snake();
    } else if (scmp(c, "about") == 0) {
        println("", 0);
        println("====== ABOUT ======", CYAN);
        println("TinyFoxyDOS v2.0", WHITE);
        println("Text-Based Operating System", WHITE);
        println("With Snake Game!", GREEN);
        println("by TFD", YELLOW);
        println("===================", CYAN);
    } else if (scmp(c, "reboot") == 0) {
        println("Rebooting...", YELLOW);
        clear_screen();
        println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
        println("Type 'help' for commands", YELLOW);
    } else if (scmp(c, "shutdown") == 0) {
        println("Shutting down...", RED);
        println("Power off.", GREEN);
        while (1) __asm__ volatile("hlt");
    } else if (scmp(c, "history") == 0) {
        for (int i = 0; i < hist_count; i++) {
            char buf[10]; buf[0] = ' '; buf[1] = '0' + i + 1;
            if (i >= 9) { buf[0] = '1'; buf[1] = '0' + i - 9; }
            buf[2] = ' '; buf[3] = 0;
            print(buf, WHITE);
            println(history[i], LGRAY);
        }
    } else {
        print("Unknown command: ", RED);
        println(c, RED);
        println("Type 'help' for commands", YELLOW);
    }
    println("", 0);
}

/* ============ MAIN ============ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic; (void)addr;
    
    /* Init keyboard */
    ps2_wait_write(); outb(0x64, 0xA8);
    ps2_wait_write(); outb(0x60, 0xF0);
    ps2_wait_write(); outb(0x60, 0x01);
    
    hide_cursor();
    clear_screen();
    println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
    println("Type 'help' for commands", YELLOW);
    println("", 0);
    
    cmd_len = 0;
    
    while (1) {
        if (!notepad_open && !calc_open && !snake_open) {
            print("TFD> ", CYAN);
            show_cursor();
        }
        
        cmd_len = 0;
        while (1) {
            char c = read_key();
            
            if (!snake_open && !notepad_open && !calc_open) {
                if (c == '\n') { putc('\n'); break; }
                else if (c == '\b') {
                    if (cmd_len > 0) { cmd_len--; putc('\b'); }
                } else if (c >= 32 && c <= 126 && cmd_len < 255) {
                    cmd[cmd_len++] = c; cmd[cmd_len] = 0;
                    putc(c);
                }
            } else if (snake_open) {
                /* Snake controls */
                if (c == 'w' || c == 'W') snake_dir = 3;
                else if (c == 's' || c == 'S') snake_dir = 1;
                else if (c == 'a' || c == 'A') snake_dir = 2;
                else if (c == 'd' || c == 'D') snake_dir = 0;
                else if (c == 'q' || c == 'Q') {
                    snake_open = 0;
                    clear_screen();
                    println("Snake closed. Score: ", YELLOW);
                    char buf[5];
                    int sc = game_score, idx = 3;
                    if (sc == 0) { buf[0] = '0'; idx = 1; }
                    else { while (sc) { buf[--idx] = '0' + sc % 10; sc /= 10; } }
                    buf[3] = 0;
                    println(buf, GREEN);
                    println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
                    println("Type 'help' for commands", YELLOW);
                    break;
                }
                if (!game_over) {
                    update_snake();
                    for (volatile int d = 0; d < 200000; d++);
                }
            } else if (notepad_open) {
                if (c == 0x01) { /* ESC */
                    notepad_open = 0;
                    clear_screen();
                    println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
                    println("Type 'help' for commands", YELLOW);
                    break;
                } else if (c == '\n') {
                    notepad_row++;
                    notepad_col = 0;
                    if (notepad_row >= 20) notepad_row = 0;
                    putc('\n');
                } else if (c == '\b') {
                    if (notepad_col > 0) {
                        notepad_col--;
                        notepad_text[notepad_row][notepad_col] = ' ';
                        putc('\b');
                    }
                } else if (c >= 32 && c <= 126) {
                    if (notepad_col < 79) {
                        notepad_text[notepad_row][notepad_col++] = c;
                        notepad_text[notepad_row][notepad_col] = 0;
                        putc(c);
                    }
                }
            } else if (calc_open) {
                if (c == 0x01) { /* ESC */
                    calc_open = 0; calc_state = 0;
                    clear_screen();
                    println("TinyFoxyDOS v2.0 - Text-Based OS", GREEN);
                    println("Type 'help' for commands", YELLOW);
                    break;
                } else if (c >= '0' && c <= '9') {
                    putc(c);
                    if (calc_state == 0) calc_num1 = calc_num1 * 10 + (c - '0');
                    else calc_num2 = calc_num2 * 10 + (c - '0');
                } else if (c == '+' || c == '-' || c == '*' || c == '/') {
                    calc_op = c; calc_state = 1;
                    putc(' '); putc(c); putc(' ');
                } else if (c == '=' || c == '\n') {
                    int result = 0;
                    if (calc_op == '+') result = calc_num1 + calc_num2;
                    else if (calc_op == '-') result = calc_num1 - calc_num2;
                    else if (calc_op == '*') result = calc_num1 * calc_num2;
                    else if (calc_op == '/') result = calc_num2 ? calc_num1 / calc_num2 : 0;
                    putc('='); putc(' ');
                    char buf[12]; int idx = 10; buf[11] = 0;
                    int tmp = result;
                    if (tmp == 0) { buf[--idx] = '0'; }
                    else while (tmp) { buf[--idx] = '0' + tmp % 10; tmp /= 10; }
                    print(buf + idx, GREEN);
                    putc('\n');
                    calc_num1 = 0; calc_num2 = 0; calc_state = 0;
                }
            }
        }
        
        if (!snake_open && !notepad_open && !calc_open) {
            hide_cursor();
            cmd[cmd_len] = 0;
            process_command(cmd);
        }
    }
}
