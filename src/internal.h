/* internal.h - shared between the SDK's source files. Not part of the API. */

#ifndef RC_INTERNAL_H
#define RC_INTERNAL_H

#ifdef RC_DEBUG
#error "Build the SDK itself without RC_DEBUG. Debug is enabled per program in rc_dev.h."
#endif

#include "rc_dev.h"

/* Display (display.c) */
void rc__display_reset(void);
void rc__display_compose(void);
const uint8_t *rc__display_rgba(void);    /* 160*144*4 bytes, R G B A */

/* Audio (audio.c) */
void rc__audio_reset(void);
void rc__audio_frame(int16_t out[RC_AUDIO_FRAME_SAMPLES * 2]);

/* Input (input.c) */
void rc__input_reset(void);
void rc__input_update(uint8_t physical);

/* Core (core.c) */
void rc__frame_begin(void);
void rc__frame_end(void);
void rc__run_game_frame(void);
int  rc__debug_panel(char *out, int size);   /* returns 0 when the panel is off */
uint32_t rc__warning_count(void);

/* Platform (platform.c): the only file that uses raylib or the browser. */
void    rc__platform_init(const char *title);
void    rc__platform_run(void);
uint8_t rc__platform_poll(void);
void    rc__platform_audio(const int16_t *samples);

#endif
