/* mouse.c - PS/2 Mouse driver */
#include <stdint.h>

extern int mouse_x, mouse_y, mouse_buttons, mouse_cycle;
extern uint8_t mouse_bytes[3];
extern int fb_width, fb_height;

void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void io_wait(void);

static void ps2_wait_write(void) { while (inb(0x64) & 2) io_wait(); }
static void ps2_wait_data(void) { while (!(inb(0x64) & 1)) io_wait(); }
static uint8_t ps2_read(void) { ps2_wait_data(); return inb(0x60); }

static void mouse_write(uint8_t d) {
    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, d);
    ps2_read();
}

void init_mouse(void) {
    /* Enable auxiliary PS/2 port */
    ps2_wait_write(); outb(0x64, 0xA8);
    /* Reset mouse */
    mouse_write(0xFF);
    ps2_read(); ps2_read(); ps2_read();
    /* Enable data reporting */
    mouse_write(0xF4);
    ps2_read();
    mouse_cycle = 0;
    mouse_x = fb_width / 2;
    mouse_y = fb_height / 2;
}

void mouse_handle_data(uint8_t d) {
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
            mouse_buttons = mouse_bytes[0] & 0x07;
            mouse_x += dx;
            mouse_y += dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= fb_width) mouse_x = fb_width - 1;
            if (mouse_y >= fb_height) mouse_y = fb_height - 1;
        }
    }
}
