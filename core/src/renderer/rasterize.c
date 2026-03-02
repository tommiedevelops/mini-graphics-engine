#include "render.h"
#include "frag_shader.h"
#include "game_math/barycentric.h"
#include "game_math/bounds.h"
#include "triangle.h"
#include "framebuffer.h"

typedef struct Vec2i 
{
	int x, y;
} Vec2i;

static inline Vec2i vec2i_sub(Vec2i a, Vec2i b)
{
	return (Vec2i){a.x - b.x, a.y - b.y};
}

void rasterize_pixel(Vec2i P,BaryCoords b, FSin* out, VSout* v[3]) 
{

	// Perspective Correct Attributes
	float w_inv = bary_mix1(b, v[0]->w_inv, v[1]->w_inv, v[2]->w_inv);
	float z = bary_mix1(b, v[0]->pos.z, v[1]->pos.z, v[2]->pos.z);

	float w = 1.0/w_inv;
	float depth = z * w;

	Vec2f uv_over_w = bary_mix2( b
			           , v[0]->uv_over_w
				   , v[1]->uv_over_w
				   , v[2]->uv_over_w
		          );

	Vec2f uv = vec2f_scale(uv_over_w, w);

	Vec3f n_over_w = bary_mix3( b
				, v[0]->n_over_w
				, v[1]->n_over_w
				, v[2]->n_over_w
		       );
	
	Vec3f normal = vec3f_normalize(vec3f_scale(n_over_w, w));

	Vec3f world_pos = bary_mix3( b
				   , v[0]->world_pos
				   , v[1]->world_pos
				   , v[2]->world_pos
				   );

	*out = (FSin) {
		.world_pos = world_pos,
		.normal   = normal,
		.uv       = uv,	
		.depth    = depth
	};

}

static inline int max_i(int a, int b) {return a > b ? a : b; }
static inline int min_i(int a, int b) {return a < b ? a : b; }

static inline int edge_func(Vec2i P, Vec2i A, Vec2i B) 
{
	return (B.x - A.x)*(P.y - A.y) - (B.y - A.y)*(P.x - A.x);
}

static inline Vec2i to_pixel_center(Vec4f p) 
{
	return (Vec2i){ (int)floorf(p.x + 0.5f), (int)floorf(p.y + 0.5f) };
}

typedef struct {
	int e_row;  // edge value (xmin, current y)
	int step_x; // how the edge changes when x++
	int step_y; // how the edge changes when y++
} EdgeStepper;

static inline EdgeStepper make_edge(Vec2i P, Vec2i A, Vec2i B) 
{
	Vec2i d = vec2i_sub(B, A);
	
	return (EdgeStepper) {
		.e_row = edge_func(P, A, B),
		.step_x = -d.y,
		.step_y = d.x
	};
}

typedef struct { int xmin, ymin, xmax, ymax; } Recti;

static Recti convert_bounds_to_ints(Bounds3 b)
{
	Recti r;
	r.xmin = (int)b.xmin;
	r.xmax = (int)(b.xmax + 0.5f);
	r.ymin = (int)b.ymin;
	r.ymax = (int)(b.ymax + 0.5f);
	return r;
}

void rasterize_triangle( Renderer* r
		       , FrameBuffer* fb
		       , Triangle* tri
		       , FragShaderF frag_shader
		       ) 
{
	
	FSin  fs_in;
	FSout fs_out;

	Recti box = convert_bounds_to_ints(tri_get_bounds(tri));	

	Vec2i V0 = to_pixel_center(tri->v[0]->pos); 
	Vec2i V1 = to_pixel_center(tri->v[1]->pos); 
	Vec2i V2 = to_pixel_center(tri->v[2]->pos); 
	Vec2i P  = (Vec2i){box.xmin, box.ymin};

	EdgeStepper e01 = make_edge(P, V0, V1);
	EdgeStepper e12 = make_edge(P, V1, V2);
	EdgeStepper e20 = make_edge(P, V2, V0);

	for(P.y = box.ymin; P.y <= box.ymax; P.y++)
	{
		int e01_xy = e01.e_row;		
		int e12_xy = e12.e_row;
		int e20_xy = e20.e_row;
		
		for(P.x = box.xmin; P.x <= box.xmax; P.x++)
		{

			bool inside_tri  = e01_xy >= 0 
				        && e12_xy >= 0 
				        && e20_xy >= 0;

			if(inside_tri)
			{
				// why does this order work and not any other?
				BaryCoords b = {e12_xy, e20_xy, e01_xy};

				rasterize_pixel(P,b,&fs_in,tri->v);

				frag_shader(&fs_in, &fs_out, r->fs_u);
				uint32_t col = vec4f_to_rgba32(fs_out.color);
				frame_buffer_draw_pixel(fb,P.x,P.y,col,
							      fs_out.depth);
			}

			e01_xy += e01.step_x;
			e12_xy += e12.step_x;
			e20_xy += e20.step_x;
		}

		e01.e_row += e01.step_y;
		e12.e_row += e12.step_y;
		e20.e_row += e20.step_y;
	}

}



