use std::f32::consts::PI;
use image::{RgbaImage, DynamicImage};
use image::imageops::FilterType;
use std::collections::HashMap;
use std::sync::{OnceLock, Mutex};
use ocl::{Platform, Device, Context, Program, Kernel, Queue, Buffer, flags};
use ocl::prm::Float3;

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
}

#[derive(Clone)]
struct PreparedTriangle {
    draw: super::Draw_components,
    v0: [f32; 3],
    v1: [f32; 3],
    v2: [f32; 3],
    color: [f32; 4],
    uv: [f32; 6],
    tex_key: String,
    has_texture: bool,
}

#[derive(Clone)]
struct BvhNode {
    aabb_min: [f32; 3],
    aabb_max: [f32; 3],
    left: Option<Box<BvhNode>>,
    right: Option<Box<BvhNode>>,
    triangles: Vec<usize>,
    triangle_offset: usize,
}

#[derive(Clone)]
struct FlatBvhNode {
    min: [f32; 3],
    max: [f32; 3],
    left: i32,
    right: i32,
}

#[derive(Clone)]
struct AtlasRect {
    u: f32,
    v: f32,
    w: f32,
    h: f32,
}

#[derive(Clone)]
struct Atlas {
    width: i32,
    height: i32,
    image: RgbaImage,
    rects: HashMap<String, AtlasRect>,
    info: Vec<i32>,
    data: Vec<u8>,
}

struct GeometryBuffers {
    buf_nodes_min: Buffer<f32>,
    buf_nodes_max: Buffer<f32>,
    buf_nodes_left: Buffer<i32>,
    buf_nodes_right: Buffer<i32>,
    buf_indices: Buffer<i32>,
    buf_v0: Buffer<f32>,
    buf_v1: Buffer<f32>,
    buf_v2: Buffer<f32>,
    buf_col: Buffer<f32>,
    buf_uv: Buffer<f32>,
    buf_texid: Buffer<i32>,
    buf_tex_info: Buffer<i32>,
    buf_tex_data: Buffer<u8>,
    num_nodes: i32,
    num_tris: i32,
    num_tex: i32,
}

static Baked_static_triangles: Mutex<Vec<PreparedTriangle>> = Mutex::new(Vec::new());
static Baked_static_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);
static Baked_static_atlas: Mutex<Option<Atlas>> = Mutex::new(None);
static Static_baked: Mutex<bool> = Mutex::new(false);
static Baked_dynamic_triangles: Mutex<Vec<PreparedTriangle>> = Mutex::new(Vec::new());
static Baked_all_triangles: Mutex<Vec<PreparedTriangle>> = Mutex::new(Vec::new());
static Baked_combined_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);
static Baked_combined_atlas: Mutex<Option<Atlas>> = Mutex::new(None);
pub static Dynamic_changed: Mutex<bool> = Mutex::new(true);

struct OpenCLState {
    context: Context,
    render_program: Program,
    queue: Queue,
}

static OPENCL_STATE: OnceLock<Mutex<Option<OpenCLState>>> = OnceLock::new();

fn get_opencl_state() -> Result<&'static Mutex<Option<OpenCLState>>, String> {
    OPENCL_STATE.get_or_init(|| match init_opencl() {
        Ok(state) => Mutex::new(Some(state)),
        Err(err) => {
            println!("{}", err);
            Mutex::new(None)
        }
    });
    OPENCL_STATE.get().ok_or_else(|| "OpenCL not initialized".to_string())
}

fn init_opencl() -> Result<OpenCLState, String> {
    let platforms = Platform::list();
    if platforms.is_empty() {
        return Err("No OpenCL platforms found".to_string());
    }
    let mut selected_device: Option<Device> = None;
    let mut selected_platform: Option<Platform> = None;
    for platform in platforms.iter() {
        if let Ok(mut devices) = Device::list(*platform, Some(flags::DeviceType::GPU)) {
            if let Some(device) = devices.pop() {
                selected_device = Some(device);
                selected_platform = Some(*platform);
                break;
            }
        }
    }
    if selected_device.is_none() {
        for platform in platforms.iter() {
            if let Ok(mut devices) = Device::list(*platform, Some(flags::DeviceType::CPU)) {
                if let Some(device) = devices.pop() {
                    selected_device = Some(device);
                    selected_platform = Some(*platform);
                    break;
                }
            }
        }
    }
    if selected_device.is_none() {
        for platform in platforms.iter() {
            if let Ok(mut devices) = Device::list(*platform, None) {
                if let Some(device) = devices.pop() {
                    selected_device = Some(device);
                    selected_platform = Some(*platform);
                    break;
                }
            }
        }
    }
    let device = selected_device.ok_or("OpenCL device not found".to_string())?;
    let platform = selected_platform.unwrap();
    match device.info(ocl::enums::DeviceInfo::Name) {
        Ok(name) => println!("OpenCL device: {}", name),
        Err(_) => println!("OpenCL device: (unknown)"),
    }
    let context = Context::builder()
        .platform(platform)
        .devices(device)
        .build()
        .map_err(|e| format!("OpenCL context initialization failed: {}", e))?;
    let render_program = Program::builder()
        .devices(device)
        .src(RENDER_KERNEL_SRC)
        .build(&context)
        .map_err(|e| format!("OpenCL render program initialization failed: {}", e))?;
    let queue = Queue::new(&context, device, None)
        .map_err(|e| format!("OpenCL queue initialization failed: {}", e))?;
    Ok(OpenCLState {
        context,
        render_program,
        queue,
    })
}

const RENDER_KERNEL_SRC: &str = r#"
typedef struct { float t, u, v; int tri_idx; int hit; } RayHit;
int intersect_aabb(float3 origin, float3 dir, float3 box_min, float3 box_max, float t_max, float* t_out) {
float tmin = 0.0f;
float tmax = t_max;
if (fabs(dir.x) < 1e-8f) {
     if (origin.x < box_min.x || origin.x > box_max.x) return 0;
 } else {
     float inv = 1.0f / dir.x;
     float t1 = (box_min.x - origin.x) * inv;
     float t2 = (box_max.x - origin.x) * inv;
     if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
     tmin = fmax(tmin, t1);
     tmax = fmin(tmax, t2);
     if (tmin > tmax) return 0;
 }
 if (fabs(dir.y) < 1e-8f) {
     if (origin.y < box_min.y || origin.y > box_max.y) return 0;
 } else {
     float inv = 1.0f / dir.y;
     float t1 = (box_min.y - origin.y) * inv;
     float t2 = (box_max.y - origin.y) * inv;
     if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
     tmin = fmax(tmin, t1);
     tmax = fmin(tmax, t2);
     if (tmin > tmax) return 0;
 }
 if (fabs(dir.z) < 1e-8f) {
     if (origin.z < box_min.z || origin.z > box_max.z) return 0;
 } else {
     float inv = 1.0f / dir.z;
     float t1 = (box_min.z - origin.z) * inv;
     float t2 = (box_max.z - origin.z) * inv;
     if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
     tmin = fmax(tmin, t1);
     tmax = fmin(tmax, t2);
     if (tmin > tmax) return 0;
 }
 if (tmin <= tmax && tmin < t_max) {
     *t_out = tmin;
     return 1;
 }
 return 0;
}
int intersect_tri(float3 origin, float3 dir, float3 v0, float3 v1, float3 v2, float t_max, float* t, float* u, float* v) {
float3 edge1 = v1 - v0;
float3 edge2 = v2 - v0;
float3 h = cross(dir, edge2);
float a = dot(edge1, h);
if (a > -1e-6f && a < 1e-6f) return 0;
float f = 1.0f / a;
float3 s = origin - v0;
*u = f * dot(s, h);
if (*u < 0.0f || *u > 1.0f) return 0;
float3 q = cross(s, edge1);
*v = f * dot(dir, q);
if (*v < 0.0f || *u + *v > 1.0f) return 0;
*t = f * dot(edge2, q);
return (*t > 1e-6f && *t < t_max);
}
float4 sample_uv_texture(
__global const uchar* tex_data,
__global const int* tex_info,
int tex_id,
float u,
float v
) {
if (tex_id < 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
int info_base = tex_id * 4;
 int tex_w = tex_info[info_base];
 int tex_h = tex_info[info_base + 1];
 int offset = tex_info[info_base + 2];
 if (tex_w <= 0 || tex_h <= 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
 int x = (int)round(u * (float)(tex_w - 1));
 int y = (int)round(v * (float)(tex_h - 1));
 x = clamp(x, 0, tex_w - 1);
 y = clamp(y, 0, tex_h - 1);
 int idx = (offset + y * tex_w + x) * 4;
 return (float4)(
     tex_data[idx] / 255.0f,
     tex_data[idx + 1] / 255.0f,
     tex_data[idx + 2] / 255.0f,
     tex_data[idx + 3] / 255.0f
 );
}
RayHit trace_ray(
float3 origin, float3 dir, float max_dist,
__global const float* nodes_min, __global const float* nodes_max,
__global const int* nodes_left, __global const int* nodes_right,
__global const int* tri_indices,
__global const float* tris_v0, __global const float* tris_v1, __global const float* tris_v2,
int num_tris
) {
RayHit best_hit;
best_hit.hit = 0;
best_hit.t = max_dist;
int stack[64];
 int stack_ptr = 0;
 stack[stack_ptr++] = 0;
 while (stack_ptr > 0) {
     int node_idx = stack[--stack_ptr];
     float3 nmin = (float3)(nodes_min[node_idx * 3], nodes_min[node_idx * 3 + 1], nodes_min[node_idx * 3 + 2]);
     float3 nmax = (float3)(nodes_max[node_idx * 3], nodes_max[node_idx * 3 + 1], nodes_max[node_idx * 3 + 2]);
     float dummy_t;
     if (!intersect_aabb(origin, dir, nmin, nmax, best_hit.t, &dummy_t)) continue;
     int left = nodes_left[node_idx];
     int right = nodes_right[node_idx];
     if (left < 0) {
         int start = -left - 1;
         int count = right;
         for (int j = 0; j < count; j++) {
             int t_idx = tri_indices[start + j];
             float3 tv0 = (float3)(tris_v0[t_idx * 3], tris_v0[t_idx * 3 + 1], tris_v0[t_idx * 3 + 2]);
             float3 tv1 = (float3)(tris_v1[t_idx * 3], tris_v1[t_idx * 3 + 1], tris_v1[t_idx * 3 + 2]);
             float3 tv2 = (float3)(tris_v2[t_idx * 3], tris_v2[t_idx * 3 + 1], tris_v2[t_idx * 3 + 2]);
             float t_hit, u_hit, v_hit;
             if (intersect_tri(origin, dir, tv0, tv1, tv2, best_hit.t, &t_hit, &u_hit, &v_hit)) {
                 best_hit.t = t_hit;
                 best_hit.u = u_hit;
                 best_hit.v = v_hit;
                 best_hit.tri_idx = t_idx;
                 best_hit.hit = 1;
             }
         }
     } else {
         stack[stack_ptr++] = left;
         stack[stack_ptr++] = right;
     }
 }
 return best_hit;
}
__kernel void render_scene(
__global uchar* output,
int width, int height,
float3 cam_origin, float3 cam_fwd, float3 cam_right, float3 cam_up,
float half_tan, float aspect, float max_dist, float3 ambient,
int num_nodes,
__global const float* nodes_min,
__global const float* nodes_max,
__global const int* nodes_left,
__global const int* nodes_right,
int num_tris,
__global const float* tris_v0,
__global const float* tris_v1,
__global const float* tris_v2,
__global const float* tris_color,
__global const float* tris_uv,
__global const int* tris_tex_id,
__global const int* tri_indices,
int num_tex,
__global const int* tex_info,
__global const uchar* tex_data
) {
int gid = get_global_id(0);
if (gid >= width * height) return;
int x = gid % width;
 int y = gid / width;
 float nx = (2.0f * (x + 0.5f) / width) - 1.0f;
 float ny = 1.0f - (2.0f * (y + 0.5f) / height);
 float px = nx * half_tan * aspect;
 float py = ny * half_tan;
 float3 dir = normalize(cam_fwd + cam_right * px + cam_up * py);
 float3 origin = cam_origin;
 float3 throughput = (float3)(1.0f, 1.0f, 1.0f);
 float3 final_color = (float3)(0.0f, 0.0f, 0.0f);
 float remaining_dist = max_dist;
 for (int depth = 0; depth < 64; depth++) {
     RayHit hit = trace_ray(
         origin, dir, remaining_dist,
         nodes_min, nodes_max, nodes_left, nodes_right,
         tri_indices, tris_v0, tris_v1, tris_v2, num_tris
     );
     if (!hit.hit) break;
     int t_idx = hit.tri_idx;
     float3 base_rgb = (float3)(tris_color[t_idx * 4], tris_color[t_idx * 4 + 1], tris_color[t_idx * 4 + 2]);
     float alpha = tris_color[t_idx * 4 + 3];
     int tex_id = tris_tex_id[t_idx];
     if (tex_id >= 0) {
         float uv_w = 1.0f - hit.u - hit.v;
         float u0 = tris_uv[t_idx * 6 + 0];
         float v0 = tris_uv[t_idx * 6 + 1];
         float u1 = tris_uv[t_idx * 6 + 2];
         float v1 = tris_uv[t_idx * 6 + 3];
         float u2 = tris_uv[t_idx * 6 + 4];
         float v2 = tris_uv[t_idx * 6 + 5];
         float tex_u = uv_w * u0 + hit.u * u1 + hit.v * u2;
         float tex_v = uv_w * v0 + hit.u * v1 + hit.v * v2;
         float4 tex_col = sample_uv_texture(tex_data, tex_info, tex_id, tex_u, tex_v);
         base_rgb.x = tex_col.x * base_rgb.x;
         base_rgb.y = tex_col.y * base_rgb.y;
         base_rgb.z = tex_col.z * base_rgb.z;
         alpha = tex_col.w * alpha;
     }
     float3 light_factor = ambient;
     if (light_factor.x <= 0.0f && light_factor.y <= 0.0f && light_factor.z <= 0.0f) {
         light_factor = (float3)(1.0f, 1.0f, 1.0f);
     }
     float3 lit_color = base_rgb * light_factor;
     if (alpha >= 1.0f) {
         final_color += throughput * lit_color;
         break;
     }
     final_color += throughput * lit_color * alpha;
     throughput *= mix((float3)(1.0f, 1.0f, 1.0f), base_rgb, alpha);
     if (throughput.x < 0.01f && throughput.y < 0.01f && throughput.z < 0.01f) break;
     if (depth == 63) break;
     float3 hit_point = origin + dir * hit.t;
     origin = hit_point + dir * 1e-3f;
     remaining_dist -= hit.t;
     if (remaining_dist <= 1e-3f) break;
 }
 int out_idx = gid * 4;
 output[out_idx + 0] = (uchar)round(fmin(fmax(final_color.x, 0.0f), 1.0f) * 255.0f);
 output[out_idx + 1] = (uchar)round(fmin(fmax(final_color.y, 0.0f), 1.0f) * 255.0f);
 output[out_idx + 2] = (uchar)round(fmin(fmax(final_color.z, 0.0f), 1.0f) * 255.0f);
 output[out_idx + 3] = (uchar)255;
}
typedef struct {
int vertex_offset;
int vertex_count;
float offset_x;
float offset_y;
float min_x;
float max_x;
float min_y;
float max_y;
float color_r;
float color_g;
float color_b;
float color_a;
int tex_id;
int symbol;
} Gpu2dObject;
int point_in_polygon(float px, float py, __global const float* vertices, int vertex_offset, int vertex_count, float offset_x, float offset_y) {
int inside = 0;
int n = vertex_count / 2;
for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float xi = vertices[(vertex_offset + 2 * i)] + offset_x;
    float yi = vertices[(vertex_offset + 2 * i + 1)] + offset_y;
    float xj = vertices[(vertex_offset + 2 * j)] + offset_x;
    float yj = vertices[(vertex_offset + 2 * j + 1)] + offset_y;
    int cond1 = ((yi > py) != (yj > py)) ? 1 : 0;
    float intersect_x = (xj - xi) * (py - yi) / (yj - yi) + xi;
    int cond2 = (px < intersect_x) ? 1 : 0;
    if (cond1 && cond2) {
        inside = !inside;
    }
}
return inside;
}
float4 sample_2d_texture(
__global const uchar* tex_data,
__global const int* tex_info,
int tex_id,
float u, float v
) {
if (tex_id < 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
int info_base = tex_id * 4;
 int tex_w = tex_info[info_base];
 int tex_h = tex_info[info_base + 1];
 int offset = tex_info[info_base + 2];
 if (tex_w <= 0 || tex_h <= 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
 int x = (int)round(u * (float)(tex_w - 1));
 int y = (int)round(v * (float)(tex_h - 1));
 x = clamp(x, 0, tex_w - 1);
 y = clamp(y, 0, tex_h - 1);
 int idx = (offset + y * tex_w + x) * 4;
 return (float4)(
     tex_data[idx] / 255.0f,
     tex_data[idx + 1] / 255.0f,
     tex_data[idx + 2] / 255.0f,
     tex_data[idx + 3] / 255.0f
 );
}
__kernel void render_2d(
__global uchar* io_color,
__global int* io_symbol,
int width, int height,
int num_2d_objects,
__global const Gpu2dObject* objects_2d,
__global const float* vertices_2d,
int num_tex,
__global const int* tex_info,
__global const uchar* tex_data
) {
int gid = get_global_id(0);
if (gid >= width * height) return;
int x = gid % width;
 int y = gid / width;
 float px = (float)x;
 float py = (float)y;
 float bg_r = io_color[gid * 4] / 255.0f;
 float bg_g = io_color[gid * 4 + 1] / 255.0f;
 float bg_b = io_color[gid * 4 + 2] / 255.0f;
 float out_r = bg_r;
 float out_g = bg_g;
 float out_b = bg_b;
 int out_symbol = io_symbol[gid];
 for (int i = 0; i < num_2d_objects; i++) {
     Gpu2dObject obj = objects_2d[i];
     if (point_in_polygon(px, py, vertices_2d, obj.vertex_offset, obj.vertex_count, obj.offset_x, obj.offset_y)) {
         float r = obj.color_r;
         float g = obj.color_g;
         float b = obj.color_b;
         float alpha = obj.color_a;
         if (obj.tex_id >= 0) {
             float uv_range_x = fmax(obj.max_x - obj.min_x, 1e-8f);
             float uv_range_y = fmax(obj.max_y - obj.min_y, 1e-8f);
             float u = (px - obj.min_x) / uv_range_x;
             float v = 1.0f - (py - obj.min_y) / uv_range_y;
             float4 tex_col = sample_2d_texture(tex_data, tex_info, obj.tex_id, u, v);
             r = tex_col.x * r;
             g = tex_col.y * g;
             b = tex_col.z * b;
             alpha = tex_col.w * alpha;
         }
         if (alpha >= 1.0f) {
             out_r = r;
             out_g = g;
             out_b = b;
             out_symbol = obj.symbol;
         } else if (alpha > 0.0f) {
             out_r = bg_r * (1.0f - alpha) + r * alpha;
             out_g = bg_g * (1.0f - alpha) + g * alpha;
             out_b = bg_b * (1.0f - alpha) + b * alpha;
             out_symbol = obj.symbol;
         }
         bg_r = out_r;
         bg_g = out_g;
         bg_b = out_b;
     }
 }
 io_color[gid * 4] = (uchar)round(fmin(fmax(out_r, 0.0f), 1.0f) * 255.0f);
 io_color[gid * 4 + 1] = (uchar)round(fmin(fmax(out_g, 0.0f), 1.0f) * 255.0f);
 io_color[gid * 4 + 2] = (uchar)round(fmin(fmax(out_b, 0.0f), 1.0f) * 255.0f);
 io_color[gid * 4 + 3] = (uchar)255;
 io_symbol[gid] = out_symbol;
}
"#;

pub fn To_console() {
    let width = super::Engine_settings.lock().unwrap().window_width as usize;
    let height = super::Engine_settings.lock().unwrap().window_height as usize;
    let screen = super::Screen.lock().unwrap();
    for y in (0..height).rev() {
        for x in 0..width {
            print!(
                "\x1b[38;2;{};{};{}m{}\x1b[0m",
                screen[y][x].pixel_RGBA_color[0],
                screen[y][x].pixel_RGBA_color[1],
                screen[y][x].pixel_RGBA_color[2],
                screen[y][x].pixel_symbol
            );
        }
        println!();
    }
}

fn normalize(v: [f32; 3]) -> [f32; 3] {
    let len = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
    if len < 1e-8 {
        return [0.0, 0.0, 1.0];
    }
    [v[0] / len, v[1] / len, v[2] / len]
}

pub fn forward_from_angles(pitch_deg: f32, yaw_deg: f32) -> [f32; 3] {
    let p = pitch_deg * PI / 180.0;
    let y = yaw_deg * PI / 180.0;
    let (cp, sp) = (p.cos(), p.sin());
    let (sy, cy) = (y.sin(), y.cos());
    normalize([sy * cp, -sp, cy * cp])
}

#[derive(Clone)]
pub struct CameraBasis {
    pub forward: [f32; 3],
    pub right: [f32; 3],
    pub up: [f32; 3],
}

pub fn camera_basis(pitch_deg: f32, yaw_deg: f32, roll_deg: f32) -> CameraBasis {
    let p = pitch_deg * PI / 180.0;
    let y = yaw_deg * PI / 180.0;
    let r = roll_deg * PI / 180.0;
    let (cp, sp) = (p.cos(), p.sin());
    let (cy, sy) = (y.cos(), y.sin());
    let (cr, sr) = (r.cos(), r.sin());
    let m11 = cr;
    let m12 = -sr;
    let m13 = 0.0;
    let m21 = sp * sr;
    let m22 = sp * cr;
    let m23 = cp;
    let m31 = -cp * sr;
    let m32 = -cp * cr;
    let m33 = sp;
    let r00 = cy * m11 + sy * m31;
    let r01 = cy * m12 + sy * m32;
    let r02 = cy * m13 + sy * m33;
    let r10 = m21;
    let r11 = m22;
    let r12 = m23;
    let r20 = -sy * m11 + cy * m31;
    let r21 = -sy * m12 + cy * m32;
    let r22 = -sy * m13 + cy * m33;
    let forward = [r02, r12, r22];
    let right = [r00, r10, r20];
    let up = [r01, r11, r21];
    CameraBasis { forward, right, up }
}

fn dot3(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn rotate_vertex_by_orientation(v: [f32; 3], pitch: f32, yaw: f32, roll: f32) -> [f32; 3] {
    if pitch == 0.0 && yaw == 0.0 && roll == 0.0 {
        return v;
    }
    let zero = camera_basis(0.0, 0.0, 0.0);
    let basis = camera_basis(pitch, yaw, roll);
    let lx = dot3(v, zero.right);
    let ly = dot3(v, zero.up);
    let lz = dot3(v, zero.forward);
    [
        lx * basis.right[0] + ly * basis.up[0] + lz * basis.forward[0],
        lx * basis.right[1] + ly * basis.up[1] + lz * basis.forward[1],
        lx * basis.right[2] + ly * basis.up[2] + lz * basis.forward[2],
    ]
}

fn base64_value(c: u8) -> Option<u8> {
    match c {
        b'A'..=b'Z' => Some(c - b'A'),
        b'a'..=b'z' => Some(c - b'a' + 26),
        b'0'..=b'9' => Some(c - b'0' + 52),
        b'+' => Some(62),
        b'/' => Some(63),
        _ => None,
    }
}

fn base64_decode(input: &str) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    let mut buf: u32 = 0;
    let mut bits: u32 = 0;
    for &c in input.as_bytes() {
        if c == b'=' {
            break;
        }
        if c.is_ascii_whitespace() {
            continue;
        }
        let v = base64_value(c)?;
        buf = (buf << 6) | v as u32;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push(((buf >> bits) & 0xff) as u8);
        }
    }
    Some(out)
}

fn get_xml_attr(tag: &str, name: &str) -> Option<String> {
    let bytes = tag.as_bytes();
    let mut idx = 0;
    while let Some(found) = tag[idx..].find(name) {
        let start = idx + found;
        if start > 0 {
            let prev = bytes[start - 1];
            if !prev.is_ascii_whitespace() && prev != b'"' && prev != b'\'' {
                idx = start + name.len();
                continue;
            }
        }
        let mut j = start + name.len();
        while j < bytes.len() && bytes[j].is_ascii_whitespace() {
            j += 1;
        }
        if j < bytes.len() && bytes[j] == b'=' {
            j += 1;
            while j < bytes.len() && bytes[j].is_ascii_whitespace() {
                j += 1;
            }
            if j < bytes.len() && (bytes[j] == b'"' || bytes[j] == b'\'') {
                let quote = bytes[j] as char;
                j += 1;
                if let Some(end) = tag[j..].find(quote) {
                    return Some(tag[j..j + end].trim().to_string());
                }
            }
        }
        idx = start + name.len();
    }
    None
}

fn parse_svg_length(s: &str) -> Option<f32> {
    let t = s.trim();
    if t.is_empty() {
        return None;
    }
    if t.ends_with('%') {
        return None;
    }
    let t = t.trim_end_matches("px");
    t.parse::<f32>().ok()
}

fn get_svg_tag(content: &str) -> Option<String> {
    let start = content.find("<svg")?;
    let end = content[start..].find('>')?;
    Some(content[start..=start + end].to_string())
}

fn load_svg_rgba(path: &str) -> Option<RgbaImage> {
    let content = std::fs::read_to_string(path).ok()?;
    let svg_tag = get_svg_tag(&content)?;
    let mut width = get_xml_attr(&svg_tag, "width").and_then(|v| parse_svg_length(&v));
    let mut height = get_xml_attr(&svg_tag, "height").and_then(|v| parse_svg_length(&v));
    if width.is_none() || height.is_none() {
        if let Some(vb) = get_xml_attr(&svg_tag, "viewBox") {
            let parts: Vec<&str> = vb
                .split(|c: char| c.is_whitespace() || c == ',')
                .filter(|s| !s.trim().is_empty())
                .collect();
            if parts.len() >= 4 {
                if width.is_none() {
                    width = parts[2].parse::<f32>().ok();
                }
                if height.is_none() {
                    height = parts[3].parse::<f32>().ok();
                }
            }
        }
    }
    let width = width.unwrap_or(1024.0).clamp(1.0, 4096.0) as u32;
    let height = height.unwrap_or(1024.0).clamp(1.0, 4096.0) as u32;
    let mut canvas = RgbaImage::from_pixel(width, height, image::Rgba([255, 255, 255, 255]));
    let mut search_pos = 0;
    while let Some(rel_start) = content[search_pos..].find("<image") {
        let abs_start = search_pos + rel_start;
        let rel_end = match content[abs_start..].find('>') {
            Some(e) => e,
            None => break,
        };
        let abs_end = abs_start + rel_end;
        let tag = &content[abs_start..=abs_end];
        let x = get_xml_attr(tag, "x").and_then(|v| parse_svg_length(&v)).unwrap_or(0.0) as i64;
        let y = get_xml_attr(tag, "y").and_then(|v| parse_svg_length(&v)).unwrap_or(0.0) as i64;
        let w = get_xml_attr(tag, "width").and_then(|v| parse_svg_length(&v));
        let h = get_xml_attr(tag, "height").and_then(|v| parse_svg_length(&v));
        let href = get_xml_attr(tag, "xlink:href").or_else(|| get_xml_attr(tag, "href"));
        if let Some(href) = href {
            let mut img: Option<RgbaImage> = None;
            if href.starts_with("data:") {
                if let Some(comma) = href.find(',') {
                    let meta = &href[..comma];
                    let data = &href[comma + 1..];
                    if meta.contains("base64") {
                        if let Some(bytes) = base64_decode(data) {
                            img = image::load_from_memory(&bytes).ok().map(|d| d.to_rgba8());
                        }
                    }
                }
            } else {
                let resolved = {
                    let p = std::path::Path::new(&href);
                    if p.is_absolute() {
                        href.clone()
                    } else {
                        let base = std::path::Path::new(path).parent();
                        if let Some(dir) = base {
                            dir.join(&href).to_string_lossy().to_string()
                        } else {
                            href.clone()
                        }
                    }
                };
                img = image::open(resolved).ok().map(|d| d.to_rgba8());
            }
            if let Some(mut img) = img {
                if let (Some(w), Some(h)) = (w, h) {
                    if w > 0.0 && h > 0.0 {
                        img = DynamicImage::ImageRgba8(img)
                            .resize_exact(w as u32, h as u32, FilterType::Nearest)
                            .to_rgba8();
                    }
                }
                let iw = img.width();
                let ih = img.height();
                for py in 0..ih {
                    let cy = y + py as i64;
                    if cy < 0 || cy >= canvas.height() as i64 {
                        continue;
                    }
                    for px in 0..iw {
                        let cx = x + px as i64;
                        if cx < 0 || cx >= canvas.width() as i64 {
                            continue;
                        }
                        canvas.put_pixel(cx as u32, cy as u32, *img.get_pixel(px, py));
                    }
                }
            }
        }
        search_pos = abs_end + 1;
    }
    Some(canvas)
}

fn load_texture_any(path: &str) -> Option<RgbaImage> {
    static CACHE: OnceLock<Mutex<HashMap<String, RgbaImage>>> = OnceLock::new();
    let cache = CACHE.get_or_init(|| Mutex::new(HashMap::new()));
    let mut lock = cache.lock().unwrap();
    if let Some(img) = lock.get(path) {
        return Some(img.clone());
    }
    let lower = path.to_lowercase();
    let img = if lower.ends_with(".svg") {
        load_svg_rgba(path)
    } else {
        image::open(path).ok().map(|dyn_img| dyn_img.to_rgba8())
    };
    if let Some(img) = img {
        lock.insert(path.to_string(), img.clone());
        Some(img)
    } else {
        None
    }
}

fn path_is_none(p: &str) -> bool {
    let t = p.trim();
    t.is_empty() || t.eq_ignore_ascii_case("none")
}

fn texture_key_for(tri: &super::Draw_components) -> String {
    if !path_is_none(&tri.draw_texture_path) {
        return tri.draw_texture_path.trim().to_string();
    }
    String::new()
}

fn split_3d_object_into_triangles(object: &super::Draw_components) -> Vec<super::Draw_components> {
    let mut out = Vec::new();
    let verts = &object.draw_vertices;
    let uvs = &object.draw_uvs;
    for i in (0..verts.len()).step_by(9) {
        if i + 8 >= verts.len() {
            break;
        }
        let mut tri_uvs = vec![0.0; 6];
        if i / 3 * 2 + 5 < uvs.len() {
            tri_uvs.copy_from_slice(&uvs[i / 3 * 2..i / 3 * 2 + 6]);
        }
        out.push(super::Draw_components {
            draw_type: object.draw_type.clone(),
            draw_x: object.draw_x,
            draw_y: object.draw_y,
            draw_z: object.draw_z,
            draw_symbol: object.draw_symbol,
            draw_vertices: vec![
                verts[i],
                verts[i + 1],
                verts[i + 2],
                verts[i + 3],
                verts[i + 4],
                verts[i + 5],
                verts[i + 6],
                verts[i + 7],
                verts[i + 8],
            ],
            draw_RGBA_color: object.draw_RGBA_color,
            draw_texture_path: object.draw_texture_path.clone(),
            draw_uvs: tri_uvs,
            pitch: object.pitch,
            yaw: object.yaw,
            roll: object.roll,
            special_properties: String::new(),
            draw_special_name: object.draw_special_name.clone(),
        });
    }
    out
}

fn prepare_triangles(inputs: &[super::Draw_components]) -> Vec<PreparedTriangle> {
    let mut prepared: Vec<PreparedTriangle> = Vec::new();
    for tri in inputs {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let lv0 = [verts[0], verts[1], verts[2]];
        let lv1 = [verts[3], verts[4], verts[5]];
        let lv2 = [verts[6], verts[7], verts[8]];
        let rv0 = rotate_vertex_by_orientation(lv0, tri.pitch, tri.yaw, tri.roll);
        let rv1 = rotate_vertex_by_orientation(lv1, tri.pitch, tri.yaw, tri.roll);
        let rv2 = rotate_vertex_by_orientation(lv2, tri.pitch, tri.yaw, tri.roll);
        let v0 = [rv0[0] + tri.draw_x, rv0[1] + tri.draw_y, rv0[2] + tri.draw_z];
        let v1 = [rv1[0] + tri.draw_x, rv1[1] + tri.draw_y, rv1[2] + tri.draw_z];
        let v2 = [rv2[0] + tri.draw_x, rv2[1] + tri.draw_y, rv2[2] + tri.draw_z];
        let color = [
            tri.draw_RGBA_color[0] as f32 / 255.0,
            tri.draw_RGBA_color[1] as f32 / 255.0,
            tri.draw_RGBA_color[2] as f32 / 255.0,
            tri.draw_RGBA_color[3] as f32 / 255.0,
        ];
        let tex_key = texture_key_for(tri);
        let has_texture = !tex_key.is_empty();
        let mut uv = [0.0; 6];
        if tri.draw_uvs.len() >= 6 {
            uv.copy_from_slice(&tri.draw_uvs[..6]);
        }
        prepared.push(PreparedTriangle {
            draw: tri.clone(),
            v0,
            v1,
            v2,
            color,
            uv,
            tex_key,
            has_texture,
        });
    }
    let mut groups: HashMap<String, Vec<usize>> = HashMap::new();
    for (i, p) in prepared.iter().enumerate() {
        let ident = if p.draw.draw_special_name.trim().is_empty() {
            format!(
                "{}|{}|{}|{}|{}|{}|{}|{}|{}",
                p.draw.draw_type,
                p.draw.draw_texture_path,
                p.draw.draw_x,
                p.draw.draw_y,
                p.draw.draw_z,
                p.draw.pitch,
                p.draw.yaw,
                p.draw.roll,
                p.tex_key
            )
        } else {
            p.draw.draw_special_name.clone()
        };
        let key = format!("{}|{}", ident, p.tex_key);
        groups.entry(key).or_insert_with(Vec::new).push(i);
    }
    for indices in groups.values() {
        if indices.is_empty() {
            continue;
        }
        let mut min = [f32::INFINITY; 3];
        let mut max = [f32::NEG_INFINITY; 3];
        for &idx in indices {
            let p = &prepared[idx];
            for v in [&p.v0, &p.v1, &p.v2] {
                for k in 0..3 {
                    if v[k] < min[k] {
                        min[k] = v[k];
                    }
                    if v[k] > max[k] {
                        max[k] = v[k];
                    }
                }
            }
        }
        let extent = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
        let (axis0, axis1) = if extent[0] >= extent[1] && extent[0] >= extent[2] {
            (0, if extent[1] >= extent[2] { 1 } else { 2 })
        } else if extent[1] >= extent[0] && extent[1] >= extent[2] {
            (1, if extent[0] >= extent[2] { 0 } else { 2 })
        } else {
            (2, if extent[0] >= extent[1] { 0 } else { 1 })
        };
        let mut min_a = f32::INFINITY;
        let mut max_a = f32::NEG_INFINITY;
        let mut min_b = f32::INFINITY;
        let mut max_b = f32::NEG_INFINITY;
        for &idx in indices {
            let p = &prepared[idx];
            for v in [&p.v0, &p.v1, &p.v2] {
                let a = v[axis0];
                let b = v[axis1];
                if a < min_a {
                    min_a = a;
                }
                if a > max_a {
                    max_a = a;
                }
                if b < min_b {
                    min_b = b;
                }
                if b > max_b {
                    max_b = b;
                }
            }
        }
        let range_a = max_a - min_a;
        let range_b = max_b - min_b;
        for &idx in indices {
            let vertices = [prepared[idx].v0, prepared[idx].v1, prepared[idx].v2];
            let mut uv = [0.0f32; 6];
            for i in 0..3 {
                let a = vertices[i][axis0];
                let b = vertices[i][axis1];
                let u = if range_a < 1e-8 {
                    0.0
                } else {
                    (a - min_a) / range_a
                };
                let v = if range_b < 1e-8 {
                    0.0
                } else {
                    1.0 - (b - min_b) / range_b
                };
                uv[i * 2] = u.clamp(0.0, 1.0);
                uv[i * 2 + 1] = v.clamp(0.0, 1.0);
            }
            prepared[idx].uv = uv;
        }
    }
    prepared
}

fn compute_prepared_aabb(tri: &PreparedTriangle) -> ([f32; 3], [f32; 3]) {
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for v in [&tri.v0, &tri.v1, &tri.v2] {
        for i in 0..3 {
            if v[i] < min[i] {
                min[i] = v[i];
            }
            if v[i] > max[i] {
                max[i] = v[i];
            }
        }
    }
    (min, max)
}

fn build_bvh_recursive(
    indices: &mut [usize],
    aabbs: &[([f32; 3], [f32; 3])],
    _triangles: &[PreparedTriangle],
    depth: u32,
    offset: usize,
) -> BvhNode {
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for &i in indices.iter() {
        let (aabb_min, aabb_max) = aabbs[i];
        for k in 0..3 {
            if aabb_min[k] < min[k] {
                min[k] = aabb_min[k];
            }
            if aabb_max[k] > max[k] {
                max[k] = aabb_max[k];
            }
        }
    }
    if indices.len() <= 8 || depth > 20 {
        return BvhNode {
            aabb_min: min,
            aabb_max: max,
            left: None,
            right: None,
            triangles: indices.to_vec(),
            triangle_offset: offset,
        };
    }
    let extent = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    let axis = if extent[0] >= extent[1] && extent[0] >= extent[2] {
        0
    } else if extent[1] >= extent[2] {
        1
    } else {
        2
    };
    indices.sort_by(|&a, &b| {
        let ca = (aabbs[a].0[axis] + aabbs[a].1[axis]) * 0.5;
        let cb = (aabbs[b].0[axis] + aabbs[b].1[axis]) * 0.5;
        ca.partial_cmp(&cb).unwrap()
    });
    let mid = indices.len() / 2;
    let (left_indices, right_indices) = indices.split_at_mut(mid);
    let left = build_bvh_recursive(left_indices, aabbs, _triangles, depth + 1, offset);
    let right = build_bvh_recursive(right_indices, aabbs, _triangles, depth + 1, offset);
    BvhNode {
        aabb_min: min,
        aabb_max: max,
        left: Some(Box::new(left)),
        right: Some(Box::new(right)),
        triangles: Vec::new(),
        triangle_offset: offset,
    }
}

fn build_bvh(triangles: &[PreparedTriangle], offset: usize) -> BvhNode {
    let mut indices: Vec<usize> = (0..triangles.len()).collect();
    let aabbs: Vec<([f32; 3], [f32; 3])> = triangles
        .iter()
        .map(|t| compute_prepared_aabb(t))
        .collect();
    build_bvh_recursive(&mut indices, &aabbs, triangles, 0, offset)
}

fn flatten_bvh(node: &BvhNode, nodes: &mut Vec<FlatBvhNode>, indices: &mut Vec<i32>) -> i32 {
    let current_idx = nodes.len() as i32;
    nodes.push(FlatBvhNode {
        min: node.aabb_min,
        max: node.aabb_max,
        left: 0,
        right: 0,
    });
    if node.triangles.is_empty() {
        if let (Some(left), Some(right)) = (&node.left, &node.right) {
            let left_idx = flatten_bvh(left, nodes, indices);
            let right_idx = flatten_bvh(right, nodes, indices);
            nodes[current_idx as usize].left = left_idx;
            nodes[current_idx as usize].right = right_idx;
        } else {
            nodes[current_idx as usize].left = -1;
            nodes[current_idx as usize].right = 0;
        }
    } else {
        let start_idx = indices.len() as i32;
        for &tri_idx in &node.triangles {
            indices.push((node.triangle_offset + tri_idx) as i32);
        }
        nodes[current_idx as usize].left = -start_idx - 1;
        nodes[current_idx as usize].right = node.triangles.len() as i32;
    }
    current_idx
}

fn build_atlas(triangles: &[PreparedTriangle]) -> Atlas {
    let mut keys: Vec<String> = Vec::new();
    for tri in triangles {
        if tri.has_texture && !keys.contains(&tri.tex_key) {
            keys.push(tri.tex_key.clone());
        }
    }
    if keys.is_empty() {
        let img = RgbaImage::from_pixel(1, 1, image::Rgba([255, 255, 255, 255]));
        let data = img.as_raw().clone();
        return Atlas {
            width: 1,
            height: 1,
            image: img,
            rects: HashMap::new(),
            info: vec![1, 1, 0, 0],
            data,
        };
    }
    let mut loaded: Vec<(String, RgbaImage)> = Vec::new();
    for key in &keys {
        let img = load_texture_any(key)
            .unwrap_or_else(|| RgbaImage::from_pixel(1, 1, image::Rgba([255, 255, 255, 255])));
        loaded.push((key.clone(), img));
    }
    let max_atlas_width: u32 = 4096;
    let mut atlas_width: u32 = 1;
    for (_, img) in &loaded {
        atlas_width = atlas_width.max(img.width().min(max_atlas_width));
    }
    let mut packed: Vec<(String, RgbaImage, u32, u32)> = Vec::new();
    let mut x = 0u32;
    let mut y = 0u32;
    let mut row_h = 0u32;
    for (key, mut img) in loaded {
        if img.width() > max_atlas_width || img.height() > max_atlas_width {
            let scale_w = if img.width() > max_atlas_width {
                max_atlas_width as f32 / img.width() as f32
            } else {
                1.0
            };
            let scale_h = if img.height() > max_atlas_width {
                max_atlas_width as f32 / img.height() as f32
            } else {
                1.0
            };
            let scale = scale_w.min(scale_h);
            let nw = ((img.width() as f32) * scale).max(1.0) as u32;
            let nh = ((img.height() as f32) * scale).max(1.0) as u32;
            img = DynamicImage::ImageRgba8(img)
                .resize_exact(nw, nh, FilterType::Nearest)
                .to_rgba8();
        }
        let w = img.width();
        let h = img.height();
        if x + w > atlas_width {
            y += row_h;
            x = 0;
            row_h = 0;
        }
        packed.push((key, img, x, y));
        x += w;
        if h > row_h {
            row_h = h;
        }
    }
    let atlas_height = (y + row_h).max(1);
    let mut canvas = RgbaImage::from_pixel(atlas_width, atlas_height, image::Rgba([255, 255, 255, 255]));
    let mut rects = HashMap::new();
    for (key, img, px, py) in packed {
        let iw = img.width();
        let ih = img.height();
        for yy in 0..ih {
            for xx in 0..iw {
                canvas.put_pixel(px + xx, py + yy, *img.get_pixel(xx, yy));
            }
        }
        rects.insert(
            key,
            AtlasRect {
                u: px as f32 / atlas_width as f32,
                v: py as f32 / atlas_height as f32,
                w: iw as f32 / atlas_width as f32,
                h: ih as f32 / atlas_height as f32,
            },
        );
    }
    let data = canvas.as_raw().clone();
    Atlas {
        width: atlas_width as i32,
        height: atlas_height as i32,
        image: canvas,
        rects,
        info: vec![atlas_width as i32, atlas_height as i32, 0, 0],
        data,
    }
}

fn rebuild_combined_scene() {
    let static_tris = Baked_static_triangles.lock().unwrap().clone();
    let dynamic_tris = Baked_dynamic_triangles.lock().unwrap().clone();
    let mut all = Vec::with_capacity(static_tris.len() + dynamic_tris.len());
    all.extend(static_tris);
    all.extend(dynamic_tris);
    let combined_bvh = if all.is_empty() {
        None
    } else {
        Some(build_bvh(&all, 0))
    };
    let combined_atlas = if all.is_empty() {
        None
    } else {
        Some(build_atlas(&all))
    };
    *Baked_all_triangles.lock().unwrap() = all;
    *Baked_combined_bvh.lock().unwrap() = combined_bvh;
    *Baked_combined_atlas.lock().unwrap() = combined_atlas;
}

fn rebuild_dynamic_scene() {
    let mut components: Vec<super::Draw_components> = Vec::new();
    {
        let queue = super::Draw_queue.lock().unwrap();
        for object in queue.iter() {
            if object.draw_type == "3d_object".to_string() {
                components.extend(split_3d_object_into_triangles(object));
            }
        }
    }
    let prepared = prepare_triangles(&components);
    *Baked_dynamic_triangles.lock().unwrap() = prepared;
    rebuild_combined_scene();
}

fn build_geometry_buffers(
    state: &OpenCLState,
    triangles: &[PreparedTriangle],
    bvh: &BvhNode,
    atlas: &Atlas,
) -> GeometryBuffers {
    let mut flat_nodes = Vec::new();
    let mut tri_indices = Vec::new();
    flatten_bvh(bvh, &mut flat_nodes, &mut tri_indices);
    let mut tri_v0: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_v1: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_v2: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_color: Vec<f32> = Vec::with_capacity(triangles.len() * 4);
    let mut tri_uv: Vec<f32> = Vec::with_capacity(triangles.len() * 6);
    let mut tri_tex_id: Vec<i32> = Vec::with_capacity(triangles.len());
    for tri in triangles {
        tri_v0.extend_from_slice(&tri.v0);
        tri_v1.extend_from_slice(&tri.v1);
        tri_v2.extend_from_slice(&tri.v2);
        tri_color.extend_from_slice(&tri.color);
        let mut tex_id = -1;
        let mut uv_out = [0.0f32; 6];
        if tri.has_texture {
            if let Some(rect) = atlas.rects.get(&tri.tex_key) {
                tex_id = 0;
                for i in 0..3 {
                    uv_out[i * 2] = rect.u + tri.uv[i * 2] * rect.w;
                    uv_out[i * 2 + 1] = rect.v + tri.uv[i * 2 + 1] * rect.h;
                }
            }
        }
        tri_tex_id.push(tex_id);
        tri_uv.extend_from_slice(&uv_out);
    }
    let nodes_min_data: Vec<f32> = if flat_nodes.is_empty() {
        vec![0.0; 3]
    } else {
        flat_nodes.iter().flat_map(|n| n.min).collect()
    };
    let nodes_max_data: Vec<f32> = if flat_nodes.is_empty() {
        vec![0.0; 3]
    } else {
        flat_nodes.iter().flat_map(|n| n.max).collect()
    };
    let nodes_left_data: Vec<i32> = if flat_nodes.is_empty() {
        vec![0]
    } else {
        flat_nodes.iter().map(|n| n.left).collect()
    };
    let nodes_right_data: Vec<i32> = if flat_nodes.is_empty() {
        vec![0]
    } else {
        flat_nodes.iter().map(|n| n.right).collect()
    };
    let buf_nodes_min: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(nodes_min_data.len().max(1))
        .copy_host_slice(&nodes_min_data)
        .build()
        .unwrap();
    let buf_nodes_max: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(nodes_max_data.len().max(1))
        .copy_host_slice(&nodes_max_data)
        .build()
        .unwrap();
    let buf_nodes_left: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(nodes_left_data.len().max(1))
        .copy_host_slice(&nodes_left_data)
        .build()
        .unwrap();
    let buf_nodes_right: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(nodes_right_data.len().max(1))
        .copy_host_slice(&nodes_right_data)
        .build()
        .unwrap();
    let tri_indices_safe = if tri_indices.is_empty() { vec![0i32] } else { tri_indices };
    let buf_indices: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_indices_safe.len().max(1))
        .copy_host_slice(&tri_indices_safe)
        .build()
        .unwrap();
    let tri_v0_safe = if tri_v0.is_empty() { vec![0.0f32] } else { tri_v0 };
    let buf_v0: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_v0_safe.len().max(1))
        .copy_host_slice(&tri_v0_safe)
        .build()
        .unwrap();
    let tri_v1_safe = if tri_v1.is_empty() { vec![0.0f32] } else { tri_v1 };
    let buf_v1: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_v1_safe.len().max(1))
        .copy_host_slice(&tri_v1_safe)
        .build()
        .unwrap();
    let tri_v2_safe = if tri_v2.is_empty() { vec![0.0f32] } else { tri_v2 };
    let buf_v2: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_v2_safe.len().max(1))
        .copy_host_slice(&tri_v2_safe)
        .build()
        .unwrap();
    let tri_color_safe = if tri_color.is_empty() { vec![0.0f32] } else { tri_color };
    let buf_col: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_color_safe.len().max(1))
        .copy_host_slice(&tri_color_safe)
        .build()
        .unwrap();
    let tri_uv_safe = if tri_uv.is_empty() { vec![0.0f32] } else { tri_uv };
    let buf_uv: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_uv_safe.len().max(1))
        .copy_host_slice(&tri_uv_safe)
        .build()
        .unwrap();
    let tri_tex_id_safe = if tri_tex_id.is_empty() { vec![0i32] } else { tri_tex_id };
    let buf_texid: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_tex_id_safe.len().max(1))
        .copy_host_slice(&tri_tex_id_safe)
        .build()
        .unwrap();
    let tex_info_gpu = if atlas.info.is_empty() { vec![0i32] } else { atlas.info.clone() };
    let tex_data_gpu = if atlas.data.is_empty() { vec![0u8] } else { atlas.data.clone() };
    let buf_tex_info: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tex_info_gpu.len().max(1))
        .copy_host_slice(&tex_info_gpu)
        .build()
        .unwrap();
    let buf_tex_data: Buffer<u8> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tex_data_gpu.len().max(1))
        .copy_host_slice(&tex_data_gpu)
        .build()
        .unwrap();
    GeometryBuffers {
        buf_nodes_min,
        buf_nodes_max,
        buf_nodes_left,
        buf_nodes_right,
        buf_indices,
        buf_v0,
        buf_v1,
        buf_v2,
        buf_col,
        buf_uv,
        buf_texid,
        buf_tex_info,
        buf_tex_data,
        num_nodes: flat_nodes.len().max(1) as i32,
        num_tris: triangles.len() as i32,
        num_tex: (atlas.info.len() / 4).max(1) as i32,
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
struct Gpu2dObject {
    vertex_offset: i32,
    vertex_count: i32,
    offset_x: f32,
    offset_y: f32,
    min_x: f32,
    max_x: f32,
    min_y: f32,
    max_y: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
    color_a: f32,
    tex_id: i32,
    symbol: i32,
}

unsafe impl ocl::core::OclPrm for Gpu2dObject {}

fn Build_static_scene() {
    let static_scene = super::Static_scene.lock().unwrap();
    let mut static_components: Vec<super::Draw_components> = Vec::new();
    for object in static_scene.iter() {
        if object.draw_type == "3d_object".to_string() {
            static_components.extend(split_3d_object_into_triangles(object));
        }
    }
    drop(static_scene);
    let prepared = prepare_triangles(&static_components);
    let atlas = build_atlas(&prepared);
    let baked_bvh = if prepared.is_empty() {
        None
    } else {
        Some(build_bvh(&prepared, 0))
    };
    *Baked_static_triangles.lock().unwrap() = prepared;
    *Baked_static_bvh.lock().unwrap() = baked_bvh;
    *Baked_static_atlas.lock().unwrap() = Some(atlas);
    *Static_baked.lock().unwrap() = true;
    rebuild_combined_scene();
}

pub fn Render_3d_to_screen(
    _dynamic_triangles: &[super::Draw_components],
    queue_2d: &[super::Draw_components],
    screen: &mut Vec<Vec<super::Pixel_structure>>,
) {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width;
    let height = settings.window_height;
    if width <= 0 || height <= 0 {
        return;
    }
    let cam = super::Camera.lock().unwrap();
    let origin = [cam.camera_x, cam.camera_y, cam.camera_z];
    let basis = camera_basis(cam.camera_pitch, cam.camera_yaw, cam.camera_roll);
    let max_dist_i128 = if cam.max_dist > 0 {
        cam.max_dist as i128
    } else {
        1_000_000
    };
    let max_dist_f32 = max_dist_i128 as f32;
    let ambient_f32 = [
        cam.ambient_light[0] as f32 / 255.0,
        cam.ambient_light[1] as f32 / 255.0,
        cam.ambient_light[2] as f32 / 255.0,
    ];
    let fov = cam.camera_fov as f32;
    drop(cam);
    drop(settings);
    let aspect = width as f32 / height as f32;
    let half_tan = if fov > 0.0 {
        ((fov * PI / 180.0) / 2.0).tan()
    } else {
        1.0
    };
    let all_triangles = Baked_all_triangles.lock().unwrap().clone();
    if all_triangles.is_empty() {
        return;
    }
    let bvh_opt = Baked_combined_bvh.lock().unwrap().clone();
    let atlas_opt = Baked_combined_atlas.lock().unwrap().clone();
    let bvh = match bvh_opt {
        Some(node) => node,
        None => build_bvh(&all_triangles, 0),
    };
    let atlas = match atlas_opt {
        Some(atlas) => atlas,
        None => build_atlas(&all_triangles),
    };
    let width_usize = width as usize;
    let height_usize = height as usize;
    if width_usize == 0 || height_usize == 0 {
        return;
    }
    let state_mutex = match get_opencl_state() {
        Ok(v) => v,
        Err(_) => return,
    };
    let state_guard = state_mutex.lock().unwrap();
    if let Some(state) = state_guard.as_ref() {
        let geom = build_geometry_buffers(state, &all_triangles, &bvh, &atlas);
        let pixel_count = width_usize * height_usize;
        let output_buffer: Buffer<u8> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_WRITE)
            .len(pixel_count * 4)
            .build()
            .unwrap();
        let arg_width = width as i32;
        let arg_height = height as i32;
        let arg_cam_origin = Float3::new(origin[0], origin[1], origin[2]);
        let arg_cam_fwd = Float3::new(basis.forward[0], basis.forward[1], basis.forward[2]);
        let arg_cam_right = Float3::new(basis.right[0], basis.right[1], basis.right[2]);
        let arg_cam_up = Float3::new(basis.up[0], basis.up[1], basis.up[2]);
        let arg_half_tan = half_tan;
        let arg_aspect = aspect;
        let arg_max_dist = max_dist_f32;
        let arg_ambient = Float3::new(ambient_f32[0], ambient_f32[1], ambient_f32[2]);
        let kernel = Kernel::builder()
            .program(&state.render_program)
            .name("render_scene")
            .queue(state.queue.clone())
            .arg(&output_buffer)
            .arg(&arg_width)
            .arg(&arg_height)
            .arg(&arg_cam_origin)
            .arg(&arg_cam_fwd)
            .arg(&arg_cam_right)
            .arg(&arg_cam_up)
            .arg(&arg_half_tan)
            .arg(&arg_aspect)
            .arg(&arg_max_dist)
            .arg(&arg_ambient)
            .arg(&geom.num_nodes)
            .arg(&geom.buf_nodes_min)
            .arg(&geom.buf_nodes_max)
            .arg(&geom.buf_nodes_left)
            .arg(&geom.buf_nodes_right)
            .arg(&geom.num_tris)
            .arg(&geom.buf_v0)
            .arg(&geom.buf_v1)
            .arg(&geom.buf_v2)
            .arg(&geom.buf_col)
            .arg(&geom.buf_uv)
            .arg(&geom.buf_texid)
            .arg(&geom.buf_indices)
            .arg(&geom.num_tex)
            .arg(&geom.buf_tex_info)
            .arg(&geom.buf_tex_data)
            .global_work_size(pixel_count)
            .build()
            .unwrap();
        unsafe {
            kernel.enq().unwrap();
        }
        if !queue_2d.is_empty() {
            let mut gpu_2d_objects: Vec<Gpu2dObject> = Vec::new();
            let mut vertices_2d: Vec<f32> = Vec::new();
            let mut texture_map: HashMap<String, RgbaImage> = HashMap::new();
            let mut path_to_tex_id: HashMap<String, i32> = HashMap::new();
            let mut tex_infos: Vec<i32> = Vec::new();
            let mut global_tex_data: Vec<u8> = Vec::new();
            let default_symbol_char = super::Empty_pixel.lock().unwrap().pixel_symbol;
            let default_symbol_i32 = default_symbol_char as u32 as i32;
            for object in queue_2d {
                if object.draw_vertices.len() < 2 {
                    continue;
                }
                let mut local_vertices: Vec<f32> = Vec::with_capacity(object.draw_vertices.len());
                for i in (0..object.draw_vertices.len()).step_by(2) {
                    if i + 1 >= object.draw_vertices.len() {
                        break;
                    }
                    let rv = rotate_vertex_by_orientation(
                        [
                            object.draw_vertices[i],
                            object.draw_vertices[i + 1],
                            0.0,
                        ],
                        object.pitch,
                        object.yaw,
                        object.roll,
                    );
                    local_vertices.push(rv[0]);
                    local_vertices.push(rv[1]);
                }
                if local_vertices.len() < 2 {
                    continue;
                }
                let mut biggest_x: f32 = 0.0;
                let mut biggest_y: f32 = 0.0;
                let mut smallest_x: f32 = width as f32;
                let mut smallest_y: f32 = height as f32;
                for i in (0..(local_vertices.len() - 1)).step_by(2) {
                    let vx = local_vertices[i] + object.draw_x;
                    let vy = local_vertices[i + 1] + object.draw_y;
                    if vx > biggest_x {
                        biggest_x = vx;
                    }
                    if vy > biggest_y {
                        biggest_y = vy;
                    }
                    if vx < smallest_x {
                        smallest_x = vx;
                    }
                    if vy < smallest_y {
                        smallest_y = vy;
                    }
                }
                let vertex_offset = vertices_2d.len() as i32;
                let vertex_count = local_vertices.len() as i32;
                vertices_2d.extend_from_slice(&local_vertices);
                let mut tex_id = -1i32;
                if !path_is_none(&object.draw_texture_path) {
                    if !texture_map.contains_key(&object.draw_texture_path) {
                        if let Some(img) = load_texture_any(&object.draw_texture_path) {
                            let id = (tex_infos.len() / 4) as i32;
                            let offset = (global_tex_data.len() / 4) as i32;
                            texture_map.insert(object.draw_texture_path.clone(), img.clone());
                            path_to_tex_id.insert(object.draw_texture_path.clone(), id);
                            tex_infos.push(img.width() as i32);
                            tex_infos.push(img.height() as i32);
                            tex_infos.push(offset);
                            tex_infos.push(0);
                            for pixel in img.pixels() {
                                global_tex_data.extend_from_slice(&[pixel[0], pixel[1], pixel[2], pixel[3]]);
                            }
                        }
                    }
                    if let Some(id) = path_to_tex_id.get(&object.draw_texture_path) {
                        tex_id = *id;
                    }
                }
                let symbol_i32 = object.draw_symbol as u32 as i32;
                gpu_2d_objects.push(Gpu2dObject {
                    vertex_offset,
                    vertex_count,
                    offset_x: object.draw_x,
                    offset_y: object.draw_y,
                    min_x: smallest_x,
                    max_x: biggest_x,
                    min_y: smallest_y,
                    max_y: biggest_y,
                    color_r: object.draw_RGBA_color[0] as f32 / 255.0,
                    color_g: object.draw_RGBA_color[1] as f32 / 255.0,
                    color_b: object.draw_RGBA_color[2] as f32 / 255.0,
                    color_a: object.draw_RGBA_color[3] as f32 / 255.0,
                    tex_id,
                    symbol: symbol_i32,
                });
            }
            let tex_info_gpu = if tex_infos.is_empty() { vec![0i32] } else { tex_infos.clone() };
            let tex_data_gpu = if global_tex_data.is_empty() { vec![0u8] } else { global_tex_data };
            let buf_tex_info_2d: Buffer<i32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(tex_info_gpu.len().max(1))
                .copy_host_slice(&tex_info_gpu)
                .build()
                .unwrap();
            let buf_tex_data_2d: Buffer<u8> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(tex_data_gpu.len().max(1))
                .copy_host_slice(&tex_data_gpu)
                .build()
                .unwrap();
            let buf_objects_2d: Buffer<Gpu2dObject> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(gpu_2d_objects.len().max(1))
                .copy_host_slice(&gpu_2d_objects)
                .build()
                .unwrap();
            let buf_vertices_2d: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(vertices_2d.len().max(1))
                .copy_host_slice(&vertices_2d)
                .build()
                .unwrap();
            let mut cpu_symbols = vec![default_symbol_i32; pixel_count];
            let symbol_buffer: Buffer<i32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_WRITE)
                .len(pixel_count)
                .copy_host_slice(&cpu_symbols)
                .build()
                .unwrap();
            let arg_num_2d_objects = gpu_2d_objects.len() as i32;
            let arg_num_tex_2d = (tex_infos.len() / 4).max(1) as i32;
            let kernel_2d = Kernel::builder()
                .program(&state.render_program)
                .name("render_2d")
                .queue(state.queue.clone())
                .arg(&output_buffer)
                .arg(&symbol_buffer)
                .arg(&(width as i32))
                .arg(&(height as i32))
                .arg(&arg_num_2d_objects)
                .arg(&buf_objects_2d)
                .arg(&buf_vertices_2d)
                .arg(&arg_num_tex_2d)
                .arg(&buf_tex_info_2d)
                .arg(&buf_tex_data_2d)
                .global_work_size(pixel_count)
                .build()
                .unwrap();
            unsafe {
                kernel_2d.enq().unwrap();
            }
            let mut cpu_output = vec![0u8; pixel_count * 4];
            output_buffer.read(&mut cpu_output).enq().unwrap();
            let mut cpu_symbols_out = vec![0i32; pixel_count];
            symbol_buffer.read(&mut cpu_symbols_out).enq().unwrap();
            for y in 0..height_usize {
                for x in 0..width_usize {
                    let idx = (y * width_usize + x) * 4;
                    let sym = cpu_symbols_out[y * width_usize + x];
                    let sym_char = char::from_u32(sym as u32).unwrap_or(' ');
                    screen[y][x] = super::Pixel_structure {
                        pixel_symbol: sym_char,
                        pixel_RGBA_color: [
                            cpu_output[idx],
                            cpu_output[idx + 1],
                            cpu_output[idx + 2],
                            cpu_output[idx + 3],
                        ],
                    };
                }
            }
        } else {
            let mut cpu_output = vec![0u8; pixel_count * 4];
            output_buffer.read(&mut cpu_output).enq().unwrap();
            let default_symbol = super::Empty_pixel.lock().unwrap().pixel_symbol;
            for y in 0..height_usize {
                for x in 0..width_usize {
                    let idx = (y * width_usize + x) * 4;
                    screen[y][x] = super::Pixel_structure {
                        pixel_symbol: default_symbol,
                        pixel_RGBA_color: [
                            cpu_output[idx],
                            cpu_output[idx + 1],
                            cpu_output[idx + 2],
                            cpu_output[idx + 3],
                        ],
                    };
                }
            }
        }
    }
}

pub fn Render_image_to_console() -> Result<(), String> {
    let mut queue_2d: Vec<super::Draw_components> = Vec::new();
    {
        let all_queue = super::Static_scene.lock().unwrap();
        for object in all_queue.iter() {
            if object.draw_type == "2d_object".to_string() {
                let mut obj = object.clone();
                obj.special_properties = String::new();
                queue_2d.push(obj);
            }
        }
    }
    {
        let all_queue = super::Draw_queue.lock().unwrap();
        for object in all_queue.iter() {
            if object.draw_type == "2d_object".to_string() {
                let mut obj = object.clone();
                obj.special_properties = String::new();
                queue_2d.push(obj);
            }
        }
    }
    let need_bake = {
        let examination = super::Is_scene_changed.lock().unwrap();
        let not_baked = !*Static_baked.lock().unwrap();
        *examination || not_baked
    };
    if need_bake {
        Build_static_scene();
        *super::Is_scene_changed.lock().unwrap() = false;
    }
    let dynamic_changed = {
        let mut flag = Dynamic_changed.lock().unwrap();
        let value = *flag;
        if value {
            *flag = false;
        }
        value
    };
    if dynamic_changed {
        rebuild_dynamic_scene();
    }
    let width = super::Engine_settings.lock().unwrap().window_width as i128;
    let height = super::Engine_settings.lock().unwrap().window_height as i128;
    if width <= 0 || height <= 0 {
        let mut screen = super::Screen.lock().unwrap();
        *screen = Vec::new();
        return Ok(());
    }
    let new_screen = vec![
        vec![super::Empty_pixel.lock().unwrap().clone(); width as usize];
        height as usize
    ];
    let mut screen = super::Screen.lock().unwrap();
    *screen = new_screen;
    Render_3d_to_screen(&[], &queue_2d, &mut screen);
    Ok(())
}