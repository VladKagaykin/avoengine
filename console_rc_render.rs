use std::f32::consts::PI;
use image::RgbaImage;
use std::collections::HashMap;
use std::sync::{OnceLock, Mutex};
use ocl::{Platform, Device, Context, Program, Kernel, Queue, Buffer, flags};
use ocl::prm::Float3;

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
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

struct GeometryBuffers {
    buf_nodes_min: Buffer<f32>,
    buf_nodes_max: Buffer<f32>,
    buf_nodes_left: Buffer<i32>,
    buf_nodes_right: Buffer<i32>,
    buf_indices: Buffer<i32>,
    buf_v0: Buffer<f32>,
    buf_v1: Buffer<f32>,
    buf_v2: Buffer<f32>,
    buf_norm: Buffer<f32>,
    buf_col: Buffer<f32>,
    buf_texid: Buffer<i32>,
    buf_refraction: Buffer<f32>,
    buf_tex_info: Buffer<i32>,
    buf_tex_data: Buffer<u8>,
    num_nodes: i32,
    num_tris: i32,
    num_tex: i32,
}

static Baked_static_triangles: Mutex<Vec<Render_triangle>> = Mutex::new(Vec::new());
static Baked_static_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);

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

float4 sample_triangle_texture(
    __global const uchar* tex_data,
    __global const int* tex_info,
    int tex_id,
    float3 v0, float3 v1, float3 v2,
    float hit_u, float hit_v
) {
    if (tex_id < 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
    int info_base = tex_id * 4;
    int tex_w = tex_info[info_base];
    int tex_h = tex_info[info_base + 1];
    int offset = tex_info[info_base + 2];
    if (tex_w <= 0 || tex_h <= 0) return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
    float3 minv = fmin(fmin(v0, v1), v2);
    float3 maxv = fmax(fmax(v0, v1), v2);
    float3 ranges = maxv - minv;
    float rx = ranges.x; float ry = ranges.y; float rz = ranges.z;
    int axis0, axis1;
    if (rx >= ry && rx >= rz) { axis0 = 0; axis1 = (ry >= rz) ? 1 : 2; }
    else if (ry >= rx && ry >= rz) { axis0 = 1; axis1 = (rx >= rz) ? 0 : 2; }
    else { axis0 = 2; axis1 = (rx >= ry) ? 0 : 1; }
    float v0_a, v0_b, v1_a, v1_b, v2_a, v2_b, min_a, min_b, range_a, range_b;
    if (axis0 == 0) { v0_a = v0.x; v1_a = v1.x; v2_a = v2.x; min_a = minv.x; range_a = rx; }
    else if (axis0 == 1) { v0_a = v0.y; v1_a = v1.y; v2_a = v2.y; min_a = minv.y; range_a = ry; }
    else { v0_a = v0.z; v1_a = v1.z; v2_a = v2.z; min_a = minv.z; range_a = rz; }
    if (axis1 == 0) { v0_b = v0.x; v1_b = v1.x; v2_b = v2.x; min_b = minv.x; range_b = rx; }
    else if (axis1 == 1) { v0_b = v0.y; v1_b = v1.y; v2_b = v2.y; min_b = minv.y; range_b = ry; }
    else { v0_b = v0.z; v1_b = v1.z; v2_b = v2.z; min_b = minv.z; range_b = rz; }
    if (range_a < 1e-8f) range_a = 1.0f;
    if (range_b < 1e-8f) range_b = 1.0f;
    float uv0_a = (v0_a - min_a) / range_a; float uv0_b = (v0_b - min_b) / range_b;
    float uv1_a = (v1_a - min_a) / range_a; float uv1_b = (v1_b - min_b) / range_b;
    float uv2_a = (v2_a - min_a) / range_a; float uv2_b = (v2_b - min_b) / range_b;
    float weight = 1.0f - hit_u - hit_v;
    float tex_u = weight * uv0_a + hit_u * uv1_a + hit_v * uv2_a;
    float tex_v = weight * uv0_b + hit_u * uv1_b + hit_v * uv2_b;
    int x = (int)round(tex_u * (tex_w - 1));
    int y = (int)round(tex_v * (tex_h - 1));
    x = clamp(x, 0, tex_w - 1);
    y = clamp(y, 0, tex_h - 1);
    int idx = (offset + y * tex_w + x) * 4;
    return (float4)(tex_data[idx] / 255.0f, tex_data[idx + 1] / 255.0f, tex_data[idx + 2] / 255.0f, tex_data[idx + 3] / 255.0f);
}

int refract_ray(float3 D, float3 N, float n1, float n2, float3* T) {
    float cos_theta1 = -dot(D, N);
    float eta = n1 / n2;
    float sin2_theta2 = eta * eta * (1.0f - cos_theta1 * cos_theta1);
    if (sin2_theta2 > 1.0f) return 0;
    float cos_theta2 = sqrt(1.0f - sin2_theta2);
    *T = normalize(eta * D + (eta * cos_theta1 - cos_theta2) * N);
    return 1;
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

int process_bounce(
    RayHit hit,
    float3* current_origin,
    float3* current_dir,
    float* remaining_dist,
    __global const float* tris_normal,
    __global const float* tris_refraction
) {
    int t_idx = hit.tri_idx;
    float ref_idx = tris_refraction[t_idx];
    float3 hit_point = *current_origin + (*current_dir) * hit.t;
    float3 normal = (float3)(tris_normal[t_idx * 3], tris_normal[t_idx * 3 + 1], tris_normal[t_idx * 3 + 2]);
    int entering = dot(*current_dir, normal) < 0.0f;
    float3 front_normal = entering ? normal : -normal;
    if (ref_idx > 0.0f) {
        float n1 = entering ? 1.0f : ref_idx;
        float n2 = entering ? ref_idx : 1.0f;
        float3 refracted_dir;
        if (refract_ray(*current_dir, front_normal, n1, n2, &refracted_dir)) {
            *current_origin = hit_point + refracted_dir * 1e-3f;
            *current_dir = refracted_dir;
        } else {
            float3 reflected_dir = *current_dir - 2.0f * dot(*current_dir, front_normal) * front_normal;
            *current_origin = hit_point + reflected_dir * 1e-3f;
            *current_dir = reflected_dir;
            return 2;
        }
    } else {
        *current_origin = hit_point + (*current_dir) * 1e-3f;
    }
    *remaining_dist -= hit.t;
    if (*remaining_dist <= 1e-3f) return 0;
    return 1;
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
    __global const float* tris_normal,
    __global const float* tris_color,
    __global const int* tris_tex_id,
    __global const float* tris_refraction,
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
        RayHit hit = trace_ray(origin, dir, remaining_dist, nodes_min, nodes_max, nodes_left, nodes_right, tri_indices, tris_v0, tris_v1, tris_v2, num_tris);
        if (!hit.hit) break;
        int t_idx = hit.tri_idx;
        float3 tv0 = (float3)(tris_v0[t_idx * 3], tris_v0[t_idx * 3 + 1], tris_v0[t_idx * 3 + 2]);
        float3 tv1 = (float3)(tris_v1[t_idx * 3], tris_v1[t_idx * 3 + 1], tris_v1[t_idx * 3 + 2]);
        float3 tv2 = (float3)(tris_v2[t_idx * 3], tris_v2[t_idx * 3 + 1], tris_v2[t_idx * 3 + 2]);
        float3 base_rgb = (float3)(tris_color[t_idx * 4], tris_color[t_idx * 4 + 1], tris_color[t_idx * 4 + 2]);
        float alpha = tris_color[t_idx * 4 + 3];
        int tex_id = tris_tex_id[t_idx];
        if (tex_id >= 0) {
            float4 tex_col = sample_triangle_texture(tex_data, tex_info, tex_id, tv0, tv1, tv2, hit.u, hit.v);
            base_rgb.x = tex_col.x * base_rgb.x;
            base_rgb.y = tex_col.y * base_rgb.y;
            base_rgb.z = tex_col.z * base_rgb.z;
            alpha = tex_col.w * alpha;
        }
        float ref_idx = tris_refraction[t_idx];
        bool is_opaque = (alpha >= 1.0f && ref_idx <= 0.0f);
        float3 light_factor = ambient;
        if (light_factor.x <= 0.0f && light_factor.y <= 0.0f && light_factor.z <= 0.0f) {
            light_factor = (float3)(1.0f, 1.0f, 1.0f);
        }
        float3 lit_color = base_rgb * light_factor;
        if (is_opaque) {
            final_color += throughput * lit_color;
            break;
        }
        final_color += throughput * lit_color * alpha;
        throughput *= mix((float3)(1.0f, 1.0f, 1.0f), base_rgb, alpha);
        if (throughput.x < 0.01f && throughput.y < 0.01f && throughput.z < 0.01f) break;
        if (depth == 63) break;
        int bounce_res = process_bounce(hit, &origin, &dir, &remaining_dist, tris_normal, tris_refraction);
        if (bounce_res == 0) break;
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
        
        int cond1 = (yi > py) != (yj > py);
        float intersect_x = (xj - xi) * (py - yi) / (yj - yi) + xi;
        int cond2 = (px < intersect_x);
        
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
    
    int x = (int)round(u * (tex_w - 1));
    int y = (int)round(v * (tex_h - 1));
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

fn cross(a: &[f32; 3], b: &[f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot(a: &[f32; 3], b: &[f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
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

fn load_texture(path: &str) -> Option<RgbaImage> {
    static CACHE: OnceLock<Mutex<HashMap<String, RgbaImage>>> = OnceLock::new();
    let cache = CACHE.get_or_init(|| Mutex::new(HashMap::new()));
    let mut lock = cache.lock().unwrap();
    if let Some(img) = lock.get(path) {
        return Some(img.clone());
    }
    if let Ok(img) = image::open(path).map(|dyn_img| dyn_img.to_rgba8()) {
        lock.insert(path.to_string(), img.clone());
        Some(img)
    } else {
        None
    }
}

fn compute_triangle_aabb(tri: &super::Draw_components) -> ([f32; 3], [f32; 3]) {
    let verts = &tri.draw_vertices;
    let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
    let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
    let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for v in [v0, v1, v2] {
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
    _triangles: &[Render_triangle],
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

fn build_bvh(triangles: &[Render_triangle], offset: usize) -> BvhNode {
    let mut indices: Vec<usize> = (0..triangles.len()).collect();
    let aabbs: Vec<([f32; 3], [f32; 3])> = triangles
        .iter()
        .map(|rt| compute_triangle_aabb(&rt.triangle))
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

fn build_geometry_buffers(
    state: &OpenCLState,
    triangles: &[Render_triangle],
    bvh: &BvhNode,
) -> GeometryBuffers {
    let mut flat_nodes = Vec::new();
    let mut tri_indices = Vec::new();
    flatten_bvh(bvh, &mut flat_nodes, &mut tri_indices);
    let mut tri_v0: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_v1: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_v2: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_normal: Vec<f32> = Vec::with_capacity(triangles.len() * 3);
    let mut tri_color: Vec<f32> = Vec::with_capacity(triangles.len() * 4);
    let mut tri_tex_id: Vec<i32> = Vec::with_capacity(triangles.len());
    let mut tri_refraction: Vec<f32> = Vec::with_capacity(triangles.len());
    let mut texture_map: HashMap<String, RgbaImage> = HashMap::new();
    let mut path_to_tex_id: HashMap<String, i32> = HashMap::new();
    let mut tex_infos: Vec<i32> = Vec::new();
    let mut global_tex_data: Vec<u8> = Vec::new();
    for rt in triangles.iter() {
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        let edge1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
        let edge2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
        let normal = normalize(cross(&edge1, &edge2));
        let color = [
            tri.draw_RGBA_color[0] as f32 / 255.0,
            tri.draw_RGBA_color[1] as f32 / 255.0,
            tri.draw_RGBA_color[2] as f32 / 255.0,
            tri.draw_RGBA_color[3] as f32 / 255.0,
        ];
        let mut tex_id = -1i32;
        if tri.draw_texture_path != "none" {
            if !texture_map.contains_key(&tri.draw_texture_path) {
                if let Some(img) = load_texture(&tri.draw_texture_path) {
                    let id = (tex_infos.len() / 4) as i32;
                    let offset = (global_tex_data.len() / 4) as i32;
                    texture_map.insert(tri.draw_texture_path.clone(), img.clone());
                    path_to_tex_id.insert(tri.draw_texture_path.clone(), id);
                    tex_infos.push(img.width() as i32);
                    tex_infos.push(img.height() as i32);
                    tex_infos.push(offset);
                    tex_infos.push(0);
                    for pixel in img.pixels() {
                        global_tex_data.extend_from_slice(&[pixel[0], pixel[1], pixel[2], pixel[3]]);
                    }
                }
            }
            if let Some(id) = path_to_tex_id.get(&tri.draw_texture_path) {
                tex_id = *id;
            }
        }
        let props = tri.special_properties.to_lowercase();
        let mut ref_val = 0.0f32;
        if let Some(idx) = props.find("refraction") {
            let rest = &props[idx + 10..];
            let rest = rest.trim_start_matches(|c: char| !c.is_ascii_digit() && c != '.' && c != '-');
            let num_str: String = rest.chars().take_while(|c| c.is_ascii_digit() || *c == '.' || *c == '-').collect();
            if let Ok(v) = num_str.parse::<f32>() {
                ref_val = v;
            }
        }
        tri_v0.extend_from_slice(&v0);
        tri_v1.extend_from_slice(&v1);
        tri_v2.extend_from_slice(&v2);
        tri_normal.extend_from_slice(&normal);
        tri_color.extend_from_slice(&color);
        tri_tex_id.push(tex_id);
        tri_refraction.push(ref_val);
    }
    let nodes_min_data: Vec<f32> = if flat_nodes.is_empty() { vec![0.0; 3] } else { flat_nodes.iter().flat_map(|n| n.min).collect() };
    let nodes_max_data: Vec<f32> = if flat_nodes.is_empty() { vec![0.0; 3] } else { flat_nodes.iter().flat_map(|n| n.max).collect() };
    let nodes_left_data: Vec<i32> = if flat_nodes.is_empty() { vec![0] } else { flat_nodes.iter().map(|n| n.left).collect() };
    let nodes_right_data: Vec<i32> = if flat_nodes.is_empty() { vec![0] } else { flat_nodes.iter().map(|n| n.right).collect() };
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
    let tri_normal_safe = if tri_normal.is_empty() { vec![0.0f32] } else { tri_normal };
    let buf_norm: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_normal_safe.len().max(1))
        .copy_host_slice(&tri_normal_safe)
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
    let tri_tex_id_safe = if tri_tex_id.is_empty() { vec![0i32] } else { tri_tex_id };
    let buf_texid: Buffer<i32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_tex_id_safe.len().max(1))
        .copy_host_slice(&tri_tex_id_safe)
        .build()
        .unwrap();
    let tri_refraction_safe = if tri_refraction.is_empty() { vec![0.0f32] } else { tri_refraction };
    let buf_refraction: Buffer<f32> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(tri_refraction_safe.len().max(1))
        .copy_host_slice(&tri_refraction_safe)
        .build()
        .unwrap();
    let tex_info_gpu = if tex_infos.is_empty() { vec![0i32] } else { tex_infos.clone() };
    let tex_data_gpu = if global_tex_data.is_empty() { vec![0u8] } else { global_tex_data };
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
        buf_norm,
        buf_col,
        buf_texid,
        buf_refraction,
        buf_tex_info,
        buf_tex_data,
        num_nodes: flat_nodes.len().max(1) as i32,
        num_tris: triangles.len() as i32,
        num_tex: (tex_infos.len() / 4).max(1) as i32,
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

pub fn Render_3d_to_screen(
    dynamic_triangles: &[super::Draw_components],
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
    let baked_static = Baked_static_triangles.lock().unwrap().clone();
    let static_bvh_opt = Baked_static_bvh.lock().unwrap().clone();
    let dynamic_render: Vec<Render_triangle> = dynamic_triangles
        .iter()
        .map(|t| Render_triangle {
            triangle: t.clone(),
        })
        .collect();
    let mut all_triangles = baked_static.clone();
    all_triangles.extend(dynamic_render.clone());
    if all_triangles.is_empty() {
        return;
    }
    let static_bvh_opt = if baked_static.is_empty() {
        None
    } else {
        Some(match static_bvh_opt {
            Some(node) => node,
            None => build_bvh(&baked_static, 0),
        })
    };
    let combined_bvh = if dynamic_render.is_empty() {
        static_bvh_opt.unwrap()
    } else if static_bvh_opt.is_none() {
        build_bvh(&dynamic_render, 0)
    } else {
        let static_bvh = static_bvh_opt.unwrap();
        let dynamic_bvh = build_bvh(&dynamic_render, baked_static.len());
        BvhNode {
            aabb_min: [
                static_bvh.aabb_min[0].min(dynamic_bvh.aabb_min[0]),
                static_bvh.aabb_min[1].min(dynamic_bvh.aabb_min[1]),
                static_bvh.aabb_min[2].min(dynamic_bvh.aabb_min[2]),
            ],
            aabb_max: [
                static_bvh.aabb_max[0].max(dynamic_bvh.aabb_max[0]),
                static_bvh.aabb_max[1].max(dynamic_bvh.aabb_max[1]),
                static_bvh.aabb_max[2].max(dynamic_bvh.aabb_max[2]),
            ],
            left: Some(Box::new(static_bvh)),
            right: Some(Box::new(dynamic_bvh)),
            triangles: Vec::new(),
            triangle_offset: 0,
        }
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
        let geom = build_geometry_buffers(state, &all_triangles, &combined_bvh);
        let pixel_count = width_usize * height_usize;
        let output_buffer: Buffer<u8> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
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
            .arg(&geom.buf_norm)
            .arg(&geom.buf_col)
            .arg(&geom.buf_texid)
            .arg(&geom.buf_refraction)
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
            let default_symbol_i32 = format!("{}", default_symbol_char).chars().next().unwrap_or(' ') as i32;

            for object in queue_2d {
                if object.draw_vertices.len() < 2 {
                    continue;
                }
                let mut biggest_x: f32 = 0.0;
                let mut biggest_y: f32 = 0.0;
                let mut smallest_x: f32 = width as f32;
                let mut smallest_y: f32 = height as f32;
                
                for i in (0..(object.draw_vertices.len() - 1)).step_by(2) {
                    let vx = object.draw_vertices[i] + object.draw_x;
                    let vy = object.draw_vertices[i + 1] + object.draw_y;
                    if vx > biggest_x { biggest_x = vx; }
                    if vy > biggest_y { biggest_y = vy; }
                    if vx < smallest_x { smallest_x = vx; }
                    if vy < smallest_y { smallest_y = vy; }
                }
                
                let vertex_offset = vertices_2d.len() as i32;
                let vertex_count = object.draw_vertices.len() as i32;
                vertices_2d.extend_from_slice(&object.draw_vertices);
                
                let mut tex_id = -1i32;
                if object.draw_texture_path != "none" {
                    if !texture_map.contains_key(&object.draw_texture_path) {
                        if let Some(img) = load_texture(&object.draw_texture_path) {
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
                
                let symbol_i32 = format!("{}", object.draw_symbol).chars().next().unwrap_or(' ') as i32;
                
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

fn Build_static_scene() {
    let static_scene = super::Static_scene.lock().unwrap();
    let mut static_triangles: Vec<super::Draw_components> = Vec::new();
    for object in static_scene.iter() {
        if object.draw_type == "3d_object".to_string() {
            let verts = &object.draw_vertices;
            for i in (0..verts.len()).step_by(9) {
                if i + 8 >= verts.len() {
                    break;
                }
                let tri = super::Draw_components {
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
                    special_properties: object.special_properties.clone(),
                    draw_special_name: object.draw_special_name.clone(),
                };
                static_triangles.push(tri);
            }
        }
    }
    drop(static_scene);
    let baked_result: Vec<Render_triangle> = static_triangles
        .into_iter()
        .map(|t| Render_triangle {
            triangle: t,
        })
        .collect();
    let baked_bvh = if baked_result.is_empty() {
        None
    } else {
        Some(build_bvh(&baked_result, 0))
    };
    let mut store = Baked_static_triangles.lock().unwrap();
    *store = baked_result.clone();
    let mut bvh_store = Baked_static_bvh.lock().unwrap();
    *bvh_store = baked_bvh.clone();
}

pub fn Render_image_to_console() -> Result<(), String> {
    let mut queue_2d: Vec<super::Draw_components> = Vec::new();
    let mut queue_3d: Vec<super::Draw_components> = Vec::new();
    let mut all_queue = super::Static_scene.lock().unwrap();
    for object in all_queue.iter() {
        if object.draw_type == "2d_object".to_string() {
            queue_2d.push(object.clone());
        }
    }
    drop(all_queue);
    let mut all_queue = super::Draw_queue.lock().unwrap();
    for object in all_queue.iter() {
        if object.draw_type == "2d_object".to_string() {
            queue_2d.push(object.clone());
        }
        if object.draw_type == "3d_object".to_string() {
            queue_3d.push(object.clone());
        }
    }
    drop(all_queue);
    super::Draw_queue.lock().unwrap().clear();
    super::Light_queue.lock().unwrap().clear();
    
    let need_bake = {
        let examination = super::Is_scene_changed.lock().unwrap();
        let not_baked = Baked_static_bvh.lock().unwrap().is_none();
        *examination || not_baked
    };
    if need_bake {
        Build_static_scene();
        *super::Is_scene_changed.lock().unwrap() = false;
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
    let mut triangles = Vec::new();
    for object in queue_3d {
        let verts = &object.draw_vertices;
        for i in (0..verts.len()).step_by(9) {
            if i + 8 >= verts.len() {
                break;
            }
            let tri = super::Draw_components {
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
                special_properties: object.special_properties.clone(),
                draw_special_name: object.draw_special_name.clone(),
            };
            triangles.push(tri);
        }
    }
    Render_3d_to_screen(&triangles, &queue_2d, &mut screen);
    Ok(())
}