#include <curses.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <signal.h>
#include <string.h>

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
#define mvaddch_sq(y, x, c) (mvaddch((y), (x) * 2, (c)), addch((c)))

#define addch_sq(p1) (addch((p1)), addch((p1)))

#define move_sq(y, x) (move((y), (x) * 2))

// END DEFINES ---------------------------------------

// STRUCTS -------------------------------------------
struct ColorSet {
    ColorPair_t DEFAULT, BG, SPAWN_ZONE, GHOST;
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

#define STATE_DIM 4
// Represents a single rotation
struct TetrominoState {
    struct Mino state[STATE_DIM][STATE_DIM];
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

#define PIECE_TO_INDEX(type) ((int)(type) - 1) // enum hack
#define INDEX_TO_PIECE(type) ((enum TetrominoType_t)(type) + 1) // enum hack
struct TetrominoDef TData[TETCOUNT] = {0};


// unit for game board positions
typedef int16_t minopos_t;

// different kinds of scoring conditions for line clears
enum ComboType_t {
    NOTHING,
    SINGLE,
    DOUBLE,
    TRIPLE,
    TETRIS,
    MINI_T_SPIN,
    T_SPIN_SINGLE,
    T_SPIN_DOUBLE,
    T_SPIN_TRIPLE,
    B2B,
    // some fun ones
    I_SPIN,
    J_SPIN,
    L_SPIN,
    S_SPIN,
    Z_SPIN
};
const char* combo_to_name(enum ComboType_t combo) {
    static const char* names[] = {
        "None",
        "Single",
        "Double",
        "Triple",
        "- Tetris -",
        "Mini T-Spin",
        "T-Spin Single",
        "- T-Spin Double -",
        "-| T-Spin Triple |-",
        "Back-To-Back",
        "I-Spin",
        "J-Spin",
        "S-Spin",
        "Z-Spin"
    };
    return names[(int)combo];
}
// The game board, handles most of game state
struct Matrix_s {
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
    bool _hdropQueued;

    uint32_t _updateFrameCounter; // Counts until it reaches updateFrameDelay, resets to zero, and updates pieces once.
    uint32_t _updateFrameDelay;

    uint32_t _lockCounter; 
    uint32_t _lockDelay; // time piece is allowed to be in contact with the floor before it sticks
    bool _pieceStopped; // if the piece is currently nudging another piece

    enum TetrominoType_t _currentPiece;
    struct TetrominoDef* _currentPieceData; // should be updated at the same time as the piece
    uint8_t _currentRot;

    enum TetrominoType_t _heldPiece; // tetris holding
    bool _holdAllowable;

    uint16_t _gravity; // amount to fall each step, only matters once the update counter is at its fastest
    size_t _linesCleared;
    size_t _points;
    size_t _lastPoints;
    size_t _b2b;
    enum ComboType_t _lastCombo;
    enum TetrominoType_t _lastScoringPiece;

    // actual game data
    struct Mino** _board;
};
typedef struct Matrix_s Matrix;
// END STRUCTS ---------------------------------------

// FUNCTS --------------------------------------------
uint8_t set_rgb_pair(uint8_t fr, uint8_t fb, uint8_t fg, uint8_t br, uint8_t bg, uint8_t bb);
void init_main();
void init_palette();
void close_main();
void circ_set(chtype x_cent, chtype y_cent, chtype r, char c, int pairno);
void draw_text_centered(int x_cent, int y_cent, const char* str);
enum TetrominoType_t toType(char tetromino_letter);
void parse_kicks_file();
void parse_rotations_file();
void parse_game_data();
ColorPair_t toPieceColor(enum TetrominoType_t piece);
// member functs ------

// private
void M_matrix_make_board(Matrix*);
void M_matrix_destroy_board(Matrix*);
void M_matrix_make_board_rs(Matrix*, minopos_t, minopos_t);
bool M_matrix_test_tet(Matrix*);
bool M_matrix_paste_tet(Matrix*);
void M_matrix_unpaste_tet(Matrix*);
void M_matrix_set_hdrop_pos(Matrix*);
bool M_matrix_test_if_stuck(Matrix*);
void M_matrix_lock(Matrix*);
void M_matrix_hdrop(Matrix*);
bool M_matrix_wallkick(Matrix*, uint8_t, uint8_t);
enum ComboType_t M_matrix_check_combo_type(Matrix*, bool, uint16_t, enum TetrominoType_t);
size_t M_matrix_add_score(Matrix* this, enum ComboType_t current_combo);

// public
Matrix* matrix_construct();
void matrix_destruct(Matrix*);
void matrix_set_current_piece(Matrix*, enum TetrominoType_t, uint8_t);
void matrix_hdrop(Matrix*);
bool matrix_respawn_tet(Matrix*);
bool matrix_respawn_tet_random(Matrix*);
bool matrix_rotate_piece(Matrix*, int8_t);
bool matrix_slide_piece(Matrix*, int8_t);
uint16_t matrix_test_lines(Matrix*);
bool matrix_apply_gravity(Matrix*);
bool matrix_hold_piece(Matrix*);
void matrix_update(Matrix*);
void matrix_draw(Matrix*);

// end member functs --

// END FUNCS ----------------------------------------

static bool running_flag = true;
int main() {


    init_main();
    init_palette();
    parse_game_data();
    Matrix* mat = matrix_construct();

    matrix_respawn_tet_random(mat);

    int c = 0;
    while (running_flag) {
        int scry, scrx;

        getmaxyx(stdscr, scry, scrx);
        for (int y = 0; y < scry; y++) {
            for (int x = 0; x < scrx; x++) {
                GCOLOR(DEFAULT, mvaddch(y, x, ' ')); // clear() doesn't work how I want it to
            }
        }

        switch (c) {
            case 'x': case 'i':
                matrix_rotate_piece(mat, 1);
            break;
            case 'z':
                matrix_rotate_piece(mat, -1);
            break;
            case 'k':
                matrix_apply_gravity(mat);
            break;
            case 'j':
                matrix_slide_piece(mat, -1);
            break;
            case 'l':
                matrix_slide_piece(mat, 1);
            break;
            case ' ':
                matrix_hdrop(mat);
            break;
            case 'c':
                matrix_hold_piece(mat);
            break;
        }
        matrix_update(mat);
        matrix_draw(mat);
        refresh();

        c = getch();

        usleep(10000);
    }
    matrix_destruct(mat);
    close_main();
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

void stop_game() {
    running_flag = false;
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
    noecho();
    signal(SIGINT, stop_game);
    cbreak();
    nodelay(stdscr, true);
}

void close_main() {
    endwin();
}

void init_palette() {
    GAME_COLORS.DEFAULT = set_rgb_pair(0xff, 0xff, 0xff, 0, 0, 0);
    GAME_COLORS.I_PIECE = set_rgb_pair(SOLID(0x42, 0xe6, 0xf5));
    GAME_COLORS.J_PIECE = set_rgb_pair(SOLID(0x35, 0x38, 0xcc));
    GAME_COLORS.L_PIECE = set_rgb_pair(SOLID(0xe8, 0xcf, 0x4f));
    GAME_COLORS.O_PIECE = set_rgb_pair(SOLID(0xea, 0xed, 0x15));
    GAME_COLORS.T_PIECE = set_rgb_pair(SOLID(0xa7, 0x1f, 0xe0));
    GAME_COLORS.S_PIECE = set_rgb_pair(SOLID(0x46, 0xe0, 0x1f));
    GAME_COLORS.Z_PIECE = set_rgb_pair(SOLID(0xe3, 0x22, 0x22));
    GAME_COLORS.BG = set_rgb_pair(SOLID(0x22, 0x22, 0x22));
    GAME_COLORS.SPAWN_ZONE = set_rgb_pair(SOLID(0x11, 0x22, 0x11));
    GAME_COLORS.GHOST = set_rgb_pair(0xcc, 0xcc, 0xcc, 0x27, 0x27, 0x27);
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

ColorPair_t toPieceColor(enum TetrominoType_t piece) {
    switch (piece) {
        case I: return GAME_COLORS.I_PIECE;
        case J: return GAME_COLORS.J_PIECE;
        case L: return GAME_COLORS.L_PIECE;
        case O: return GAME_COLORS.O_PIECE;
        case T: return GAME_COLORS.T_PIECE;
        case S: return GAME_COLORS.S_PIECE;
        case Z: return GAME_COLORS.Z_PIECE;
        default: return GAME_COLORS.DEFAULT;
    }
}

// parsing defines
#define CHUNKSIZE 256
// same as accept, but doesn't test for character/newline
#define SET_STATE(next_state) { \
    state = next_state; \
    break; }
// ACCEPT is a soft accept, it will not break upon invalid entry.
#define ACCEPT(to_accept, next_state) if (buf[c] == to_accept) { \
    if (to_accept == '\n') ++lineno; \
    state = next_state; \
    break; }
#define DECLINE_IF(expr, filename) if ((expr)) FAILF("main.c(%d): Unexpected character %c in %s(%ld)\n", __LINE__, buf[c], filename, lineno)
#define DECLINE(filename) FAILF("main.c(%d): Unexpected character %c in %s(%ld)\n", __LINE__, buf[c], filename, lineno)
#define SKIP_WHITESPACE() if (isspace(buf[c]) && buf[c] != '\n') break

void parse_rotations_file() {
    int rotFile = open("./rotations.dat", O_RDONLY);
    if (rotFile < 0) FAIL("Could not load ./rotations.dat. Make sure executable is in the same folder as the source code.\n");

    char buf[CHUNKSIZE] = {0};
    ssize_t read_count = 0;
    size_t lineno = 1;
    enum TetrominoType_t currentPiece = INVALID;

    // for reading in piece data
    int curX = 0, curY = 0;
    size_t rotCounter = 0;

    int state = 0; // state machine for parsing
    while ((read_count = read(rotFile, buf, CHUNKSIZE))) {
        for (uint32_t c = 0; c < read_count; c++) {
            switch (state) {
                case 0: // expect ':'
                    SKIP_WHITESPACE();
                    ACCEPT('\n', 0); // newline = stay in state 0
                    ACCEPT(':', 1);
                    DECLINE("rotations.dat"); // fallthrough
                break;
                case 1: // expect a piece name
                    SKIP_WHITESPACE();
                    if (currentPiece != INVALID) {
                        DECLINE_IF(buf[c] != '\n', "rotations.dat"); // accept only one
                        ACCEPT('\n', 2);
                    }
                    currentPiece = toType(buf[c]);
                    if (currentPiece == INVALID)
                        FAILF("Unexpected piece type provided in rotations.dat(%ld): %c\n", lineno, buf[c]);
                    // accept valid piece
                break;
                case 2: // parse piece data 
                    SKIP_WHITESPACE();
                    ACCEPT('$', 99);
                    if (buf[c] == ':') {
                        curX = 0;
                        curY = 0;
                        rotCounter = 0;
                        currentPiece = INVALID;
                        SET_STATE(1); // reset state if piece data done
                    }
                    if (buf[c] == '\n') {
                        ++curY;
                        curX = 0;
                        DECLINE_IF(curY > STATE_DIM, "rotations.dat"); // out of range
                        ACCEPT('\n', 2);
                    }
                    if (buf[c] == '>') {
                        curY = -1; // workaround
                        rotCounter++;
                        SET_STATE(2);
                    }
                    DECLINE_IF(!(buf[c] == '0' || buf[c] == '1'), "rotation.dat");
                    struct TetrominoState* currentRots = TData[PIECE_TO_INDEX(currentPiece)].rotations;
                    if (buf[c] == '0') {
                        currentRots[rotCounter].state[curY][curX].occupied = false;
                        currentRots[rotCounter].state[curY][curX].col = GAME_COLORS.DEFAULT;
                        ++curX; // goes one past the last character, normally
                        SET_STATE(2);
                        DECLINE_IF(curX > STATE_DIM, "rotation.dat"); // out of range
                    }
                    if (buf[c] == '1') {
                        currentRots[rotCounter].state[curY][curX].occupied = true;
                        currentRots[rotCounter].state[curY][curX].col = toPieceColor(currentPiece);
                        ++curX; // goes one past the last character normally
                        SET_STATE(2);
                        DECLINE_IF(curX > STATE_DIM, "rotation.dat"); // out of range
                    }
                break;
                default: break;
            }
        }
    }


}

void parse_kicks_file() {
    int kckFile = open("./wallkicks.dat", O_RDONLY);
    if (kckFile < 0) FAIL("Could not load ./wallkicks.dat. Make sure executable is in the same folder as the source code.\n");
    
    char buf[CHUNKSIZE] = {0};
    ssize_t read_count = 0;
    size_t lineno = 1;
    enum TetrominoType_t currentPiece = INVALID;

    int start_rot = -1, end_rot = -1;

    int offset_row = 0;
    int offset_col = 0; // for parsing pairs
    int digits_read = 0; // for error checking
    bool negate = false;

    int state = 0; // state machine for parsing
    while ((read_count = read(kckFile, buf, CHUNKSIZE))) {
        for (uint32_t c = 0; c < read_count; c++) {
            switch (state) {
                case 0: // expect ':'
                    SKIP_WHITESPACE();
                    ACCEPT('\n', 0); // newline = stay in state 0
                    ACCEPT(':', 1);
                    DECLINE("wallkicks.dat"); // fallthrough
                break;
                case 1: // expect a piece name
                    SKIP_WHITESPACE();
                    if (currentPiece != INVALID) {
                        DECLINE_IF(buf[c] != '\n', "wallkicks.dat"); // accept only one
                        ACCEPT('\n', 2);
                    }
                    currentPiece = toType(buf[c]);
                    if (currentPiece == INVALID)
                        FAILF("Unexpected piece type provided in wallkicks.dat(%ld): %c\n", lineno, buf[c])
                    // accept valid piece
                break;
                case 2: // expect #
                    ACCEPT('#', 3);
                    DECLINE("wallkicks.dat");
                break;
                case 3: // parse starting rotation state
                    if (isdigit(buf[c])) {
                        start_rot = buf[c] - '0';
                        SET_STATE(4);
                    }
                    DECLINE("wallkicks.dat");
                break;
                case 4: // parse ending rotation state
                    if (isdigit(buf[c])) {
                        end_rot = buf[c] - '0';
                        SET_STATE(5);
                    }
                    DECLINE("wallkicks.dat");
                break;
                case 5: // expect newline after state definition
                    SKIP_WHITESPACE();
                    ACCEPT('\n', 6);
                break;
                case 6: // parse offset pairs
                    SKIP_WHITESPACE();
                    if (buf[c] == '#' || buf[c] == ':') {
                        offset_row = 0;
                        offset_col = 0;
                        negate = false;
                        start_rot = -1;
                        end_rot = -1;
                    }
                    ACCEPT('$', 99);
                    ACCEPT('#', 3);
                    if (buf[c] == ':') currentPiece = INVALID;
                    ACCEPT(':', 1);
                    if (buf[c] == '\n') {
                        ++offset_row;
                        offset_col = 0;
                        digits_read = 0;
                        DECLINE_IF(offset_row > 4, "wallkicks.dat");
                        ACCEPT('\n', 6);
                    }
                    if (buf[c] == '-') {
                        negate = true;
                        SET_STATE(6); // stay in state
                    }
                    if (buf[c] == ',') {
                        negate = false; // reset
                        ++offset_col;
                        DECLINE_IF(offset_col > 1, "wallkicks.dat");
                        SET_STATE(6);
                    }
                    if (isdigit(buf[c]) && digits_read < 2) { // accept digit
                        // long line, but sets the wall kick data of the current piece in TData based on the parse file
                        TData[PIECE_TO_INDEX(currentPiece)].wallkicks[start_rot][end_rot].offsets[offset_row][offset_col] = (int8_t)((negate? -1 : 1) * (buf[c] - '0'));
                        ++digits_read;
                        SET_STATE(6); // stay in state
                    }
                    DECLINE("wallkicks.dat");
                break;
                default: break;
            }
        }
    }
    
    close(kckFile);
}

void parse_game_data() {
    parse_rotations_file();
    parse_kicks_file();
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

void draw_text_centered(int x_cent, int y_cent, const char* str) {
    size_t len = strlen(str);
    int x_start = x_cent - (int)len / 2;
    mvaddstr(y_cent, x_start, str);
}

// initialize class
Matrix* matrix_construct() {
    Matrix* ret = (Matrix*)calloc(1, sizeof(Matrix));
    ret->_ncols = 10; // these could be #defines, but I feel like making it adjustable
    ret->_nrows = 24;

    ret->_rootX = 3;
    ret->_rootY = 3;

    ret->_tetX = ret->_rootX;
    ret->_tetY = ret->_rootY;

    ret->_heldPiece = INVALID;
    ret->_currentPiece = INVALID;
    ret->_currentPieceData = NULL;

    ret->_currentRot = 0;

    ret->_holdAllowable = true;
    ret->_pieceStopped = false;

    ret->_hdropX = 0;
    ret->_hdropY = 0;
    ret->_hdropQueued = false;

    ret->_updateFrameCounter = 0;
    ret->_updateFrameDelay = 40;

    ret->_lockCounter = 0;
    ret->_lockDelay = 2;

    ret->_gravity = 1;
    ret->_linesCleared = 0;
    ret->_points = 0;
    ret->_lastPoints = 0;
    ret->_b2b = 0;
    ret->_lastCombo = NOTHING;
    ret->_lastScoringPiece = INVALID;

    ret->_board = NULL;
    M_matrix_make_board(ret); // default size
    return ret;
}

void M_matrix_destroy_board(Matrix* this) {
    if (this->_board == NULL) return;

    for (minopos_t row = 0; row < this->_nrows; row++) {
        free(this->_board[row]);
    }
    free(this->_board);
    this->_board = NULL;
}

void M_matrix_make_board(Matrix* this) {
    if (this->_board != NULL) {
        M_matrix_destroy_board(this);
    }

    this->_board = (struct Mino**)calloc((size_t)this->_nrows, sizeof(struct Mino*));
    for (minopos_t row = 0; row < this->_nrows; row++) {
        this->_board[row] = (struct Mino*)calloc((size_t)this->_ncols, sizeof(struct Mino));
    }
}
// resize overload (unused)
void M_matrix_make_board_rs(Matrix* this, minopos_t p_nrows, minopos_t p_ncols) {
    if (this->_board != NULL) {
        M_matrix_destroy_board(this);
    }
    if (p_ncols < 1 || p_nrows < 1) FAIL("Invalid board size, negative or zero.\n");
    this->_nrows = p_nrows;
    this->_ncols = p_ncols;

    this->_board = (struct Mino**)calloc((size_t)p_nrows, sizeof(struct Mino*));
    for (minopos_t row = 0; row < p_nrows; row++) {
        this->_board[row] = (struct Mino*)calloc((size_t)p_ncols, sizeof(struct Mino));
    }
}

// returns true or false depending on whether or not the current tetromino can fit where it is
bool M_matrix_test_tet(Matrix* this) {
    // at least one dim is out of bounds
    bool OOBXflag = false;
    bool OOBYflag = false;
    for (minopos_t y = this->_tetY; y < this->_tetY + STATE_DIM; y++) {
        if (y < 0 || y >= this->_nrows) OOBYflag = true;

        for (minopos_t x = this->_tetX; x < this->_tetX + STATE_DIM; x++) {
            if (x < 0 || x >= this->_ncols) OOBXflag = true;

            struct TetrominoDef* dat = &TData[PIECE_TO_INDEX(this->_currentPiece)];
            struct Mino* currentCell = &dat->rotations[this->_currentRot].state[y - this->_tetY][x - this->_tetX];
            if (currentCell->occupied) {
                if (OOBXflag || OOBYflag) return false; // piece failed to paste due to OOB
                if (this->_board[y][x].occupied) return false; // piece failed due to occupied position
            }
            OOBXflag = false;
        }
        OOBYflag = false;
    }
    return true;
}
// pastes the current tetromino into the matrix
bool M_matrix_paste_tet(Matrix* this) {
    if (this->_currentPiece == INVALID) FAIL("Invalid game action! Attempted to paste an empty piece.\n");

    if (!M_matrix_test_tet(this)) return false;

    for (int y = this->_tetY; y < this->_tetY + STATE_DIM; y++) {
        if (y < 0 || y >= this->_nrows) continue;
        for (int x = this->_tetX; x < this->_tetX + STATE_DIM; x++) {
            if (x < 0 || x >= this->_ncols) continue;
            struct Mino* currentCell = &TData[PIECE_TO_INDEX(this->_currentPiece)].rotations[this->_currentRot].state[y - this->_tetY][x - this->_tetX];
            if (currentCell->occupied)
                this->_board[y][x] = *currentCell; // no checks failed, add to board
        }
    }

    return true;
}

void M_matrix_unpaste_tet(Matrix* this) {
    if (this->_currentPiece == INVALID) FAIL("Invalid game action! Attempted to unpaste an empty piece.\n");

    for (int y = this->_tetY; y < this->_tetY + STATE_DIM; y++) {
        if (y < 0 || y >= this->_nrows) continue;

        for (int x = this->_tetX; x < this->_tetX + STATE_DIM; x++) {
            if (x < 0 || x >= this->_ncols) continue;
            struct TetrominoDef* dat = &TData[PIECE_TO_INDEX(this->_currentPiece)];
            struct Mino* currentCell = &dat->rotations[this->_currentRot].state[y - this->_tetY][x - this->_tetX];
            if (currentCell->occupied) {
                this->_board[y][x].occupied = false; // remove mino
                this->_board[y][x].col = GAME_COLORS.DEFAULT;
            }

        }
        
    }
}

// for dropping the piece instantly with space, as well as the preview
void M_matrix_set_hdrop_pos(Matrix* this) {
    M_matrix_unpaste_tet(this);
    minopos_t start_y = this->_tetY;

    minopos_t max_itr = this->_nrows;
    for (minopos_t i = 0; i < max_itr; i++) {
        this->_tetY++;
        if (!M_matrix_test_tet(this))
            break;
    }
    this->_hdropX = this->_tetX;
    this->_hdropY = this->_tetY - 1;

    this->_tetY = start_y;
    M_matrix_paste_tet(this);
}

// for spins
bool M_matrix_test_if_stuck(Matrix* this) {
    M_matrix_unpaste_tet(this);
    bool flag = false;
    this->_tetX += 1; // check right
    flag = flag || M_matrix_test_tet(this);
    this->_tetX -= 2; // check left
    flag = flag || M_matrix_test_tet(this);
    this->_tetX += 1; // check top
    this->_tetY -= 1;
    flag = flag || M_matrix_test_tet(this);
    this->_tetY += 2; // check bottom
    flag = flag || M_matrix_test_tet(this);
    this->_tetY -= 1;
    M_matrix_paste_tet(this);
    return !flag;
}

enum ComboType_t M_matrix_check_combo_type(Matrix* this, bool is_stuck, uint16_t lines_cleared, enum TetrominoType_t locked_piece) {
    if (is_stuck) {
        switch (locked_piece) { // O should never be able to be locked
            case T:
                switch (lines_cleared) {
                    case 0: return MINI_T_SPIN;
                    case 1: return T_SPIN_SINGLE;
                    case 2: return T_SPIN_DOUBLE;
                    case 3: return T_SPIN_TRIPLE;
                    default: return NOTHING;
                }
            break;
            case I: return I_SPIN;
            case J: return J_SPIN;
            case L: return L_SPIN;
            case S: return S_SPIN;
            case Z: return Z_SPIN;
            default: return NOTHING;
        }
    } else {
        switch (locked_piece) {
            case I: case J: case L: case S: case Z: case O: case T:
            switch (lines_cleared) {
                case 0: return NOTHING;
                case 1: return SINGLE;
                case 2: return DOUBLE;
                case 3: return TRIPLE;
                case 4: 
                    if (this->_lastCombo == TETRIS || this->_lastCombo == B2B)
                        return B2B;
                    else
                        return TETRIS;
                default: return NOTHING;
            }
            default: return NOTHING;
        }
    }

}

size_t M_matrix_add_score(Matrix* this, enum ComboType_t current_combo) {
    size_t score_to_add;
    switch (current_combo) {
        case NOTHING: score_to_add = 0; break;
        case SINGLE: score_to_add = 100; break;
        case DOUBLE: score_to_add = 300; break;
        case TRIPLE: score_to_add = 500; break;
        case TETRIS: score_to_add = 800; break;
        case MINI_T_SPIN: score_to_add = 100; break;
        case T_SPIN_SINGLE: score_to_add = 800; break;
        case T_SPIN_DOUBLE: score_to_add = 1200;
            if (this->_lastCombo == B2B || this->_lastCombo == T_SPIN_DOUBLE || this->_lastCombo == T_SPIN_TRIPLE)
                score_to_add += 600; // bonus for chaining hard moves
        break;
        case T_SPIN_TRIPLE: 
            score_to_add = 1600;
            if (this->_lastCombo == B2B || this->_lastCombo == T_SPIN_DOUBLE || this->_lastCombo == T_SPIN_TRIPLE)
                score_to_add += 800; // bonus for chaining hard moves
        break;
        case B2B: 
            switch (this->_lastScoringPiece) {
                case I:
                    score_to_add = 1200;
                break;
                case T:
                    score_to_add = 1800;
                break;
                default: score_to_add = 0;
            }
        break;
        // some fun ones
        case I_SPIN: score_to_add = 300; break;
        case J_SPIN: score_to_add = 300; break;
        case L_SPIN: score_to_add = 300; break;
        case S_SPIN: score_to_add = 300; break;
        case Z_SPIN: score_to_add = 300; break;
    }

    if (current_combo == B2B || current_combo == T_SPIN_DOUBLE || current_combo == T_SPIN_TRIPLE) {
        this->_b2b++;
    } else {
        this->_b2b = 0;
    }
    this->_points += score_to_add;
    return score_to_add;
}
void matrix_set_current_piece(Matrix* this, enum TetrominoType_t kind, uint8_t rot_index) {
    this->_currentPiece = kind;
    this->_currentRot = rot_index % 4;
    this->_currentPieceData = &TData[PIECE_TO_INDEX(kind)];
}

void matrix_hdrop(Matrix* this) {
    this->_hdropQueued = true;
}

void M_matrix_hdrop(Matrix* this) {
    if (!this->_hdropQueued) return;
    this->_hdropQueued = false;
    M_matrix_unpaste_tet(this);
    this->_tetX = this->_hdropX;
    this->_tetY = this->_hdropY;
    M_matrix_paste_tet(this);
    M_matrix_lock(this);
}
// 7bag tetris
static bool bag[TETCOUNT] = {0};
static uint16_t picked_count = 0;
enum TetrominoType_t bag_pick() {
    if (picked_count == TETCOUNT) { // reset bag
        for (uint16_t i = 0; i < TETCOUNT; i++) bag[i] = false;
        picked_count = 0;
    }
    uint16_t chosen;
    while (true) { // asymptotic 
        chosen = (uint16_t)(rand() % TETCOUNT);
        if (bag[chosen]) continue;
        picked_count++;
        bag[chosen] = true;
        return INDEX_TO_PIECE(chosen);
        break;
    }
}

bool matrix_respawn_tet_random(Matrix* this) {
    matrix_set_current_piece(this, bag_pick(), 0);
    this->_tetX = this->_rootX;
    this->_tetY = this->_rootY;
    this->_lockCounter = 0;
    this->_updateFrameCounter = 0;
    this->_holdAllowable = true;

    return M_matrix_paste_tet(this);
}

bool matrix_respawn_tet(Matrix* this) {
    this->_tetX = this->_rootX;
    this->_tetY = this->_rootY;
    this->_lockCounter = 0;
    this->_updateFrameCounter = 0;
    this->_holdAllowable = true;

    return M_matrix_paste_tet(this);
}

// assume tet is already unpasted, to handle all pasting
bool M_matrix_wallkick(Matrix* this, uint8_t start_rot, uint8_t end_rot) {
    int startX = (int)this->_tetX;
    int startY = (int)this->_tetY;

    // retrieve kick data for current inital-final rotation states
    struct WallkickDef* kickSubject = &this->_currentPieceData->wallkicks[start_rot][end_rot];

    // go through each offset one by one, checking each xy pair
    for (uint8_t kick_index = 0; kick_index < 4; kick_index++) {
        int offX = startX + kickSubject->offsets[kick_index][0];
        int offY = startY - kickSubject->offsets[kick_index][1];
        if (offX < 0 || offX >= this->_ncols) continue;
        if (offY < 0 || offY >= this->_nrows) continue;
        this->_tetX = (minopos_t)offX;
        this->_tetY = (minopos_t)offY;
        if (M_matrix_paste_tet(this)) return true;
    }
    this->_tetX = (minopos_t)startX;
    this->_tetY = (minopos_t)startY;
    return false;
}

// assume pasted, dir = -1 for ccw, dir = 1 for cw
bool matrix_rotate_piece(Matrix* this, int8_t dir) {
    M_matrix_unpaste_tet(this);
    // clamp range
    if (dir > -1) dir = 1;
    if (dir < 0) dir = -1;

    uint8_t start_rot = this->_currentRot;
    // loop rotation with modulo
    this->_currentRot = (uint8_t)((uint8_t)(this->_currentRot + 4u) + dir) % 4u;
    uint8_t end_rot = this->_currentRot;

    if (M_matrix_paste_tet(this)) {
        // success, no need to do any kicks
        return true;
    } else {
        // fail, attempt to shift the piece around
        bool attempt = M_matrix_wallkick(this, start_rot, end_rot);
        if (!attempt) { // failed to wallkick
            this->_currentRot = start_rot;
            M_matrix_paste_tet(this);
        } else
            return true;
    }

    return false;
}

bool matrix_slide_piece(Matrix* this, int8_t shift) {
    M_matrix_unpaste_tet(this);
    this->_tetX += (minopos_t)shift;
    if (M_matrix_paste_tet(this)) {
        return true;
    } else {
        this->_tetX -= (minopos_t)shift;
        M_matrix_paste_tet(this);
        return false;
    }
}

uint16_t matrix_test_lines(Matrix* this) {

    struct Mino** next_board = (struct Mino**)calloc((size_t)this->_nrows, sizeof(struct Mino*));
    for (minopos_t row = 0; row < this->_nrows; row++) {
        next_board[row] = (struct Mino*)calloc((size_t)this->_ncols, sizeof(struct Mino));
    }
    uint16_t lines_cleared = 0;
    uint16_t lines_not_cleared = 0;
    for (minopos_t y = this->_nrows - 1; y >= 0; y--) {
        bool line_flag = true;
        for (minopos_t x = 0; x < this->_ncols; x++) {
            if (!this->_board[y][x].occupied) {
                line_flag = false;
                break;
            }
        }
        if (!line_flag) {
            for (minopos_t x = 0; x < this->_ncols; x++) {
                next_board[this->_nrows - 1 - lines_not_cleared][x] = this->_board[y][x];
            }
            lines_not_cleared++;
        } else {
            lines_cleared++;
        }
    }

    // swap active board
    M_matrix_destroy_board(this);
    this->_board = next_board;

    return lines_cleared;
}

bool matrix_apply_gravity(Matrix* this) {
    // attempt to move down, true if succeed, false if stuck
    for (uint16_t step = 0; step < this->_gravity; step++) {
        M_matrix_unpaste_tet(this);
        this->_tetY++;
        if (!M_matrix_paste_tet(this)) {
            this->_tetY--;
            M_matrix_paste_tet(this);
            return false;
        }
    }
    return true;
}

// return "false" is for failure to hold
bool matrix_hold_piece(Matrix* this) {
    if (!this->_holdAllowable) return false;

    M_matrix_unpaste_tet(this);
    
    if (this->_heldPiece == INVALID) {
        this->_heldPiece = this->_currentPiece;
        bool ret =  matrix_respawn_tet_random(this);
        this->_holdAllowable = false; // stop from holding twice in a row
        return ret;
    } else {
        enum TetrominoType_t next = this->_heldPiece;
        this->_heldPiece = this->_currentPiece;
        this->_currentPiece = next;
        bool ret = matrix_respawn_tet(this);
        this->_holdAllowable = false; // stop from holding twice in a row
        return ret;
    }
    
}
// solidifies the current piece
void M_matrix_lock(Matrix* this) {
    M_matrix_unpaste_tet(this);
    this->_tetY++;
    // don't lock if piece can still fall
    if (M_matrix_test_tet(this)) {
        this->_tetY--;
        M_matrix_paste_tet(this);
        return;
    }
    this->_tetY--;

    bool is_stuck = M_matrix_test_if_stuck(this);

    M_matrix_paste_tet(this);

    enum TetrominoType_t last_dropped = this->_currentPiece;
    uint16_t lines_cleared = matrix_test_lines(this);
    this->_linesCleared += lines_cleared;

    enum ComboType_t current_combo = M_matrix_check_combo_type(this, is_stuck, lines_cleared, last_dropped);

    if (current_combo != NOTHING) {
        this->_lastScoringPiece = last_dropped;
        this->_lastPoints = M_matrix_add_score(this, current_combo);
        this->_lastCombo = current_combo;
    }


    matrix_respawn_tet_random(this);
}

void matrix_update(Matrix* this) {
    this->_updateFrameCounter = (this->_updateFrameCounter + 1) % this->_updateFrameDelay;
    M_matrix_set_hdrop_pos(this);
    M_matrix_hdrop(this);
    if (this->_updateFrameCounter == 0) {
        if (this->_lockCounter > this->_lockDelay) {
            if (matrix_apply_gravity(this)) this->_lockCounter -= 1; // quick fix
            M_matrix_lock(this);
        }
        if (!matrix_apply_gravity(this)) {
            this->_lockCounter++;
        }
    }

}

void matrix_draw(Matrix* this) {
    int winx, winy;
    getmaxyx(stdscr, winy, winx);
    winx /= 2;
    
    int startx = (winx / 2) - (this->_ncols / 2);
    int starty = (winy / 2) - (this->_nrows / 2);


    for (int y = starty; y < this->_nrows + starty; y++) {
        if (y < 0 || y > winy - 3) {
            GCOLOR(DEFAULT, mvaddstr(0, winx, "^ Make window taller! ^"));
            GCOLOR(DEFAULT, mvaddstr(winy - 1, winx, "v Make window taller! v"));
            continue;
        }
        for (int x = startx; x < this->_ncols + startx; x++) {
            if (x < 0 || x > winx - 3) {
                GCOLOR(DEFAULT, mvaddstr(winy / 2, 0, "<- Make window wider! ->"));
                continue;
            }
            if (y >= starty + STATE_DIM + this->_rootY) {
                GCOLOR(BG, mvaddch_sq(y, x, ' '));
            } else {
                GCOLOR(SPAWN_ZONE, mvaddch_sq(y, x, ' '));
            }
            // draw drop ghost
            minopos_t ghost_local_x = (minopos_t)(x - startx - this->_hdropX);
            minopos_t ghost_local_y = (minopos_t)(y - starty - this->_hdropY);
            if (ghost_local_x >= 0 && ghost_local_x < STATE_DIM && ghost_local_y >= 0 && ghost_local_y < STATE_DIM) {
                struct TetrominoDef* dat = &TData[PIECE_TO_INDEX(this->_currentPiece)];
                struct Mino* gmino = &dat->rotations[this->_currentRot].state[ghost_local_y][ghost_local_x];
                if (gmino->occupied) {
                    GCOLOR(GHOST, mvaddch_sq(y, x, '#'));
                }
            }
            struct Mino* mino = &this->_board[y - starty][x - startx];
            if (mino->occupied)
                COLOR(mino->col, mvaddch_sq(y, x, ' '));

        }
    }

    // draw held piece
    for (int y = starty; y < starty + STATE_DIM + 2; y++) {
        for (int x = this->_ncols + startx + 2; x < this->_ncols + startx + 2 + STATE_DIM + 2; x++) { 
            GCOLOR(BG, mvaddch_sq(y, x, ' '));
            if (this->_heldPiece == INVALID) continue;

            minopos_t held_local_x = (minopos_t)(x - (this->_ncols + startx + 2)) - 1;
            minopos_t held_local_y = (minopos_t)(y - (starty)) - 1;

            if (held_local_x >= STATE_DIM || held_local_y >= STATE_DIM || held_local_x < 0 || held_local_y < 0) continue;

            struct TetrominoDef* dat = &TData[PIECE_TO_INDEX(this->_heldPiece)];
            struct Mino* mino = &dat->rotations[this->_currentRot].state[held_local_y][held_local_x];

            if (mino->occupied)
                COLOR(mino->col, mvaddch_sq(y, x, ' '));

        }
    }
    GCOLOR(BG, draw_text_centered((this->_ncols + startx + 2) * 2 + (STATE_DIM * 2 + 4) / 2, starty, "HELD:"));
    char lines_cleared_str[64] = {0};
    char score_str[64] = {0};
    char last_score_str[64] = {0};
    char last_combo_str[64] = {0};
    snprintf(lines_cleared_str, 63, "Current Lines Cleared: %ld", this->_linesCleared);
    snprintf(score_str, 63, "Current Total Score: %ld", this->_points);
    snprintf(last_score_str, 63, "Latest Score: %ld", this->_lastPoints);
    snprintf(last_combo_str, 63, "Latest Combo: %s", combo_to_name(this->_lastCombo));
    GCOLOR(DEFAULT, mvaddstr(starty + this->_nrows - 4, startx * 2 + this->_ncols * 2 + 2, lines_cleared_str));
    GCOLOR(DEFAULT, mvaddstr(starty + this->_nrows - 3, startx * 2 + this->_ncols * 2 + 2, score_str));
    GCOLOR(DEFAULT, mvaddstr(starty + this->_nrows - 2, startx * 2 + this->_ncols * 2 + 2, last_score_str));
    GCOLOR(DEFAULT, mvaddstr(starty + this->_nrows - 1, startx * 2 + this->_ncols * 2 + 2, last_combo_str));
}

void matrix_destruct(Matrix* this) {
    M_matrix_destroy_board(this);
    free(this);
}