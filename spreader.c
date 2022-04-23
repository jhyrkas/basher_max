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
/* ------------------------ spreader~ ----------------------------- */

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
    void *pan_out;                  // output for panning
} t_spreader;

t_symbol *ps_list; // needed for list output? based on thresh.c example in max-sdk

void set_more_diss(t_spreader *x, int t) {
    x->more_diss = t > 0 ? 1 : 0;
}

// function to compute current dissonance based on panning
float get_curr_diss(t_spread *x) {
    // TODO: implement
    return 0.f;
}

// this sets the frequencies and computes the current dissonance
void set_new_freqs(t_spreader *x, t_symbol *s, long argc, t_atom *argv) {
    // argv contains (f1, f2, f3....f_n)
    int limit = argc < MAXFREQS ? argc : MAXFREQS;

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

    // output lists regardless of bashing
    for (int i = 0; i < limit; i++) {
        int osc_index = x->workspace[i].osc_index; // this aligns the inputs and outputs in case of sorting
        SETFLOAT(x->output_p+osc_index, x->workspace[i].pan);
    }

    outlet_list(x->pan_out, ps_list, limit, x->output_p);
}

// make an optimization step when receiving a bang
void opt_step(t_spreader *x) {

}

// constructor
static void *spreader_new(t_symbol *s, int argc, t_atom *argv)
{
    t_spreader *x = (t_spreader *)object_alloc(spreader_class); // create new instance

    intin(x,1); // optimize for consonance or dissonance

    int mem_size = MAXFREQS*sizeof(freq_pan);
    memset(x->workspace, 0, mem_size);

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
