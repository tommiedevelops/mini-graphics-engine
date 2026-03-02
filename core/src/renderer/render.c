#include <stdlib.h>
#include <string.h>

#include "game_math/vector.h"
#include "game_math/transformation.h"
#include "game_math/barycentric.h"

#include "asset_manager/material.h"
#include "asset_manager/mesh.h"

#include "clip.h"
#include "triangle.h"
#include "vert_shader.h"
#include "frag_shader.h"
#include "framebuffer.h"
#include "render.h"
#include "rasterize.h"

#include "scene_manager/game_object.h"
#include "scene_manager/camera.h"
#include "scene_manager/scene.h"
#include "scene_manager/transform.h"

Renderer* renderer_create() 
{
	Renderer*   r    = malloc(sizeof(Renderer));
	VSUniforms* vs_u = malloc(sizeof(VSUniforms));
	FSUniforms* fs_u = malloc(sizeof(FSUniforms));
	r->vs_u = vs_u;
	r->fs_u = fs_u;
	return r;
}

void renderer_destroy(Renderer* r) 
{
	if(!r) return;
	free(r->vs_u);
	free(r->fs_u);
	free(r);
}

static void assemble_vertex_shader_input_from_tri(const Mesh* const mesh, 
			                          int tri_idx, VSin in[3]) 
{
	for(int i = 0; i < 3; i++)
	{
		const int* tri_uvs   = mesh->triangle_uvs;
		const int* tri_norms = mesh->triangle_normals;
		const int* tris      = mesh->triangles;

		int pos_idx = tris[tri_idx + i];

		int uv_idx  = tri_uvs 
			    ? tri_uvs[tri_idx + i] 
			    : 0;

		int n_idx   = tri_norms 
			    ? tri_norms[tri_idx + i] 
			    : 0;

		Vec3f pos = mesh->vertices[pos_idx];

		Vec2f uv  =  mesh->uvs && tri_uvs
			  ?  mesh->uvs[uv_idx] 
			  :  VEC2F_0;

		Vec3f n   =  mesh->normals && tri_norms
			  ?  mesh->normals[n_idx] 
			  :  VEC3F_0;
		
		in[i].pos = pos;
		in[i].n   = n;
		in[i].uv  = uv;
	}
}

static void draw_triangle(
		  	   Renderer*    r,      // use this renderer 
		  	   FrameBuffer* fb,     // output to this framebuffer
		  	   Mesh*        mesh,   // draw this mesh
	          	   Material*    mat,    // with this material
		  	   int          tri_idx // this particular triangle of the mesh
			 ) 
{
	VertShaderF vert_shader = mat->vert_shader;
	FragShaderF frag_shader = mat->frag_shader;

	// vertex shader input and output
	VSin  vs_in[3];
	VSout vs_out[3];

	assemble_vertex_shader_input_from_tri(mesh, tri_idx, vs_in);

	// apply the vertex shader and save perspective correct attributes
	for(size_t i = 0; i < 3; ++i) 
	{
		vert_shader(&vs_in[i], &vs_out[i], r->vs_u);

		vs_out[i].w_inv = 1.0f/vs_out[i].pos.w;
		vs_out[i].uv_over_w = vec2f_scale( vs_out[i].uv,
			       		           vs_out[i].w_inv
			              );
		vs_out[i].n_over_w = vec3f_scale( vs_out[i].normal,
						  vs_out[i].w_inv
				     );
	}

	// clip the vertex shader output against the clipping planes
	VSout clip_out[16] = {0};
	int clip_out_n = 0;

	clip_tri_against_clip_planes(vs_out, clip_out, &clip_out_n); 

	// apply the perspective divide and viewport transformation operations
	Mat4 vp = r->vs_u->viewport;
	for(int i = 0; i < clip_out_n; i++) 
	{
		VSout* vert = &clip_out[i];
		vert->pos = vec4f_scale(vert->pos, vert->w_inv);
		vert->pos = mat4_mul_vec4(vp, vert->pos);
	}	

	int num_tris = clip_out_n < 2 ? 0: clip_out_n - 2;

	// re-assemble the clipped vertices into triangles and rasterize
	Triangle tri;
	tri.v[0] = &clip_out[0];

	for(int k = 0; k < num_tris; k++) 
	{	
		tri.v[1] = &clip_out[k+1];
		tri.v[2] = &clip_out[k+2];
		tri.id = k;
		rasterize_triangle(r,fb,&tri,frag_shader);
	}
}

static void draw_game_object(Renderer* r, FrameBuffer* fb, GameObj* go) 
{
	for(int t = 0; t < go->mesh->num_triangles; t++) 
	{
		const int tri_idx = 3*t;
		draw_triangle(r,fb,go->mesh,go->mat,tri_idx);
	}	
}

static void prepare_per_scene_uniforms(Renderer* r, Scene* scene)
{
	const Camera* cam = scene_get_camera(scene);
	const Transform* tr  = camera_get_transform(cam);
	float near = camera_get_near(cam); 
	float far  = camera_get_far(cam);
	float fov  = camera_get_fov(cam);

	const int   width    = camera_get_screen_width(cam);
	const int   height   = camera_get_screen_height(cam);
	const float aspect   = (float)height/(float)width;

	const Mat4  view     = get_view_matrix(tr->position, 
					       tr->rotation, 
					       tr->scale);
	const Mat4  proj     = get_projection_matrix(fov, near, far, aspect);
	const Mat4  viewport = get_viewport_matrix(near, far, width, height);

	r->vs_u->view       = view;
	r->vs_u->proj       = proj;
	r->vs_u->viewport   = viewport;

	r->fs_u->light            = scene_get_light(scene);
	r->fs_u->cam_world_pos    = tr->position;
}

static void prepare_per_game_object_uniforms(Renderer* r, GameObj* go)
{
	const Transform* tr = go->tr;
	r->vs_u->model      = get_model_matrix(tr->position, 
					       tr->rotation, 
					       tr->scale);
	r->fs_u->base_color    = material_get_base_color(go->mat);
	r->fs_u->tex           = material_get_texture(go->mat);
}

void renderer_draw_scene(Renderer* r, FrameBuffer* fb, Scene* scene) 
{
	if(!r || !scene || !scene_get_camera(scene)) return;
	
	prepare_per_scene_uniforms(r,scene);

	const size_t count = scene_get_num_gos(scene);
	GameObj** gos = scene_get_game_obj_arr(scene);

	for(size_t i = 0; i < count; i++) 
	{
#ifdef DEBUG
	printf("gameobject: %ld\n\n", i);
#endif
		GameObj* go = gos[i];
		if(!go || !go->mesh || !go->mat) continue;
		prepare_per_game_object_uniforms(r, go);
		draw_game_object(r, fb, go);
	}
}

