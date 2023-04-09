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
#define max(a,b) a > b ? a : b

// copied from old ext_mess.h...bad idea??
#define SETFLOAT(ap, x) ((ap)->a_type = A_FLOAT, (ap)->a_w.w_float = (x))
/* ------------------------ whacker~ ----------------------------- */

static t_class *whacker_class;

// for sorting and whacking
typedef struct _as_pairs
{
    float frequency;
    float amplitude;
    int osc_index;
    int eligible; // param to check that we don't whack things more than once
} as_pair;

int pair_compar(const void* p1, const void* p2) {
    return ((as_pair*)p1)->frequency - ((as_pair*)p2)->frequency;
}

typedef struct _whacker
{
    t_object x_obj; 	            /* obligatory header */
    as_pair workspace[MAXFREQS];    // workspace for whacking
    t_atom output_f[MAXFREQS];      // local memory for frequency output
    t_atom output_a[MAXFREQS];      // local memory for amplitude output
    float min_diff;                 // min of threshold
    float max_diff;                 // max of threshold
    float whack_amt;                // figure out what this parameter means
    int whacker_on;                 // bool: attach a toggle to get whacked amplitudes
    void *freq_out;                 // output for frequencies (right now it is just a pass through)
    void *whack_out;                // output for whacked amplitudes
} t_whacker;

t_symbol *ps_list; // needed for list output? based on thresh.c example in max-sdk

void set_whacker_on(t_whacker *x, int t) {
    x->whacker_on = t > 0 ? 1 : 0;
}

// set the threshold min
void set_min(t_whacker *x, double f) {
    if (f <= 0) {
        object_error((t_object *)x, "received %f: min diff must be > 0 Hz; ignoring argument", f);
        return;
    }
    x->min_diff = f; 
    if (f >= x->max_diff) {
        object_error((t_object *)x, "min diff must be smaller than max diff, setting max diff to min+1");
        x->max_diff = f+1;
    }
}
// set the threshold max
void set_max(t_whacker *x, double f) {
    if (f <= 0) {
        object_error((t_object *)x, "received %f: max diff must be > 0 Hz; ignoring argument", f);
        return;
    }
    x->max_diff = f; 
    if (f <= x->min_diff) {
        object_error((t_object *)x, "max diff must be smaller than min diff, setting min diff to max-1");
        x->min_diff = max(0,f-1);
    }
}

// set the whack amt
void set_amt(t_whacker *x, double f) { 
    if (f < 0.f || f > 1.f) {
        object_error((t_object *)x, "received %f: whack_amt must between 0 and 1; ignoring argument", f);
        return;
    }
    x->whack_amt = f;
}

// set the freqs
void whack_amps(t_whacker *x, t_symbol *s, long argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n, a1, a2, a3....a_n)
    int limit = argc/2 < MAXFREQS ? argc/2 : MAXFREQS;

    // set frequencies and amplitudes
    // NOTE: now taking it in pairs (i.e. f1 a1 f2 a2 f3 a3 ....)
    for (int i = 0; i < limit; i++) {
        int ind = 2*i;
        x->workspace[i].frequency = atom_getfloat(argv+ind);
        x->workspace[i].amplitude = atom_getfloat(argv+ind+1);
        x->workspace[i].osc_index = i;
        x->workspace[i].eligible = 1;
    }

    // slow but thorough whacking: start with the smallest difference in range and then continue until no more are found
    if (x->whacker_on) {
        int search = 1;
        int iters = 0;
        while (search && iters < 100) {
            // sort by frequency
            // sorting and conditionals seems like this couldn't be at audio rate without a lot of thinking...
            qsort(x->workspace, limit, sizeof(as_pair), pair_compar);

            // finding the smallest difference that is eligible for whacking
            int index_l, index_h = 0;
            float min_diff_cur = x->max_diff + 1;
            for (int i = 1; i < limit; i++) {
                float c_freq = x->workspace[i].frequency;
                float l_freq = x->workspace[i-1].frequency;
                float l_diff = c_freq - l_freq;
                // only whack if in range and the quieter pair hasn't been whacked already
                int eligible_index = x->workspace[i].amplitude < x->workspace[i-1].amplitude ? i : i-1;
                if (l_diff > x->min_diff && l_diff <= min_diff_cur && x->workspace[eligible_index].eligible) {
                    index_l = i-1; index_h=i;
                    min_diff_cur = l_diff;
                }
            }

            // whacking the amplitudes if it there is an eligible pair
            if (min_diff_cur <= x->max_diff && min_diff_cur >= x->min_diff) {
                float h_freq = x->workspace[index_h].frequency;
                float l_freq = x->workspace[index_l].frequency;
                float diff = h_freq - l_freq;
                // NOTE: taking abs here....is there something else we should do to
                // handle negative amplitudes? getting into amplitude and phase...
                float h_amp = fabsf(x->workspace[index_h].amplitude);
                float l_amp = fabsf(x->workspace[index_l].amplitude);
                int h_sign = 2 * (x->workspace[index_h].amplitude > 0.f) - 1;
                int l_sign = 2 * (x->workspace[index_l].amplitude > 0.f) - 1;
                // whack the lower amplitude so it goes down and the higher amplitude goes up
                if (l_amp > h_amp) {
                    float delta = x->whack_amt * h_amp;
                    x->workspace[index_h].amplitude = h_sign * (h_amp - delta);
                    x->workspace[index_l].amplitude = l_sign * (l_amp + delta);
                    x->workspace[index_h].eligible = 0;
                }
                else {
                    float delta = x->whack_amt * l_amp;
                    x->workspace[index_h].amplitude = h_sign * (h_amp + delta);
                    x->workspace[index_l].amplitude = l_sign * (l_amp - delta);
                    x->workspace[index_l].eligible = 0;
                }

            // or stop searching
            } else {
                search = 0;
            }
            iters++;
        }
    }

    // output lists regardless of whacking
    for (int i = 0; i < limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_f+osc_index, x->workspace[i].frequency);
        SETFLOAT(x->output_a+osc_index, x->workspace[i].amplitude);
    }

    outlet_list(x->whack_out, ps_list, limit, x->output_a);
    outlet_list(x->freq_out, ps_list, limit, x->output_f);
}

// constructor
static void *whacker_new(t_symbol *s, int argc, t_atom *argv)
{
    t_whacker *x = (t_whacker *)object_alloc(whacker_class); // create new instance

    floatin(x,4); // whack amt
    floatin(x,3); // max inlet
    floatin(x,2); // min inlet
    intin(x,1); // toggle

    int mem_size = MAXFREQS*sizeof(as_pair);
    memset(x->workspace, 0, mem_size);

    x->whack_out = listout((t_object *)x); // add outlet
    x->freq_out = listout((t_object *)x); // add outlet

    // defaults
    x->whacker_on = 1;
    x->min_diff = 10;
    x->max_diff = 30;
    x->whack_amt = 0.5f;
    // handling GIMME
    switch (argc) {
        default : // more than 3
        case 4: 
            x->whack_amt = atom_getfloat(argv+3);
        case 3:
            x->max_diff = atom_getfloat(argv+2);
        case 2:
            x->min_diff = atom_getfloat(argv+1);
        case 1:
            x->whacker_on = atom_getfloat(argv) > 0 ? 1 : 0;
            break;
        case 0:
            break;
    }

    if (x->min_diff >= x->max_diff || x->min_diff <= 0 || x->max_diff <= 1 || x->whack_amt < 0.f || x->whack_amt > 1.f) {
        object_error((t_object *)x, "need min_diff < max_diff, min >= 0 Hz and max >= 1 Hz, 0 <= whack_amt <= 1. using default params");
        x->min_diff = 10;
        x->max_diff = 30;
        x->whack_amt = 0.5f;
    }
    return (void *)x;
}

void ext_main(void* r)
{
    ps_list = gensym("list");
    whacker_class = class_new(
            "whacker", // class name
            (method)whacker_new, // constructor
            (method)NULL, // destructor
    	    sizeof(t_whacker), // class size in bytes
            0L, // graphical representation, depr
            A_GIMME, // params
            0 // default value
    );
    class_addmethod(whacker_class, (method)whack_amps, "list", A_GIMME, 0);
    class_addmethod(whacker_class, (method)set_whacker_on, "in1", A_LONG, 0);
    class_addmethod(whacker_class, (method)set_min, "ft2", A_FLOAT, 0);
    class_addmethod(whacker_class, (method)set_max, "ft3", A_FLOAT, 0);
    class_addmethod(whacker_class, (method)set_amt, "ft4", A_FLOAT, 0);
    class_register(CLASS_BOX, whacker_class);
}
