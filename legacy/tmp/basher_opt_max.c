#include "ext.h" // max
#include <math.h> // TODO: needed?
#include <stdlib.h> // malloc(), qsort()
#include <string.h> // memset()
#ifdef NT
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )
#endif
#define MAXFREQS 128 // arbitrary global value for memory allocation
#define max(a,b) a > b ? a : b
/* ------------------------ basher_opt~ ----------------------------- */

static t_class *basher_opt_class;

// for sorting and bashing
typedef struct _as_pairs
{
    float frequency;
    float amplitude;
    int osc_index;
    // temporary variable for accumlated amplitude when fully fusing harmonics (bash_amt = 0)
    float bashed_amp;
} as_pair;

int pair_compar(const void* p1, const void* p2) {
    return ((as_pair*)p1)->frequency - ((as_pair*)p2)->frequency;
}

typedef struct _basher_opt
{
    t_object x_obj; 	            /* obligatory header */
    as_pair workspace[MAXFREQS];    // workspace for bashing
    t_atom output_f[MAXFREQS];      // local memory for frequency output
    t_atom output_a[MAXFREQS];      // local memory for amplitude output
    t_float min_diff;               // min of threshold
    t_float max_diff;               // max of threshold
    t_float bash_amt;               // freq in Hz to bash conflicting harmonic to (0 Hz = set to same freq, 1 Hz = set 1Hz apart, etc) 
    int bash_on;                    // bool: attach a toggle to get bashed or unbashed frequencies
    t_outlet *bash_out;             // output for bashed frequencies
    t_outlet *amp_out;              // output for amplitudes (right now it is just a pass through)
} t_basher_opt;


// NOTE: not really sure if this is the best behavior but oh well

void set_bash_on(t_basher_opt *x, t_floatarg f) {
    x->bash_on = f > 0 ? 1 : 0;
}

// set the threshold min
void set_min(t_basher_opt *x, t_floatarg f) {
    if (f <= 0) {
        pd_error(x, "max diff must be > 0 Hz; ignoring argument");
        return;
    }
    x->min_diff = f; 
    if (f >= x->max_diff) {
        pd_error(x, "min diff must be smaller than max diff, setting max diff to min+1");
        x->max_diff = f+1;
    }
}
// set the threshold max
void set_max(t_basher_opt *x, t_floatarg f) {
    if (f <= 0) {
        pd_error(x, "max diff must be > 0 Hz; ignoring argument");
        return;
    }
    x->max_diff = f; 
    if (f <= x->min_diff) {
        pd_error(x, "max diff must be smaller than min diff, setting min diff to max-1");
        x->min_diff = max(0,f-1);
    }
}

// set the bash amt
void set_amt(t_basher_opt *x, t_floatarg f) { 
    if (f < 0) {
        pd_error(x, "bash amt must be >= 0 Hz; ignoring argument");
        return;
    }
    x->bash_amt = f;
    if (f >= x->min_diff) {
        pd_error(x, "bash amount should be < min diff, proceeding but results may be unexpected.");
    }
}

// set the freqs
void bash_freqs(t_basher_opt *x, t_symbol *s, int argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n, a1, a2, a3....a_n)
    int limit = argc/2 < MAXFREQS ? argc/2 : MAXFREQS;

    // set frequencies
    for (int i = 0; i < limit; i++) {
        x->workspace[i].frequency = atom_getfloat(argv+i);
        x->workspace[i].osc_index = i;
    }
    // set amplitudes
    for (int i = 0; i < limit; i++) {
        x->workspace[i].amplitude = atom_getfloat(argv+limit+i);
        x->workspace[i].bashed_amp = x->workspace[i].amplitude;
    }

    // sorting and conditionals seems like this couldn't be at audio rate without a lot of thinking...


    // slow but thorough bashing: start by merging the smallest difference in range and then continue until no more are found
    if (x->bash_on) {
        int search = 1;
        int iters = 0;
        while (search && iters < 100) {
//          while(search) {
            // sort by frequency
            qsort(x->workspace, limit, sizeof(as_pair), pair_compar);

            // in the special case where frequencies are fully fused, we need to 
            // make sure that oscillators with the same frequency have the right accumulated amplitude
            // NOTE: perhaps this would make sense in other cases too, sort of like accumulating weight in a particular
            // range where harmonics are bashed to?
            if (x->bash_amt == 0.) {
                int range_bottom = 0;
                int range_top = 0;
                float acc_amp = x->workspace[range_bottom].amplitude;
                float freq = x->workspace[range_bottom].frequency;
                for (int i = 1; i < limit; i++) {
                    // keep accumulating amplitude
                    if (x->workspace[i].frequency == freq) {
                        acc_amp += x->workspace[i].amplitude;
                        range_top = i;
                    } else {
                        // set amplitude of range
                        for (int r = range_bottom; r <= range_top; r++) {
                            x->workspace[r].bashed_amp = acc_amp;
                        }
                        // start new range
                        acc_amp = x->workspace[i].amplitude;
                        freq = x->workspace[i].frequency;
                        range_bottom = range_top = i;
                    }
                }

                // last range
                for (int r = range_bottom; r <= range_top; r++) {
                    x->workspace[r].bashed_amp = acc_amp;
                }
            }

            // finding the smallest difference that is eligible for bashing
            int index_l, index_h = 0;
            float min_diff_cur = x->max_diff + 1;
            for (int i = 1; i < limit; i++) {
                t_float c_freq = x->workspace[i].frequency;
                t_float l_freq = x->workspace[i-1].frequency;
                t_float l_diff = c_freq - l_freq;
                if (l_diff > x->min_diff && l_diff <= min_diff_cur) {
                    index_l = i-1; index_h=i;
                    min_diff_cur = l_diff;
                }
            }

            // bashing the difference if it there is one eligible
            if (min_diff_cur <= x->max_diff && min_diff_cur >= x->min_diff) {
                t_float h_freq = x->workspace[index_h].frequency;
                t_float l_freq = x->workspace[index_l].frequency;
                t_float diff = h_freq - l_freq;
                // NOTE: taking abs here....is there something else we should do to
                // handle negative amplitudes? getting into amplitude and phase...
                t_float h_amp = fabsf(x->workspace[index_h].bashed_amp);
                t_float l_amp = fabsf(x->workspace[index_l].bashed_amp);
                if (l_amp > h_amp) {
                    x->workspace[index_h].frequency = l_freq + x->bash_amt;
                }
                else {
                    x->workspace[index_l].frequency = h_freq - x->bash_amt;
                }

            // or stop searching
            } else {
                search = 0;
            }
            iters++;
        }
        if (iters == 1000) {
            post("maxed out iters");
        }
    }

    // output lists regardless of bashing
    for (int i = 0; i < limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_f+osc_index, x->workspace[i].frequency);
        SETFLOAT(x->output_a+osc_index, x->workspace[i].amplitude);
    }

    outlet_list(x->bash_out, &s_list, limit, x->output_f);
    outlet_list(x->amp_out, &s_list, limit, x->output_a);
}

// constructor
static void *basher_opt_new(t_symbol *s, int argc, t_atom *argv)
{
    t_basher_opt *x = (t_basher_opt *)object_alloc(basher_opt_class); // create new instance

    floatin(x,4); // bash amt
    floatin(x,3); // max inlet
    floatin(x,2); // min inlet
    intin(x,1); // toggle

    inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("bash_on_inlet"));
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("min_inlet"));
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("max_inlet"));
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("bash_amt_inlet"));

    int mem_size = MAXFREQS*sizeof(as_pair);
    memset(x->workspace, 0, mem_size);

    x->bash_out = outlet_new(&x->x_obj, &s_list); // add outlet
    x->amp_out = outlet_new(&x->x_obj, &s_list); // add outlet

    // defaults
    x->bash_on = 1;
    x->min_diff = 10;
    x->max_diff = 30;
    x->bash_amt = 3;
    // handling GIMME
    switch (argc) {
        default : // more than 3
        case 4: 
            x->bash_amt = atom_getfloat(argv+3);
        case 3:
            x->max_diff = atom_getfloat(argv+2);
        case 2:
            x->min_diff = atom_getfloat(argv+1);
        case 1:
            x->bash_on = atom_getfloat(argv) > 0 ? 1 : 0;
            break;
        case 0:
            break;
    }

    if (x->min_diff >= x->max_diff || x->min_diff <= 0 || x->max_diff <= 1) {
        pd_error(x, "min diff must be smaller than max diff, min >= 0 Hz and max >= 1 Hz. using default params");
        x->min_diff = 10;
        x->max_diff = 30;
        x->bash_amt = 3;
    }
    return (void *)x;
}

void ext_main(void* r)
{
    basher_opt_class = class_new(
            "basher", // class name
            (method)basher_opt_new, // constructor
            (method)NULL, // destructor
    	    sizeof(t_basher_opt), // class size in bytes
            NULL, // graphical representation, depr
            A_GIMME, // params
            0 // default value
    );
    class_addlist(basher_opt_class, bash_freqs);
    class_addmethod(basher_opt_class, (t_method)set_bash_on, gensym("bash_on_inlet"), A_FLOAT, 0);
    class_addmethod(basher_opt_class, (t_method)set_min, gensym("min_inlet"), A_FLOAT, 0);
    class_addmethod(basher_opt_class, (t_method)set_max, gensym("max_inlet"), A_FLOAT, 0);
    class_addmethod(basher_opt_class, (t_method)set_amt, gensym("bash_amt_inlet"), A_FLOAT, 0);
}
