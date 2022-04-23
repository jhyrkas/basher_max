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
#define max(a,b) a > b ? a : b

// copied from old ext_mess.h...bad idea??
#define SETFLOAT(ap, x) ((ap)->a_type = A_FLOAT, (ap)->a_w.w_float = (x))
/* ------------------------ spreader ----------------------------- */

static t_class *spreader_class;

// for sorting and panning
typedef struct _freq_pan
{
    float frequency;
    int osc_index;
    float pan;
} freq_pan;

int freq_pan_compar(const void* p1, const void* p2) {
    return ((freq_pan*)p1)->frequency - ((freq_pan*)p2)->frequency;
}

typedef struct _spreader
{
    t_object x_obj; 	            /* obligatory header */
    freq_pan workspace[MAXFREQS];   // workspace for panning
    t_atom output_p[MAXFREQS];      // local memory for list output
    int limit;                      // current number of frequencies (and number to output)
    float curr_diss;                // dissonance of current panning
    int more_diss;                  // 1 if banging should lead to more dissonance, 0 if more consonance
    void *bang_out;                 // output for bang when changing
    void *pan_out;                  // output for panning
} t_spreader;

t_symbol *ps_list; // needed for list output? based on thresh.c example in max-sdk

void set_more_diss(t_spreader *x, int t) {
    x->more_diss = t > 0 ? 1 : 0;
}

// function to compute current dissonance based on panning
float get_curr_diss(t_spread *x) {
    float diss = 0.f;
    for (int i = 0; i < x->limit; i++) {
        for (int j = i+1; j < x->limit; j++) {
            float distance = absf(x->workspace[i].pan - x->workspace[j].pan);
            float scaler = 0.24f / (0.021f*minf(x->workspace[i].frequency, x->workspace[j].frequency) + 19);
            float F = scaler * absf(x->workspace[i].frequency - x->workspace[j].frequency);
            float diss_ = exp(-3.5f*F) - exp(-5.75*F);
            diss += diss_;
        }
    }
    return diss;
}

// this sets the frequencies and computes the current dissonance
void set_new_freqs(t_spreader *x, t_symbol *s, long argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n)
    int limit = argc < MAXFREQS ? argc : MAXFREQS;
    x->limit = limit;

    // set frequencies
    for (int i = 0; i < limit; i++) {
        x->workspace[i].frequency = atom_getfloat(argv+i);
        x->workspace[i].osc_index = i;
    }

    // initial pan: spread frequencies as input from -1 to 1 (left to right)
    if (limit == 1) {
        x->workspace[0].pan = 0.f;
    }
    else {
        float step = 2.f / (limit-1);
        float ppan = -1.f;
        for (int i = 0; i < limit; i++) {
            x->workspace[i].pan = ppan;
            ppan += step;
        }
    }

    // compute dissonance
    x->curr_diss = get_curr_diss(x);
    
    send_output(x, 1);
}

inline void send_output(t_spreader *x, int change) {
    // output pan
    for (int i = 0; i < x->limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_p+osc_index, x->workspace[i].pan);
    }

    outlet_list(x->pan_out, ps_list, limit, x->output_p);
    if (change) {
        outlet_bang(x->bang_out);
    }
}

void randint(int limit) {
    float r = rand() / float(RAND_MAX);
    return int(r*limit);
}

// make an optimization step when receiving a bang
void opt_step(t_spreader *x) {
    if (x->limit < 3) {
        send_output(x, 0);
        return;
    }

    int index1 = randint(x->limit);
    int index2 = index1;
    while (index1 == index2) {
        index2 = randint(x->limit);
    }

    // try a swap
    float oldpan1 = x->workspace[index1].pan;
    float oldpan2 = x->workspace[index2].pan;

    x->workspace[index1].pan = oldpan2;
    x->workspace[index2].pan = oldpan1;
    float tmp_diss = get_curr_diss(x);
    float diss_diff = tmp_diss - x->curr_diss;
    int change = 0;
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
    }

    send_output(x, change);
}

// constructor
static void *spreader_new(t_symbol *s, int argc, t_atom *argv)
{
    t_spreader *x = (t_spreader *)object_alloc(spreader_class); // create new instance

    intin(x,1); // optimize for consonance or dissonance

    int mem_size = MAXFREQS*sizeof(freq_pan);
    memset(x->workspace, 0, mem_size);

    x->bang_out = bangout((t_object *)x); // add outlet
    x->pan_out = listout((t_object *)x); // add outlet

    // handling GIMME
    // TODO: GIMME needed?
    switch (argc) {
        default : // more than 3
        case 4: 
        case 3:
        case 2:
        case 1:
            break;
        case 0:
            break;
    }

    return (void *)x;
}

void ext_main(void* r)
{
    ps_list = gensym("list");
    spreader_class = class_new(
            "spreader", // class name
            (method)spreader_new, // constructor
            (method)NULL, // destructor
    	    sizeof(t_spreader), // class size in bytes
            0L, // graphical representation, depr
            A_GIMME, // params
            0 // default value
    );
    class_addmethod(spreader_class, (method)opt_step, "bang", A_BANG, 0);
    class_addmethod(spreader_class, (method)set_new_freqs, "list", A_GIMME, 0);
    class_addmethod(spreader_class, (method)set_more_diss, "in1", A_LONG, 0);
    class_register(CLASS_BOX, spreader_class);
}
