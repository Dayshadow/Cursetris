#include <curses.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/fcntl.h>
#include <ctype.h>

// DEFINES ----------------------------------------
#define COLOR(x, stmt) {attron(COLOR_PAIR(x)); \
stmt; \
attroff(COLOR_PAIR(x));}

#define SPAWN_ONE_IN_X 35
#define C_CHAR(x) (x & 255) // strip extra info off of chtype
#define ELMCOUNT(x) (sizeof(x) / sizeof(x[0]))

// 0-255 0-1000
#define C_RESCALE(x) ((short int)((float)x * 3.90625f))
#define SOLID(r, g, b) r, g, b, r, g, b // duplicate pairs

#define FAIL(msg) { \
  endwin(); \
  printf(msg); \
  exit(1); \
} 
#define FAILF(msg, fmt...) { \
  endwin(); \
  printf(msg, fmt); \
  exit(1); \
} 
typedef uint8_t ColorPair_t;

// square-approximate version of the character printing functions
#define mvaddch_sq(p1, p2, p3) (mvaddch((p1), (p2) * 2, (p3)), addch((p3)))

#define addch_sq(p1) (addch((p1)), addch((p1)))

#define move_sq(y, x) (move((y), (x) * 2))

// END DEFINES ---------------------------------------

// STRUCTS -------------------------------------------
struct ColorSet {
    ColorPair_t DEFAULT;
    ColorPair_t I_PIECE, J_PIECE, L_PIECE, O_PIECE, T_PIECE, S_PIECE, Z_PIECE;
} GAME_COLORS;
#define GCOLOR(x, stmt) COLOR(GAME_COLORS.x, (stmt)) // version that aliases colors stored within the global struct

struct Mino {
    bool occupied;
    ColorPair_t col;
};

#define TETCOUNT 7
enum TetrominoType_t {
    INVALID, I, J, L, S, Z, O, T
};

// Represents a single rotation
struct TetrominoState {
    struct Mino state[4][4];
    uint8_t dimx, dimy;
};
// What the piece should do if attempting to rotate into an occupied cell
struct WallkickDef {
    int8_t offsets[4][2]; // Every element on the list is tried in priority order until an offset works.
};
// represents all rotations, as well as wallkicks
struct TetrominoDef {
    struct TetrominoState rotations[4];
    struct WallkickDef wallkicks[4][4];
};

struct TetrominoDef TData[TETCOUNT] = {0};


// unit for game board positions
typedef uint16_t minopos_t;

// The game board, handles most of game state
struct Matrix {
    minopos_t _nrows;
    minopos_t _ncols;

    // place where pieces start from
    minopos_t _rootX;
    minopos_t _rootY;

    minopos_t _tetX;
    minopos_t _tetY;

    // drop position location if the piece were to fall all the way
    minopos_t _hdropX;
    minopos_t _hdropY;

    uint32_t _updateFrameCounter; // Counts until it reaches updateFrameDelay, resets to zero, and updates pieces once.
    uint32_t _updateFrameDelay;

    uint32_t _lockCounter; 
    uint32_t _lockDelay; // time piece is allowed to be in contact with the floor before it sticks
    bool _pieceStopped; // if the piece is currently nudging another piece

    enum TetrominoType_t _currentPiece;

    enum TetrominoType_t _heldPiece; // tetris holding
    bool _holdAllowable;

    size_t _linesCleared;

    // actual game data
    struct Mino** _board;
};
// END STRUCTS ---------------------------------------

// FUNCTS --------------------------------------------
uint8_t set_rgb_pair(uint8_t fr, uint8_t fb, uint8_t fg, uint8_t br, uint8_t bg, uint8_t bb);
void init_main();
void init_palette();
void close_main();
void circ_set(chtype x_cent, chtype y_cent, chtype r, char c, int pairno);
enum TetrominoType_t toType(char tetromino_letter);
void parse_game_data();

// member functs ------

// create piece data
void M_matrix_make_board(struct Matrix*);

// initialize class
struct Matrix* matrix_construct() {
    struct Matrix* ret = (struct Matrix*)calloc(1, sizeof(struct Matrix));
    ret->_ncols = 10; // these could be #defines, but I feel like making it adjustable
    ret->_nrows = 24;

    ret->_rootX = 3;
    ret->_rootY = 3;

    ret->_tetX = ret->_rootX;
    ret->_tetY = ret->_rootY;

    ret->_heldPiece = INVALID;
    ret->_currentPiece = INVALID;

    ret->_hdropX = 0;
    ret->_hdropY = 0;

    ret->_updateFrameCounter = 0;
    ret->_updateFrameDelay = 40;

    ret->_lockCounter = 0;
    ret->_lockDelay = 60;

    ret->_linesCleared = 0;

    ret->_board = NULL;
    M_matrix_make_board(ret); // default size
    return ret;
}
void M_matrix_destroy_board(struct Matrix* instance) {
    if (instance->_board == NULL) return;

    for (minopos_t row = 0; row < instance->_nrows; row++) {
        free(instance->_board[row]);
    }
    free(instance->_board);
    instance->_board = NULL;
}
void M_matrix_make_board(struct Matrix* instance) {
    if (instance->_board != NULL) {
        M_matrix_destroy_board(instance);
    }

    instance->_board = (struct Mino**)calloc(instance->_nrows, sizeof(struct Mino*));
    for (minopos_t row = 0; row < instance->_nrows; row++) {
        instance->_board[row] = (struct Mino*)calloc(instance->_ncols, sizeof(struct Mino));
    }
}
// resize overload
void M_matrix_make_board_rs(struct Matrix* instance, minopos_t p_nrows, minopos_t p_ncols) {
    if (instance->_board != NULL) {
        M_matrix_destroy_board(instance);
    }
    instance->_nrows = p_nrows;
    instance->_ncols = p_ncols;



    instance->_board = (struct Mino**)calloc(p_nrows, sizeof(struct Mino*));
    for (minopos_t row = 0; row < p_nrows; row++) {
        instance->_board[row] = (struct Mino*)calloc(p_ncols, sizeof(struct Mino));
    }
}

void matrix_destruct(struct Matrix* instance) {
    M_matrix_destroy_board(instance);
    free(instance);
}
// end member functs --

// END FUNCS ----------------------------------------


int main() {
    init_main();
    init_palette();
    int testgetch = 0;

    move_sq(0, 0);

    while (true) {
        //GCOLOR(I_PIECE, addch_sq(' '));

        testgetch = getch();
        switch (testgetch) {
            case 'l':
            GCOLOR(I_PIECE, addch_sq('>'));
            break;
            case 'j':
            GCOLOR(O_PIECE, addch_sq('<'));
            break;
        }
        refresh();

    }

    close_main();

    printf("%d\n", testgetch);
    return 0;
}


// allocates two color slots from the global state, for fg/bg color. returns pair number
uint8_t set_rgb_pair(uint8_t fr, uint8_t fb, uint8_t fg, uint8_t br, uint8_t bg, uint8_t bb) {
    static ColorPair_t S_palette_back = 2; // current pair index for set function. Only really works when set to 2 initially.
    if (S_palette_back >= 126) FAIL("Too many color pairs created!\n");

    int err_ret;
    // set fg/bg color to make a pair
    err_ret = init_color((short)(S_palette_back * 2 + 0), C_RESCALE(fr), C_RESCALE(fg), C_RESCALE(fb));
    if (err_ret == ERR) FAILF("init_color for fg failed, for one reason or another. (paletteno = %d)\n", S_palette_back);
     
    err_ret = init_color((short)(S_palette_back * 2 + 1), C_RESCALE(br), C_RESCALE(bg), C_RESCALE(bb));
    if (err_ret == ERR) FAILF("init_color for bg failed, for one reason or another. (paletteno = %d)\n", S_palette_back);

    err_ret = init_pair(S_palette_back, (short)(S_palette_back * 2 + 0), (short)(S_palette_back * 2 + 1));
    if (err_ret == ERR) FAILF("init_pair failed, for one reason or another. (paletteno = %d)\n", S_palette_back);

    S_palette_back++;
    return S_palette_back - 1;
}

void init_main() {
    parse_game_data();
    initscr();
    start_color();
    if (!can_change_color()) {
        printf("Error in terminal: Does not support custom colors.\n");
        close_main();
        exit(1);
    }

    clear();
    curs_set(0);
    noecho();
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

enum TetrominoType_t toType(char tetromino_letter) {
    switch (tetromino_letter) {
        case 'I': case 'i': return I;
        case 'J': case 'j': return J;
        case 'L': case 'l': return L;
        case 'O': case 'o': return O;
        case 'T': case 't': return T;
        case 'S': case 's': return S;
        case 'Z': case 'z': return Z;
        default: return INVALID;
    }
}

void parse_game_data() {
    int rotFile = open("./rotations.dat", O_RDONLY);
    if (rotFile < 0) printf("Could not load ./rotations.dat. Make sure executable is in the same folder as the source code.\n");
    int kckFile = open("./wallkicks.dat", O_RDONLY);
    if (kckFile < 0) printf("Could not load ./wallkicks.dat. Make sure executable is in the same folder as the source code.\n");
    if (rotFile < 0 || kckFile < 0) {
        if (rotFile > 0) close(rotFile);
        if (kckFile > 0) close(kckFile);
        exit(1);
    }

    #define CHUNKSIZE 256
    char buf[CHUNKSIZE] = {0};
    ssize_t read_count = 0;
    enum TetrominoType_t currentPiece = INVALID;
    int state = 0; // state machine for parsing
    while (read_count = read(rotFile, buf, CHUNKSIZE)) {
        for (uint32_t c = 0; c < CHUNKSIZE; c++)
        switch (state) {
            case 0: // expect ':'
            break;
        }
    }

    close(rotFile);
    close(kckFile);
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