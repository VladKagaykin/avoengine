use std::f32::consts::PI;
use std::thread;
use image::RgbaImage;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock, Mutex};
use ocl::{Platform, Device, Context, Program, Kernel, Queue, Buffer, flags};
use ocl::prm::Float3;

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
    pub baked_light: Option<[f32; 3]>,
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

static Baked_static_triangles: Mutex<Vec<Render_triangle>> = Mutex::new(Vec::new());
static Baked_static_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);

struct OpenCLState {
    context: Context,
    prepare_program: Program,
    bake_program: Program,
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
        return Err("No OpenCL platforms found, using CPU rendering".to_string());
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
            if let Ok(mut devices) = Device::list(*platform, None::<flags::DeviceType>) {
                if let Some(device) = devices.pop() {
                    selected_device = Some(device);
                    selected_platform = Some(*platform);
                    break;
                }
            }
        }
    }
    let device = selected_device.ok_or("OpenCL device not found, using CPU rendering".to_string())?;
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
    let prepare_program = Program::builder()
        .devices(device)
        .src(PREPARE_KERNEL_SRC)
        .build(&context)
        .map_err(|e| format!("OpenCL prepare program initialization failed: {}", e))?;
    let bake_program = Program::builder()
        .devices(device)
        .src(BAKE_KERNEL_SRC)
        .build(&context)
        .map_err(|e| format!("OpenCL bake program initialization failed: {}", e))?;
    let render_program = Program::builder()
        .devices(device)
        .src(RENDER_KERNEL_SRC)
        .build(&context)
        .map_err(|e| format!("OpenCL render program initialization failed: {}", e))?;
    let queue = Queue::new(&context, device, None)
        .map_err(|e| format!("OpenCL queue initialization failed: {}", e))?;
    Ok(OpenCLState {
        context,
        prepare_program,
        bake_program,
        render_program,
        queue,
    })
}

const RENDER_KERNEL_SRC: &str = r#"
typedef struct { float t, u, v; int tri_idx; } Hit;

bool intersect_aabb(float3 origin, float3 dir, float3 box_min, float3 box_max, float t_max, float* t_out) {
    float tmin = 0.0f;
    float tmax = t_max;
    if (fabs(dir.x) < 1e-8f) {
        if (origin.x < box_min.x || origin.x > box_max.x) return false;
    } else {
        float inv = 1.0f / dir.x;
        float t1 = (box_min.x - origin.x) * inv;
        float t2 = (box_max.x - origin.x) * inv;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = fmax(tmin, t1);
        tmax = fmin(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (fabs(dir.y) < 1e-8f) {
        if (origin.y < box_min.y || origin.y > box_max.y) return false;
    } else {
        float inv = 1.0f / dir.y;
        float t1 = (box_min.y - origin.y) * inv;
        float t2 = (box_max.y - origin.y) * inv;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = fmax(tmin, t1);
        tmax = fmin(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (fabs(dir.z) < 1e-8f) {
        if (origin.z < box_min.z || origin.z > box_max.z) return false;
    } else {
        float inv = 1.0f / dir.z;
        float t1 = (box_min.z - origin.z) * inv;
        float t2 = (box_max.z - origin.z) * inv;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = fmax(tmin, t1);
        tmax = fmin(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmin <= tmax && tmin < t_max) {
        *t_out = tmin;
        return true;
    }
    return false;
}

bool intersect_tri(float3 origin, float3 dir, float3 v0, float3 v1, float3 v2, float t_max, float* t, float* u, float* v) {
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 h = cross(dir, edge2);
    float a = dot(edge1, h);
    if (a > -1e-6f && a < 1e-6f) return false;
    float f = 1.0f / a;
    float3 s = origin - v0;
    *u = f * dot(s, h);
    if (*u < 0.0f || *u > 1.0f) return false;
    float3 q = cross(s, edge1);
    *v = f * dot(dir, q);
    if (*v < 0.0f || *u + *v > 1.0f) return false;
    *t = f * dot(edge2, q);
    return (*t > 1e-6f && *t < t_max);
}

void sort_hits(Hit* hits, int count) {
    for (int i = 1; i < count; i++) {
        Hit key = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].t > key.t) {
            hits[j + 1] = hits[j];
            j = j - 1;
        }
        hits[j + 1] = key;
    }
}

float4 sample_triangle_texture(
    __global const uchar* tex_data,
    __global const int* tex_info,
    int tex_id,
    float3 v0,
    float3 v1,
    float3 v2,
    float hit_u,
    float hit_v
) {
    if (tex_id < 0) {
        return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
    }
    int info_base = tex_id * 4;
    int tex_w = tex_info[info_base];
    int tex_h = tex_info[info_base + 1];
    int offset = tex_info[info_base + 2];
    if (tex_w <= 0 || tex_h <= 0) {
        return (float4)(1.0f, 1.0f, 1.0f, 1.0f);
    }
    float3 minv = fmin(fmin(v0, v1), v2);
    float3 maxv = fmax(fmax(v0, v1), v2);
    float3 ranges = maxv - minv;
    float rx = ranges.x;
    float ry = ranges.y;
    float rz = ranges.z;
    int axis0, axis1;
    if (rx >= ry && rx >= rz) {
        axis0 = 0;
        axis1 = (ry >= rz) ? 1 : 2;
    } else if (ry >= rx && ry >= rz) {
        axis0 = 1;
        axis1 = (rx >= rz) ? 0 : 2;
    } else {
        axis0 = 2;
        axis1 = (rx >= ry) ? 0 : 1;
    }
    float v0_a, v0_b, v1_a, v1_b, v2_a, v2_b;
    float min_a, min_b;
    float range_a, range_b;
    if (axis0 == 0) {
        v0_a = v0.x; v1_a = v1.x; v2_a = v2.x; min_a = minv.x; range_a = rx;
    } else if (axis0 == 1) {
        v0_a = v0.y; v1_a = v1.y; v2_a = v2.y; min_a = minv.y; range_a = ry;
    } else {
        v0_a = v0.z; v1_a = v1.z; v2_a = v2.z; min_a = minv.z; range_a = rz;
    }
    if (axis1 == 0) {
        v0_b = v0.x; v1_b = v1.x; v2_b = v2.x; min_b = minv.x; range_b = rx;
    } else if (axis1 == 1) {
        v0_b = v0.y; v1_b = v1.y; v2_b = v2.y; min_b = minv.y; range_b = ry;
    } else {
        v0_b = v0.z; v1_b = v1.z; v2_b = v2.z; min_b = minv.z; range_b = rz;
    }
    if (range_a < 1e-8f) range_a = 1.0f;
    if (range_b < 1e-8f) range_b = 1.0f;
    float uv0_a = (v0_a - min_a) / range_a;
    float uv0_b = (v0_b - min_b) / range_b;
    float uv1_a = (v1_a - min_a) / range_a;
    float uv1_b = (v1_b - min_b) / range_b;
    float uv2_a = (v2_a - min_a) / range_a;
    float uv2_b = (v2_b - min_b) / range_b;
    float weight = 1.0f - hit_u - hit_v;
    float tex_u = weight * uv0_a + hit_u * uv1_a + hit_v * uv2_a;
    float tex_v = weight * uv0_b + hit_u * uv1_b + hit_v * uv2_b;
    int x = (int)round(tex_u * (tex_w - 1));
    int y = (int)round(tex_v * (tex_h - 1));
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

void intersect_scene(
    float3 origin,
    float3 dir,
    float max_dist,
    __global const float* nodes_min,
    __global const float* nodes_max,
    __global const int* nodes_left,
    __global const int* nodes_right,
    __global const int* tri_indices,
    __global const float* tris_v0,
    __global const float* tris_v1,
    __global const float* tris_v2,
    int num_tris,
    Hit* out_hits,
    int* out_count,
    int max_hits
) {
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;
    *out_count = 0;
    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        float3 nmin = (float3)(
            nodes_min[node_idx * 3],
            nodes_min[node_idx * 3 + 1],
            nodes_min[node_idx * 3 + 2]
        );
        float3 nmax = (float3)(
            nodes_max[node_idx * 3],
            nodes_max[node_idx * 3 + 1],
            nodes_max[node_idx * 3 + 2]
        );
        float dummy_t;
        if (!intersect_aabb(origin, dir, nmin, nmax, max_dist, &dummy_t)) continue;
        int left = nodes_left[node_idx];
        int right = nodes_right[node_idx];
        if (left < 0) {
            int start = -left - 1;
            int count = right;
            for (int j = 0; j < count; j++) {
                int t_idx = tri_indices[start + j];
                float3 tv0 = (float3)(
                    tris_v0[t_idx * 3],
                    tris_v0[t_idx * 3 + 1],
                    tris_v0[t_idx * 3 + 2]
                );
                float3 tv1 = (float3)(
                    tris_v1[t_idx * 3],
                    tris_v1[t_idx * 3 + 1],
                    tris_v1[t_idx * 3 + 2]
                );
                float3 tv2 = (float3)(
                    tris_v2[t_idx * 3],
                    tris_v2[t_idx * 3 + 1],
                    tris_v2[t_idx * 3 + 2]
                );
                float t_hit, u_hit, v_hit;
                if (intersect_tri(origin, dir, tv0, tv1, tv2, max_dist, &t_hit, &u_hit, &v_hit)) {
                    if (*out_count < max_hits) {
                        out_hits[*out_count].t = t_hit;
                        out_hits[*out_count].u = u_hit;
                        out_hits[*out_count].v = v_hit;
                        out_hits[*out_count].tri_idx = t_idx;
                        (*out_count)++;
                    } else {
                        int max_i = 0;
                        for (int k = 1; k < max_hits; k++) {
                            if (out_hits[k].t > out_hits[max_i].t) max_i = k;
                        }
                        if (t_hit < out_hits[max_i].t) {
                            out_hits[max_i].t = t_hit;
                            out_hits[max_i].u = u_hit;
                            out_hits[max_i].v = v_hit;
                            out_hits[max_i].tri_idx = t_idx;
                        }
                    }
                }
            }
        } else {
            stack[stack_ptr++] = left;
            stack[stack_ptr++] = right;
        }
    }
    sort_hits(out_hits, *out_count);
}

float3 compute_dynamic_lighting(
    float3 point,
    float3 normal,
    int num_lights,
    __global const float* lights,
    __global const float* nodes_min,
    __global const float* nodes_max,
    __global const int* nodes_left,
    __global const int* nodes_right,
    __global const float* tris_v0,
    __global const float* tris_v1,
    __global const float* tris_v2,
    __global const float* tris_color,
    __global const int* tri_indices,
    __global const int* tris_tex_id,
    __global const uchar* tex_data,
    __global const int* tex_info,
    int num_tris
) {
    float3 result = (float3)(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < num_lights; i++) {
        int lb = i * 11;
        float3 light_pos = (float3)(lights[lb + 0], lights[lb + 1], lights[lb + 2]);
        float3 light_dir = (float3)(lights[lb + 3], lights[lb + 4], lights[lb + 5]);
        float cos_half = lights[lb + 6];
        float falloff = lights[lb + 7];
        float3 light_color = (float3)(lights[lb + 8], lights[lb + 9], lights[lb + 10]);
        float3 to_light = light_pos - point;
        float dist = length(to_light);
        if (dist < 1e-6f) continue;
        float3 to_light_dir = to_light / dist;
        if (dot(light_dir, -to_light_dir) < cos_half) continue;
        float attenuation = 1.0f / (1.0f + (dist / falloff) * (dist / falloff));
        if (attenuation < 0.01f) continue;
        float3 offset = point + normal * 1e-3f;
        float3 shadow_dir = to_light_dir;
        float3 filtered_color = (float3)(1.0f, 1.0f, 1.0f);
        bool occluded = false;
        Hit shadow_hits[8];
        int hit_count = 0;
        intersect_scene(
            offset, shadow_dir, dist,
            nodes_min, nodes_max, nodes_left, nodes_right,
            tri_indices,
            tris_v0, tris_v1, tris_v2,
            num_tris,
            shadow_hits, &hit_count, 8
        );
        for (int k = 0; k < hit_count; k++) {
            Hit h = shadow_hits[k];
            int t_idx = h.tri_idx;
            float alpha = tris_color[t_idx * 4 + 3];
            int tex_id = tris_tex_id[t_idx];
            if (tex_id >= 0) {
                float4 tex_col = sample_triangle_texture(
                    tex_data, tex_info, tex_id,
                    (float3)(tris_v0[t_idx * 3], tris_v0[t_idx * 3 + 1], tris_v0[t_idx * 3 + 2]),
                    (float3)(tris_v1[t_idx * 3], tris_v1[t_idx * 3 + 1], tris_v1[t_idx * 3 + 2]),
                    (float3)(tris_v2[t_idx * 3], tris_v2[t_idx * 3 + 1], tris_v2[t_idx * 3 + 2]),
                    h.u, h.v
                );
                alpha *= tex_col.w;
            }
            if (alpha >= 1.0f) {
                occluded = true;
                break;
            } else {
                float3 obj_color = (float3)(
                    tris_color[t_idx * 4 + 0],
                    tris_color[t_idx * 4 + 1],
                    tris_color[t_idx * 4 + 2]
                );
                if (tex_id >= 0) {
                    float4 tex_col = sample_triangle_texture(
                        tex_data, tex_info, tex_id,
                        (float3)(tris_v0[t_idx * 3], tris_v0[t_idx * 3 + 1], tris_v0[t_idx * 3 + 2]),
                        (float3)(tris_v1[t_idx * 3], tris_v1[t_idx * 3 + 1], tris_v1[t_idx * 3 + 2]),
                        (float3)(tris_v2[t_idx * 3], tris_v2[t_idx * 3 + 1], tris_v2[t_idx * 3 + 2]),
                        h.u, h.v
                    );
                    obj_color.x *= tex_col.x;
                    obj_color.y *= tex_col.y;
                    obj_color.z *= tex_col.z;
                }
                float inv_alpha = 1.0f - alpha;
                filtered_color.x *= (obj_color.x * alpha + inv_alpha);
                filtered_color.y *= (obj_color.y * alpha + inv_alpha);
                filtered_color.z *= (obj_color.z * alpha + inv_alpha);
                if (filtered_color.x < 0.01f && filtered_color.y < 0.01f && filtered_color.z < 0.01f) {
                    occluded = true;
                    break;
                }
            }
        }
        if (!occluded) {
            float diff = fmax(dot(normal, to_light_dir), 0.0f);
            float3 lit = light_color * (attenuation * diff);
            result.x += lit.x * filtered_color.x;
            result.y += lit.y * filtered_color.y;
            result.z += lit.z * filtered_color.z;
        }
    }
    return result;
}

__kernel void render_scene(
    __global uchar* output,
    int width,
    int height,
    float3 cam_origin,
    float3 cam_fwd,
    float3 cam_right,
    float3 cam_up,
    float half_tan,
    float aspect,
    float max_dist,
    float3 ambient,
    int num_dynamic_lights,
    __global const float* dynamic_lights,
    int num_all_lights,
    __global const float* all_lights,
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
    __global const float* tris_baked,
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
    Hit hits[8];
    int hit_count = 0;
    intersect_scene(
        cam_origin, dir, max_dist,
        nodes_min, nodes_max, nodes_left, nodes_right,
        tri_indices,
        tris_v0, tris_v1, tris_v2,
        num_tris,
        hits, &hit_count, 8
    );
    float3 final_color = (float3)(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;
    for (int i = 0; i < hit_count; i++) {
        Hit h = hits[i];
        int t_idx = h.tri_idx;
        float3 tv0 = (float3)(
            tris_v0[t_idx * 3],
            tris_v0[t_idx * 3 + 1],
            tris_v0[t_idx * 3 + 2]
        );
        float3 tv1 = (float3)(
            tris_v1[t_idx * 3],
            tris_v1[t_idx * 3 + 1],
            tris_v1[t_idx * 3 + 2]
        );
        float3 tv2 = (float3)(
            tris_v2[t_idx * 3],
            tris_v2[t_idx * 3 + 1],
            tris_v2[t_idx * 3 + 2]
        );
        float3 base_rgb = (float3)(
            tris_color[t_idx * 4],
            tris_color[t_idx * 4 + 1],
            tris_color[t_idx * 4 + 2]
        );
        float base_alpha = tris_color[t_idx * 4 + 3];
        int tex_id = tris_tex_id[t_idx];
        if (tex_id >= 0) {
            float4 tex_col = sample_triangle_texture(
                tex_data, tex_info, tex_id, tv0, tv1, tv2, h.u, h.v
            );
            base_rgb.x = tex_col.x * base_rgb.x;
            base_rgb.y = tex_col.y * base_rgb.y;
            base_rgb.z = tex_col.z * base_rgb.z;
            base_alpha = tex_col.w * base_alpha;
        }
        float3 hit_point = cam_origin + dir * h.t;
        float3 normal = (float3)(
            tris_normal[t_idx * 3],
            tris_normal[t_idx * 3 + 1],
            tris_normal[t_idx * 3 + 2]
        );
        float baked_x = tris_baked[t_idx * 3];
        float baked_y = tris_baked[t_idx * 3 + 1];
        float baked_z = tris_baked[t_idx * 3 + 2];
        float3 dyn;
        float3 light_factor;
        if (baked_x >= 0.0f && baked_y >= 0.0f && baked_z >= 0.0f) {
            dyn = compute_dynamic_lighting(
                hit_point, normal,
                num_dynamic_lights, dynamic_lights,
                nodes_min, nodes_max, nodes_left, nodes_right,
                tris_v0, tris_v1, tris_v2,
                tris_color, tri_indices, tris_tex_id,
                tex_data, tex_info,
                num_tris
            );
            light_factor = (float3)(
                fmin(ambient.x + baked_x + dyn.x, 1.0f),
                fmin(ambient.y + baked_y + dyn.y, 1.0f),
                fmin(ambient.z + baked_z + dyn.z, 1.0f)
            );
        } else {
            dyn = compute_dynamic_lighting(
                hit_point, normal,
                num_all_lights, all_lights,
                nodes_min, nodes_max, nodes_left, nodes_right,
                tris_v0, tris_v1, tris_v2,
                tris_color, tri_indices, tris_tex_id,
                tex_data, tex_info,
                num_tris
            );
            light_factor = (float3)(
                fmin(ambient.x + dyn.x, 1.0f),
                fmin(ambient.y + dyn.y, 1.0f),
                fmin(ambient.z + dyn.z, 1.0f)
            );
        }
        if (light_factor.x <= 0.0f && light_factor.y <= 0.0f && light_factor.z <= 0.0f) {
            light_factor = (float3)(1.0f, 1.0f, 1.0f);
        }
        float3 lit_color = (float3)(
            base_rgb.x * light_factor.x,
            base_rgb.y * light_factor.y,
            base_rgb.z * light_factor.z
        );
        float alpha = base_alpha;
        if (alpha <= 0.0f) continue;
        float weight = alpha * transmittance;
        final_color.x += lit_color.x * weight;
        final_color.y += lit_color.y * weight;
        final_color.z += lit_color.z * weight;
        transmittance *= (1.0f - alpha);
        if (transmittance <= 0.0f) break;
    }
    int out_idx = gid * 4;
    output[out_idx + 0] = (uchar)round(fmin(fmax(final_color.x, 0.0f), 1.0f) * 255.0f);
    output[out_idx + 1] = (uchar)round(fmin(fmax(final_color.y, 0.0f), 1.0f) * 255.0f);
    output[out_idx + 2] = (uchar)round(fmin(fmax(final_color.z, 0.0f), 1.0f) * 255.0f);
    output[out_idx + 3] = (uchar)255;
}
"#;

const PREPARE_KERNEL_SRC: &str = r#"
__kernel void prepare_triangle_data(
    __global const float* vertices,
    __global float* output,
    const int num_triangles
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles) return;
    int base = gid * 9;
    float3 v0 = (float3)(vertices[base + 0], vertices[base + 1], vertices[base + 2]);
    float3 v1 = (float3)(vertices[base + 3], vertices[base + 4], vertices[base + 5]);
    float3 v2 = (float3)(vertices[base + 6], vertices[base + 7], vertices[base + 8]);
    float3 centroid = (v0 + v1 + v2) / 3.0f;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 normal = cross(edge1, edge2);
    float len = length(normal);
    if (len > 1e-8f) {
        normal = normal / len;
    }
    int out_base = gid * 6;
    output[out_base + 0] = centroid.x;
    output[out_base + 1] = centroid.y;
    output[out_base + 2] = centroid.z;
    output[out_base + 3] = normal.x;
    output[out_base + 4] = normal.y;
    output[out_base + 5] = normal.z;
}

__kernel void prepare_occluder_data(
    __global const float* vertices,
    __global const float* alphas,
    __global float* output,
    const int num_occluders
) {
    int gid = get_global_id(0);
    if (gid >= num_occluders) return;
    int v_base = gid * 9;
    int out_base = gid * 10;
    output[out_base + 0] = vertices[v_base + 0];
    output[out_base + 1] = vertices[v_base + 1];
    output[out_base + 2] = vertices[v_base + 2];
    output[out_base + 3] = vertices[v_base + 3];
    output[out_base + 4] = vertices[v_base + 4];
    output[out_base + 5] = vertices[v_base + 5];
    output[out_base + 6] = vertices[v_base + 6];
    output[out_base + 7] = vertices[v_base + 7];
    output[out_base + 8] = vertices[v_base + 8];
    output[out_base + 9] = alphas[gid];
}

__kernel void prepare_light_data(
    __global const float* positions,
    __global const float* directions,
    __global const float* cone_angles,
    __global const float* distances,
    __global const float* colors,
    __global float* output,
    const int num_lights
) {
    int gid = get_global_id(0);
    if (gid >= num_lights) return;
    int out_base = gid * 11;
    output[out_base + 0] = positions[gid * 3 + 0];
    output[out_base + 1] = positions[gid * 3 + 1];
    output[out_base + 2] = positions[gid * 3 + 2];
    output[out_base + 3] = directions[gid * 3 + 0];
    output[out_base + 4] = directions[gid * 3 + 1];
    output[out_base + 5] = directions[gid * 3 + 2];
    float half_cone = (cone_angles[gid] * 0.5f) * 3.14159265f / 180.0f;
    output[out_base + 6] = cos(half_cone);
    float safe_distance = max(distances[gid], 1e-6f);
    float falloff = safe_distance / sqrt(1.0f / 0.01f - 1.0f);
    output[out_base + 7] = falloff;
    output[out_base + 8] = colors[gid * 3 + 0] / 255.0f;
    output[out_base + 9] = colors[gid * 3 + 1] / 255.0f;
    output[out_base + 10] = colors[gid * 3 + 2] / 255.0f;
}
"#;

const BAKE_KERNEL_SRC: &str = r#"
__kernel void bake_lighting(
    __global const float* tri_data,
    __global const float* occluder_data,
    __global const float* light_data,
    const int num_triangles,
    const int num_occluders,
    const int num_lights,
    __global float* output
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles) return;
    float3 centroid = (float3)(
        tri_data[gid * 6 + 0],
        tri_data[gid * 6 + 1],
        tri_data[gid * 6 + 2]
    );
    float3 normal = (float3)(
        tri_data[gid * 6 + 3],
        tri_data[gid * 6 + 4],
        tri_data[gid * 6 + 5]
    );
    float len = sqrt(dot(normal, normal));
    if (len > 1e-8f) {
        normal = normal / len;
    }
    float3 result = (float3)(0.0f, 0.0f, 0.0f);
    for (int li = 0; li < num_lights; li++) {
        int base_l = li * 11;
        float3 light_pos = (float3)(
            light_data[base_l + 0],
            light_data[base_l + 1],
            light_data[base_l + 2]
        );
        float3 light_dir = (float3)(
            light_data[base_l + 3],
            light_data[base_l + 4],
            light_data[base_l + 5]
        );
        float cos_half = light_data[base_l + 6];
        float falloff = light_data[base_l + 7];
        float3 light_color = (float3)(
            light_data[base_l + 8],
            light_data[base_l + 9],
            light_data[base_l + 10]
        );
        float3 to_light = light_pos - centroid;
        float dist = length(to_light);
        if (dist < 1e-6f) continue;
        float3 to_light_dir = to_light / dist;
        float3 point_dir_from_light = -to_light_dir;
        float cos_angle = dot(light_dir, point_dir_from_light);
        if (cos_angle < cos_half) continue;
        float attenuation = 1.0f / (1.0f + (dist / falloff) * (dist / falloff));
        if (attenuation < 0.01f) continue;
        float transmittance = 1.0f;
        float3 offset = (float3)(
            centroid.x + normal.x * 0.001f,
            centroid.y + normal.y * 0.001f,
            centroid.z + normal.z * 0.1f
        );
        for (int oi = 0; oi < num_occluders; oi++) {
            int base_o = oi * 10;
            float3 v0 = (float3)(
                occluder_data[base_o + 0],
                occluder_data[base_o + 1],
                occluder_data[base_o + 2]
            );
            float3 v1 = (float3)(
                occluder_data[base_o + 3],
                occluder_data[base_o + 4],
                occluder_data[base_o + 5]
            );
            float3 v2 = (float3)(
                occluder_data[base_o + 6],
                occluder_data[base_o + 7],
                occluder_data[base_o + 8]
            );
            float alpha = occluder_data[base_o + 9];
            float3 edge1 = v1 - v0;
            float3 edge2 = v2 - v0;
            float3 h = cross(to_light_dir, edge2);
            float a = dot(edge1, h);
            if (fabs(a) < 1e-6f) continue;
            float f = 1.0f / a;
            float3 s = offset - v0;
            float u = f * dot(s, h);
            if (u < 0.0f || u > 1.0f) continue;
            float3 q = cross(s, edge1);
            float v = f * dot(to_light_dir, q);
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = f * dot(edge2, q);
            if (t > 1e-6f && t < dist) {
                if (alpha >= 1.0f) {
                    transmittance = 0.0f;
                    break;
                } else {
                    transmittance *= (1.0f - alpha);
                    if (transmittance < 0.01f) {
                        transmittance = 0.0f;
                        break;
                    }
                }
            }
        }
        if (transmittance <= 0.0f) continue;
        float diff = fmax(dot(normal, to_light_dir), 0.0f);
        float factor = attenuation * diff * transmittance;
        result += light_color * factor;
    }
    result = fmin(result, (float3)(1.0f, 1.0f, 1.0f));
    output[gid * 3 + 0] = result.x;
    output[gid * 3 + 1] = result.y;
    output[gid * 3 + 2] = result.z;
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

fn point_in_polygon_offset(px: f32, py: f32, vertices: &[f32], offset_x: f32, offset_y: f32) -> bool {
    let mut inside = false;
    let n = vertices.len() / 2;
    for i in 0..n {
        let j = (i + 1) % n;
        let xi = vertices[2 * i] + offset_x;
        let yi = vertices[2 * i + 1] + offset_y;
        let xj = vertices[2 * j] + offset_x;
        let yj = vertices[2 * j + 1] + offset_y;
        let intersect = ((yi > py) != (yj > py))
            && (px < (xj - xi) * (py - yi) / (yj - yi) + xi);
        if intersect {
            inside = !inside;
        }
    }
    inside
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

fn ray_triangle_intersect(
    origin: &[f32; 3],
    dir: &[f32; 3],
    v0: &[f32; 3],
    v1: &[f32; 3],
    v2: &[f32; 3],
) -> Option<(f32, f32, f32)> {
    let eps = 1e-6;
    let edge1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
    let edge2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
    let h = cross(dir, &edge2);
    let a = dot(&edge1, &h);
    if a.abs() < eps {
        return None;
    }
    let f = 1.0 / a;
    let s = [origin[0] - v0[0], origin[1] - v0[1], origin[2] - v0[2]];
    let u = f * dot(&s, &h);
    if u < 0.0 || u > 1.0 {
        return None;
    }
    let q = cross(&s, &edge1);
    let v = f * dot(dir, &q);
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let t = f * dot(&edge2, &q);
    if t > eps {
        Some((t, u, v))
    } else {
        None
    }
}

fn generate_uv_for_triangle(
    v0: &[f32; 3],
    v1: &[f32; 3],
    v2: &[f32; 3],
) -> ((f32, f32), (f32, f32), (f32, f32)) {
    let mut min = [v0[0], v0[1], v0[2]];
    let mut max = [v0[0], v0[1], v0[2]];
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
    let ranges = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    let mut axes = [0, 1, 2];
    axes.sort_by(|&a, &b| ranges[b].partial_cmp(&ranges[a]).unwrap());
    let a = axes[0];
    let b = axes[1];
    let denom_a = if ranges[a] < 1e-8 { 1.0 } else { ranges[a] };
    let denom_b = if ranges[b] < 1e-8 { 1.0 } else { ranges[b] };
    let uv0 = ((v0[a] - min[a]) / denom_a, (v0[b] - min[b]) / denom_b);
    let uv1 = ((v1[a] - min[a]) / denom_a, (v1[b] - min[b]) / denom_b);
    let uv2 = ((v2[a] - min[a]) / denom_a, (v2[b] - min[b]) / denom_b);
    (uv0, uv1, uv2)
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

fn ray_intersect_aabb(
    origin: &[f32; 3],
    dir: &[f32; 3],
    aabb_min: &[f32; 3],
    aabb_max: &[f32; 3],
    max_t: f32,
) -> bool {
    let mut tmin: f32 = 0.0;
    let mut tmax: f32 = max_t;
    for i in 0..3 {
        if dir[i].abs() < 1e-8 {
            if origin[i] < aabb_min[i] || origin[i] > aabb_max[i] {
                return false;
            }
        } else {
            let inv = 1.0 / dir[i];
            let t1 = (aabb_min[i] - origin[i]) * inv;
            let t2 = (aabb_max[i] - origin[i]) * inv;
            let (t_near, t_far) = if t1 < t2 { (t1, t2) } else { (t2, t1) };
            tmin = tmin.max(t_near);
            tmax = tmax.min(t_far);
            if tmin > tmax {
                return false;
            }
        }
    }
    true
}

fn collect_candidates(
    origin: &[f32; 3],
    dir: &[f32; 3],
    max_t: f32,
    node: &BvhNode,
    _triangles: &[Render_triangle],
    out: &mut Vec<usize>,
) {
    if !ray_intersect_aabb(origin, dir, &node.aabb_min, &node.aabb_max, max_t) {
        return;
    }
    if node.triangles.is_empty() {
        if let Some(left) = &node.left {
            collect_candidates(origin, dir, max_t, left, _triangles, out);
        }
        if let Some(right) = &node.right {
            collect_candidates(origin, dir, max_t, right, _triangles, out);
        }
    } else {
        for &idx in &node.triangles {
            out.push(node.triangle_offset + idx);
        }
    }
}

fn light_transmittance(
    point: [f32; 3],
    normal: [f32; 3],
    light_pos: [f32; 3],
    dist_to_light: f32,
    bvh: &BvhNode,
    triangles: &[Render_triangle],
) -> f32 {
    let offset = [
        point[0] + normal[0] * 1e-3,
        point[1] + normal[1] * 1e-3,
        point[2] + normal[2] * 0.1,
    ];
    let to_light = [
        light_pos[0] - point[0],
        light_pos[1] - point[1],
        light_pos[2] - point[2],
    ];
    let dir = normalize(to_light);
    let mut candidates = Vec::new();
    collect_candidates(&offset, &dir, dist_to_light, bvh, triangles, &mut candidates);
    let mut transmittance = 1.0;
    for &idx in &candidates {
        let rt = &triangles[idx];
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        if let Some((t, _, _)) = ray_triangle_intersect(&offset, &dir, &v0, &v1, &v2) {
            if t > 1e-3 && t < dist_to_light {
                let alpha = tri.draw_RGBA_color[3] as f32 / 255.0;
                if alpha >= 1.0 {
                    return 0.0;
                }
                transmittance *= 1.0 - alpha;
                if transmittance < 0.01 {
                    return transmittance;
                }
            }
        }
    }
    transmittance
}

fn compute_lighting(
    point: [f32; 3],
    normal: [f32; 3],
    bvh: &BvhNode,
    triangles: &[Render_triangle],
    lights: &[super::Light_components],
    ambient: [u8; 3],
) -> [f32; 3] {
    let mut r = ambient[0] as f32 / 255.0;
    let mut g = ambient[1] as f32 / 255.0;
    let mut b = ambient[2] as f32 / 255.0;
    const EPSILON: f32 = 0.01;
    for light in lights {
        let light_pos = [light.light_x, light.light_y, light.light_z];
        let to_light = [
            light_pos[0] - point[0],
            light_pos[1] - point[1],
            light_pos[2] - point[2],
        ];
        let dist_sq = to_light[0] * to_light[0] + to_light[1] * to_light[1] + to_light[2] * to_light[2];
        let dist = dist_sq.sqrt();
        if dist < 1e-6 {
            continue;
        }
        let to_light_dir = normalize(to_light);
        let light_forward = camera_basis(light.light_pitch, light.light_yaw, 0.0).forward;
        let point_dir_from_light = [-to_light_dir[0], -to_light_dir[1], -to_light_dir[2]];
        let cos_angle = dot(&light_forward, &point_dir_from_light);
        let half_cone = (light.light_cone_angle * 0.5) * PI / 180.0;
        let cos_half_cone = half_cone.cos();
        if cos_angle < cos_half_cone {
            continue;
        }
        let transmittance = light_transmittance(point, normal, light_pos, dist, bvh, triangles);
        if transmittance <= 0.0 {
            continue;
        }
        let falloff = light.light_distance.max(1e-6) / (1.0f32 / EPSILON - 1.0f32).sqrt();
        let attenuation = 1.0 / (1.0 + (dist / falloff).powi(2));
        if attenuation < EPSILON {
            continue;
        }
        let diff = dot(&normal, &to_light_dir).max(0.0);
        let factor = attenuation * diff * transmittance;
        r += (light.light_RGB_color[0] as f32 / 255.0) * factor;
        g += (light.light_RGB_color[1] as f32 / 255.0) * factor;
        b += (light.light_RGB_color[2] as f32 / 255.0) * factor;
    }
    [r.min(1.0), g.min(1.0), b.min(1.0)]
}

pub fn ray_tracing(
    start_x: f32,
    start_y: f32,
    start_z: f32,
    length: i128,
    dir: [f32; 3],
    bvh: &BvhNode,
    triangles: &[Render_triangle],
    textures: &HashMap<String, RgbaImage>,
    dynamic_lights: &[super::Light_components],
    all_lights: &[super::Light_components],
    ambient_light: [u8; 3],
) -> super::Pixel_structure {
    let origin = [start_x, start_y, start_z];
    let max_t = length as f32;
    let mut candidates = Vec::new();
    collect_candidates(&origin, &dir, max_t, bvh, triangles, &mut candidates);
    let mut hits = Vec::new();
    for &idx in &candidates {
        let rt = &triangles[idx];
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        if let Some((t, u, v)) = ray_triangle_intersect(&origin, &dir, &v0, &v1, &v2) {
            if t > 0.0 && t < max_t {
                let (r, g, b, a) = if tri.draw_texture_path != "none" {
                    if let Some(img) = textures.get(&tri.draw_texture_path) {
                        let (uv0, uv1, uv2) = generate_uv_for_triangle(&v0, &v1, &v2);
                        let w = 1.0 - u - v;
                        let tex_u = w * uv0.0 + u * uv1.0 + v * uv2.0;
                        let tex_v = w * uv0.1 + u * uv1.1 + v * uv2.1;
                        let tex_x = (tex_u * (img.width() as f32 - 1.0)).round() as u32;
                        let tex_y = (tex_v * (img.height() as f32 - 1.0)).round() as u32;
                        let tex_x = tex_x.min(img.width() - 1);
                        let tex_y = tex_y.min(img.height() - 1);
                        let pixel = img.get_pixel(tex_x, tex_y);
                        let c = &tri.draw_RGBA_color;
                        let a = (pixel[3] as f32 / 255.0) * (c[3] as f32 / 255.0);
                        let r = ((pixel[0] as f32 / 255.0) * (c[0] as f32 / 255.0) * 255.0).round() as u8;
                        let g = ((pixel[1] as f32 / 255.0) * (c[1] as f32 / 255.0) * 255.0).round() as u8;
                        let b = ((pixel[2] as f32 / 255.0) * (c[2] as f32 / 255.0) * 255.0).round() as u8;
                        (r, g, b, a)
                    } else {
                        let c = &tri.draw_RGBA_color;
                        (c[0], c[1], c[2], c[3] as f32 / 255.0)
                    }
                } else {
                    let c = &tri.draw_RGBA_color;
                    (c[0], c[1], c[2], c[3] as f32 / 255.0)
                };
                let edge1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
                let edge2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
                let normal = normalize(cross(&edge1, &edge2));
                let hit_point = [
                    origin[0] + dir[0] * t,
                    origin[1] + dir[1] * t,
                    origin[2] + dir[2] * t,
                ];
                let mut light_factor = if let Some(baked) = rt.baked_light {
                    let dynamic_contrib = compute_lighting(
                        hit_point,
                        normal,
                        bvh,
                        triangles,
                        dynamic_lights,
                        [0, 0, 0],
                    );
                    let amb = [
                        ambient_light[0] as f32 / 255.0,
                        ambient_light[1] as f32 / 255.0,
                        ambient_light[2] as f32 / 255.0,
                    ];
                    [
                        (amb[0] + baked[0] + dynamic_contrib[0]).min(1.0),
                        (amb[1] + baked[1] + dynamic_contrib[1]).min(1.0),
                        (amb[2] + baked[2] + dynamic_contrib[2]).min(1.0),
                    ]
                } else {
                    compute_lighting(hit_point, normal, bvh, triangles, all_lights, ambient_light)
                };
                if light_factor[0] <= 0.0 && light_factor[1] <= 0.0 && light_factor[2] <= 0.0 {
                    light_factor = [1.0, 1.0, 1.0];
                }
                let lit_r = (r as f32 * light_factor[0]).round() as u8;
                let lit_g = (g as f32 * light_factor[1]).round() as u8;
                let lit_b = (b as f32 * light_factor[2]).round() as u8;
                hits.push((t, lit_r, lit_g, lit_b, a, tri.draw_symbol));
            }
        }
    }
    hits.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());
    let mut best_pixel = super::Empty_pixel.lock().unwrap().clone();
    let mut best_alpha = 0.0;
    for (_, r, g, b, a, symbol) in hits {
        if a <= 0.0 {
            continue;
        }
        let fg = [r, g, b, 255];
        if a >= 1.0 {
            let bg = best_pixel.pixel_RGBA_color;
            let mut blended = [0u8; 4];
            for i in 0..3 {
                blended[i] = ((bg[i] as f32 * (1.0 - best_alpha)) + (fg[i] as f32 * a)) as u8;
            }
            blended[3] = 255;
            best_pixel = super::Pixel_structure {
                pixel_symbol: symbol,
                pixel_RGBA_color: blended,
            };
            break;
        } else {
            let bg = best_pixel.pixel_RGBA_color;
            let mut blended = [0u8; 4];
            for i in 0..3 {
                blended[i] = ((bg[i] as f32 * (1.0 - best_alpha)) + (fg[i] as f32 * a)) as u8;
            }
            blended[3] = 255;
            best_pixel = super::Pixel_structure {
                pixel_symbol: symbol,
                pixel_RGBA_color: blended,
            };
            best_alpha += a * (1.0 - best_alpha);
        }
    }
    best_pixel
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

fn pack_lights(lights: &[super::Light_components]) -> Vec<f32> {
    let mut data = Vec::with_capacity(lights.len() * 11);
    for l in lights {
        let fwd = camera_basis(l.light_pitch, l.light_yaw, 0.0).forward;
        let half_cone = (l.light_cone_angle * 0.5) * PI / 180.0;
        let falloff = l.light_distance.max(1e-6) / (1.0f32 / 0.01f32 - 1.0f32).sqrt();
        data.extend_from_slice(&[
            l.light_x,
            l.light_y,
            l.light_z,
            fwd[0],
            fwd[1],
            fwd[2],
            half_cone.cos(),
            falloff,
            l.light_RGB_color[0] as f32 / 255.0,
            l.light_RGB_color[1] as f32 / 255.0,
            l.light_RGB_color[2] as f32 / 255.0,
        ]);
    }
    data
}

pub fn Render_3d_to_screen(
    dynamic_triangles: &[super::Draw_components],
    screen: &mut Vec<Vec<super::Pixel_structure>>,
    dynamic_lights: &[super::Light_components],
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
    let ambient_u8 = [cam.ambient_light[0], cam.ambient_light[1], cam.ambient_light[2]];
    let ambient_f32 = [
        cam.ambient_light[0] as f32 / 255.0,
        cam.ambient_light[1] as f32 / 255.0,
        cam.ambient_light[2] as f32 / 255.0,
    ];
    let fov = cam.camera_fov as f32;
    drop(cam);
    let aspect = width as f32 / height as f32;
    let half_tan = if fov > 0.0 {
        ((fov * PI / 180.0) / 2.0).tan()
    } else {
        1.0
    };
    let tan_h = half_tan * aspect;
    let tan_v = half_tan;
    let baked_static = Baked_static_triangles.lock().unwrap().clone();
    let static_bvh_opt = Baked_static_bvh.lock().unwrap().clone();
    let static_bvh = match static_bvh_opt {
        Some(node) => node,
        None => build_bvh(&baked_static, 0),
    };
    let dynamic_render: Vec<Render_triangle> = dynamic_triangles
        .iter()
        .map(|t| Render_triangle {
            triangle: t.clone(),
            baked_light: None,
        })
        .collect();
    let mut all_triangles = baked_static.clone();
    all_triangles.extend(dynamic_render.clone());
    if all_triangles.is_empty() {
        return;
    }
    let combined_bvh = if dynamic_render.is_empty() {
        static_bvh
    } else if baked_static.is_empty() {
        build_bvh(&dynamic_render, 0)
    } else {
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
    let static_lights = super::Static_light.lock().unwrap().clone();
    let mut all_lights: Vec<super::Light_components> = static_lights;
    all_lights.extend(dynamic_lights.iter().cloned());
    let all_lights_culled: Vec<super::Light_components> = all_lights;
    let dynamic_lights_culled: Vec<super::Light_components> = dynamic_lights.to_vec();
    let available = thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(1);
    let cores_raw = settings.cores_multiply.clone();
    let cores = if cores_raw > 0 {
        cores_raw as usize
    } else {
        1
    };
    let thread_count = available
        .saturating_mul(cores)
        .max(1);
    drop(settings);
    let width_usize = width as usize;
    let height_usize = height as usize;
    if width_usize == 0 || height_usize == 0 {
        return;
    }
    let mut flat_nodes = Vec::new();
    let mut tri_indices = Vec::new();
    flatten_bvh(&combined_bvh, &mut flat_nodes, &mut tri_indices);
    let mut tri_v0: Vec<f32> = Vec::with_capacity(all_triangles.len() * 3);
    let mut tri_v1: Vec<f32> = Vec::with_capacity(all_triangles.len() * 3);
    let mut tri_v2: Vec<f32> = Vec::with_capacity(all_triangles.len() * 3);
    let mut tri_normal: Vec<f32> = Vec::with_capacity(all_triangles.len() * 3);
    let mut tri_color: Vec<f32> = Vec::with_capacity(all_triangles.len() * 4);
    let mut tri_baked: Vec<f32> = Vec::with_capacity(all_triangles.len() * 3);
    let mut tri_tex_id: Vec<i32> = Vec::with_capacity(all_triangles.len());
    let mut texture_map: HashMap<String, RgbaImage> = HashMap::new();
    let mut path_to_tex_id: HashMap<String, i32> = HashMap::new();
    let mut tex_infos: Vec<i32> = Vec::new();
    let mut global_tex_data: Vec<u8> = Vec::new();
    for rt in all_triangles.iter() {
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
        let baked = rt.baked_light.unwrap_or([-1.0, -1.0, -1.0]);
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
        tri_v0.extend_from_slice(&v0);
        tri_v1.extend_from_slice(&v1);
        tri_v2.extend_from_slice(&v2);
        tri_normal.extend_from_slice(&normal);
        tri_color.extend_from_slice(&color);
        tri_baked.extend_from_slice(&baked);
        tri_tex_id.push(tex_id);
    }
    let state_mutex = match get_opencl_state() {
        Ok(v) => v,
        Err(_) => return,
    };
    let state_guard = state_mutex.lock().unwrap();
    if let Some(state) = state_guard.as_ref() {
        let pixel_count = width_usize * height_usize;
        let output_buffer: Buffer<u8> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(pixel_count * 4)
            .build()
            .unwrap();
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
        let tri_baked_safe = if tri_baked.is_empty() { vec![0.0f32] } else { tri_baked };
        let buf_baked: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_ONLY)
            .len(tri_baked_safe.len().max(1))
            .copy_host_slice(&tri_baked_safe)
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
        let dynamic_light_data = pack_lights(&dynamic_lights_culled);
        let all_light_data = pack_lights(&all_lights_culled);
        let dynamic_light_gpu = if dynamic_light_data.is_empty() { vec![0.0f32] } else { dynamic_light_data };
        let all_light_gpu = if all_light_data.is_empty() { vec![0.0f32] } else { all_light_data };
        let buf_dynamic_lights: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_ONLY)
            .len(dynamic_light_gpu.len().max(1))
            .copy_host_slice(&dynamic_light_gpu)
            .build()
            .unwrap();
        let buf_all_lights: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_ONLY)
            .len(all_light_gpu.len().max(1))
            .copy_host_slice(&all_light_gpu)
            .build()
            .unwrap();
        let tex_info_gpu = if tex_infos.is_empty() { vec![0i32] } else { tex_infos.clone() };
        let tex_data_gpu = if global_tex_data.is_empty() { vec![0u8] } else { global_tex_data.clone() };
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
        let arg_num_dynamic_lights = dynamic_lights_culled.len() as i32;
        let arg_num_all_lights = all_lights_culled.len() as i32;
        let arg_num_nodes = flat_nodes.len().max(1) as i32;
        let arg_num_tris = all_triangles.len() as i32;
        let arg_num_tex = (tex_infos.len() / 4).max(1) as i32;
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
            .arg(&arg_num_dynamic_lights)
            .arg(&buf_dynamic_lights)
            .arg(&arg_num_all_lights)
            .arg(&buf_all_lights)
            .arg(&arg_num_nodes)
            .arg(&buf_nodes_min)
            .arg(&buf_nodes_max)
            .arg(&buf_nodes_left)
            .arg(&buf_nodes_right)
            .arg(&arg_num_tris)
            .arg(&buf_v0)
            .arg(&buf_v1)
            .arg(&buf_v2)
            .arg(&buf_norm)
            .arg(&buf_col)
            .arg(&buf_baked)
            .arg(&buf_texid)
            .arg(&buf_indices)
            .arg(&arg_num_tex)
            .arg(&buf_tex_info)
            .arg(&buf_tex_data)
            .global_work_size(pixel_count)
            .build()
            .unwrap();
        unsafe {
            kernel.enq().unwrap();
        }
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
    } else {
        let bvh_arc = Arc::new(combined_bvh);
        let triangles_arc = Arc::new(all_triangles);
        let texture_arc = Arc::new(texture_map);
        let thread_count = thread_count.max(1).min(height_usize.max(1));
        let base_rows = height_usize / thread_count;
        let remainder = height_usize % thread_count;
        let mut start_y = 0usize;
        let mut handles = Vec::new();
        for t in 0..thread_count {
            let rows = base_rows + if t < remainder { 1 } else { 0 };
            let end_y = start_y + rows;
            if start_y >= end_y {
                start_y = end_y;
                continue;
            }
            let bvh_clone = bvh_arc.clone();
            let triangles_clone = triangles_arc.clone();
            let texture_map = texture_arc.clone();
            let dynamic_lights_local = dynamic_lights_culled.to_vec();
            let all_lights_local = all_lights_culled.to_vec();
            let ox = origin[0];
            let oy = origin[1];
            let oz = origin[2];
            let max_dist = max_dist_i128;
            let width = width_usize;
            let height = height_usize;
            let aspect = aspect;
            let half_tan = half_tan;
            let basis = basis.clone();
            let ambient_light = ambient_u8;
            handles.push(thread::spawn(move || {
                let mut result = Vec::with_capacity((end_y - start_y) * width);
                for y in start_y..end_y {
                    for x in 0..width {
                        let nx = (2.0 * (x as f32 + 0.5) / width as f32) - 1.0;
                        let ny = 1.0 - (2.0 * (y as f32 + 0.5) / height as f32);
                        let px = nx * half_tan * aspect;
                        let py = ny * half_tan;
                        let dir = normalize([
                            basis.forward[0] + basis.right[0] * px + basis.up[0] * py,
                            basis.forward[1] + basis.right[1] * px + basis.up[1] * py,
                            basis.forward[2] + basis.right[2] * px + basis.up[2] * py,
                        ]);
                        let pixel = ray_tracing(
                            ox,
                            oy,
                            oz,
                            max_dist,
                            dir,
                            &bvh_clone,
                            &triangles_clone,
                            &texture_map,
                            &dynamic_lights_local,
                            &all_lights_local,
                            ambient_light,
                        );
                        result.push((y, x, pixel));
                    }
                }
                result
            }));
            start_y = end_y;
        }
        for handle in handles {
            for (y, x, pixel) in handle.join().unwrap() {
                screen[y][x] = pixel;
            }
        }
    }
}

fn build_bvh_on_gpu(triangles: &[Render_triangle]) -> BvhNode {
    build_bvh(triangles, 0)
}

fn Bake_scene() {
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
    let static_lights = super::Static_light.lock().unwrap().clone();
    let occluders: Vec<Render_triangle> = static_triangles
        .iter()
        .cloned()
        .map(|t| Render_triangle {
            triangle: t,
            baked_light: None,
        })
        .collect();
    let num_triangles = static_triangles.len();
    let num_occluders = occluders.len();
    let num_lights = static_lights.len();
    let mut raw_vertices: Vec<f32> = Vec::with_capacity(num_triangles * 9);
    let mut raw_alphas: Vec<f32> = Vec::with_capacity(num_occluders);
    let mut light_positions: Vec<f32> = Vec::with_capacity(num_lights * 3);
    let mut light_directions: Vec<f32> = Vec::with_capacity(num_lights * 3);
    let mut light_cone_angles: Vec<f32> = Vec::with_capacity(num_lights);
    let mut light_distances: Vec<f32> = Vec::with_capacity(num_lights);
    let mut light_colors: Vec<f32> = Vec::with_capacity(num_lights * 3);
    for tri in &static_triangles {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
        raw_vertices.push(verts[0] + tri.draw_x);
        raw_vertices.push(verts[1] + tri.draw_y);
        raw_vertices.push(verts[2] + tri.draw_z);
        raw_vertices.push(verts[3] + tri.draw_x);
        raw_vertices.push(verts[4] + tri.draw_y);
        raw_vertices.push(verts[5] + tri.draw_z);
        raw_vertices.push(verts[6] + tri.draw_x);
        raw_vertices.push(verts[7] + tri.draw_y);
        raw_vertices.push(verts[8] + tri.draw_z);
    }
    for rt in &occluders {
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
        raw_vertices.push(verts[0] + tri.draw_x);
        raw_vertices.push(verts[1] + tri.draw_y);
        raw_vertices.push(verts[2] + tri.draw_z);
        raw_vertices.push(verts[3] + tri.draw_x);
        raw_vertices.push(verts[4] + tri.draw_y);
        raw_vertices.push(verts[5] + tri.draw_z);
        raw_vertices.push(verts[6] + tri.draw_x);
        raw_vertices.push(verts[7] + tri.draw_y);
        raw_vertices.push(verts[8] + tri.draw_z);
        raw_alphas.push(tri.draw_RGBA_color[3] as f32 / 255.0);
    }
    for light in &static_lights {
        light_positions.push(light.light_x);
        light_positions.push(light.light_y);
        light_positions.push(light.light_z);
        let light_forward = camera_basis(light.light_pitch, light.light_yaw, 0.0).forward;
        light_directions.push(light_forward[0]);
        light_directions.push(light_forward[1]);
        light_directions.push(light_forward[2]);
        light_cone_angles.push(light.light_cone_angle);
        light_distances.push(light.light_distance);
        light_colors.push(light.light_RGB_color[0] as f32);
        light_colors.push(light.light_RGB_color[1] as f32);
        light_colors.push(light.light_RGB_color[2] as f32);
    }
    let mut output = vec![0.0f32; num_triangles * 3];
    if num_triangles > 0 && num_lights > 0 {
        let state_mutex = get_opencl_state().expect("Failed to get OpenCL state");
        let state_guard = state_mutex.lock().unwrap();
        if let Some(state) = state_guard.as_ref() {
            let arg_num_triangles = num_triangles as i32;
            let arg_num_occluders = num_occluders as i32;
            let arg_num_lights = num_lights as i32;
            let raw_vertices_safe = if raw_vertices.is_empty() { vec![0.0f32] } else { raw_vertices.clone() };
            let raw_vertices_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(raw_vertices_safe.len().max(1))
                .copy_host_slice(&raw_vertices_safe)
                .build()
                .unwrap();
            let tri_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len((num_triangles * 6).max(1))
                .build()
                .unwrap();
            let prepare_tri_kernel = Kernel::builder()
                .program(&state.prepare_program)
                .name("prepare_triangle_data")
                .queue(state.queue.clone())
                .arg(&raw_vertices_buffer)
                .arg(&tri_data_buffer)
                .arg(&arg_num_triangles)
                .global_work_size(num_triangles)
                .build()
                .unwrap();
            unsafe {
                prepare_tri_kernel.enq().unwrap();
            }
            let raw_occluder_vertices_safe = if raw_vertices.len() < num_occluders * 9 {
                vec![0.0f32; (num_occluders * 9).max(1)]
            } else {
                raw_vertices[0..num_occluders * 9].to_vec()
            };
            let raw_occluder_vertices_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(raw_occluder_vertices_safe.len().max(1))
                .copy_host_slice(&raw_occluder_vertices_safe)
                .build()
                .unwrap();
            let raw_alphas_safe = if raw_alphas.is_empty() { vec![0.0f32] } else { raw_alphas };
            let raw_alphas_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(raw_alphas_safe.len().max(1))
                .copy_host_slice(&raw_alphas_safe)
                .build()
                .unwrap();
            let occluder_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len((num_occluders * 10).max(1))
                .build()
                .unwrap();
            let prepare_occluder_kernel = Kernel::builder()
                .program(&state.prepare_program)
                .name("prepare_occluder_data")
                .queue(state.queue.clone())
                .arg(&raw_occluder_vertices_buffer)
                .arg(&raw_alphas_buffer)
                .arg(&occluder_data_buffer)
                .arg(&arg_num_occluders)
                .global_work_size(num_occluders)
                .build()
                .unwrap();
            unsafe {
                prepare_occluder_kernel.enq().unwrap();
            }
            let light_positions_safe = if light_positions.is_empty() { vec![0.0f32] } else { light_positions };
            let light_directions_safe = if light_directions.is_empty() { vec![0.0f32] } else { light_directions };
            let light_cone_angles_safe = if light_cone_angles.is_empty() { vec![0.0f32] } else { light_cone_angles };
            let light_distances_safe = if light_distances.is_empty() { vec![0.0f32] } else { light_distances };
            let light_colors_safe = if light_colors.is_empty() { vec![0.0f32] } else { light_colors };
            let light_positions_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_positions_safe.len().max(1))
                .copy_host_slice(&light_positions_safe)
                .build()
                .unwrap();
            let light_directions_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_directions_safe.len().max(1))
                .copy_host_slice(&light_directions_safe)
                .build()
                .unwrap();
            let light_cone_angles_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_cone_angles_safe.len().max(1))
                .copy_host_slice(&light_cone_angles_safe)
                .build()
                .unwrap();
            let light_distances_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_distances_safe.len().max(1))
                .copy_host_slice(&light_distances_safe)
                .build()
                .unwrap();
            let light_colors_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_colors_safe.len().max(1))
                .copy_host_slice(&light_colors_safe)
                .build()
                .unwrap();
            let light_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len((num_lights * 11).max(1))
                .build()
                .unwrap();
            let prepare_light_kernel = Kernel::builder()
                .program(&state.prepare_program)
                .name("prepare_light_data")
                .queue(state.queue.clone())
                .arg(&light_positions_buffer)
                .arg(&light_directions_buffer)
                .arg(&light_cone_angles_buffer)
                .arg(&light_distances_buffer)
                .arg(&light_colors_buffer)
                .arg(&light_data_buffer)
                .arg(&arg_num_lights)
                .global_work_size(num_lights)
                .build()
                .unwrap();
            unsafe {
                prepare_light_kernel.enq().unwrap();
            }
            let output_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(output.len().max(1))
                .build()
                .unwrap();
            let bake_kernel = Kernel::builder()
                .program(&state.bake_program)
                .name("bake_lighting")
                .queue(state.queue.clone())
                .arg(&tri_data_buffer)
                .arg(&occluder_data_buffer)
                .arg(&light_data_buffer)
                .arg(&arg_num_triangles)
                .arg(&arg_num_occluders)
                .arg(&arg_num_lights)
                .arg(&output_buffer)
                .global_work_size(num_triangles)
                .build()
                .unwrap();
            unsafe {
                bake_kernel.enq().unwrap();
            }
            output_buffer.read(&mut output).enq().unwrap();
        } else {
            let occluders_bvh = build_bvh(&occluders, 0);
            let mut idx = 0;
            for tri in &static_triangles {
                let verts = &tri.draw_vertices;
                let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
                let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
                let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
                let edge1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
                let edge2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
                let normal = normalize(cross(&edge1, &edge2));
                let centroid = [
                    (v0[0] + v1[0] + v2[0]) / 3.0,
                    (v0[1] + v1[1] + v2[1]) / 3.0,
                    (v0[2] + v1[2] + v2[2]) / 3.0,
                ];
                let baked = compute_lighting(
                    centroid,
                    normal,
                    &occluders_bvh,
                    &occluders,
                    &static_lights,
                    [0, 0, 0],
                );
                output[idx * 3] = baked[0];
                output[idx * 3 + 1] = baked[1];
                output[idx * 3 + 2] = baked[2];
                idx += 1;
            }
        }
    }
    let mut baked_result: Vec<Render_triangle> = Vec::with_capacity(num_triangles);
    for i in 0..num_triangles {
        let baked_light = [output[i * 3], output[i * 3 + 1], output[i * 3 + 2]];
        baked_result.push(Render_triangle {
            triangle: static_triangles[i].clone(),
            baked_light: Some(baked_light),
        });
    }
    let baked_bvh = build_bvh_on_gpu(&baked_result);
    let mut store = Baked_static_triangles.lock().unwrap();
    *store = baked_result;
    let mut bvh_store = Baked_static_bvh.lock().unwrap();
    *bvh_store = Some(baked_bvh);
}

pub fn Render_image_to_console() -> Result<(), String> {
    let mut queue_2d: Vec<super::Draw_components> = Vec::new();
    let mut queue_3d: Vec<super::Draw_components> = Vec::new();
    let mut light_queue: Vec<super::Light_components> = Vec::new();
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
    let mut all_ligts = super::Light_queue.lock().unwrap();
    for light in all_ligts.iter() {
        light_queue.push(light.clone());
    }
    drop(all_ligts);
    super::Light_queue.lock().unwrap().clear();
    let need_bake = {
        let examination = super::Is_scene_changed.lock().unwrap();
        let not_baked = Baked_static_bvh.lock().unwrap().is_none();
        *examination || not_baked
    };
    if need_bake {
        Bake_scene();
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
    Render_3d_to_screen(&triangles, &mut screen, &light_queue);
    for object in queue_2d {
        if object.draw_vertices.len() < 2 {
            continue;
        }
        let mut biggest_x: f32 = 0.0;
        let mut biggest_y: f32 = 0.0;
        let mut smallest_x: f32 = width as f32;
        let mut smallest_y: f32 = height as f32;
        for i in (0..(object.draw_vertices.len() - 1)).step_by(2) {
            biggest_x = if object.draw_vertices[i] + object.draw_x > biggest_x {
                object.draw_vertices[i] + object.draw_x
            } else {
                biggest_x
            };
            biggest_y = if object.draw_vertices[i + 1] + object.draw_y > biggest_y {
                object.draw_vertices[i + 1] + object.draw_y
            } else {
                biggest_y
            };
            smallest_x = if object.draw_vertices[i] + object.draw_x < smallest_x {
                object.draw_vertices[i] + object.draw_x
            } else {
                smallest_x
            };
            smallest_y = if object.draw_vertices[i + 1] + object.draw_y < smallest_y {
                object.draw_vertices[i + 1] + object.draw_y
            } else {
                smallest_y
            };
        }
        let start_x = if smallest_x < 0.0 { 0 } else { smallest_x as i128 };
        let end_x = if biggest_x > width as f32 { width } else { biggest_x as i128 };
        let start_y = if smallest_y < 0.0 { 0 } else { smallest_y as i128 };
        let end_y = if biggest_y > height as f32 { height } else { biggest_y as i128 };
        let uv_range_x = if (biggest_x - smallest_x).abs() < 1e-8 {
            1.0
        } else {
            biggest_x - smallest_x
        };
        let uv_range_y = if (biggest_y - smallest_y).abs() < 1e-8 {
            1.0
        } else {
            biggest_y - smallest_y
        };
        let texture = if object.draw_texture_path != "none" {
            load_texture(&object.draw_texture_path)
        } else {
            None
        };
        let base_alpha = object.draw_RGBA_color[3] as f32 / 255.0;
        for x in start_x..end_x {
            for y in start_y..end_y {
                if point_in_polygon_offset(x as f32, y as f32, &object.draw_vertices, object.draw_x, object.draw_y) {
                    let (r, g, b, alpha) = if let Some(img) = &texture {
                        let u = (x as f32 - smallest_x) / uv_range_x;
                        let v = 1.0 - (y as f32 - smallest_y) / uv_range_y;
                        let tex_x = (u * (img.width() as f32 - 1.0))
                            .round()
                            .clamp(0.0, img.width() as f32 - 1.0) as u32;
                        let tex_y = (v * (img.height() as f32 - 1.0))
                            .round()
                            .clamp(0.0, img.height() as f32 - 1.0) as u32;
                        let pixel = img.get_pixel(tex_x, tex_y);
                        let c = &object.draw_RGBA_color;
                        let r = ((pixel[0] as f32 / 255.0) * (c[0] as f32 / 255.0) * 255.0).round() as u8;
                        let g = ((pixel[1] as f32 / 255.0) * (c[1] as f32 / 255.0) * 255.0).round() as u8;
                        let b = ((pixel[2] as f32 / 255.0) * (c[2] as f32 / 255.0) * 255.0).round() as u8;
                        (r, g, b, (pixel[3] as f32 / 255.0) * base_alpha)
                    } else {
                        let c = &object.draw_RGBA_color;
                        (c[0], c[1], c[2], base_alpha)
                    };
                    if alpha >= 1.0 {
                        screen[y as usize][x as usize] = super::Pixel_structure {
                            pixel_symbol: object.draw_symbol,
                            pixel_RGBA_color: [r, g, b, 255],
                        };
                    } else if alpha > 0.0 {
                        let bg = &screen[y as usize][x as usize].pixel_RGBA_color;
                        let fg = [r, g, b];
                        let mut blended = [0u8; 4];
                        for i in 0..3 {
                            blended[i] = ((bg[i] as f32 * (1.0 - alpha)) + (fg[i] as f32 * alpha)) as u8;
                        }
                        blended[3] = 255;
                        screen[y as usize][x as usize] = super::Pixel_structure {
                            pixel_symbol: object.draw_symbol,
                            pixel_RGBA_color: blended,
                        };
                    }
                }
            }
        }
    }
    Ok(())
}