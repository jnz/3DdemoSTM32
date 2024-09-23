/* SYSTEM HEADER ------------------------------------------------------------ */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/* PROJECT HEADER ----------------------------------------------------------- */
#include "engine.h"
#include "sdl_scancodes.h"

/* DEFINES ------------------------------------------------------------------ */
#define EPSILON 0.00001f
#define FOV     60.0f

/* LOCAL DATA --------------------------------------------------------------- */
texture_t g_textures[MAX_TEXTURES] = { 0 };
texture_t g_sprites[MAX_SPRITES] = { 0 };

/* FUNCTION PROTOTYPES ------------------------------------------------------ */

// Game
static void g_move(vertex_t* pos_current, const vertex_t* dx,
    const uint8_t* map, const int map_width, const int map_height);

// Math
static void m_rotateVertex(vertex_t* v, const float angleRad);
static void m_normalize(vertex_t* v);
static bool m_rayPlaneIntersection(const vertex_t* planeNormal, float planeD, const vertex_t* rayStart, const vertex_t* rayDir, float* f);

// Render functions
static void r_drawBackground(uint32_t* fb);
static void r_drawcolumn(uint8_t* framebuf, const texture_t* t, int x, int y_high, int y_low, float tex_column, bool transparency);
void r_drawsprite(uint32_t* fb, const float zbuffer[WIDTH], const texture_t* t,
    vertex_t player_pos, vertex_t player_dir, vertex_t sprite_pos);
static uint8_t r_raycast(const uint8_t* map, const int width, const int height,
    float fStartX, float fStartY, float fEndX, float fEndY,
    float* xHit, float* yHit, int* xBlock, int* yBlock,
    int* xNormal, int* yNormal, float* f);

/* FUNCTION BODIES ---------------------------------------------------------- */
void g_update(const float dt_sec, const uint8_t* kb, gamestate_t* game)
{
    const float f = 1.0f * kb[SDL_SCANCODE_W] - 1.0f * kb[SDL_SCANCODE_S];
    vertex_t dx = { .n = game->player_dir.n * 1.250f * f * dt_sec,
                    .e = game->player_dir.e * 1.250f * f * dt_sec };
    g_move(&game->player_pos, &dx, game->level, game->level_width, game->level_height);

    const float dir = -1.0f * (kb[SDL_SCANCODE_A]!=0) + 1.0f * (kb[SDL_SCANCODE_D]!=0);
    const float da = 45.0f * dir * M_PI_F / 180.0f * dt_sec;
    m_normalize(&game->player_dir);
    m_rotateVertex(&game->player_dir, da);
}

void r_render(uint32_t* fb, const gamestate_t* game)
{
    r_drawBackground(fb);

    const float WALLHEIGHT = 2.2f * HEIGHT/2;
    const float maxdist = 100.0f;
    vertex_t ray;
    vertex_t target; // max. raycast location
    vertex_t hit; // location of block the ray hits
    int column = 0;
    int eNormal, nNormal; // normal vector of ray hit (east/north)
    float zbuffer[WIDTH];

    /* for each column in framebuffer (e.g. 320 columns) cast a ray: */
    for (float angle = -FOV / 2; angle < FOV / 2; angle += (FOV / WIDTH), column++)
    {
        ray = game->player_dir;
        m_rotateVertex(&ray, angle*M_PI_F/180.0f);
        target.n = game->player_pos.n + ray.n * maxdist;
        target.e = game->player_pos.e + ray.e * maxdist;

        // return: block and hit (location of block hit)
        const uint8_t block =
            r_raycast(game->level, game->level_width, game->level_height,
                game->player_pos.e, game->player_pos.n, target.e, target.n,
                &hit.e, &hit.n, NULL, NULL, &eNormal, &nNormal, NULL);

        if (block == 0)
        {
            zbuffer[column] = INFINITY;
            continue;
        }

        // direction vector from player to hit location
        vertex_t dx = { .n = hit.n - game->player_pos.n, .e = hit.e - game->player_pos.e };
        // distance to block (dot product):
        const float dist = dx.n * game->player_dir.n + dx.e * game->player_dir.e;
        zbuffer[column] = dist;
        const float height = WALLHEIGHT / dist;
        if (height > 50 * WALLHEIGHT)
            continue;

        int y_hi = HEIGHT / 2 - (int)(height / 2);
        int y_lo = HEIGHT / 2 + (int)(height / 2);

#ifdef TEXTURES_DISABLED
        const uint32_t blockmap[] = { COLOR(0,0,0), COLOR(255, 0, 0), COLOR(0,255,0), COLOR(0,255,0), COLOR(0,255,0), COLOR(0,255,0), COLOR(0,255,0), COLOR(0,255,0) };
        y_hi = r_clamp(y_hi, 0, HEIGHT);
        y_lo = r_clamp(y_lo, 0, HEIGHT);
        for (int y = y_hi; y < y_lo; y++) // draw pixels from y_hi down to y_lo
        {
            fb[y * WIDTH + column] = blockmap[block];
        }
#else
        const float tex_column = eNormal != 0 ? hit.n-floorf(hit.n) : hit.e-floorf(hit.e);
        const texture_t* t = &g_textures[block];
        r_drawcolumn((uint8_t*)fb, t, column, y_hi, y_lo, tex_column, false);
#endif
    }

    // vertex_t sprite_pos = { .n = 5.0f, .e = 2.0f };
    // r_drawsprite(fb, zbuffer, &g_sprites[0], game->player_pos, game->player_dir, sprite_pos);
    (void)zbuffer;
}


static void g_move(vertex_t* pos_current, const vertex_t* dx,
    const uint8_t* map, const int map_width, const int map_height)
{
    uint8_t b; // block that is being hit (if any)
    int xBlock, yBlock; // coordinates of block that is hit
    int eNormal, nNormal; // normal (if block is hit)
    float xHit, yHit; // exact position where wall is hit
    float f; // fraction of travel along ray (1.0f = full distance of dx)
    vertex_t n; // wall normal

    if (dx->e == 0.0f && dx->n == 0.0f) { return; }
    b = r_raycast(map, map_width, map_height,
              pos_current->e, pos_current->n, pos_current->e + dx->e, pos_current->n + dx->n,
              &xHit, &yHit, &xBlock, &yBlock, &eNormal, &nNormal, &f);
    if (b == 0 || f > 1.0f) // nothing hit, perform full movement
    {
        pos_current->e += dx->e;
        pos_current->n += dx->n;
        return;
    }
    n.e = (float)eNormal;
    n.n = (float)nNormal;

    const float PLANE_OFFSET = 0.050f;
    float plane_d = -(n.e * xHit + n.n * yHit) - PLANE_OFFSET;

    m_rayPlaneIntersection(&n, plane_d, pos_current, dx, &f);
    vertex_t q = { .n = dx->n * f, .e = dx->e * f };

    // P3 = end point after slide
    // P2 = point inside wall
    // Q = point in front of wall
    // N = Normal
    // P3 = P2 - [(P2 - Q)*N]*N

    pos_current->e += q.e;
    pos_current->n += q.n;
}

texture_t* r_texture_dict(void)
{
    return &g_textures[0];
}

texture_t* r_sprite_dict(void)
{
    return &g_sprites[0];
}


static void r_drawBackground(uint32_t* fb)
{
    int y;
    for (y = 0; y < HEIGHT / 2; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            fb[y * WIDTH + x] = COLOR(10, 169, 216);
        }
    }
    for (; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            fb[y * WIDTH + x] = COLOR(108, 108, 108);
        }
    }
}

static uint8_t r_raycast(const uint8_t* map, const int width, const int height,
    float fStartX, float fStartY, float fEndX, float fEndY,
    float* xHit, float* yHit, int* xBlock, int* yBlock,
    int* xNormal, int* yNormal, float* f)
{
    const float dx = fEndX - fStartX;
    const float dy = fEndY - fStartY;
    const int stepX = r_signum(dx);
    const int stepY = r_signum(dy);
    const float tDeltaX = stepX / dx;
    const float tDeltaY = stepY / dy;

    const float fbX_ = dx >= 0.0f ? ceilf(fStartX) : floorf(fStartX);
    const float fbY_ = dy >= 0.0f ? ceilf(fStartY) : floorf(fStartY);
    const float dfX = fbX_ - fStartX;
    const float dfY = fbY_ - fStartY;
    float tMaxX = (dfX != 0.0f ? dfX : stepX) / dx;
    float tMaxY = (dfY != 0.0f ? dfY : stepY) / dy;

    int x = (int)fStartX; // East
    int y = (int)fStartY; // North
    int nx, ny;

    for (float dist = 0.0f; dist <= 1.0f;/*NOP*/)
    {
        dist = r_min(tMaxX, tMaxY); // travel along ray
        if (tMaxX < tMaxY)
        {
            tMaxX += tDeltaX;
            x += stepX;
            nx = stepX; ny = 0;
        }
        else
        {
            tMaxY += tDeltaY;
            y += stepY;
            ny = stepY; nx = 0;
        }

        if (x < 0 || x >= width || y < 0 || y >= height) // outside of map?
        {
            continue;
        }

        const int ymap = height - 1 - y;
        const uint8_t b = map[ymap * width + x];
        if (b > 0) // ray has hit a wall
        {
            if (xHit) { *xHit = fStartX + dx * dist; } // location of wall hit
            if (yHit) { *yHit = fStartY + dy * dist; }
            if (xBlock) { *xBlock = x; } // block index in map
            if (yBlock) { *yBlock = y; }
            if (xNormal) { *xNormal = -nx; }
            if (yNormal) { *yNormal = -ny; }
            if (f) { *f = dist; }

            assert(b >= 0 && b<=7);
            return b;
        }
    }
    if (f) { *f = 1.0f; }

    return 0;
}

void r_drawsprite(uint32_t* fb, const float zbuffer[WIDTH], const texture_t* t,
                  vertex_t player_pos, vertex_t player_dir, vertex_t sprite_pos)
{
    const vertex_t dx = { .n = sprite_pos.n - player_pos.n, .e = sprite_pos.e - player_pos.e };
    vertex_t player_tangent;
    player_tangent.e =  player_dir.n;
    player_tangent.n = -player_dir.e;

    // transform sprite position into player camera system:
    const float dist = dx.n * player_dir.n + dx.e * player_dir.e; // sprite distance from player
    const float east = dx.n * player_tangent.n + dx.e * player_tangent.e; // sprite position relative to screen center

    if (dist < 0.1f)
        return;

    const float SPRITEHEIGHT = HEIGHT/2;
    const float SPRITEWIDTH = 1.0f;
    const float s = (WIDTH / 2) / tanf(FOV*M_PI_F/180.0f / 2); // scale factor to project x coord. on screen

    int x_right = WIDTH/2 + (int)(s * (east + SPRITEWIDTH * 0.5f) / dist); // project right edge of sprite on screen
    int x_left = WIDTH/2 + (int)(s * (east - SPRITEWIDTH * 0.5f) / dist);
    const float tx = 1.0f / (x_right - x_left);
    float txcolumn = 0.0f;
    const float height = SPRITEHEIGHT / dist;

    for (int x = x_left; x < x_right; x++, txcolumn+=tx)
    {
        if (x >= WIDTH)
            break;
        if (x < 0) // FIXME this is inefficient
            continue;
        if (dist > zbuffer[x])
            continue;
        r_drawcolumn((uint8_t*)fb, t, x, (int)(HEIGHT / 2 - height / 2), (int)(HEIGHT / 2 + height / 2), txcolumn, true);
    }
}

static void r_drawcolumn(uint8_t* framebuf, const texture_t* t, int x, int y_high, int y_low, float tex_column, bool transparency)
{
    assert(y_low > y_high);
    assert(x >= 0 && x < WIDTH);
    assert(tex_column >= 0.0f && tex_column <= 1.0f);

    const int ylen = y_low - y_high; //
    if (ylen < 1 || y_low < 0)
        return;

    const int rl = t->rowlength;
    const int tx = (int)(tex_column * (t->width-1)); // fixed column
    const float ty_stride = (float)(t->height-1) / (ylen);

    float ty = 0.0f;
    if (y_high < 0)
    {
        ty = (-(float)y_high / ylen) * (float)(t->height-1);
        assert(ty >= 0 && ty < t->height);
        y_high = 0;
    }
    y_low = r_min(y_low, HEIGHT);

    const int bpp  = t->bytesperpixel;
    for (int c = y_high; c < y_low; c++)
    {
        assert((int)ty < t->height);
        assert(((int)ty) * rl + tx * bpp + 2 < t->rowlength * t->height);
        assert(c * WIDTH * BPP + x * BPP + 2 < WIDTH * HEIGHT * BPP);
        const uint8_t R = t->pixels[((int)ty) * rl + tx * bpp + 0];
        const uint8_t G = t->pixels[((int)ty) * rl + tx * bpp + 1];
        const uint8_t B = t->pixels[((int)ty) * rl + tx * bpp + 2];
        ty += ty_stride;
        if (transparency && R == 0xff && G == 0x0 && B == 0xff)
            continue;

        framebuf[c * WIDTH * BPP + x * BPP + 0] = R;
        framebuf[c * WIDTH * BPP + x * BPP + 1] = G;
        framebuf[c * WIDTH * BPP + x * BPP + 2] = B;
    }
}

static void m_rotateVertex(vertex_t* v, const float angleRad)
{
    const float len = v->n * v->n + v->e * v->e;
    assert(len > 0.99f && len < 1.01f);

    const float n = v->n;
    const float e = v->e;
    const float s = sinf(angleRad);
    const float c = cosf(angleRad);
    v->n = c * n - s * e; // [ cos(a)   -sin(a)]
    v->e = s * n + c * e; // [ sin(a)    cos(a)]

}

static void m_normalize(vertex_t* v)
{
    const float ilen = 1.0f / sqrtf(v->n * v->n + v->e * v->e);
    v->n *= ilen;
    v->e *= ilen;
}

static bool m_rayPlaneIntersection(const vertex_t* planeNormal, float planeD, const vertex_t* rayStart, const vertex_t* rayDir, float* f)
{
    const float q = planeNormal->e * rayDir->e + planeNormal->n * rayDir->n;
    if(fabsf(q) < EPSILON)
        return false; // no intersection, plane and line are parallel

    *f = -(planeNormal->n*rayStart->n + planeNormal->e*rayStart->e + planeD) / q;
    return true;
}
