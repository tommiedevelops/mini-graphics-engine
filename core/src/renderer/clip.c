#include <string.h>
#include <stdlib.h>

#include "game_math/lerp.h"
#include "game_math/plane.h"
#include "renderer/vert_shader.h"
#include "error_log.h"

static const Plane4 kClipPlanes[] = {

	{ // top (y = w)
          .n = {0.0f, -1.0f, 0.0f, 1.0f},
	  .p = {0.0f,  1.0f, 0.0f, 1.0f}
	}, 

	{ // bottom (y = -w)
	  .n = {0.0f,  1.0f, 0.0f, 1.0f},
	  .p = {0.0f, -1.0f, 0.0f, 1.0f}
	},

	{ // left ( x = -w )
	  .n = { 1.0f, 0.0f, 0.0f, 1.0f},
	  .p = {-1.0f, 0.0f, 0.0f, 1.0f}
	},

	{ // right ( x = -w )
	  .n = {-1.0f, 0.0f, 0.0f, 1.0f},
	  .p = {1.0f,  0.0f, 0.0f, 1.0f}
	},

	{ // near ( z = 0 )
	  .n = {0.0f, 0.0f, 1.0f, 0.0f},
	  .p = {0.0f, 0.0f, 0.0f, 1.0f}
	}, 

	{ // far ( z = w )
	  .n = {0.0f, 0.0f, -1.0f, 1.0f},
	  .p = {0.0f, 0.0f, 1.0f, 1.0f}
	}

};

static void compute_intersection(VSout s, VSout e, VSout* i, float t) 
{
	// assumes memory at &i already allocated
	i->pos       = lerp_vec4f(s.pos, e.pos, t);
	i->world_pos = lerp_vec3f(s.world_pos, e.world_pos, t);
	i->normal    = lerp_vec3f(s.normal, e.normal, t);
	i->uv_over_w = lerp_vec2f(s.uv_over_w, e.uv_over_w, t);
	i->w_inv     = lerp_float(s.w_inv, e.w_inv, t);
}

static inline void write_vert_to_out(VSout* v, VSout* out, int* out_n_ptr)
{
	out[(*out_n_ptr)++] = *v;
}

static inline void write_itx_to_out(Plane4* P, VSout* s, VSout* e, VSout* out,
					int* out_n_ptr)
{
	float t = plane4_compute_intersect_t(*P,s->pos,e->pos);

	t = (t < 0.0f) ? 0.0f : t;
	t = (t > 1.0f) ? 1.0f : t;

	VSout* i = &out[(*out_n_ptr)++];
	compute_intersection(*s,*e,i,t);
}

/// Implements the conditional checks in Sutherland Hodgman for a single
/// pair of vertices.
/// See: https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
static void clip_edge(VSout s, VSout e, Plane4 P, VSout* out, int* out_n) 
{

	bool s_inside = plane4_inside(P,s.pos);
	bool e_inside = plane4_inside(P,e.pos); 

	if(s_inside && e_inside) 
		write_vert_to_out(&e, out, out_n);

	if(s_inside && !e_inside) 
		write_itx_to_out(&P,&s,&e,out,out_n);

	if(!s_inside && e_inside) 
	{
		write_itx_to_out(&P,&s,&e,out,out_n);	
		write_vert_to_out(&e, out,out_n);
	}

}

static void clip_against_plane(const Plane4 P, 
		               const VSout* in, const int in_n, 
			       VSout* out, int* out_n_ptr) 
{
	*out_n_ptr = 0;

	for(int v = 0; v < in_n; v++){
		VSout s = in[v];
		VSout e = in[(v+1)%in_n];
		clip_edge(s,e,P,out,out_n_ptr);
	}
}

static inline void swap_ptrs(void** a, void** b) 
{
	void* temp = *a;
	*a = *b;
	*b = temp;
}

void clip_tri_against_clip_planes(const VSout in[3], VSout out[16], int* out_n) 
{
	VSout bufA[16], bufB[16];
	int sizeA = 3, sizeB = 0;

	VSout* in_ptr = bufA;
	VSout* out_ptr = bufB;
	int* in_size_ptr = &sizeA;
	int* out_size_ptr = &sizeB;

	for(int i = 0; i < 3; i++) bufA[i] = in[i];
	
	for(int i = 0; i < 6; i++)
	{
		clip_against_plane(kClipPlanes[i], 
				   in_ptr, *in_size_ptr,
			       	   out_ptr, out_size_ptr);

		swap_ptrs((void*)&in_ptr,(void*)&out_ptr);
		swap_ptrs((void*)&in_size_ptr, (void*)&out_size_ptr);
	}

	for(int i = 0; i < sizeB; i++) out[i] = bufB[i];
	*out_n = sizeB;
}


