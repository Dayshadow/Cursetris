#include <curses.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>


// DEFINES -------------------------------------------------------------------
#define COLOR(x, stmt) {attron(COLOR_PAIR(x)); \
stmt; \
attroff(COLOR_PAIR(x));}

#define SPAWN_ONE_IN_X 35
#define C_CHAR(x) (x & 255) // strip extra info off of chtype
#define ELMCOUNT(x) (sizeof(x) / sizeof(x[0]))

// 0-255 0-1000
#define C_RESCALE(x) ((short int)((float)x * 3.90625f))
#define SOLID(r, g, b) r, g, b, r, g, b // duplicate pairs

typedef uint8_t ColorPair_t;

// square-approximate version of the character printing functions
#define mvaddch_sq(p1, p2, p3) (mvaddch((p1), (p2) * 2, (p3)), addch((p3)))
#define addch_sq(p1) addch((p1)), addch((p1)); 
#define move_sq(y, x) move((y), (x * 2))

struct ColorSet {
    ColorPair_t I_PIECE, J_PIECE, L_PIECE, O_PIECE, T_PIECE, S_PIECE, Z_PIECE;
} GAME_COLORS;

#define GCOLOR(x, stmt) COLOR(GAME_COLORS.x, (stmt))
// END DEFINES ---------------------------------------------------------------

// FUNCTS ------------------------
uint8_t set_rgb_pair(uint8_t fr, uint8_t fb, uint8_t fg, uint8_t br, uint8_t bg, uint8_t bb);
void init_main();
void init_palette();
void close_main();
void circ_set(chtype x_cent, chtype y_cent, chtype r, char c, int pairno);
// END FUNCS --------------------


int main() {
    init_main();
    init_palette();

    move_sq(0, 0);
    GCOLOR(I_PIECE, mvaddch_sq(0, 0, ' '));
    GCOLOR(J_PIECE, mvaddch_sq(1, 0, ' '));
    GCOLOR(L_PIECE, mvaddch_sq(2, 0, ' '));
    GCOLOR(O_PIECE, mvaddch_sq(3, 0, ' '));
    GCOLOR(T_PIECE, mvaddch_sq(4, 0, ' '));
    GCOLOR(S_PIECE, mvaddch_sq(5, 0, ' '));
    GCOLOR(Z_PIECE, mvaddch_sq(6, 0, ' '));
    refresh();

    close_main();
    return 0;
}


// allocates two color slots from the global state, for fg/bg color. returns pair number
uint8_t set_rgb_pair(uint8_t fr, uint8_t fb, uint8_t fg, uint8_t br, uint8_t bg, uint8_t bb) {
    static ColorPair_t palette_back = 2; // current pair index for set function. Only really works when set to 2 initially.
    if (palette_back >= 126) {
        endwin();
        printf("Too many color pairs created!\n");
        exit(1);
    }
    int err_ret;
    // set fg/bg color to make a pair
    err_ret = init_color((short)(palette_back * 2 + 0), C_RESCALE(fr), C_RESCALE(fg), C_RESCALE(fb));
    if (err_ret == ERR) {
        endwin();
        printf("init_color for fg failed, for one reason or another. (paletteno = %d)\n", palette_back);
        abort();
    }
    err_ret = init_color((short)(palette_back * 2 + 1), C_RESCALE(br), C_RESCALE(bg), C_RESCALE(bb));
    if (err_ret == ERR) {
        endwin();
        printf("init_color for bg failed, for one reason or another. (paletteno = %d)\n", palette_back);
        abort();
    }
    err_ret = init_pair(palette_back, (short)(palette_back * 2 + 0), (short)(palette_back * 2 + 1));
    if (err_ret == ERR) {
        endwin();
        printf("init_pair failed, for one reason or another. (paletteno = %d)\n", palette_back);
        abort();
    }
    palette_back++;
    return palette_back - 1;
}

void init_main() {
    initscr();
    start_color();
    if (!can_change_color()) {
        printf("Error in terminal: Does not support custom colors.\n");
        close_main();
        exit(1);
    }

    clear();
    curs_set(0);
}

void close_main() {
    getch();
    endwin();
}

void init_palette() {
    GAME_COLORS.I_PIECE = set_rgb_pair(SOLID(0x42, 0xe6, 0xf5));
    GAME_COLORS.J_PIECE = set_rgb_pair(SOLID(0x35, 0x38, 0xcc));
    GAME_COLORS.L_PIECE = set_rgb_pair(SOLID(0xe8, 0xcf, 0x4f));
    GAME_COLORS.O_PIECE = set_rgb_pair(SOLID(0xea, 0xed, 0x15));
    GAME_COLORS.T_PIECE = set_rgb_pair(SOLID(0xa7, 0x1f, 0xe0));
    GAME_COLORS.S_PIECE = set_rgb_pair(SOLID(0x46, 0xe0, 0x1f));
    GAME_COLORS.Z_PIECE = set_rgb_pair(SOLID(0xe3, 0x22, 0x22));

}

void circ_set(chtype x_cent, chtype y_cent, chtype r, char c, int pairno) {
    int y_start = (int)y_cent - (int)r;
    int y_end = (int)y_cent + (int)r;
    int x_start = (int)x_cent - (int)r;
    int x_end = (int)x_cent + (int)r;

    for (int cy = y_start; cy <= y_end; cy++) {
        if (cy < 0) continue;
        for (int cx = x_start; cx <= x_end; cx++) {
            if (cx < 0) continue;
            int x_mov = cx - (int)x_cent;
            int y_mov = (cy - (int)y_cent) * 2; // aspect ratio
            attron(COLOR_PAIR(pairno));
            if (x_mov * x_mov + y_mov * y_mov < r * r)
                mvaddch(cy, cx, (chtype)c);
            attroff(COLOR_PAIR(pairno));

        }
    }
}