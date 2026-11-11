#ifndef PUFF_INPUT_H
#define PUFF_INPUT_H
#include "tinyrt_audio.h"

typedef struct {
    ta_blow_t detector;
    int peak_db_q8,peak_age,peak_seen,cooldown,strength;
} puff_blow_t;

static void puff_blow_init(puff_blow_t *state,const ta_calib_t *calibration) {
    *state=(puff_blow_t){0};
    ta_blow_cfg_t config=ta_blow_default;
    config.gate.enter_ms=160;
    config.spike_peak=2048;
    ta_blow_init(&state->detector,&config,calibration);
}

static ta_output_t puff_blow_push(puff_blow_t *state,const tinyrt_mic_frame_t *input) {
    tinyrt_mic_frame_t frame=*input;
    if(!state->detector.gate.active) {
        if(!state->peak_seen || frame.rms_db_q8>state->peak_db_q8) {
            state->peak_db_q8=frame.rms_db_q8;state->peak_age=0;state->peak_seen=1;
        } else {
            state->peak_age++;
        }
        // A sharp peak followed by a rapid 10 dB decay is an impact candidate.
        if(state->peak_age<=12 && frame.rms_db_q8<state->peak_db_q8-10*256) {
            state->cooldown=12;state->detector.gate=(ta_gate_t){0};state->peak_seen=0;
        }
        if(state->peak_age>12)state->peak_seen=0;
    } else {
        state->peak_seen=0;
    }
    if(state->cooldown>0)state->cooldown--;
    int low_band=frame.band_q15[0]+frame.band_q15[1];
    if(state->cooldown || low_band<24576 || frame.pitch_conf_q15>14745)
        frame.rms_db_q8=frame.floor_db_q8;
    ta_output_t output=ta_blow_push(&state->detector,&frame);
    if(output.valid && output.active) {
        // Use one consistent absolute dBFS reference, independent of floor tracking.
        int reference=state->detector.calib.ref_db_q8;
        int target=ta_ramp(input->rms_db_q8,reference-6*256,reference+6*256);
        state->strength+=(target-state->strength)/10;
    } else {
        state->strength=0;
        if(!output.valid)state->peak_seen=state->cooldown=0;
    }
    output.strength_q15=state->strength;
    return output;
}
#endif
