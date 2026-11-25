/* input.c - held/pressed/released state and the opposite-direction rule. */

#include <string.h>

#include "internal.h"

static uint8_t  g_physical, g_held, g_pressed, g_released;
static uint8_t  g_seen;          /* down inputs the game has seen as active */
static uint32_t g_order[RC_BUTTON_COUNT]; /* press order per input bit */
static uint32_t g_clock;
static bool     g_feeding;
static uint8_t  g_feed;

static int bit_index(uint8_t mask)
{
    int i = 0;
    while (!(mask & 1)) { mask >>= 1; i++; }
    return i;
}

/* If both opposite directions are down, removes the older one from active. */
static uint8_t resolve(uint8_t active, uint8_t a, uint8_t b)
{
    if ((active & a) && (active & b))
        active &= (uint8_t)~(g_order[bit_index(a)] > g_order[bit_index(b)] ? b : a);
    return active;
}

void rc__input_reset(void)
{
    g_physical = g_held = g_pressed = g_released = g_seen = 0;
    memset(g_order, 0, sizeof g_order);
    g_clock = 0;
    g_feeding = false;
    g_feed = 0;
}

void rc__input_update(uint8_t physical)
{
    uint8_t down, up, active;
    int i;

    if (g_feeding) physical = g_feed;

    down = (uint8_t)(physical & ~g_physical);
    up   = (uint8_t)(~physical & g_physical);

    /* Ascending bit order: on a same-frame tie, Down beats Up and Right beats Left. */
    for (i = 0; i < RC_BUTTON_COUNT; i++)
        if (down & (1u << i)) g_order[i] = ++g_clock;

    active = resolve(physical, RC_BTN_LEFT, RC_BTN_RIGHT);
    active = resolve(active, RC_BTN_UP, RC_BTN_DOWN);

    g_pressed  = (uint8_t)(down & active);
    g_released = (uint8_t)(up & g_seen);
    g_seen     = (uint8_t)((g_seen & ~up) | active);
    g_held     = active;
    g_physical = physical;
}

bool rc_held(int buttons)     { return (g_held & buttons) != 0; }
bool rc_pressed(int buttons)  { return (g_pressed & buttons) != 0; }
bool rc_released(int buttons) { return (g_released & buttons) != 0; }

void rc_input_feed(int buttons)
{
    g_feeding = true;
    g_feed = (uint8_t)(buttons & RC_BTN_ALL);
}

void rc_input_feed_stop(void) { g_feeding = false; }

void rc__dbg_input(rc_input_info *out)
{
    if (!out) return;
    out->held = g_held;
    out->pressed = g_pressed;
    out->released = g_released;
    out->physical = g_physical;
}
