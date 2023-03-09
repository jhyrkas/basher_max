#include "ext.h" // max
#include "ext_obex.h" // max?
#include <math.h> // TODO: needed?
#include <stdlib.h> // qsort()
#include <string.h> // memset()
#ifdef NT
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )
#endif
#define MAXFREQS 128 // arbitrary global value for memory allocation
#define NUMBARKS 25
#define max(a,b) a > b ? a : b
#define min(a,b) a < b ? a : b

// copied from old ext_mess.h...bad idea??
#define SETFLOAT(ap, x) ((ap)->a_type = A_FLOAT, (ap)->a_w.w_float = (x))
/* ------------------------ basher_cb~ ----------------------------- */

// num_barks = 25
static const float barks[] = {20., 100., 200., 300., 400., 510., 630., 770., 920., 1080., 1270., 1480., \
                1720., 2000., 2320., 2700., 3150., 3700., 4400., 5300., 6400., 7700., 9500., 12000., 15500.};

static t_class *basher_cb_class;

// for sorting and bashing
typedef struct _as_pairs
{
    float frequency;
    float amplitude;
    int osc_index;
    // temporary variable for accumlated amplitude when fully fusing harmonics (bash_amt = 0)
    int bashed;
} as_pair;

int pair_compar(const void* p1, const void* p2) {
    return ((as_pair*)p1)->frequency - ((as_pair*)p2)->frequency;
}

// this could be a bad idea but hopefully it doesn't take up too much time looping
float get_cb(const float f1, const float f2) {
    int cb1 = 1;
    int cb2 = 1;
    for (int i = 0; i < NUMBARKS-1; i++) {
        cb1 = f1 > barks[i] ? i : cb1;
        cb2 = f2 > barks[i] ? i : cb2;
    }

    return ((barks[cb1+1] - barks[cb1]) + (barks[cb2+1] - barks[cb2]))*0.5;
}

float roughness_sethares(const float f1, const float f2) {
    const float a = -3.5;
    const float b = -5.75;
    const float d = 0.24;
    const float s1 = 0.021;
    const float s2 = 19;
    const float s = d / (s1 * min(f1,f2) + s2);
    const float freq_diff = fabsf(f1-f2);
    return exp(a*s*freq_diff) - exp(b*s*freq_diff);
}

float get_new_freq(const float f_const, const float f_change, float bw_low, float bw_high, bool cons) {
    const float bw = get_cb(f_const, f_change);
    float freq_low = f_change < f_const ? f_const - (bw_high*bw) : f_const + (bw_low*bw);
    float freq_high = f_change < f_const ? f_const - (bw_low*bw) : f_const + (bw_high*bw);

    post("%f %f %f", bw, freq_low, freq_high);

    float freq_step = (freq_high - freq_low) / 100.;
    float r_curr = roughness_sethares(freq_low, f_const);
    float new_freq = freq_low;
    float tmp_freq = freq_low;
    for (int i = 1; i < 100; i++) {
        tmp_freq += freq_step;
        float new_r = roughness_sethares(tmp_freq, f_const);
        if (cons && new_r < r_curr) {
            r_curr = new_r;
            new_freq = tmp_freq;
        } else if (!cons && new_r > r_curr) {
            r_curr = new_r;
            new_freq = tmp_freq;
        }
    }
    return new_freq;
}

typedef struct _basher_cb
{
    t_object x_obj; 	            /* obligatory header */
    as_pair workspace[MAXFREQS];    // workspace for bashing
    t_atom output_f[MAXFREQS];      // local memory for frequency output
    t_atom output_a[MAXFREQS];      // local memory for amplitude output
    float min_bw;                   // min of threshold in critical bw
    float max_bw;                   // max of threshold in critical bw
    int diss;                       // bool: consonance or dissonance mode
    float perc_move;                // percentage the frequency will move to min/max consonance: 0 means basher is off
    void *amp_out;                  // output for amplitudes (right now it is just a pass through)
    void *bash_out;                 // output for bashed frequencies
} t_basher_cb;

t_symbol *ps_list; // needed for list output? based on thresh.c example in max-sdk

void set_perc_move(t_basher_cb *x, double p) {
    if (p < 0. || p > 1.){
        object_error((t_object *)x, "received %f: perc_move must be 0. <= p <= 1.; ignoring argument", p);
        return;
    }
    x->perc_move = p;
}

// set the threshold min
void set_min(t_basher_cb *x, double p) {
    if (p < 0. || p > 1.){
        object_error((t_object *)x, "received %f: min_bw must be 0. <= p <= 1.; ignoring argument", p);
        return;
    }
    if (p >= x->max_bw) {
        object_error((t_object *)x, "min_bw must be smaller than max_bw");
        return;
    }
    x->min_bw = p;
}
// set the threshold max
void set_max(t_basher_cb *x, double p) {
    if (p < 0. || p > 1.){
        object_error((t_object *)x, "received %f: max_bw must be 0. <= p <= 1.; ignoring argument", p);
        return;
    }
    if (p <= x->min_bw) {
        object_error((t_object *)x, "max_bw must be larger than min_bw");
        return;
    }
    x->max_bw = p;
}

// set the bash amt
void set_diss(t_basher_cb *x, int d) {
    x->diss = d > 0 ? 1 : 0;
}

// set the freqs
void bash_freqs(t_basher_cb *x, t_symbol *s, long argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n, a1, a2, a3....a_n)
    int limit = argc/2 < MAXFREQS ? argc/2 : MAXFREQS;

    // set frequencies and amplitudes
    // NOTE: now taking it in pairs (i.e. f1 a1 f2 a2 f3 a3 ....)
    for (int i = 0; i < limit; i++) {
        int ind = 2*i;
        x->workspace[i].frequency = atom_getfloat(argv+ind);
        x->workspace[i].amplitude = atom_getfloat(argv+ind+1);
        x->workspace[i].bashed = 0;
        x->workspace[i].osc_index = i;
    }

    // slow but thorough bashing: start by merging the smallest difference in range and then continue until no more are found
    int search = 1;
    int iters = 0;
    // sort by frequency
    // sorting and conditionals seems like this couldn't be at audio rate without a lot of thinking...
    qsort(x->workspace, limit, sizeof(as_pair), pair_compar);
    while (search && iters < 100) {
        // finding the best candidate for bashing
        // asymptotically n^2 but hopefully not in practice
        int index_l, index_h = 0;
        float max_r = 0;
        for (int i = 0; i < limit-1; i++) {
            for (int j = i+1; j < limit; j++) {
                const float f_i = x->workspace[i].frequency;
                const float f_j = x->workspace[j].frequency;
                if (x->workspace[j].bashed) {continue;}
                float cb = get_cb(f_i, f_j);
                float perc_bw = (f_j - f_i) / cb;
                if (perc_bw < x-> min_bw) {continue;}
                // break loop early
                if (perc_bw > x->max_bw) {break;}
                float r = roughness_sethares(f_i, f_j);
                if (max_r < r) {
                    max_r = r;
                    index_l = i;
                    index_h = j;
                }
            }
        }
        // something is eligible for bashing
        if (index_l != index_h) {
            int low_louder = x->workspace[index_l].amplitude > x->workspace[index_h].amplitude; // bool
            int stable_index = low_louder ? index_l : index_h;
            int change_index = low_louder? index_h : index_l;
            const float old_freq = x->workspace[stable_index].frequency;
            float new_freq = get_new_freq(x->workspace[stable_index].frequency, x->workspace[change_index].frequency, x->min_bw, x->max_bw, x->diss == 0);
            object_post((t_object *)x, "new freq %f", new_freq);
            x->workspace[change_index].frequency = old_freq + x->perc_move * (new_freq - old_freq);
            x->workspace[change_index].bashed = 1;
         // or stop searching
        } else {
            search = 0;
        }
        iters++;
    }

    // output lists regardless of bashing
    for (int i = 0; i < limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_f+osc_index, x->workspace[i].frequency);
        SETFLOAT(x->output_a+osc_index, x->workspace[i].amplitude);
    }

    outlet_list(x->amp_out, ps_list, limit, x->output_a);
    outlet_list(x->bash_out, ps_list, limit, x->output_f);
}

// constructor
static void *basher_cb_new(t_symbol *s, int argc, t_atom *argv)
{
    t_basher_cb *x = (t_basher_cb *)object_alloc(basher_cb_class); // create new instance

    floatin(x,4);   // toggle
    floatin(x,3);   // perc_move
    floatin(x,2);   // max_bw
    floatin(x,1);   // min_bw

    int mem_size = MAXFREQS*sizeof(as_pair);
    memset(x->workspace, 0, mem_size);

    x->amp_out = listout((t_object *)x); // add outlet
    x->bash_out = listout((t_object *)x); // add outlet

    // defaults
    x->min_bw = .1;
    x->max_bw = .35;
    x->perc_move = 0.;
    x->diss = 0;
    // handling GIMME
    switch (argc) {
        default : // more than 3
        case 4: 
            x->diss = atom_getfloat(argv+3) > 0;
        case 3:
            x->perc_move = atom_getfloat(argv+2);
        case 2:
            x->max_bw = atom_getfloat(argv+1);
        case 1:
            x->min_bw = atom_getfloat(argv);
            break;
        case 0:
            break;
    }

    if (x->min_bw < 0. || x->min_bw > 1. || x->max_bw < 0. || x->max_bw > 1. || x->perc_move < 0. || x->perc_move > 1. || x->min_bw >= x->max_bw) {
        object_error((t_object *)x, "all percents must be 0 <= p <= 1, and minimum must be below maximum. returning to default values");
        x->min_bw = .1;
        x->max_bw = .35;
        x->perc_move = 0.;
    }
    return (void *)x;
}

void ext_main(void* r)
{
    ps_list = gensym("list");
    basher_cb_class = class_new(
            "basher_cb", // class name
            (method)basher_cb_new, // constructor
            (method)NULL, // destructor
    	    sizeof(t_basher_cb), // class size in bytes
            0L, // graphical representation, depr
            A_GIMME, // params
            0 // default value
    );
    class_addmethod(basher_cb_class, (method)bash_freqs, "list", A_GIMME, 0);
    class_addmethod(basher_cb_class, (method)set_min, "ft1", A_FLOAT, 0);
    class_addmethod(basher_cb_class, (method)set_max, "ft2", A_FLOAT, 0);
    class_addmethod(basher_cb_class, (method)set_perc_move, "ft3", A_FLOAT, 0);
    class_addmethod(basher_cb_class, (method)set_diss, "ft4", A_FLOAT, 0);
    class_register(CLASS_BOX, basher_cb_class);
}
