/* Integer detectors for the Level 3 microphone's 10 ms feature stream. */
#ifndef TINYRT_AUDIO_H
#define TINYRT_AUDIO_H
#include "tinyrt.h"

typedef struct { int32_t active, score_q15, strength_q15, valid; } ta_output_t;
typedef struct { int16_t floor_db_q8, ref_db_q8; int32_t ok; } ta_calib_t;
typedef struct { uint32_t seq; int seen; } ta_stream_t;

static inline int ta_frame_valid(ta_stream_t *s, const tinyrt_mic_frame_t *f) {
    int contiguous = !s->seen || f->seq == s->seq + 1u;
    s->seq = f->seq;
    s->seen = 1;
    return contiguous && !(f->flags & (TINYRT_MICF_SETTLING | TINYRT_MICF_OVERRUN));
}

/* Start a fresh window for each step. Collect quiet first, then light blowing.
 * The device can store the two signed Q8 results in separate KV slots. */
typedef struct {
    ta_stream_t stream;
    uint16_t frames;
    int32_t sum_db_q8;
} ta_calib_window_t;

static inline int ta_calib_push(ta_calib_window_t *w, const tinyrt_mic_frame_t *f,
                                int reference, ta_calib_t *result) {
    unsigned required = reference ? 30u : 100u;
    if (!ta_frame_valid(&w->stream, f) || (f->flags & (TINYRT_MICF_CLIPPED | TINYRT_MICF_PLAYBACK))
        || (reference && f->rms_db_q8 < result->floor_db_q8 + 6 * 256)) {
        w->sum_db_q8 = 0;
        w->frames = 0;
        return 0;
    }
    w->sum_db_q8 += f->rms_db_q8;
    if (++w->frames < required) return 0;
    int16_t value = (int16_t)(w->sum_db_q8 / (int)required);
    w->frames = 0;
    w->sum_db_q8 = 0;
    result->ok = 0;
    if (reference) {
        result->ref_db_q8 = value;
        result->ok = 1;
        return 1;
    }
    result->floor_db_q8 = value;
    return 1;
}

static inline int ta_ramp(int x, int lo, int hi) {
    if (x <= lo) return 0;
    if (x >= hi) return 32767;
    return (int)(((int64_t)(x - lo) * 32767) / (hi - lo));
}

typedef struct { uint16_t enter_ms, exit_ms; int16_t enter_q15, exit_q15; } ta_gate_cfg_t;
typedef struct { uint16_t duration_ms; int active; } ta_gate_t;

static inline int ta_gate_push(ta_gate_t *g, const ta_gate_cfg_t *cfg, int score) {
    int switching = g->active ? score < cfg->exit_q15 : score >= cfg->enter_q15;
    if (switching) {
        unsigned elapsed = g->duration_ms + 10u;
        if (elapsed >= (g->active ? cfg->exit_ms : cfg->enter_ms)) {
            g->active = !g->active;
            elapsed = 0;
        }
        g->duration_ms = (uint16_t)elapsed;
    } else {
        g->duration_ms = 0;
    }
    return g->active;
}

typedef struct { int32_t lo, hi; uint16_t weight; } ta_ramp_cfg_t;
typedef struct {
    ta_ramp_cfg_t level, unvoiced, low_band, flatness;
    ta_gate_cfg_t gate;
    uint16_t spike_peak, spike_frames, spike_decay_ratio;
    int16_t playback_db_q8;
} ta_blow_cfg_t;

static const ta_blow_cfg_t ta_blow_default = {
    {5 * 256, 13 * 256, 46}, {16384, 26214, 20},
    {13107, 24576, 18}, {6554, 16384, 16},
    {100, 80, 24575, 19660}, 24000, 6, 10, 6 * 256
};
typedef struct {
    ta_stream_t stream;
    ta_gate_t gate;
    ta_blow_cfg_t cfg;
    ta_calib_t calib;
    int strength;
    unsigned spike_age, spike;
} ta_blow_t;

static inline void ta_blow_init(ta_blow_t *s, const ta_blow_cfg_t *cfg, const ta_calib_t *cal) {
    *s = (ta_blow_t){0};
    s->cfg = cfg ? *cfg : ta_blow_default;
    if (cal) s->calib = *cal;
}

static inline int ta_weighted(const ta_ramp_cfg_t *r, int x) {
    return r->weight * ta_ramp(x, r->lo, r->hi);
}

static inline ta_output_t ta_blow_push(ta_blow_t *s, const tinyrt_mic_frame_t *f) {
    ta_output_t out = {0};
    if (!ta_frame_valid(&s->stream, f)) {
        s->gate = (ta_gate_t){0};
        s->strength = 0;
        s->spike = 0;
        return out;
    }
    const ta_blow_cfg_t *c = &s->cfg;
    int margin = f->rms_db_q8 - f->floor_db_q8;
    int level = margin;
    if (!s->gate.active && (f->flags & TINYRT_MICF_PLAYBACK)) level -= c->playback_db_q8;
    /* Weights are percentages, sum 100; endpoints and units are in the spec. */
    int score = (ta_weighted(&c->level, level) + ta_weighted(&c->unvoiced, 32767 - f->pitch_conf_q15)
               + ta_weighted(&c->low_band, f->band_q15[0] + f->band_q15[1])
               + ta_weighted(&c->flatness, f->flatness_q15)) / 100;
    if (!s->gate.active) {
        if (s->spike && ++s->spike_age > c->spike_frames) s->spike = 0;
        if (f->peak >= c->spike_peak && f->peak >= s->spike) {
            s->spike = f->peak;
            s->spike_age = 0;
        } else if (s->spike && (unsigned)f->peak * c->spike_decay_ratio <= s->spike) {
            score = 0;
            s->spike = 0;
        }
    }
    out.valid = 1;
    out.score_q15 = score;
    out.active = ta_gate_push(&s->gate, &c->gate, score);
    if (out.active) {
        s->spike = 0;
        int ref = s->calib.ok ? s->calib.ref_db_q8 - s->calib.floor_db_q8 : 9 * 256;
        int target = ta_ramp(margin, ref, ref + 15 * 256);
        s->strength += (target - s->strength) / 10;
    } else {
        s->strength = 0;
    }
    out.strength_q15 = s->strength;
    return out;
}

typedef struct { ta_stream_t stream; unsigned peak, age, cooldown; } ta_clap_t;
static inline void ta_clap_init(ta_clap_t *s) { *s = (ta_clap_t){0}; }
static inline ta_output_t ta_clap_push(ta_clap_t *s, const tinyrt_mic_frame_t *f) {
    ta_output_t out = {0};
    if (!ta_frame_valid(&s->stream, f)) {
        s->cooldown = s->peak = 0;
        return out;
    }
    out.valid = 1;
    if (s->cooldown && --s->cooldown) return out;
    if (s->peak && ++s->age > 6) s->peak = 0;
    if (f->peak >= 24000 && f->flatness_q15 >= 11469 && f->peak >= s->peak) {
        s->peak = f->peak;
        s->age = 0;
    } else if (s->peak && f->peak * 10u <= s->peak) {
        s->cooldown = 20;
        s->peak = 0;
        out.active = 1;
        out.strength_q15 = out.score_q15 = 32767;
    }
    return out;
}

typedef struct { ta_stream_t stream; ta_gate_t gate; } ta_scream_t;
static inline void ta_scream_init(ta_scream_t *s) { *s = (ta_scream_t){0}; }
static inline ta_output_t ta_scream_push(ta_scream_t *s, const tinyrt_mic_frame_t *f) {
    static const ta_gate_cfg_t cfg = {150, 120, 16384, 16384};
    ta_output_t out = {0};
    if (!ta_frame_valid(&s->stream, f)) {
        s->gate = (ta_gate_t){0};
        return out;
    }
    int margin = f->rms_db_q8 - f->floor_db_q8;
    out.valid = 1;
    if (margin >= 25 * 256 && f->pitch_conf_q15 >= 16384 && f->band_q15[2] >= 8192)
        out.score_q15 = 32767;
    out.active = ta_gate_push(&s->gate, &cfg, out.score_q15);
    if (out.active) out.strength_q15 = ta_ramp(margin, 25 * 256, 40 * 256);
    return out;
}

typedef struct {
    ta_stream_t stream;
    uint16_t hz[5], count, index, missing, median, confidence;
} ta_pitch_t;
typedef struct { int32_t valid, pitch_hz_q4, midi_cents, confidence_q15; } ta_pitch_output_t;
static inline void ta_pitch_init(ta_pitch_t *s) { *s = (ta_pitch_t){0}; }

static inline int ta_midi_cents(unsigned hz_q4) {
    /* Semitone boundaries in Q4 Hz, beginning at concert A. */
    static const int scale[] = {7040,7459,7902,8372,8870,9397,9956,10548,11175,11840,12544,13290,14080};
    int cents = 6900;
    if (hz_q4 == 0) return 0;
    while (hz_q4 < 7040) { hz_q4 *= 2; cents -= 1200; }
    while (hz_q4 >= 14080) { hz_q4 /= 2; cents += 1200; }
    int note = 0;
    while ((int)hz_q4 >= scale[note + 1]) ++note;
    return cents + 100 * note + (100 * ((int)hz_q4 - scale[note])) / (scale[note + 1] - scale[note]);
}

static inline ta_pitch_output_t ta_pitch_push(ta_pitch_t *s, const tinyrt_mic_frame_t *f) {
    if (!ta_frame_valid(&s->stream, f)) {
        s->count = s->index = s->median = s->missing = 0;
    } else if (f->pitch_conf_q15 >= 19661 && f->pitch_hz_q4) {
        uint16_t sorted[5];
        s->hz[s->index] = f->pitch_hz_q4;
        s->index = (s->index + 1) % 5;
        if (s->count < 5) ++s->count;
        for (int i = 0; i < s->count; ++i) {
            int j = i;
            while (j > 0 && sorted[j - 1] > s->hz[i]) { sorted[j] = sorted[j - 1]; --j; }
            sorted[j] = s->hz[i];
        }
        s->median = sorted[s->count / 2];
        s->confidence = f->pitch_conf_q15;
        s->missing = 0;
    } else if (s->median && ++s->missing >= 3) {
        s->count = s->index = s->median = 0;
    }
    return (ta_pitch_output_t){s->median != 0, s->median, ta_midi_cents(s->median),
                               s->median ? s->confidence : 0};
}
#endif
