#include "ext.h" // max
#include "ext_obex.h" // max?
#include <math.h> // exp
#include <stdlib.h> // rand
#include <string.h> // memset()
#ifdef NT
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )
#endif
#define MAXFREQS 128 // arbitrary global value for memory allocation
#define min(a,b) a < b ? a : b

// copied from old ext_mess.h...bad idea??
#define SETFLOAT(ap, x) ((ap)->a_type = A_FLOAT, (ap)->a_w.w_float = (x))
/* ------------------------ disspanner ----------------------------- */

static t_class *disspanner_class;

// for sorting and panning
typedef struct _freq_pan
{
    float frequency;
    int osc_index;
    float pan;
    float amp_l;
    float amp_r;
} freq_pan;

int freq_pan_compar(const void* p1, const void* p2) {
    return ((freq_pan*)p1)->frequency - ((freq_pan*)p2)->frequency;
}

typedef struct _disspanner
{
    t_object x_obj; 	            /* obligatory header */
    freq_pan workspace[MAXFREQS];   // workspace for panning
    t_atom output_p[MAXFREQS];      // local memory for list output
    int limit;                      // current number of frequencies (and number to output)
    float curr_diss;                // dissonance of current panning
    int more_diss;                  // 1 if banging should lead to more dissonance, 0 if more consonance
    void *bang_out;                 // output for bang when changing
    void *pan_out;                  // output for panning
    float halfpi;                   // pi / 2 (used for panning)
} t_disspanner;

t_symbol *ps_list; // needed for list output? based on thresh.c example in max-sdk

void set_more_diss(t_disspanner *x, int t) {
    x->more_diss = t > 0 ? 1 : 0;
}

// function to compute current dissonance based on panning
float get_curr_diss(t_disspanner *x) {
    float diss_l = 0.f;
    float diss_r = 0.f;
    float epsilon = 0.0000001f; // avoid divide by zero
    for (int i = 0; i < x->limit; i++) {
        for (int j = i+1; j < x->limit; j++) {
            // equations from vassilakis paper

            // frequency distance
            float freq_dist = fabs(x->workspace[i].frequency - x->workspace[j].frequency);
            float min_freq = min(x->workspace[i].frequency, x->workspace[j].frequency);
            float scaler = 0.24f / (0.021f*min_freq + 19);
            float F = scaler * freq_dist;
            float Z = exp(-3.5f*F) - exp(-5.75*F);

            // amplitude multipliers
            float amp_l1 = x->workspace[i].amp_l;
            float amp_r1 = x->workspace[i].amp_r;
            float amp_l2 = x->workspace[j].amp_l;
            float amp_r2 = x->workspace[j].amp_r;
            float amp_min_l = min(amp_l1, amp_l2);
            float amp_min_r = min(amp_r1, amp_r2);

            float X_l = powf(amp_l1 * amp_l2, 0.1f);
            float X_r = powf(amp_r1 * amp_r2, 0.1f);

            float Y_l = 0.5f * (powf((2.f * amp_min_l) / (amp_l1 + amp_l2 + epsilon), 3.11));
            float Y_r = 0.5f * (powf((2.f * amp_min_r) / (amp_r1 + amp_r2 + epsilon), 3.11));

            // left and right dissonance
            diss_l += X_l*Y_l*Z;
            diss_r += X_r*Y_r*Z;
        }
    }
    // NOTE: this is probably not accurate! could be modelled?
    return diss_l + diss_r;
}

void send_output(t_disspanner *x, int change) {
    // output pan
    for (int i = 0; i < x->limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_p+osc_index, x->workspace[i].pan);
    }

    outlet_list(x->pan_out, ps_list, x->limit, x->output_p);
    if (change) {
        outlet_bang(x->bang_out);
    }
}

// this sets the frequencies and computes the current dissonance
void set_new_freqs(t_disspanner *x, t_symbol *s, long argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n)
    int limit = argc < MAXFREQS ? argc : MAXFREQS;
    x->limit = limit;

    // set frequencies
    for (int i = 0; i < limit; i++) {
        x->workspace[i].frequency = atom_getfloat(argv+i);
        x->workspace[i].osc_index = i;
    }

    // compute dissonance
    x->curr_diss = get_curr_diss(x);
}

void set_uniform_spread(t_disspanner *x, long s) {
    // don't change pan
    if (s < 1) {
        object_error(x, "Cannot set spread for %d frequencies; must be > 0", s, 0);
        return;
    }

    // initial pan: spread frequencies as input from -1 to 1 (left to right)
    if (s == 1) {
        x->workspace[0].pan = 0.f;
        x->workspace[0].amp_l = cosf(x->halfpi * 0.5f);
        x->workspace[0].amp_r = cosf(x->halfpi * 0.5f);
    }
    else {
        float step = 2.f / (s-1);
        float ppan = -1.f;
        for (int i = 0; i < s; i++) {
            x->workspace[i].pan = ppan;
            float rads = ((0.5f*ppan) + 0.5f) * x->halfpi;
            x->workspace[i].amp_l = cosf(rads);
            x->workspace[i].amp_r = sinf(rads);
            ppan += step;
        }
    }
    send_output(x, 1);
    x->curr_diss = get_curr_diss(x);
}

int randint(int limit) {
    float r = (float)rand() / RAND_MAX;
    return (int)(r*limit);
}

// make an optimization step when receiving a bang
void opt_step(t_disspanner *x) {
    if (x->limit < 3) {
        send_output(x, 0);
        return;
    }

    int change = 0;
    int iters = 0;
    while (change == 0 && iters < 100) {
        int index1 = randint(x->limit);
        int index2 = index1;
        while (index1 == index2) {
            index2 = randint(x->limit);
        }

        // try a swap
        float oldpan1 = x->workspace[index1].pan;
        float oldpan2 = x->workspace[index2].pan;
        float old_amp_l1 = x->workspace[index1].amp_l;
        float old_amp_l2 = x->workspace[index2].amp_l;
        float old_amp_r1 = x->workspace[index1].amp_r;
        float old_amp_r2 = x->workspace[index2].amp_r;

        x->workspace[index1].pan = oldpan2;
        x->workspace[index2].pan = oldpan1;
        x->workspace[index1].amp_l = old_amp_l2;
        x->workspace[index2].amp_l = old_amp_l1;
        x->workspace[index1].amp_r = old_amp_r2;
        x->workspace[index2].amp_r = old_amp_r1;
        float tmp_diss = get_curr_diss(x);
        float diss_diff = tmp_diss - x->curr_diss;
        // change for more consonance
        if (diss_diff < 0 && x->more_diss == 0) {
            change = 1;
        // change for more dissonance
        } else if (diss_diff > 0 && x->more_diss == 1) {
            change = 1;
        }

        // swap back if it was a worse change
        if (change == 0) {
            x->workspace[index1].pan = oldpan1;
            x->workspace[index2].pan = oldpan2;
            x->workspace[index1].amp_l = old_amp_l1;
            x->workspace[index2].amp_l = old_amp_l2;
            x->workspace[index1].amp_r = old_amp_r1;
            x->workspace[index2].amp_r = old_amp_r2;
        } else {
            x->curr_diss = tmp_diss;
        }
        iters++;
    }

    send_output(x, change);
}

// constructor
static void *disspanner_new(long n)
{
    t_disspanner *x = (t_disspanner *)object_alloc(disspanner_class); // create new instance

    intin(x,1); // optimize for consonance or dissonance

    int mem_size = MAXFREQS*sizeof(freq_pan);
    memset(x->workspace, 0, mem_size);

    x->halfpi = M_PI_2; // not really necessary but maybe it's nice to have locally

    x->bang_out = bangout((t_object *)x); // add outlet
    x->pan_out = listout((t_object *)x); // add outlet
    set_uniform_spread(x, n); // initial spread

    return (void *)x;
}

void ext_main(void* r)
{
    ps_list = gensym("list");
    disspanner_class = class_new(
            "disspanner", // class name
            (method)disspanner_new, // constructor
            (method)NULL, // destructor
    	    sizeof(t_disspanner), // class size in bytes
            0L, // graphical representation, depr
            A_DEFLONG, // initial spread
            0 // default value
    );
    class_addmethod(disspanner_class, (method)opt_step, "bang", 0);
    class_addmethod(disspanner_class, (method)set_uniform_spread, "int", A_LONG, 0);
    class_addmethod(disspanner_class, (method)set_new_freqs, "list", A_GIMME, 0);
    class_addmethod(disspanner_class, (method)set_more_diss, "in1", A_LONG, 0);
    class_register(CLASS_BOX, disspanner_class);
}
