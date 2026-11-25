/* display.c - palettes, tile banks, maps, sprites and the compositor. */

#include <string.h>

#include "internal.h"

#define GROUP_BG      0
#define GROUP_SPRITE  1

typedef struct { int first, size; } bank_t;
typedef struct { int bank, first; } slot_t;
typedef struct { int16_t x, y; uint8_t tile, attr; bool visible; } sprite_t;

static uint8_t      g_tiles[RC_MAX_TILES][RC_TILE_BYTES];
static bool         g_loaded[RC_MAX_TILES];
static bank_t       g_banks[RC_MAX_BANKS];
static int          g_bank_count;
static slot_t       g_slots[2][RC_BIND_SLOTS];

static rc_color     g_bg_pal[RC_BG_PALETTES][RC_BG_PALETTE_COLORS];
static rc_color     g_spr_pal[RC_SPRITE_PALETTES][RC_SPRITE_PALETTE_COLORS];

static rc_map_entry g_maps[2][RC_MAP_SIZE][RC_MAP_SIZE];
static int          g_scroll_x, g_scroll_y;
static int          g_win_x, g_win_y;
static bool         g_win_show;

static sprite_t     g_sprites[RC_MAX_SPRITES];
static int          g_sprite_height;

static rc_color     g_frame[RC_SCREEN_HEIGHT][RC_SCREEN_WIDTH];
static uint8_t      g_rgba[RC_SCREEN_HEIGHT * RC_SCREEN_WIDTH * 4];

/* Statistics about the last composed frame. */
static uint8_t      g_line_count[RC_SCREEN_HEIGHT];
static uint8_t      g_line_order[RC_SCREEN_HEIGHT][RC_MAX_SPRITES];
static uint8_t      g_dropped_lines[RC_MAX_SPRITES];
static uint64_t     g_colors_shown;

static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

void rc__display_reset(void)
{
    int g, s;
    memset(g_tiles, 0, sizeof g_tiles);
    memset(g_loaded, 0, sizeof g_loaded);
    memset(g_banks, 0, sizeof g_banks);
    g_bank_count = 0;
    for (g = 0; g < 2; g++)
        for (s = 0; s < RC_BIND_SLOTS; s++) { g_slots[g][s].bank = RC_NO_BANK; g_slots[g][s].first = 0; }
    memset(g_bg_pal, 0, sizeof g_bg_pal);
    memset(g_spr_pal, 0, sizeof g_spr_pal);
    memset(g_maps, 0, sizeof g_maps);
    g_scroll_x = g_scroll_y = 0;
    g_win_x = g_win_y = 0;
    g_win_show = false;
    memset(g_sprites, 0, sizeof g_sprites);
    g_sprite_height = 8;
    memset(g_line_count, 0, sizeof g_line_count);
    memset(g_line_order, 0, sizeof g_line_order);
    memset(g_dropped_lines, 0, sizeof g_dropped_lines);
    g_colors_shown = 0;
    rc__display_compose();      /* start from a black picture */
    g_colors_shown = 0;
}

/* --- Palettes ------------------------------------------------------------ */

void rc_bg_palette(int palette, const rc_color colors[RC_BG_PALETTE_COLORS])
{
    int i;
    if (palette < 0 || palette >= RC_BG_PALETTES || !colors) return;
    for (i = 0; i < RC_BG_PALETTE_COLORS; i++) g_bg_pal[palette][i] = colors[i] & 0x7FFF;
}

void rc_sprite_palette(int palette, const rc_color colors[RC_SPRITE_PALETTE_COLORS])
{
    int i;
    if (palette < 0 || palette >= RC_SPRITE_PALETTES || !colors) return;
    for (i = 0; i < RC_SPRITE_PALETTE_COLORS; i++) g_spr_pal[palette][i] = colors[i] & 0x7FFF;
}

void rc_palette_load(const rc_palette_data *data)
{
    int i, n;
    if (!data) return;
    if (data->bg) {
        n = data->bg_count > RC_BG_PALETTES ? RC_BG_PALETTES : data->bg_count;
        for (i = 0; i < n; i++) rc_bg_palette(i, data->bg + i * RC_BG_PALETTE_COLORS);
    }
    if (data->sprite) {
        n = data->sprite_count > RC_SPRITE_PALETTES ? RC_SPRITE_PALETTES : data->sprite_count;
        for (i = 0; i < n; i++) rc_sprite_palette(i, data->sprite + i * RC_SPRITE_PALETTE_COLORS);
    }
}

/* --- Tile banks ---------------------------------------------------------- */

int rc_banks_setup(const int *sizes, int count)
{
    int i, g, s, used = 0;

    memset(g_tiles, 0, sizeof g_tiles);
    memset(g_loaded, 0, sizeof g_loaded);
    memset(g_banks, 0, sizeof g_banks);
    g_bank_count = 0;
    for (g = 0; g < 2; g++)
        for (s = 0; s < RC_BIND_SLOTS; s++) { g_slots[g][s].bank = RC_NO_BANK; g_slots[g][s].first = 0; }

    if (!sizes || count <= 0) return 0;
    if (count > RC_MAX_BANKS) count = RC_MAX_BANKS;

    for (i = 0; i < count; i++) {
        int size = sizes[i] < 0 ? 0 : sizes[i];
        if (size > RC_MAX_TILES - used) size = RC_MAX_TILES - used;
        g_banks[i].first = used;
        g_banks[i].size = size;
        used += size;
    }
    g_bank_count = count;
    return count;
}

int rc_bank_count(void) { return g_bank_count; }

int rc_bank_size(int bank)
{
    return (bank < 0 || bank >= g_bank_count) ? 0 : g_banks[bank].size;
}

void rc_bank_load(int bank, int first, const uint8_t *tiles, int count)
{
    int i, size = rc_bank_size(bank);
    if (!tiles || count <= 0 || first < 0 || first >= size) return;
    if (count > size - first) count = size - first;
    for (i = 0; i < count; i++) {
        int t = g_banks[bank].first + first + i;
        memcpy(g_tiles[t], tiles + i * RC_TILE_BYTES, RC_TILE_BYTES);
        g_loaded[t] = true;
    }
}

void rc_bank_clear(int bank)
{
    int size = rc_bank_size(bank);
    if (size == 0) return;
    memset(g_tiles[g_banks[bank].first], 0, (size_t)size * RC_TILE_BYTES);
    memset(&g_loaded[g_banks[bank].first], 0, (size_t)size * sizeof g_loaded[0]);
}

static void bind(int group, int slot, int bank, int first)
{
    if (slot < 0 || slot >= RC_BIND_SLOTS) return;
    if (first < 0 || first >= rc_bank_size(bank)) {
        g_slots[group][slot].bank = RC_NO_BANK;
        g_slots[group][slot].first = 0;
        return;
    }
    g_slots[group][slot].bank = bank;
    g_slots[group][slot].first = first;
}

void rc_bg_bind(int slot, int bank, int first)     { bind(GROUP_BG, slot, bank, first); }
void rc_sprite_bind(int slot, int bank, int first) { bind(GROUP_SPRITE, slot, bank, first); }

static const uint8_t *tile_at(int group, int slot, int index)
{
    const slot_t *s = &g_slots[group][slot];
    int offset;
    if (s->bank == RC_NO_BANK) return NULL;
    offset = s->first + index;
    if (offset >= g_banks[s->bank].size) return NULL;
    return g_tiles[g_banks[s->bank].first + offset];
}

/* --- Maps ---------------------------------------------------------------- */

static bool layer_ok(int layer) { return layer == RC_LAYER_BG || layer == RC_LAYER_WINDOW; }
static bool cell_ok(int x, int y) { return x >= 0 && x < RC_MAP_SIZE && y >= 0 && y < RC_MAP_SIZE; }

void rc_map_set(int layer, int x, int y, rc_map_entry entry)
{
    if (layer_ok(layer) && cell_ok(x, y)) g_maps[layer][y][x] = entry;
}

rc_map_entry rc_map_get(int layer, int x, int y)
{
    rc_map_entry zero = { 0, 0 };
    return (layer_ok(layer) && cell_ok(x, y)) ? g_maps[layer][y][x] : zero;
}

void rc_map_fill(int layer, rc_map_entry entry)
{
    int x, y;
    if (!layer_ok(layer)) return;
    for (y = 0; y < RC_MAP_SIZE; y++)
        for (x = 0; x < RC_MAP_SIZE; x++) g_maps[layer][y][x] = entry;
}

void rc_map_load(int layer, int x, int y, const rc_map_data *map)
{
    int r, c;
    if (!layer_ok(layer) || !map || !map->entries) return;
    for (r = 0; r < map->height; r++)
        for (c = 0; c < map->width; c++)
            rc_map_set(layer, x + c, y + r, map->entries[r * map->width + c]);
}

void rc_bg_scroll(int x, int y)
{
    g_scroll_x = x & 255;
    g_scroll_y = y & 255;
}

void rc_window_position(int x, int y)
{
    g_win_x = x < -7 ? -7 : x;
    g_win_y = y;
}

void rc_window_show(bool show) { g_win_show = show; }

/* --- Sprites ------------------------------------------------------------- */

static bool sprite_ok(int i) { return i >= 0 && i < RC_MAX_SPRITES; }

void rc_sprite_size(int size)
{
    if (size == RC_SPRITE_8X8) g_sprite_height = 8;
    else if (size == RC_SPRITE_8X16) g_sprite_height = 16;
}

void rc_sprite_set(int sprite, int x, int y, uint8_t tile, uint8_t attr)
{
    if (!sprite_ok(sprite)) return;
    g_sprites[sprite].x = (int16_t)clamp16(x);
    g_sprites[sprite].y = (int16_t)clamp16(y);
    g_sprites[sprite].tile = tile;
    g_sprites[sprite].attr = attr;
    g_sprites[sprite].visible = true;
}

void rc_sprite_move(int sprite, int x, int y)
{
    if (!sprite_ok(sprite)) return;
    g_sprites[sprite].x = (int16_t)clamp16(x);
    g_sprites[sprite].y = (int16_t)clamp16(y);
}

void rc_sprite_tile(int sprite, uint8_t tile) { if (sprite_ok(sprite)) g_sprites[sprite].tile = tile; }
void rc_sprite_attr(int sprite, uint8_t attr) { if (sprite_ok(sprite)) g_sprites[sprite].attr = attr; }
void rc_sprite_show(int sprite, bool show)    { if (sprite_ok(sprite)) g_sprites[sprite].visible = show; }

void rc_sprites_hide_all(void)
{
    int i;
    for (i = 0; i < RC_MAX_SPRITES; i++) g_sprites[i].visible = false;
}

/* --- Compositor ---------------------------------------------------------- */

static int tile_pixel(const uint8_t *tile, int x, int y)
{
    int bit = 7 - x;
    if (!tile) return 0;
    return ((tile[y * 2] >> bit) & 1) | (((tile[y * 2 + 1] >> bit) & 1) << 1);
}

/* Reads a background/window pixel at map pixel (mx, my). */
static int map_pixel(int layer, int mx, int my, uint8_t *attr)
{
    rc_map_entry e = g_maps[layer][(my >> 3) & 31][(mx >> 3) & 31];
    int px = mx & 7, py = my & 7;
    if (e.attr & RC_FLIP_X) px = 7 - px;
    if (e.attr & RC_FLIP_Y) py = 7 - py;
    *attr = e.attr;
    return tile_pixel(tile_at(GROUP_BG, (e.attr & RC_MAP_SLOT1) ? 1 : 0, e.tile), px, py);
}

static int sprite_pixel(const sprite_t *s, int sx, int row)
{
    int tile = s->tile, slot = (s->attr & RC_SPRITE_SLOT1) ? 1 : 0;
    if (s->attr & RC_FLIP_X) sx = 7 - sx;
    if (s->attr & RC_FLIP_Y) row = g_sprite_height - 1 - row;
    if (g_sprite_height == 16) {
        tile = (tile & 0xFE) | (row >= 8 ? 1 : 0);
        row &= 7;
    }
    return tile_pixel(tile_at(GROUP_SPRITE, slot, tile), sx, row);
}

static void compose_line(int line)
{
    uint8_t bg_value[RC_SCREEN_WIDTH], bg_attr[RC_SCREEN_WIDTH];
    uint8_t spr_value[RC_SCREEN_WIDTH], spr_index[RC_SCREEN_WIDTH];
    int x, i, covering = 0, drawn;

    for (x = 0; x < RC_SCREEN_WIDTH; x++) {
        if (g_win_show && line >= g_win_y && x >= g_win_x)
            bg_value[x] = (uint8_t)map_pixel(RC_LAYER_WINDOW, x - g_win_x, line - g_win_y, &bg_attr[x]);
        else
            bg_value[x] = (uint8_t)map_pixel(RC_LAYER_BG, x + g_scroll_x, line + g_scroll_y, &bg_attr[x]);
    }

    /* Check the table in order. Only Y matters, so off-screen X still counts. */
    for (i = 0; i < RC_MAX_SPRITES; i++) {
        const sprite_t *s = &g_sprites[i];
        if (s->visible && line >= s->y && line < s->y + g_sprite_height) {
            g_line_order[line][covering++] = (uint8_t)i;
            if (covering > RC_MAX_SPRITES_PER_LINE) g_dropped_lines[i]++;
        }
    }
    g_line_count[line] = (uint8_t)covering;
    drawn = covering > RC_MAX_SPRITES_PER_LINE ? RC_MAX_SPRITES_PER_LINE : covering;

    /* Draw last to first, so the earliest sprite's opaque pixels win. */
    memset(spr_value, 0, sizeof spr_value);
    memset(spr_index, 0, sizeof spr_index);
    for (i = drawn - 1; i >= 0; i--) {
        int id = g_line_order[line][i], px;
        const sprite_t *s = &g_sprites[id];
        for (px = 0; px < RC_TILE_PIXELS; px++) {
            int sx = s->x + px, v;
            if (sx < 0 || sx >= RC_SCREEN_WIDTH) continue;
            v = sprite_pixel(s, px, line - s->y);
            if (v) { spr_value[sx] = (uint8_t)v; spr_index[sx] = (uint8_t)id; }
        }
    }

    for (x = 0; x < RC_SCREEN_WIDTH; x++) {
        bool show_sprite = spr_value[x] != 0;
        if (show_sprite && bg_value[x] != 0 &&
            ((bg_attr[x] & RC_MAP_PRIORITY) || (g_sprites[spr_index[x]].attr & RC_SPRITE_BEHIND)))
            show_sprite = false;

        if (show_sprite) {
            int pal = g_sprites[spr_index[x]].attr & 7, c = spr_value[x] - 1;
            g_frame[line][x] = g_spr_pal[pal][c];
            g_colors_shown |= (uint64_t)1 << (RC_BG_PALETTES * RC_BG_PALETTE_COLORS + pal * RC_SPRITE_PALETTE_COLORS + c);
        } else {
            int pal = bg_attr[x] & 7;
            g_frame[line][x] = g_bg_pal[pal][bg_value[x]];
            g_colors_shown |= (uint64_t)1 << (pal * RC_BG_PALETTE_COLORS + bg_value[x]);
        }
    }
}

void rc__display_compose(void)
{
    int x, y;
    uint8_t *p = g_rgba;

    memset(g_dropped_lines, 0, sizeof g_dropped_lines);
    g_colors_shown = 0;

    for (y = 0; y < RC_SCREEN_HEIGHT; y++) {
        compose_line(y);
        for (x = 0; x < RC_SCREEN_WIDTH; x++) {
            rc_color c = g_frame[y][x];
            int r = RC_COLOR_R(c), g = RC_COLOR_G(c), b = RC_COLOR_B(c);
            *p++ = (uint8_t)((r << 3) | (r >> 2));
            *p++ = (uint8_t)((g << 3) | (g >> 2));
            *p++ = (uint8_t)((b << 3) | (b >> 2));
            *p++ = 255;
        }
    }
}

const uint8_t *rc__display_rgba(void) { return g_rgba; }

/* --- Debug --------------------------------------------------------------- */

static int count_reachable(int group)
{
    static bool seen[RC_MAX_TILES];
    int s, t, n = 0;
    memset(seen, 0, sizeof seen);
    for (s = 0; s < RC_BIND_SLOTS; s++) {
        const slot_t *b = &g_slots[group][s];
        if (b->bank == RC_NO_BANK) continue;
        for (t = 0; t < RC_SLOT_TILES && b->first + t < g_banks[b->bank].size; t++) {
            int id = g_banks[b->bank].first + b->first + t;
            if (!seen[id]) { seen[id] = true; n++; }
        }
    }
    return n;
}

void rc__dbg_usage(rc_usage *out)
{
    int i, best = 0;
    uint64_t bits;
    if (!out) return;
    memset(out, 0, sizeof *out);

    out->banks = g_bank_count;
    for (i = 0; i < g_bank_count; i++) out->tiles_reserved += g_banks[i].size;
    for (i = 0; i < RC_MAX_TILES; i++) if (g_loaded[i]) out->tiles_loaded++;
    out->bg_tiles_bound = count_reachable(GROUP_BG);
    out->sprite_tiles_bound = count_reachable(GROUP_SPRITE);

    for (i = 0; i < RC_MAX_SPRITES; i++) {
        if (g_sprites[i].visible) out->sprites_visible++;
        if (g_dropped_lines[i]) out->sprites_dropped++;
    }

    out->busiest_line = -1;
    for (i = 0; i < RC_SCREEN_HEIGHT; i++)
        if (g_line_count[i] > best) { best = g_line_count[i]; out->busiest_line = i; }
    out->busiest_line_sprites = best;

    for (bits = g_colors_shown; bits; bits &= bits - 1) out->palette_colors_shown++;
    out->warnings = rc__warning_count();
}

void rc__dbg_line(int line, rc_line_info *out)
{
    int i;
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (line < 0 || line >= RC_SCREEN_HEIGHT) return;
    out->covering = g_line_count[line];
    for (i = 0; i < out->covering; i++) {
        if (i < RC_MAX_SPRITES_PER_LINE) out->drawn[out->drawn_count++] = g_line_order[line][i];
        else out->dropped[out->dropped_count++] = g_line_order[line][i];
    }
}

int rc__dbg_dropped(int out[RC_MAX_SPRITES])
{
    int i, n = 0;
    for (i = 0; i < RC_MAX_SPRITES; i++)
        if (g_dropped_lines[i]) { if (out) out[n] = i; n++; }
    return n;
}

int rc__dbg_sprite_dropped_lines(int sprite)
{
    return sprite_ok(sprite) ? g_dropped_lines[sprite] : 0;
}

rc_color rc__dbg_pixel(int x, int y)
{
    if (x < 0 || x >= RC_SCREEN_WIDTH || y < 0 || y >= RC_SCREEN_HEIGHT) return 0;
    return g_frame[y][x];
}
