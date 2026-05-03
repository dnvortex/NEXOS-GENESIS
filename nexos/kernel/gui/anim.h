/* NexOS — kernel/gui/anim.h
 * Integer-only easing helpers for 30-fps kernel animations.
 * All progress values use the range [0, 256]:  0 = start, 256 = complete.
 * No floating-point — safe for -mno-sse / -mno-sse2 kernel builds.
 * MIT License */
#pragma once
#include <stdint.h>

/* ── Clamp ───────────────────────────────────────────────────────────────── */
static inline int anim_clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── Lerp ────────────────────────────────────────────────────────────────── */
static inline int anim_lerp(int a, int b, int t) {
    /* t in [0,256].  Result = a + (b-a)*t/256 */
    return a + (((b - a) * anim_clamp(t, 0, 256)) >> 8);
}

/* ── Easing curves ───────────────────────────────────────────────────────── */

/* Ease-out quadratic — fast start, decelerates to rest */
static inline int anim_ease_out_quad(int p) {
    p = anim_clamp(p, 0, 256);
    return (p * (512 - p)) >> 8;  /* p*(2-p) mapped to 0-256 */
}

/* Ease-out cubic — stronger deceleration */
static inline int anim_ease_out_cubic(int p) {
    p = anim_clamp(p, 0, 256);
    int r = 256 - p;
    return 256 - ((r * r * r) >> 16);
}

/* Ease-in quadratic — slow start, accelerates */
static inline int anim_ease_in_quad(int p) {
    p = anim_clamp(p, 0, 256);
    return (p * p) >> 8;
}

/* Ease-in-out — smooth S-curve */
static inline int anim_ease_in_out(int p) {
    p = anim_clamp(p, 0, 256);
    if (p < 128) return (p * p) >> 6;
    int q = 256 - p;
    return 256 - ((q * q) >> 6);
}

/* Ease-out-back — overshoots slightly then settles (great for pop-in) */
static inline int anim_ease_out_back(int p) {
    p = anim_clamp(p, 0, 256);
    /* Approximation: scale past 256 then come back */
    int v = 256 - p;
    v = (v * v * v) >> 16;             /* cubic */
    v = v - ((v * 40) >> 8);           /* add ~15% overshoot */
    return anim_clamp(256 - v, 0, 280);
}

/* ── Color helpers ───────────────────────────────────────────────────────── */

/* Blend two RGB24 colors.  t=0 → a,  t=256 → b */
static inline uint32_t anim_color_lerp(uint32_t a, uint32_t b, int t) {
    t = anim_clamp(t, 0, 256);
    uint8_t ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba_ = a & 0xFF;
    uint8_t rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb_ = b & 0xFF;
    uint8_t r = (uint8_t)(ra + (((int)(rb - ra) * t) >> 8));
    uint8_t g = (uint8_t)(ga + (((int)(gb - ga) * t) >> 8));
    uint8_t bl= (uint8_t)(ba_ + (((int)(bb_ - ba_) * t) >> 8));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
}

/* Fade an RGB24 color towards black by alpha factor (0=black, 256=original) */
static inline uint32_t anim_color_fade(uint32_t c, int alpha) {
    alpha = anim_clamp(alpha, 0, 256);
    uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * alpha >> 8);
    uint8_t g = (uint8_t)(((c >>  8) & 0xFF) * alpha >> 8);
    uint8_t b = (uint8_t)((c & 0xFF) * alpha >> 8);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ── Triangle wave oscillator ────────────────────────────────────────────── */
/* t = millisecond timestamp,  period_ms = full cycle length.
   Returns 0-256 going up, then 256-0 going down — like a smooth ping-pong. */
static inline int anim_pingpong(uint32_t t, uint32_t period_ms) {
    uint32_t half = period_ms >> 1;
    uint32_t phase = t % period_ms;
    if (phase < half)
        return (int)(phase * 256 / half);
    else
        return (int)((period_ms - phase) * 256 / half);
}
