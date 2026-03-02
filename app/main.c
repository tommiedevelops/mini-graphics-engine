#include <stdio.h>
#include "cpu_engine.h"

#define CLEAR_COLOR (0x87CEEBFF)
#define W_WIDTH (1280)
#define W_HEIGHT (720)
#define W_NAME ("cpu graphics engine")

typedef struct GameData {
	Vec2f mouse_input;
	Vec2f move_input;
	float time;
} GameData;

void on_init(App* app, void* game_data) {
	/* for preparing Assets */

	// plane mesh
	Mesh* bunny_mesh = mesh_parse_from_obj("assets/models/bunny.obj");
	Mesh* plane_mesh = mesh_parse_from_obj("assets/models/plane.obj");
	assets_add_mesh(app->assets, plane_mesh, "plane");
	assets_add_mesh(app->assets, bunny_mesh, "bunny");
	mesh_recalculate_normals(plane_mesh);
	mesh_recalculate_normals(bunny_mesh);

	Vec4f col = (Vec4f){1.0f,0.0f,1.0f,1.0f};
	Vec4f col2 = (Vec4f){0.0f,1.0f,1.0f,1.0f};

	Material* m_pink = material_create(col, NULL, vs_default, fs_toon);
	Material* m_blue = material_create(col2, NULL, vs_default, fs_lit);
	assets_add_material(app->assets, m_pink, "pink");
	assets_add_material(app->assets, m_blue, "blue");
}

static Camera* setup_camera(){
	// Camera Setup
	Transform* cam_tr = transform_default();
	cam_tr->position = (Vec3f){-4.0f, 0.0f, -5.0f};
	Camera* cam  = camera_create(cam_tr, W_WIDTH, W_HEIGHT);
	return cam;
}

static Light* setup_light(){
	Vec3f light_dir = (Vec3f){1.5f, -0.5f, 0.0f};
	Vec4f light_col = (Vec4f){235.0f/255.0f, 80.0f/255.0f, 211/255.0f, 1.0f};
	Light* light = light_create(light_dir, VEC4F_1);
	return light;
}

static void setup_game_data(void* game_data) {
	GameData* gd = (GameData*)game_data;
	gd->time = 0.0f;
	gd->move_input = VEC2F_0;
	gd->mouse_input = VEC2F_0;
}

static void init_scene(App* app, void* game_data) {
	setup_game_data(game_data);

	Camera* cam = setup_camera();
	Light* light = setup_light();
	Scene* scene = scene_create(cam, light);

	app->scene = scene;
}

void on_start(App* app, void* game_data) {
	/* for preparing Scene */
	init_scene(app, game_data);

	Mesh *plane_mesh = assets_get_mesh(app->assets, "plane");
	Mesh *bunny_mesh = assets_get_mesh(app->assets, "bunny");
	Material *pink = assets_get_material(app->assets, "pink");
	Material *blue = assets_get_material(app->assets, "blue");

	Quat rot_90y = quat_angle_axis(-3.14/2, VEC3F_X);
	Quat rot = QUAT_IDENTITY;

	const float height_above_ground = 0.3f;
	Vec3f bunny_pos = (Vec3f){0.0f, height_above_ground, 0.0f};
	GameObj* bunny = game_obj_create (
				transform_create(bunny_pos, QUAT_IDENTITY, VEC3F_1),
				bunny_mesh, pink
			);
		
	Vec3f floor_scale = (Vec3f){3.0f, 1.0f, 3.0f};
	GameObj* floor = game_obj_create(
				transform_create(VEC3F_0, QUAT_IDENTITY, VEC3F_1),
				plane_mesh, blue
			);

	scene_add_game_obj(app->scene, floor, "floor");
	scene_add_game_obj(app->scene, bunny, "bunny");
}

void on_event(App* app, void* game_data, SDL_Event* e) {
	GameData* gd = (GameData*)game_data;

	switch(e->type) {
		case SDL_MOUSEMOTION:
			gd->mouse_input.x += e->motion.xrel;
			gd->mouse_input.y += e->motion.yrel;
			break;
		case SDL_KEYDOWN:
			if(e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) app_shutdown(app);
			break;
		case SDL_MOUSEBUTTONDOWN:
			printf("Mouse Position =  (x=%d, y=%d)\n", e->button.x, e->button.y);
			break;
	}
}

static inline bool approx(float x, float y, float eps) {
	return abs(x-y) < eps;
}

static void handle_movement(Transform* cam_tr, GameData* gd, float dt) {

	const Uint8* kb = SDL_GetKeyboardState(NULL);
	if(kb[SDL_SCANCODE_W]) gd->move_input.x += 1.0f;
	if(kb[SDL_SCANCODE_A]) gd->move_input.y -= 1.0f;
	if(kb[SDL_SCANCODE_S]) gd->move_input.x -= 1.0f;
	if(kb[SDL_SCANCODE_D]) gd->move_input.y += 1.0f; 
	if(kb[SDL_SCANCODE_Q]) cam_tr->position.y -= dt*1.0f;
	if(kb[SDL_SCANCODE_E]) cam_tr->position.y += dt*1.0f;


	float horiz_sens = 0.5f;

	Quat rot = quat_angle_axis(gd->mouse_input.x * horiz_sens * dt, VEC3F_Y);
	transform_apply_rotation(cam_tr, rot);

	Vec3f forward  = quat_get_forward(cam_tr->rotation);
	Vec3f right    = quat_get_right(cam_tr->rotation);

	Vec3f fwd_back   = vec3f_scale(forward, gd->move_input.x);
	Vec3f left_right = vec3f_scale(right, gd->move_input.y);

	Vec3f move_dir = vec3f_add(fwd_back, left_right);

	if(!approx(vec3f_magnitude(move_dir), 0.0f, 0.001)) 
		move_dir = vec3f_normalize(move_dir);

	float speed = 2.0f;

	Vec3f movement = vec3f_scale(move_dir, speed * dt);

	transform_apply_translation(cam_tr, movement);

	// Reset movement input
	gd->move_input = VEC2F_0;
	gd->mouse_input = VEC2F_0;
}

static void rotate_camera(Camera* cam, float dt) {
	float ang_vel = 2.0f;
	Transform* tr = cam->transform;
	float angle = dt * ang_vel;
	Vec3f axis = (Vec3f){0.0f, 1.0f, 0.0f};
	Quat rot = quat_angle_axis(angle, axis);
	transform_apply_rotation(tr,rot);
}

static void rotate_go(GameObj* go, float dt) {
	float ang_vel = 2.0f;
	Transform* tr = go->tr;
	float angle = dt * ang_vel;
	Vec3f axis = (Vec3f){0.0f, 1.0f, 0.0f};
	Quat rot = quat_angle_axis(angle, axis);
	transform_apply_rotation(tr,rot);
}

static void move_bunny(GameObj* go, float dt) {
	float f = 2.0f;
	float A = 0.5f;
	Transform* tr = go->tr;

	tr->position.y = -1.0f + A*sin(f*dt);
}

void on_update(App* app, void* game_data, float dt) {
	GameData* gd = (GameData*)game_data;

	gd->time += dt;

	Scene* scene = app->scene;
	Camera* cam = scene_get_camera(scene);
	GameObj* go = scene_get_game_obj(scene, "bunny");
	rotate_go(go, dt);
	handle_movement(cam->transform, gd, dt);
}

void on_shutdown(App* app, void* game_data) {
	GameData* gd = (GameData*)game_data;
	// free any resources you requested
	scene_destroy(app->scene);
	assets_destroy(app->assets);
}

int main(void) {

	App         app;
	GameData     gd;

	AppVTable v_table = {
		on_init,
		on_start,
		on_event,
		on_update,
		on_shutdown,
		(void*)&gd
	};

	AppCfg cfg = {
		.w_width       = W_WIDTH,
		.w_height      = W_HEIGHT,
		.w_name        = W_NAME,
		.w_clear_color = CLEAR_COLOR
	};

	app_init(&app, &v_table, &cfg);

	app_run(&app);
}
