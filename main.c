#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mach_defines.h"
#include "sdk.h"
#include "gfx_load.h"
#include "cache.h"

#include "libmidi.h"
#include "libsynth.h"
#include "Tetris-hand_edits.h"


// the name of the data from the include file
//The bgnd.png image got linked into the binary of this app, and these two chars are the first
//and one past the last byte of it.
extern char _binary_badgetris_bgnd_png_start;
extern char _binary_badgetris_bgnd_png_end;
extern char _binary_badgetris_tileset_png_start;
extern char _binary_badgetris_tileset_png_end;

//Pointer to the framebuffer memory.
uint8_t *fbmem;
static FILE *console;

#define FB_WIDTH 512
#define FB_HEIGHT 320
#define FB_PAL_OFFSET 256

extern volatile uint32_t MISC[];
#define MISC_REG(i) MISC[(i)/4]
extern volatile uint32_t GFXREG[];
#define GFX_REG(i) GFXREG[(i)/4]

uint32_t *GFXSPRITES = (uint32_t *)0x5000C000;

//Used to debounce buttons
#define BUTTON_READ_DELAY		15
#define BUTTON_FAST_REPEAT		BUTTON_READ_DELAY/2

//Borrowed this from lcd.c until a better solution comes along :/
static void __INEFFICIENT_delay(int n) {
    for (int i=0; i<n; i++) {
        for (volatile int t=0; t<(1<<11); t++);
    }
}

//Wait until all buttons are released
static inline void __button_wait_for_press() {
    while (MISC_REG(MISC_BTN_REG) == 0);
}

//Wait until all buttons are released
static inline void __button_wait_for_release() {
    while (MISC_REG(MISC_BTN_REG));
}

static inline void __sprite_set(int index, int x, int y, int size_x, int size_y, int tile_index, int palstart) {
    x+=64;
    y+=64;
    GFXSPRITES[index*2]=(y<<16)|x;
    GFXSPRITES[index*2+1]=size_x|(size_y<<8)|(tile_index<<16)|((palstart/4)<<25);
}

//Helper function to set a tile on layer a
static inline void __tile_a_set(uint8_t x, uint8_t y, uint32_t index) {
    GFXTILEMAPA[y*GFX_TILEMAP_W+x] = index;
}

//Helper function to set a tile on layer b
static inline void __tile_b_set(uint8_t x, uint8_t y, uint32_t index) {
    GFXTILEMAPB[y*GFX_TILEMAP_W+x] = index;
}

//Helper function to move tile layer 1
static inline void __tile_a_translate(int dx, int dy) {
    GFX_REG(GFX_TILEA_OFF)=(dy<<16)+(dx &0xffff);
}

//Helper function to move tile layer b
static inline void __tile_b_translate(int dx, int dy) {
    GFX_REG(GFX_TILEB_OFF)=(dy<<16)+(dx &0xffff);
}

uint32_t counter60hz(void) {
    return GFX_REG(GFX_VBLCTR_REG);
}

#include "main.h"
#include <stdint.h>

//FIXME: these should probably not be globals
uint32_t  wait_until;
uint8_t drop_timer_flag = 0;
uint16_t state;
uint8_t audio_on = 1;
#define SYNTHVOICES 5

int8_t sstr[3];

void tetrapuzz(void)
{
    __INEFFICIENT_delay(200);
    tetrapuzz_init();

    uint32_t buttondebounce = counter60hz();

    while(1)
    {
        // Need tetris tunes.
        if (audio_on) {
            midi_play_song(tetris, sizeof(tetris)/sizeof(uint16_t)/3, BPM(130));
        }

        if (counter60hz() > buttondebounce) {
            //Service button inputs as necessary

            if ((MISC_REG(MISC_BTN_REG) & (BUTTON_UP | BUTTON_A))) {
                BOX_rotate(1);
                buttondebounce = counter60hz()+BUTTON_READ_DELAY; //prevent multiple button reads
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_LEFT)) {
                BOX_lt();
                buttondebounce = counter60hz()+BUTTON_READ_DELAY; //prevent multiple button reads
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_RIGHT)) {
                BOX_rt();
                buttondebounce = counter60hz()+BUTTON_READ_DELAY; //prevent multiple button reads
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_DOWN)) {
                BOX_dn();
                buttondebounce = counter60hz()+BUTTON_FAST_REPEAT; //prevent multiple button reads
                //Reset drop timer to prevent jitter if you hold the down button
                wait_until = counter60hz()+BOX_get_delay();
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_B)) {
                while(BOX_dn() == 0) ;;  //Do nothing until piece has fully dropped
                buttondebounce = counter60hz()+(BUTTON_READ_DELAY); //prevent multiple button reads
                //Reset drop timer to prevent jitter if you hold the down button
                wait_until = counter60hz()+BOX_get_delay();
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_START)) {
                if (audio_on) {
                    synth_all_off();
                    audio_on = 0;
                }
                else {
                    synth_init(SYNTHVOICES);
                    audio_on = 1;
                }
                buttondebounce = counter60hz()+BUTTON_READ_DELAY; //prevent multiple button reads
            }
            if ((MISC_REG(MISC_BTN_REG) & BUTTON_SELECT)) {
                return;
            }
        }

        //Service the loop on a non-blocking delay here
        if (counter60hz() > wait_until)
        {
            wait_until = counter60hz()+BOX_get_delay();
            tetrapuzz_loop();
        }
    }
}

void tetrapuzz_init(void)
{
    BOX_clearscreen();

    drop_timer_flag = 0;

    wait_until = counter60hz();
    BOX_start_game();
}

void tetrapuzz_loop(void)
{
    BOX_dn();

    if (BOX_end_game())
    {
        // Stop audio
        synth_all_off();
        //Print game ending information
        BOX_show_gameover();
        //Loop until a button is pushed
        __button_wait_for_release();
        __INEFFICIENT_delay(200);
        __button_wait_for_press();

        //Start game anew
        tetrapuzz_init();
    }
}

void tetrapuzz_pause(void)
{
    //FIXME: we probably don't need a pause mode.
    /* deprecated camera badge code:
    if(butpress)
    	{
    	BOX_start_game();
    	state=s_run;
    	return(0);
    	}
    */
}

void tetrapuzz_gameover(void)
{
    //FIXME: we probably don't need this gameover mode.
    /* deprecated camera badge code:
    }
    if(butpress) {
    	BOX_start_game();
    	state=s_run;
    }
    */
}

/*
Program flow:
  -Initialize
  -Spawn piece
    -load from reference
    -calculate y_loc
    -draw piece
  -Wait for game input
*/

/**BOX_location[] Array********************
* Used to track where boxes are in the    *
* playing area.  One byte per column with *
* byte0 as the left column.  One bit per  *
* row with the LSB at the top.  If there  *
* are more than 8 rows, the array will be *
* Increased by BOX_board_right+1 bytes to *
* accomodate 8 more rows as needed.       *
*******************************************/

/*****************************************
* BOX_piece[] is a 4x4 representation of *
* the currently selected playing piece.  *
* When a piece spawns its shape is       *
* written to this array in the least     *
* significant nibble of each byte:       *
* Byte0= left, Byte3= right		         *
* The LSB of each nibble is the top of   *
* the display area.                      *
******************************************/

//Total rows-1 (max row-number, zero indexed)
#define BOX_BOARD_BOTTOM 7
//Total columns-1 (max column-number, zero indexed)
#define BOX_BOARD_RIGHT		7
//Pixel size of each box
#define BOX_MULTIPLIER          1
//Pixel offsets from left and top of screen
#define BOX_XOFFSET	4
#define BOX_YOFFSET 0
//Values for frame around grid
#define BOX_FRAMEX				40
#define BOX_FRAMEY				0
#define BOX_FRAME_THICKNESS		4
#define BOX_FRAMECOLOR			0xFFFFFF
//Values for frame around score
#define BOX_GAMETITLE_Y			2
#define BOX_GAMEOVER_X			11
#define BOX_GAMEOVER_Y			10
#define BOX_SCOREBOX_X			17
#define BOX_SCOREBOX_Y			14
#define BOX_SCOREBOX_WIDTH		1
#define BOX_SCOREBOX_HEIGHT		1

//Sprite Indices
#define BOX_FRAME_LT			144
#define BOX_FRAME_RT			146
#define BOX_FRAME_T				145
#define BOX_FRAME_L				160
#define BOX_FRAME_R				162
#define BOX_FRAME_B				177
#define BOX_FRAME_LB			176
#define BOX_FRAME_RB			178
#define BOX_BLACKSQUARE			148
#define BOX_SPRITE_00			161
#define BOX_SPRITE_01			128

#define ARRAY_SIZE (((BOX_BOARD_BOTTOM+8)/8) * (BOX_BOARD_RIGHT + 1))

#define DEFAULT_FG_COLOR	BOX_SPRITE_01
#define DEFAULT_BG_COLOR	0x000000
#define DEFAULT_STONE_COLOR	135

#define DEFAULT_DROP_DELAY	24

static uint8_t cursor_x, cursor_y;

volatile uint8_t random_piece = 0;	//Used to select a piece "randomly" (but not really)
uint8_t piece_color = DEFAULT_FG_COLOR;	 //Used to color the moving pieces

uint8_t BOX_piece[4];

const uint8_t BOX_reference[7][4][4] = {
    //T
    {
        {
            0b00000010,
            0b00000011,
            0b00000010,
            0b00000000
        },

        {
            0b00000000,
            0b00000111,
            0b00000010,
            0b00000000
        },

        {
            0b00000001,
            0b00000011,
            0b00000001,
            0b00000000
        },

        {
            0b00000010,
            0b00000111,
            0b00000000,
            0b00000000
        }
    },

    // S
    {
        {
            0b00000010,
            0b00000011,
            0b00000001,
            0b00000000
        },

        {
            0b00000011,
            0b00000110,
            0b00000000,
            0b00000000
        },

        {
            0b00000010,
            0b00000011,
            0b00000001,
            0b00000000
        },

        {
            0b00000011,
            0b00000110,
            0b00000000,
            0b00000000
        }
    },

    // Z
    {
        {
            0b00000001,
            0b00000011,
            0b00000010,
            0b00000000
        },

        {
            0b00000110,
            0b00000011,
            0b00000000,
            0b00000000
        },

        {
            0b00000001,
            0b00000011,
            0b00000010,
            0b00000000
        },

        {
            0b00000110,
            0b00000011,
            0b00000000,
            0b00000000
        }
    },

    // L
    {
        {
            0b00000011,
            0b00000001,
            0b00000001,
            0b00000000
        },

        {
            0b00000000,
            0b00000001,
            0b00000111,
            0b00000000
        },

        {
            0b00000010,
            0b00000010,
            0b00000011,
            0b00000000
        },

        {
            0b00000000,
            0b00000111,
            0b00000100,
            0b00000000
        }
    },

    // J
    {
        {
            0b00000001,
            0b00000001,
            0b00000011,
            0b00000000
        },

        {
            0b00000000,
            0b00000100,
            0b00000111,
            0b00000000
        },

        {
            0b00000011,
            0b00000010,
            0b00000010,
            0b00000000
        },

        {
            0b00000000,
            0b00000111,
            0b00000001,
            0b00000000
        }
    },

    // Box
    {
        {
            0b00000011,
            0b00000011,
            0b00000000,
            0b00000000,
        },

        {
            0b00000011,
            0b00000011,
            0b00000000,
            0b00000000,
        },

        {
            0b00000011,
            0b00000011,
            0b00000000,
            0b00000000,
        },

        {
            0b00000011,
            0b00000011,
            0b00000000,
            0b00000000,
        }
    },

    // Line
    {
        {
            0b00000010,
            0b00000010,
            0b00000010,
            0b00000010
        },

        {
            0b00000000,
            0b00001111,
            0b00000000,
            0b00000000
        },

        {
            0b00000010,
            0b00000010,
            0b00000010,
            0b00000010
        },

        {
            0b00000000,
            0b00001111,
            0b00000000,
            0b00000000
        }
    }
};

//Variables
uint8_t BOX_location[BOX_BOARD_RIGHT+1][BOX_BOARD_BOTTOM+1];
uint8_t x_loc, y_loc;     //Bottom left index of each piece
uint8_t cur_piece = 0;	//Index for BOX_reference
uint8_t rotate = 0;		//Index for piece rotation
static uint8_t score = 0;		//Track the number of rows completed
static uint8_t game_over = 0;

//Messages
const uint8_t message1[] = { "Badgetris!" };
const uint8_t message2[] = { "click to start" };
const uint8_t message3[] = { "Game Over" };
const uint8_t message4[] = { "Lines:" };

//Functions

/*******************************
 * Display specific functions: *
 *   Change these to suit      *
 *   whatever display will     *
 *   be used.                  *
 *******************************/


/* Returns a number 0..6 representing the next piece to use.
*  This will pull all six pieces in random order, then
*  generate a new random set of the six when necessary.
*/
uint8_t BOX_get_random_piece(void) {
    /* Make a randomized array
    *  Prewind the next piece and send the prewind from last time
    *  If the new prewind is the last in the array, make a new random array
    */

    //Line things up for the first run
    static uint8_t prewind_next_choice = 42;
    static uint8_t next_random_piece_idx = 7;
    static uint8_t bag_of_pieces[7] = {0,1,2,3,4,5,6};
    if (prewind_next_choice > 6) prewind_next_choice = (MISC_REG(MISC_RNG_REG) % 7);

    //This is what we'll send back at the end of the function
    uint8_t piece_to_return = prewind_next_choice;

    //Check and generate a new randomized array if necessary
    if (next_random_piece_idx > 6) {
        //Blank out the array
        for (uint8_t i=0; i<7; i++) bag_of_pieces[i] = 42;
        //Randomly fill array with all available pieces
        for (uint8_t i=0; i<7; i++) {
            while(1) {
                uint8_t rand_idx = (MISC_REG(MISC_RNG_REG) % 7);
                if (bag_of_pieces[rand_idx] > 6) {
                    bag_of_pieces[rand_idx] = i;
                    break;
                }
            }
        }
        //Reset the tracking index since we have a new array
        next_random_piece_idx = 0;
    }

    //Prewind the piece for the next time this function is called and inc the idx
    prewind_next_choice = bag_of_pieces[next_random_piece_idx++];

    return piece_to_return;
}

uint8_t BOX_get_score(void)
{
    return score;
}

uint16_t BOX_get_delay(void)
{
    if (BOX_get_score)
    {
        uint8_t level = BOX_get_score()/4;
        int16_t dropdelay = DEFAULT_DROP_DELAY;
        while(level)
        {
            dropdelay -= (dropdelay/8);
            --level;
            //if (dropdelay<200) return 200;	//Keep speed from becoming insane
        }
        if (dropdelay < 0) return 0;
        return dropdelay;
    }
    return DEFAULT_DROP_DELAY;
}

void BOX_clearscreen(void)
{
    //Clear both tilemaps
    memset(GFXTILEMAPA,0,0x4000);
    memset(GFXTILEMAPB,0,0x4000);
    //Clear sprites that IPL may have loaded
    memset(GFXSPRITES,0,0x4000);
}


// one color per piece - 7 pieces
static const uint8_t color2rgb[][3] = {
    // { green, red, blue }
    // blue
    { 0, 0, 128 },
    // teal
    { 128, 0, 128},
    // green
    { 128, 0, 0},
    // orange
    { 30, 128, 0},
    // yellow
    { 128, 128, 0},
    // purple
    { 0, 80, 80},
    // red
    { 0, 128, 0}
};


static void neopixel_write(int index, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t data = (r << 16) | (g << 8) | (b);

    // Write the index first because we will actually write to memory fb when you write the data.
    MISC_REG(MISC_GPEXT_NEOPIXEL_INDEX_REG) = index;
    MISC_REG(MISC_GPEXT_NEOPIXEL_DATA_REG) = data;
}

void np_set(uint8_t X, uint8_t Y, uint8_t r, uint8_t g, uint8_t b) {
    // Convert origins - neopixel starts in top right and goes down - tetris starts top left and goes down.
    X = 7 - X;
    neopixel_write(X * 8 + Y, r, g,b);
}

void BOX_draw(uint8_t X, uint8_t Y, uint32_t color)
{
    //Draw box
    uint8_t row = Y*BOX_MULTIPLIER;
    uint16_t col = X*BOX_MULTIPLIER;

    __tile_a_set(col+BOX_XOFFSET,row+BOX_YOFFSET,color);

    // we only want to draw the bottom 8 lines
    Y = Y - (BOX_BOARD_BOTTOM - 7);
    if (X < 8 && Y < 8 && Y >= 0) {
        color = color - DEFAULT_FG_COLOR;
        np_set(X, Y, color2rgb[color][0], color2rgb[color][1], color2rgb[color][2]);
    }
}

void BOX_erase(uint8_t X, uint8_t Y)
{
    //Erase box
    uint8_t row = Y*BOX_MULTIPLIER;
    uint16_t col = X*BOX_MULTIPLIER;

    //__sprite_set(0, col, row, BOX_MULTIPLIER, BOX_MULTIPLIER, 0, 0);
    __tile_a_set(col+BOX_XOFFSET,row+BOX_YOFFSET,BOX_SPRITE_00);

    Y = Y - (BOX_BOARD_BOTTOM - 7);
    if (X < 8 && Y < 8 && Y >= 0) {
        np_set(X, Y, 0, 0, 0);
    }
}

void BOX_pregame(void)
{
    //Draw frame around grid
    for (uint8_t vert = 0; vert<20; vert++)
    {
        __tile_a_set(BOX_BOARD_RIGHT+1+BOX_XOFFSET,vert,BOX_FRAME_R);
        if (BOX_XOFFSET > 0) {
            __tile_a_set(BOX_XOFFSET-1,vert,BOX_FRAME_L);
        }
    }

    __tile_a_set(BOX_XOFFSET-1,19,BOX_FRAME_LB);
    __tile_a_set(BOX_XOFFSET+BOX_BOARD_RIGHT+1,19,BOX_FRAME_RB);
    for (uint8_t hor=BOX_XOFFSET; hor<=BOX_BOARD_RIGHT+BOX_XOFFSET; hor++) {
        __tile_a_set(hor,19,BOX_FRAME_B);
    }

    //Frame around game title
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_GAMETITLE_Y-1,BOX_FRAME_LT);
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_GAMETITLE_Y,BOX_FRAME_L);
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_GAMETITLE_Y+1,BOX_FRAME_LB);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_GAMETITLE_Y-1,BOX_FRAME_RT);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_GAMETITLE_Y,BOX_FRAME_R);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_GAMETITLE_Y+1,BOX_FRAME_RB);
    for (uint8_t i=0; i<10; i++) {
        __tile_a_set(BOX_SCOREBOX_X+i,BOX_GAMETITLE_Y-1,BOX_FRAME_T);
        __tile_a_set(BOX_SCOREBOX_X+i,BOX_GAMETITLE_Y+1,BOX_FRAME_B);
    }

    //Show game title
    BOX_print_string(message1,BOX_SCOREBOX_X,BOX_GAMETITLE_Y,0xFFFFFF,DEFAULT_BG_COLOR);

    //Show game area
    BOX_rewrite_display(piece_color);


    //Frame around score
    for (uint8_t i=0; i<10; i++) {
        __tile_a_set(BOX_SCOREBOX_X+i,BOX_SCOREBOX_Y-1,BOX_FRAME_T);
        __tile_a_set(BOX_SCOREBOX_X+i,BOX_SCOREBOX_Y,BOX_BLACKSQUARE);
        __tile_a_set(BOX_SCOREBOX_X+i,BOX_SCOREBOX_Y+1,BOX_FRAME_B);
    }
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_SCOREBOX_Y-1,BOX_FRAME_LT);
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_SCOREBOX_Y,BOX_FRAME_L);
    __tile_a_set(BOX_SCOREBOX_X-1,BOX_SCOREBOX_Y+1,BOX_FRAME_LB);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_SCOREBOX_Y-1,BOX_FRAME_RT);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_SCOREBOX_Y,BOX_FRAME_R);
    __tile_a_set(BOX_SCOREBOX_X+10,BOX_SCOREBOX_Y+1,BOX_FRAME_RB);

    BOX_update_score();

}

void BOX_start_game(void)
{
    score = 0; //Reset score
    game_over = 0;

    //Set up blank array
    uint16_t i, j;
    for (i=0; i<(BOX_BOARD_RIGHT+1); i++) {
        for (j=0; j<(BOX_BOARD_BOTTOM+1); j++) {
            BOX_location[i][j] = 0;
        }
    }
    BOX_pregame();
    BOX_spawn();
}

void BOX_show_gameover(void)
{
    //Show game title
    for (uint8_t i=0; i<9; i++) {
        __tile_b_set(BOX_GAMEOVER_X+i,BOX_GAMEOVER_Y-1,BOX_FRAME_T);
        //__tile_b_set(BOX_GAMEOVER_X+i,BOX_GAMEOVER_Y,BOX_BLACKSQUARE);
        __tile_b_set(BOX_GAMEOVER_X+i,BOX_GAMEOVER_Y+1,BOX_FRAME_B);
    }
    __tile_b_set(BOX_GAMEOVER_X-1,BOX_GAMEOVER_Y-1,BOX_FRAME_LT);
    __tile_b_set(BOX_GAMEOVER_X+9,BOX_GAMEOVER_Y-1,BOX_FRAME_RT);
    __tile_b_set(BOX_GAMEOVER_X-1,BOX_GAMEOVER_Y,BOX_FRAME_L);
    __tile_b_set(BOX_GAMEOVER_X+9,BOX_GAMEOVER_Y,BOX_FRAME_R);
    __tile_b_set(BOX_GAMEOVER_X-1,BOX_GAMEOVER_Y+1,BOX_FRAME_LB);
    __tile_b_set(BOX_GAMEOVER_X+9,BOX_GAMEOVER_Y+1,BOX_FRAME_RB);

    BOX_print_string(message3,BOX_GAMEOVER_X,BOX_GAMEOVER_Y,0xFFFF00,0xFF0000);

    __tile_b_set(BOX_GAMEOVER_X+4,BOX_GAMEOVER_Y,BOX_BLACKSQUARE);
}

uint8_t BOX_end_game(void)
{
    //FIXME: What happens when game ends?
    return game_over;
}

void BOX_update_score(void)
{
    //Show score on screen
    //FIXME: Make locations and colors #define values
    BOX_print_string(message4,BOX_SCOREBOX_X,BOX_SCOREBOX_Y,0xFFFFFF,DEFAULT_BG_COLOR);

    //This hack turns score numbers into a string
    int8_t mystring[4] = {'0',0,0,0};
    uint8_t i = 0;
    uint8_t hundred, ten, one;
    hundred = score/100;
    ten = (score%100)/10;
    one = (score%100)%10;
    if (hundred) mystring[i++] = hundred+'0';
    if (hundred || ten) mystring[i++] = ten+'0';
    if (hundred || ten || one) mystring[i++] = one+'0';

    BOX_print_string(mystring, BOX_SCOREBOX_X+8,BOX_SCOREBOX_Y,0xFFFFFF,DEFAULT_BG_COLOR);
}

void BOX_print_string(const int8_t * buf, uint16_t x_pixel, uint8_t y_pixel, uint32_t fgcolor, uint32_t bgcolor)
{
    fprintf(console, "\033%dX\033%dY%s\n", x_pixel, y_pixel,buf);
}

/**********************************
 * End Display specific functions *
 **********************************/

/**********************************************
 * Functions that handle bits in BOX_location[]
 * BOX_loc_return_bit
 * BOX_loc_set_bit
 * BOX_loc_clear_bit
 ************************************************/

uint8_t BOX_loc_return_bit(uint8_t X, uint8_t Y) {
    return BOX_location[X][Y];
}

void BOX_loc_set_bit(uint8_t X, uint8_t Y, uint8_t color) {
    BOX_location[X][Y] = color;
}

void BOX_loc_clear_bit(uint8_t X, uint8_t Y) {
    BOX_location[X][Y] = 0;
}

/********************************
 * Functions that handle bits in BOX_piece[]
 * BOX_piece_return_bit()
 * BOX_piece_set_bit()
 * BOX_piece_clear_bit()
 */

void BOX_store_loc(void)
{
    //Step through 4 columns
    uint8_t temp_col, temp_row;
    for (temp_col=0; temp_col<4; temp_col++)
    {
        //Only if x_loc is not out of bounds
        if ((unsigned char)(x_loc+temp_col) <= BOX_BOARD_RIGHT)
        {
            //Step through 4 rows
            for (temp_row=0; temp_row<4; temp_row++)
            {
                //Only if y_loc is not out of bounds
                if (y_loc-temp_row <= BOX_BOARD_BOTTOM)
                {
                    if (BOX_piece[temp_col] & 1<<(temp_row))	//Checks nibbles in Box_piece array
                    {
                        BOX_loc_set_bit((unsigned char)(x_loc+temp_col),y_loc-temp_row, piece_color);
                    }
                }
            }
        }
    }
}

void BOX_clear_loc(void)
{
    //Step through 4 columns
    uint8_t temp_col, temp_row;
    for (temp_col=0; temp_col<4; temp_col++)
    {
        //Only if x_loc is not out of bounds
        if ((unsigned char)(x_loc+temp_col) <= BOX_BOARD_RIGHT)
        {
            //Step through 4 rows
            for (temp_row=0; temp_row<4; temp_row++)
            {
                //Only if y_loc is not out of bounds
                if (y_loc-temp_row <= BOX_BOARD_BOTTOM)
                {
                    if (BOX_piece[temp_col] & 1<<(temp_row))	//Checks nibbles in Box_piece array
                    {
                        BOX_loc_clear_bit((unsigned char)(x_loc+temp_col),y_loc-temp_row);;
                    }
                }
            }
        }
    }
}

void BOX_load_reference(uint8_t piece, uint8_t rotation)
{
    BOX_piece[0] = BOX_reference[piece][rotation][0];
    BOX_piece[1] = BOX_reference[piece][rotation][1];
    BOX_piece[2] = BOX_reference[piece][rotation][2];
    BOX_piece[3] = BOX_reference[piece][rotation][3];
}

void BOX_rotate(uint8_t direction)
{
    //TODO: Allow for adjustments if rotation is prevented due to proximity

    BOX_clear_loc(); //Clear current location so we don't have false compares

    //Load in the candidate rotation
    uint8_t new_rotate = rotate;
    if (++new_rotate > 3) new_rotate = 0;
    BOX_load_reference(cur_piece,new_rotate);

    //Check for overlaps
    if (BOX_check(0, 0))
    {
        //Overlap will be caused, restore piece settings and return
        BOX_load_reference(cur_piece, rotate);
        BOX_store_loc();
        return;
    }
    //No overlap found, allow new rotation
    else
    {
        //Load the current rotation back in and clear the piece from display
        BOX_load_reference(cur_piece, rotate);
        BOX_clear_piece();

        //Load new rotation, display, and save its location
        rotate = new_rotate;
        BOX_load_reference(cur_piece, rotate);

        BOX_store_loc();
        BOX_write_piece();
    }
    BOX_update_screen;
}



void BOX_write_piece(void)  //Writes piece to display
{
    uint8_t i, j;
    for (i=0; i<4; i++)  //Step through each of 4 columns
    {
        for (j=0; j<4; j++) //Step through each of 4 rows
        {
            //prevent invalid indices from being written
            if ((y_loc-j) >= 0)
            {
                if (BOX_piece[i] & 1<<j)
                {
                    BOX_draw(x_loc+i, y_loc-j, piece_color);
                }
            }
        }
    }
}

void BOX_clear_piece(void)  //Clears piece from display
{
    uint8_t i, j;
    for (i=0; i<4; i++)  //Step through each of 4 columns
    {
        for (j=0; j<4; j++) //Step through each of 4 rows
        {
            //prevent invalid indices from being written
            if ((y_loc-j) >= 0)
            {
                if (BOX_piece[i] & 1<<j)
                {
                    BOX_erase(x_loc+i, y_loc-j);
                }
            }
        }
    }
}

void BOX_rewrite_display(uint32_t fgcolor)	//Rewrites entire playing area
{
    //printf(cls);

    uint8_t cols, rows;
    for (cols=0; cols<=BOX_BOARD_RIGHT; cols++)
    {
        for (rows=0; rows<=BOX_BOARD_BOTTOM; rows++)
        {
            uint8_t boxColor = BOX_loc_return_bit(cols,rows);
            if(boxColor) BOX_draw(cols,rows,boxColor);
            else BOX_erase(cols,rows);
        }
    }
}

void BOX_update_screen(void)
{
    //This function call is not needed when a screen buffer isn't used
    //BOX_rewrite_display(default_fg_color, default_bg_color);
}

void BOX_spawn(void)
{
    printf("BOX_spawn start\n");
    x_loc = 4;
    y_loc = 1;
    cur_piece = BOX_get_random_piece();
    piece_color = DEFAULT_FG_COLOR+cur_piece;
    rotate = 0;

    BOX_load_reference(cur_piece, rotate);  //load from reference



    //Check to see if we've filled the screen
    if (BOX_check(0,0))
    {
        //BOX_end_game();
        ++game_over;
    }

    BOX_store_loc(); //Store new location
    BOX_write_piece(); //draw piece
    BOX_update_screen();
    printf("BOX_spawn end\n");
}

uint8_t BOX_check(int8_t X_offset, int8_t Y_offset)
{
    uint8_t temp_area[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint8_t i;
    //Build compare mask in temp_area[]

    //Clear the current piece from BOX_location[] so we don't have a false overlap
    //Do this only if X_offset and Y_offset aren't both 0 (used for BOX_spawn)
    if (X_offset || Y_offset) BOX_clear_loc();
    //mask will be 4 sets of nibbles (2 bytes)
    for (i=0; i<4; i++)
    {
        //if out of bounds on the x axis
        if ((unsigned char)(x_loc+X_offset+i) > BOX_BOARD_RIGHT) temp_area[i] = 0x0F;
        else
        {
            uint8_t j;
            for (j=0; j<4; j++)
            {
                //if we're out of bounds on the y axis
                if (((unsigned char)(y_loc+Y_offset-j) > BOX_BOARD_BOTTOM) ||
                        (BOX_loc_return_bit((unsigned char)(x_loc+X_offset+i),(unsigned char)(y_loc+Y_offset-j))))
                {
                    temp_area[i] |= 1<<j;
                }
            }
        }
    }
    if (X_offset || Y_offset) BOX_store_loc(); //Restore the location we cleared earlier

    if ((temp_area[0] & BOX_piece[0]) | (temp_area[1] & BOX_piece[1]) | (temp_area[2] & BOX_piece[2]) | (temp_area[3] & BOX_piece[3]))
    {
        //Conflict has been found
        return 1;
    }
    else return 0;
}

void BOX_line_check(void)
{
    //TODO: Tweak this to enable scoring

    //Check every line on the playing area for complete rows and record them in an array
    uint8_t complete_lines[4];	//There will never be more than 4 complete rows
    uint8_t temp_index = 0;		//Index for complete_lines[]

    uint8_t board_rows;
    for (board_rows=0; board_rows<=BOX_BOARD_BOTTOM; board_rows++)
    {
        uint8_t board_cols=0;
        while ((board_cols<=BOX_BOARD_RIGHT) && (BOX_loc_return_bit(board_cols,board_rows)))
        {
            //Complete row found, record in complete_lines[]
            if (board_cols == BOX_BOARD_RIGHT) complete_lines[temp_index++] = board_rows;
            ++board_cols;
        }
    }
    if (temp_index == 0) return;  //No complete lines found, return


    //If there are complete rows
    //TODO: Disable interrupts to pause game flow
    //TODO: Add an arbitrary delay, perhaps make complete lines flash?
    //Turn LEDs on, wait, turn LEDs off
    for (uint8_t i=0; i<8; i++) {
        //LEDs on
        MISC_REG(MISC_LED_REG) |= 0b111111;
        __INEFFICIENT_delay(20);
        //LEDs off
        MISC_REG(MISC_LED_REG) &= ~(0b111111);
        __INEFFICIENT_delay(20);
    }

    score += temp_index; //Add the completed rows to our score

    --temp_index;	//This was incremented one too many times earlier, get it back to the proper index.

    //Rewrite BOX_location[] data without completed lines
    uint8_t read_from_row = BOX_BOARD_BOTTOM;
    uint8_t write_to_row = BOX_BOARD_BOTTOM;

    //When we have read from all rows, this will be set to 0 and
    //remaining bits cleared from BOX_location[]
    uint8_t rows_left_to_read = 1;

    //Use variable i to iterate through every row of the board
    uint8_t i=0;
    while (i <= BOX_BOARD_BOTTOM)
    {
        //If the current row is a complete line
        if (read_from_row == complete_lines[temp_index])
        {
            //Decrement indexes
            if (read_from_row == 0)
            {
                rows_left_to_read = 0;

                //Change complete_lines[0] so we don't do this again
                complete_lines[0] = BOX_BOARD_BOTTOM;
            }
            else
            {
                --read_from_row;
                if (temp_index) --temp_index;
            }
        }

        else
        {
            //Write data to all columns of current row
            uint8_t col;
            for (col=0; col<=BOX_BOARD_RIGHT; col++)
            {
                //If there are rows left to read from, do so.
                if (rows_left_to_read)
                {
                    uint8_t readColor = BOX_loc_return_bit(col,read_from_row);
                    if (readColor) BOX_loc_set_bit(col, write_to_row, readColor);
                    else BOX_loc_clear_bit(col, write_to_row);
                }
                //There are no rows left to read from, fill with 0
                else
                {
                    BOX_loc_clear_bit(col, write_to_row);
                }
            }

            //A row has now been read from, decrement the counter
            if (read_from_row) --read_from_row;
            else rows_left_to_read = 0;

            //A row has now been written to, increment the counter, decrement the tracker
            ++i;
            --write_to_row;
        }
    }

    BOX_rewrite_display(DEFAULT_STONE_COLOR);
    BOX_update_score();
}


void BOX_up(void)
{
    BOX_clear_loc();
    BOX_clear_piece();

    if (++cur_piece > 6) cur_piece = 0;
    x_loc = 4;
    y_loc = 0;

    BOX_spawn();
}

uint8_t BOX_dn(void)
{
    if (BOX_check(0, 1))
    {
        //Set piece here and spawn a new one
        BOX_rewrite_display(DEFAULT_STONE_COLOR);
        BOX_line_check();
        BOX_spawn();
        BOX_update_score();
        return 1; //Return non-zero if a new piece was spawned.
    }

    BOX_clear_loc();
    BOX_clear_piece();
    ++y_loc;

    BOX_store_loc();
    BOX_write_piece();
    BOX_update_screen();
    return 0;
}

void BOX_lt(void)
{
    if (BOX_check(-1, 0)) return; //Do nothing if moving causes an overlap
    BOX_clear_loc();
    BOX_clear_piece();
    x_loc--;

    BOX_store_loc();
    BOX_write_piece();
    BOX_update_screen();
}

void BOX_rt(void)
{
    if (BOX_check(1, 0)) return; //Do nothing if moving causes an overlap
    BOX_clear_loc();
    BOX_clear_piece();
    ++x_loc;

    BOX_store_loc();
    BOX_write_piece();
    BOX_update_screen();
}


void main(int argc, char **argv) {
    //Allocate framebuffer memory
    fbmem=malloc(320*512/2);

    //Shut off LEDs
    MISC_REG(MISC_LED_REG)=0x00;

    // Configure the audio synthesizer
    synth_init(SYNTHVOICES);
    // Default triangle-wave voices are fine for the high pitches, maybe with a snappier envelope
    for (uint8_t i=0; i<3; i++) {
        synth_now->voice[i].ctrl   = SYNTH_VOICE_CTRL_ENABLE | SYNTH_VOICE_CTRL_PULSE;
        synth_now->voice[i].attack = 0x0088;
        synth_now->voice[i].decay  = 0x0020;
        synth_now->voice[i].phase_cmp = (1 << 13);
        synth_now->voice[i].volume = SYNTH_VOICE_VOLUME(192,192);
    }

    // But a sawtooth, cello-esque thing is nice in the bass
    for (uint8_t i=3; i<5; i++) {
        synth_now->voice[i].ctrl   = SYNTH_VOICE_CTRL_ENABLE | SYNTH_VOICE_CTRL_SAWTOOTH	;
        synth_now->voice[i].attack = 0x0040;
        synth_now->voice[i].decay  = 0x0040;
        synth_now->voice[i].volume = SYNTH_VOICE_VOLUME(192,192);
    }

    //Set up the framebuffer address.
    GFX_REG(GFX_FBADDR_REG)=((uint32_t)fbmem)&0xFFFFFF;
    //We're going to use a pitch of 512 pixels, and the fb palette will start at 256.
    GFX_REG(GFX_FBPITCH_REG)=(FB_PAL_OFFSET<<GFX_FBPITCH_PAL_OFF)|(512<<GFX_FBPITCH_PITCH_OFF);
    //Blank out fb while we're loading stuff.
    GFX_REG(GFX_LAYEREN_REG)=0;

    //Load up the default tileset and font.
    //ToDo: loading pngs takes a long time... move over to pcx instead.
    printf("Loading tiles...\n");
    int gfx_tiles_err = gfx_load_tiles_mem(GFXTILES, &GFXPAL[0], &_binary_badgetris_tileset_png_start, (&_binary_badgetris_tileset_png_end-&_binary_badgetris_tileset_png_start));
    printf("Tiles initialized err=%d\n", gfx_tiles_err);


    //The IPL leaves us with a tileset that has tile 0 to 127 map to ASCII characters, so we do not need to
    //load anything specific for this. In order to get some text out, we can use the /dev/console device
    //that will use these tiles to put text in a tilemap. It uses escape codes to do so, see
    //ipl/gloss/console_out.c for more info.
    //Note that without the setvbuf command, no characters would be printed until 1024 characters are
    //buffered.
    console=fopen("/dev/console", "w");
    setvbuf(console, NULL, _IOLBF, 1024); //make console line buffered
    if (console==NULL) {
        printf("Error opening console!\n");
    }

    //Now, use a library function to load the image into the framebuffer memory. This function will also set up the palette entries,
    //we tell it to start writing from entry 0.
    //PAL offset changes the colors that the 16-bit png maps to?
    gfx_load_fb_mem(fbmem, &GFXPAL[FB_PAL_OFFSET], 4, 512, &_binary_badgetris_bgnd_png_start, (&_binary_badgetris_bgnd_png_end-&_binary_badgetris_bgnd_png_start));

    //Flush the memory region to psram so the GFX hw can stream it from there.
    cache_flush(fbmem, fbmem+FB_WIDTH*FB_HEIGHT);

    //Copied from IPL not sure why yet
    GFX_REG(GFX_LAYEREN_REG)=GFX_LAYEREN_FB|GFX_LAYEREN_TILEB|GFX_LAYEREN_TILEA|GFX_LAYEREN_SPR;
    GFXPAL[FB_PAL_OFFSET+0x100]=0x00ff00ff; //Note: For some reason, the sprites use this as default bgnd. ToDo: fix this...
    GFXPAL[FB_PAL_OFFSET+0x1ff]=0x40ff00ff; //so it becomes this instead.

    __button_wait_for_release();

    //Set map to tilemap B, clear tilemap, set attr to 0
    //Not sure yet what attr does, but tilemap be is important as it will have the effect of layering
    //on top of our scrolling game
    fprintf(console, "\0331M\033C\0330A\n");
    //Note that without the newline at the end, all printf's would stay in the buffer.


    //Clear both tilemaps
    memset(GFXTILEMAPA,0,0x4000);
    memset(GFXTILEMAPB,0,0x4000);
    //Clear sprites that IPL may have loaded
    memset(GFXSPRITES,0,0x4000);

    //The user can still see nothing of this graphics goodness, so let's re-enable the framebuffer and
    //tile layer A (the default layer for the console).
    //Normal FB enabled (vice 8 bit) because background is loaded into the framebuffer above in 4 bit mode.
    //TILEA is where text is printed by default
    GFX_REG(GFX_LAYEREN_REG)=GFX_LAYEREN_FB|GFX_LAYEREN_TILEA|GFX_LAYEREN_TILEB|GFX_LAYEREN_SPR;


    // Clear the Neopixel matrix
    for (int i = 0; i < 64; i++) {
        neopixel_write(i, 0,0,0);
    }

    for (int i = 0; i < 8; i++) {
        neopixel_write(i, i *8, 0,0);
    }


    /*****************************
     * this is going to start the game and the rest of this will not run
     */

    tetrapuzz();
    synth_all_off();

    // Clear the Neopixel matrix
    for (int i = 0; i < 64; i++) {
        neopixel_write(i, 0,0,0);
    }
    return;
}
