#pragma once

/* SYSTEM HEADER ------------------------------------------------------------ */
#include <stdint.h>
#include <stdbool.h>

/* PROJECT HEADER ----------------------------------------------------------- */

/* DEFINES ------------------------------------------------------------------ */

#define M_PI_F   3.14159265358979323846264338327950288f

#define MAX_TEXTURES 8 /**<  max. number of textures in texture dictionary */
#define MAX_SPRITES  8 /**<  max. number of sprites in sprite dictionary */

#define WIDTH 240 /**< framebuffer width in pixel */
#define HEIGHT 320 /**< framebuffer height in pixel */
#define BPP 4 /**< bytes/pixel */

/* Screen can be larger, framebuffer is scaled up */
#define SCREENWIDTH (WIDTH*4)
#define SCREENHEIGHT (HEIGHT*4)

/* MACROS ------------------------------------------------------------------- */
#define r_min(x, y) (((x) < (y)) ? (x) : (y))
#define r_max(x, y) (((x) > (y)) ? (x) : (y))
#define r_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define r_signum(x) ((x) == 0 ? 0 : (x) < 0 ? -1 : 1 )
#define COLOR(r,g,b)   ((uint32_t)(0xff000000 | ((r) << 16) | ((g) << 8) | (b)))

/* TYPEDEFS ----------------------------------------------------------------- */

/** basic 2d vector/vertex type */
typedef struct
{
    float n; /**< North */
    float e; /**< East  */
} vertex_t;

typedef struct
{
    vertex_t player_pos; /**< Player position in the world */
    vertex_t player_dir; /**< Player view direction vector */

    uint8_t* level;
    int level_width;
    int level_height;
} gamestate_t;

typedef struct
{
    unsigned char* pixels;
    int            width;
    int            height;
    int            rowlength; /**< = pitch, the number of bytes in a row */
    int            bytesperpixel;
} texture_t;

/* FUNCTION PROTOTYPES ------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

texture_t* r_texture_dict(void);
texture_t* r_sprite_dict(void);

void g_update(const float dt_sec, const uint8_t* kb, gamestate_t* game);
void r_render(uint32_t* fb, const gamestate_t* game);

#ifdef __cplusplus
}
#endif
