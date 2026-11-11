/* Native entry point for eval_detector.py. Input files contain LE ABI frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tinyrt_audio.h"
#include "blow_params.h" /* Generated from checked numeric overrides for this run. */

_Static_assert(sizeof(tinyrt_mic_frame_t) == 32, "unexpected feature ABI");

static int calibrate(const char *path, int reference, ta_calib_t *cal) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    ta_calib_window_t window = {0};
    tinyrt_mic_frame_t f;
    int result = 0;
    while (!result && fread(&f, sizeof(f), 1, fp) == 1)
        result = ta_calib_push(&window, &f, reference, cal);
    fclose(fp);
    return result;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "calibrate") == 0) {
        ta_calib_t cal = {0};
        if (calibrate(argv[2], 0, &cal) != 1 || calibrate(argv[3], 1, &cal) != 1) {
            fputs("Need 100 clean quiet frames, then 30 clean reference frames at least 6 dB louder.\n", stderr);
            return 1;
        }
        printf("{\"floor_db_q8\":%d,\"ref_db_q8\":%d,\"ok\":1}\n", cal.floor_db_q8, cal.ref_db_q8);
        return 0;
    }
    if (argc != 3 && argc != 5) return 2;
    FILE *fp = fopen(argv[2], "rb");
    if (!fp) return 1;
    ta_calib_t cal = {0};
    if (argc == 5) {
        cal.floor_db_q8 = (int16_t)atoi(argv[3]);
        cal.ref_db_q8 = (int16_t)atoi(argv[4]);
        cal.ok = 1;
    }
    ta_blow_t blow;
    ta_clap_t clap;
    ta_scream_t scream;
    ta_pitch_t pitch;
    ta_blow_cfg_t cfg = ta_blow_default;
    apply_params(&cfg);
    ta_blow_init(&blow, &cfg, &cal);
    ta_clap_init(&clap);
    ta_scream_init(&scream);
    ta_pitch_init(&pitch);
    tinyrt_mic_frame_t f;
    while (fread(&f, sizeof(f), 1, fp) == 1) {
        ta_output_t out;
        int hz_q4 = 0;
        if (strcmp(argv[1], "blow") == 0) out = ta_blow_push(&blow, &f);
        else if (strcmp(argv[1], "clap") == 0) out = ta_clap_push(&clap, &f);
        else if (strcmp(argv[1], "scream") == 0) out = ta_scream_push(&scream, &f);
        else {
            ta_pitch_output_t p = ta_pitch_push(&pitch, &f);
            out = (ta_output_t){p.valid, p.confidence_q15, 0, p.valid};
            hz_q4 = p.pitch_hz_q4;
        }
        printf("%d,%d,%d,%d,%d\n", out.active, out.valid, out.score_q15, out.strength_q15, hz_q4);
    }
    int error = ferror(fp);
    fclose(fp);
    return error ? 1 : 0;
}
