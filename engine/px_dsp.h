/* OpenPocket — guitar sound engine for the Donner Pocket X / M-Vave (JieLi AC791N)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * px_dsp.h — v3: a preset = 11 blocks; each block has a TYPE, an ON/OFF state and 5 settings 0..100.
 * The UI and the engine share exactly this structure.
 *
 *   default order: IN -> GATE -> COMP -> PITCH -> SFX -> SYN -> [ DRIVE -> AMP ] (x4) -> CAB -> EQ -> MOD -> DELAY
 *                  -> REVERB -> OUT
 *   v29: each preset carries its own block order (order[]); DRIVE and AMP share one oversampled section when AMP
 *   comes right after DRIVE, otherwise each runs its own.
 *
 * Models are voiced from published circuit analyses (ElectroSmash, Electric Druid, Rob Robinette,
 * Duncan Amps, Celestion/Jensen datasheets, Boss/Roland/Ampeg manuals...) — see docs/SOUNDS-AND-CONTROLS.md.
 * Names evoke the models they are inspired by, without using any brand.
 * Single-precision float (AC791N FPU); minimal libm: sinf/cosf/expf/logf/powf/sqrtf (firmware/px_math.c).
 */
#ifndef PX_DSP_H
#define PX_DSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PX_SR
#define PX_SR 44100.0f
#endif
#ifndef PX_OVERSAMPLE
#define PX_OVERSAMPLE 4
#endif
#define PX_MAX_BLOCK 64

/* --------------------------------------------------------------- blocks --- */
enum { PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SFX, PX_B_DRIVE, PX_B_AMP, PX_B_CAB, PX_B_EQ, PX_B_MOD,
       PX_B_DELAY, PX_B_REVERB, PX_B_SYN, PX_NBLK };   /* SYN appended (v29): older presets keep their indices */
#define PX_NPAR 5

enum { PX_CMP_SQUASH, PX_CMP_STUDIO, PX_CMP_OPTO, PX_CMP_SWELL, PX_CMP_COUNT };
enum { PX_PIT_OCTAVER, PX_PIT_POLYOCT, PX_PIT_WHAMMY, PX_PIT_HARMONY, PX_PIT_DETUNE, PX_PIT_LOWOCT, PX_PIT_COUNT };
enum { PX_SFX_AUTOWAH, PX_SFX_LFOWAH, PX_SFX_RING, PX_SFX_CRUSH, PX_SFX_SLICER, PX_SFX_LOFI, PX_SFX_SYNTH, PX_SFX_COUNT };
enum { PX_DRV_TS9, PX_DRV_KLON, PX_DRV_M77, PX_DRV_HM2, PX_DRV_COUNT };
enum { PX_AMP_TWIN, PX_AMP_AC15, PX_AMP_JCM900, PX_AMP_JCM2000, PX_AMP_MARK4, PX_AMP_SHIRLEY, PX_AMP_PROMOD,
       PX_AMP_5150, PX_AMP_COUNT };
/* v37: Neural Amp Modeler A2-Lite (px_nam.c) as one more AMP type, after the 8 fitted amps. Its 5 settings: MODEL,
 * GAIN (into the model, +-12 dB), BASS, MID, TREBLE (after it). Models: PX_NAM_NFACT in the firmware, PX_NAM_NUSER
 * slots the web editor fills with A2 captures (.nam files, e.g. from TONE3000). */
#define PX_AMP_NAM    PX_AMP_COUNT
#define PX_AMP_NTYPES (PX_AMP_COUNT + 1)
#define PX_NAM_NFACT  12
#define PX_NAM_NUSER  16
#define PX_NAM_N      (PX_NAM_NFACT + PX_NAM_NUSER)
enum { PX_CAB_112JN, PX_CAB_212BLUE, PX_CAB_412GB, PX_CAB_412T75, PX_CAB_412V30, PX_CAB_112CREAM, PX_CAB_DI,
       PX_CAB_COUNT };
enum { PX_SYN_ORGAN, PX_SYN_CHURCH, PX_SYN_STRINGS, PX_SYN_CHOIR, PX_SYN_MONO, PX_SYN_PWM, PX_SYN_BASS,
       PX_SYN_12STR, PX_SYN_ACOUSTIC, PX_SYN_SITAR, PX_SYN_BARI, PX_SYN_PAD, PX_SYN_COUNT };
enum { PX_EQ_5BAND, PX_EQ_GEQ, PX_EQ_PARAM, PX_EQ_COUNT };
enum { PX_MOD_CHORUS, PX_MOD_DIMENSION, PX_MOD_FLANGER, PX_MOD_PHASE90, PX_MOD_STONE, PX_MOD_UNIVIBE,
       PX_MOD_OPTOTREM, PX_MOD_BIASTREM, PX_MOD_HARMTREM, PX_MOD_ROTARY, PX_MOD_VIBRATO, PX_MOD_COUNT };
enum { PX_DLY_DIGITAL, PX_DLY_ANALOG, PX_DLY_MEMORY, PX_DLY_TAPE, PX_DLY_SPACE, PX_DLY_REVERSE, PX_DLY_LOFI,
       PX_DLY_DUAL, PX_DLY_COUNT };
enum { PX_REV_ROOM, PX_REV_HALL, PX_REV_CATHEDRAL, PX_REV_PLATE, PX_REV_SPRING, PX_REV_SHIMMER, PX_REV_GATED,
       PX_REV_AMBIENT, PX_REV_COUNT };

/* ---------------------------------------------------------------- preset --- */
typedef struct {
    char    name[14];
    uint8_t on[PX_NBLK];
    uint8_t type[PX_NBLK];
    uint8_t p[PX_NBLK][PX_NPAR];   /* 0..100 */
    uint8_t order[PX_NBLK];        /* processing (and display) order: a permutation of the block indices */
} px_preset_t;

void px_order_default(uint8_t *order);
int  px_order_valid(const uint8_t *order);   /* 1 = a permutation of 0..PX_NBLK-1 */

/* ------------------------------------------------------------ UI catalog --- */
const char *px_block_label(int blk);
const char *px_block_name(int blk);
int         px_ntypes(int blk);
const char *px_type_name(int blk, int type);
int         px_nparams(int blk, int type);
const char *px_param_name(int blk, int type, int k);
/* text for a knob value when a bare 0..100 says little ("+5TH", "440" ms...); returns 0 = show the number */
int         px_type_v34(int blk, int old);   /* presets saved before v34: old type -> new */
#define PX_PATCH_LEVEL 2                /* the preset LEVEL: p[PX_B_GATE][2] ("PRESET LVL" on the GT page), 50 = 0 dB (v36) */
int         px_dly_time_v35(int type, int v);   /* presets saved before v35: delay TIME (20..1000 ms) -> today's knob */
int         px_dly_value_for_ms(int type, float ms);   /* the TIME value nearest to a delay of ms (MIDI clock sync) */
int         px_param_text(int blk, int type, int k, int v, char *buf8);
int         px_preset_count(void);
/* NAM models: the MODEL knob (0..100) <-> model 0..PX_NAM_N-1 (factory first, then the user slots) */
int         px_nam_model(int v);
int         px_nam_knob(int model);
int32_t    *px_nam_scratch(void);                    /* bench: the NAM ring memory as scratch (PXN_TEST_WORDS+) */
int         px_nam_check(uint32_t (*ticks)(void));    /* start-up: NAM routines checked, then (ticks given) its
                                                         fastest way to run chosen (1 = hand routines in use) */
uint32_t    px_nam_tuned(int mode);                   /* that timing: ticks for 256 frames, by mode (0 = n/a) */
void        px_nam_helper(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));   /* the other core */
void        px_nam_fast(void *base, unsigned bytes);   /* memory away from the CPUs' RAM, for the vector unit (before px_nam_check) */
int         px_nam_name(int model, char *buf12);         /* 1 = the model exists ("U5 EMPTY" otherwise) */
/* the user slots, provided by the firmware (flash; px_dsp.c has a weak, empty stand-in):
 * 1 = slot holds a model; its name (11 chars max), its level trim (dB); w (1871 floats) only if w != 0 */
int         px_nam_user(int slot, char *name12, float *level_db, float *w);
void        px_preset_factory(px_preset_t *p, int idx);

/* ---------------------------------------------------------------- state --- */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } px_bq;
typedef struct { float b0, b1, a1, z; } px_op;      /* first-order section (RC) */

#define PX_PS_N 2048                   /* pitch shifter buffer (46 ms) */
#define PX_LO_DEC 8                    /* LOW OCTAVE: its low band at sr / 8 (5.5 kHz) */
#define PX_PAD_DEC 8                   /* SYN PAD: its grains at sr / 8 */
#define PX_PAD_NORM 1.05f              /* SYN PAD: MIX 50 = about the chord's own level */
#ifndef PX_PAD_NG
#define PX_PAD_NG 2                    /* SYN PAD: grains per slice, crossing over in phase (4: 4 dB of wobble, 2: 2.3) */
#endif
#define PX_SYN_CB 1024                 /* SYN: ensemble / doubler / body reflections (23 ms) */
#define PX_DLY_N 89600                 /* 2.03 s of delay (16-bit: 175 KB) */
#define PX_RV_M 16384                  /* reverb memory: one 16-bit ring (32 KB) at half rate, laid out per type */
#define PX_CABM 6                      /* cabinet detail: cone breakup modes (2-pole resonators) */
#define PX_CABT 8                      /* cabinet detail: sparse taps (mic comb, back wave) */

typedef struct {
    float sr;
    px_preset_t cur;

    float dc_x1, dc_y1, odc_x1, odc_y1, dn;          /* dn: alternating 1e-15 anti-denormal offset */

    /* gate */
    float gate_env, gate_g, gate_thr, gate_rel;
    /* comp */
    float cmp_env, cmp_slow, cmp_att, cmp_rel, cmp_k, cmp_make, cmp_mix, cmp_sw, cmp_thr, cmp_rise, cmp_slowk;
    /* pitch: delay-line shifters (2 voices) + analog octaver */
    float ps_buf[PX_PS_N]; int ps_w, ps_nv;
    float ps_ph[2], ps_inc[2], ps_W[2], ps_lvl[2], ps_off[2][2], ps_dry, ps_r, ps_rt, ps_glide, ps_invW;
    px_bq oct_lp, oct_lp2, oct_sublp, oct_sublp2, oct_bp, oct_uphp;
    float oct_env, oct_ff, oct_ff2, oct_prev, oct_dry, oct_s1, oct_s2, oct_up;
    /* LOW OCTAVE: the lowest note below RANGE (or of the whole chord: LOWEST) an octave down, the dry signal
     * untouched. Its low band runs at sr / PX_LO_DEC. */
    px_bq lo_aa[2], lo_sel[3], lo_hp;
    int lo_k;
    float lo_y0, lo_y1, lo_lp, lo_lpk, lo_dry, lo_lvl;
    float lo_g, lo_gk, lo_gn, lo_glp, lo_glk;         /* GROWL: the octave alone through a soft clipper */
    float lo_ax[8][2], lo_ay[8][2], lo_ad, lo_hr, lo_hi;   /* phase halving: the 90-degree pair, the half phase */
    float lo_t[6], lo_tk;                             /* the tilt: the lowest note leads the phase */
    int lo_np, lo_lowest;
    float lo_bx[8][2], lo_by[8][2], lo_bd;            /* a second 90-degree pair, on the band before the tilt */
    float lo_amp, lo_aatk, lo_arel, lo_c;             /* the octave's level, COMP */
    /* special fx */
    float sfx_lo, sfx_bp, sfx_env, sfx_ph, sfx_ph2, sfx_inc, sfx_ff, sfx_prev, sfx_g;
    float sfx_a, sfx_b, sfx_c, sfx_d, sfx_mix, sfx_qd;
    int   sfx_pat, crush_n, crush_k; float crush_v, crush_q;
    px_bq sfx_f1, sfx_f2;
    /* oversampling (set A: amp, or drive + amp together; set B: a drive that is not right before the amp) */
    px_bq os_up, os_up2, os_dn, os_dn2;
    px_bq osb_up, osb_up2, osb_dn, osb_dn2;
    px_bq osc_up, osc_up2, osc_dn, osc_dn2;          /* v37: set C, x2 (a drive before a NAM amp) */
    int drv_os;                                       /* the drive's oversampling: PX_OVERSAMPLE, or 2 before a NAM amp */
    struct { int32_t x1, x2, y1, y2; } fq_cab[8], fq_os[3][4];   /* v37: the integer filters' state (px_fx_int) */
    uint8_t ord[PX_NBLK];
    /* synth / instrument block (v29) */
    union { float syn_buf[PX_PS_N]; int16_t syn_pad[2 * PX_PS_N]; };   /* shifter source (organ, strings...); */
    int syn_w;                                        /* sitar: string combs; PAD: two of its three slices */
    union { float syn_cb[PX_SYN_CB]; int16_t syn_pad2[2 * PX_SYN_CB]; };   /* PAD: its third slice */
    int syn_cw;
    float syn_ph[3], syn_off[3][2], syn_inc[3], syn_W[3], syn_lvl[3]; int syn_nv;
    float syn_dry, syn_out, syn_d8;                    /* dry guitar, effect level, organ 8' (the guitar's own pitch) */
    float syn_env, syn_slow, syn_ea, syn_er, syn_sr2;  /* envelope (fast, slow), coefficients */
    float syn_kg, syn_ka, syn_kr, syn_thr; int syn_key;   /* key gate: gain, attack, release, threshold, held */
    float syn_perc, syn_pk, syn_plvl, syn_click, syn_clk; uint32_t syn_rnd; int syn_snap;
    float syn_lfo[3], syn_linc[2], syn_cdep[2], syn_cbase;
    px_bq syn_f[6]; int syn_nf;
    float syn_lp, syn_lpk, syn_hp, syn_hpk;
    /* tracker + oscillator + ladder filter (mono / pwm / bass synths) */
    float syn_t1, syn_t2, syn_prev, syn_cnt, syn_per, syn_p1, syn_p2, syn_f0, syn_f0t, syn_glk, syn_oph, syn_mul;
    float syn_wave, syn_lad[4], syn_cut, syn_res, syn_ea2, syn_fenv, syn_fdk, syn_amp, syn_ff, syn_ff2, syn_sq;
    float syn_pwm, syn_pw0, syn_pwd;
    /* PAD: a chord caught on a footswitch (px_pad_press) at sr / 8 into one of three 16-bit slices, played as
     * overlapping grains; one slice fades in, one fades out, one records */
    volatile uint32_t pad_press, pad_stop;            /* bumped by the UI and MIDI only (px_pad_press / px_pad_stop) */
    uint32_t pad_seen_p, pad_seen_s;
    int pad_k, pad_cap, pad_capr, pad_wait, pad_skip, pad_act;
    float pad_g[3], pad_gt[3], pad_rk[3], pad_ka, pad_kr, pad_kf, pad_env, pad_slow, pad_mot, pad_mix;
    int pad_attp;
    float pad_gp[3][4], pad_gi[3][4], pad_gt0[3][4];  /* each slice's grains (PX_PAD_NG): start, step, phase */
    px_bq pad_aa[2], pad_tone[2];
    float pad_y0, pad_y1;
    /* sitar */
    int   syn_cl[6], syn_cp[6], syn_c0[6], syn_type; float syn_cs[6], syn_cfb, syn_cdamp, syn_buzz, syn_sym;
    /* drive */
    px_bq drv_hp, drv_shelf, drv_lp, drv_mid, drv_t1, drv_t2, drv_t3;
    float drv_gain, drv_level, drv_blend, drv_bias, drv_off, drv_gate, drv_dcs, drv_dck, drv_shelf_lo, drv_mix;
    int   drv_clip, drv_tm, drv_np, drv_pre, drv_dcb;
    /* amp */
    int   amp_n, amp_hard;
    float amp_g[4], amp_bias[4], amp_boff[4], amp_env[4], amp_envk, amp_bk;   /* env: bias shift (blocking), x depth */
    px_bq amp_tight, amp_bright;
    px_op amp_hp[4], amp_lp[4];
    px_bq ts_low, ts_mid, ts_high, amp_pres, amp_res, amp_voice;
    float amp_sag, amp_sagk, amp_pw, amp_pwn, amp_out;
    /* NAM (v37): one model at a time (its network and history: px_dsp.c, outside this struct) */
    int   amp_nam, nam_idx;                            /* AMP type is NAM; the model loaded (-1: none) */
    float nam_in_g, nam_out_g;
    float nam_buf[PX_MAX_BLOCK];
    /* cabinet */
    px_bq cab[8]; float cab_g; int cab_n;
    /* fine structure (v27: same response as v25's 128-tap FIR, ~4x cheaper): sparse taps + breakup resonators */
    float cab_rb[64], cab_tg[PX_CABT], cab_m[PX_CABM][3], cab_ms[PX_CABM][2], cab_dg;
    uint8_t cab_ti[PX_CABT]; int cab_rw, cab_nt, cab_firon;
    /* eq */
    px_bq eq[5]; int eq_n;
    /* modulation */
    float mod_buf[2048]; int mod_w;
    float mod_ph, mod_inc, mod_depth, mod_mix, mod_fb, mod_base, mod_sweep, mod_shape;
    px_bq mod_lp;
    float ph_z[4], ph_fb, ph_lo, ph_hi, uv_k[4], uv_lamp, uv_up, uv_dn, trem_g, trem_att, trem_rel;
    float rot_h, rot_d, rot_hs, rot_ds, rot_ht, rot_dt, rot_hk, rot_dk, rot_bal;
    /* delay */
    int16_t dly_buf[PX_DLY_N]; int dly_w, dly_type, dly_heads, rev_start, rev_cnt;
    float dly_len, dly_tgt, dly_fb, dly_mix, dly_ph, dly_ph2, dly_inc, dly_inc2, dly_wow, dly_flut, dly_mod;
    float dly_sat, dly_satn, dly_q, dly_hn, dly_hf, rev_xf, dly_env, dly_rq;
    px_bq dly_lp, dly_hp;
    /* reverb (v35): pre-delay + early reflections, 4 input diffusers, then either an 8-line feedback delay network
     * with modulated lines and per-line high-frequency decay, or Dattorro's plate tank (8 units, 2 modulated);
     * all lines in one 16-bit ring that turns once per sample at 22.05 kHz (+ spring dispersion, shimmer, gate) */
    int16_t rv_m[PX_RV_M];
    int   rv_lay, rv_alg;                              /* layout in use (type + 1; 0: none), 0 FDN / 1 plate */
    int   rv_b[13], rv_n[13], rv_ptr, rv_tick;         /* units: 0 pre-delay, 1..4 diffusers, 5..12 lines / tank */
    float rv_sr, rv_xa, rv_y0, rv_y1;                  /* half rate: its rate, the input kept, the last two outputs */
    float rv_d[13], rv_dg[4];                          /* delay of each unit (samples), diffuser gains */
    int   rv_pd, rv_ner, rv_ert[4]; float rv_erg[4];   /* pre-delay, early reflections (taps, gains) */
    float rv_g[8], rv_p[8], rv_s[8];                   /* FDN: loop gain (low), damping pole, damping state */
    float rv_ph1, rv_ph2, rv_inc1, rv_inc2, rv_mdep;   /* modulation: two slow LFOs, depth (samples) */
    float rv_dec, rv_dd2, rv_damp, rv_bw, rv_fl, rv_fr; /* plate: decay, decay diffusion 2, damping, input band, feedback */
    int   rv_tap[14];                                  /* plate: output taps (samples into tank units) */
    float rv_in, rv_out, rv_mix, rv_env, rv_rq, rv_fade;
    float cmp_la[128]; int cmp_lw;                     /* v36: swell look-ahead (128 samples) */
    float btr[PX_NBLK], patch_g;                        /* v36: per-block level trims, preset level */          /* rv_rq: rounding (0.5), or 0 once the tail is very quiet */
    int   rv_gate, rv_hold_n, sp_on; float rv_hold, rv_genv, rv_gg, rv_gate_rel;
    float sp_z[6], sp_a;
    float sh_buf[PX_PS_N]; int sh_w; float sh_ph, sh_out, sh_lp, sh_k, rv_shim, sh_hp, sh_hk;
    px_bq rv_tone, rv_hp;
} px_engine;
int         px_nam_dual(const px_engine *e);          /* 1 = its NAM runs on two cores: process it on the job-giving core */

void px_engine_init(px_engine *e, float sr);
void px_engine_set(px_engine *e, const px_preset_t *p);
void px_pad_press(px_engine *e);   /* PAD: catch the chord being played (footswitch, MIDI CC 64) */
void px_pad_stop(px_engine *e);    /* PAD: fade it out over RELEASE (footswitch held 0.7 s) */
void px_engine_process(px_engine *e, const float *in, float *out, int n);
enum { PX_PART_ALL, PX_PART_PRE, PX_PART_NAM, PX_PART_POST };   /* v37: px_engine_part's stages (see px_dsp.c) */
void px_engine_part(px_engine *e, const float *in, float *out, int n, int part);
extern int px_fx_int;                   /* v37: 1 = the cabinet and drive filters in integer arithmetic (off: slower) */
extern int px_fx_hand;                  /* v37: their loop by hand (checked at start-up) */

#ifdef __cplusplus
}
#endif
#endif
