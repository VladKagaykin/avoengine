use std::f32::consts::PI;
use std::thread;
use image::RgbaImage;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock, Mutex};
use ocl::{Platform, Device, Context, Program, Kernel, Queue, Buffer, builders::DeviceSpecifier, flags};
use ocl::prm::Float3;

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
    pub baked_light: Option<[f32;3]>,
}

static Baked_static_triangles: Mutex<Vec<Render_triangle>> = Mutex::new(Vec::new());
static Baked_static_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);

struct OpenCLState {
    context: Context,
    prepare_program: Program,
    bake_program: Program,
    bvh_program: Program,
    queue: Queue,
}

static OPENCL_STATE: OnceLock<Mutex<Option<OpenCLState>>> = OnceLock::new();

fn get_opencl_state() -> Result<&'static Mutex<Option<OpenCLState>>, String> {
    OPENCL_STATE.get_or_init(|| {
        Mutex::new(init_opencl().ok())
    });
    OPENCL_STATE.get().ok_or_else(|| "OpenCL not initialized".to_string())
}

fn init_opencl() -> Result<OpenCLState, String> {
    let platforms = Platform::list();
    let platform = platforms.into_iter().next().ok_or("No OpenCL platform")?;
    let devices = Device::list(platform, Some(flags::DeviceType::GPU))
        .map_err(|e| e.to_string())?;
    let device = devices.into_iter().next().ok_or("No GPU device found")?;

    let context = Context::builder()
        .platform(platform)
        .devices(device)
        .build()
        .map_err(|e| e.to_string())?;
    
    let prepare_program = Program::builder()
        .devices(device)
        .src(PREPARE_KERNEL_SRC)
        .build(&context)
        .map_err(|e| e.to_string())?;
    
    let bake_program = Program::builder()
        .devices(device)
        .src(BAKE_KERNEL_SRC)
        .build(&context)
        .map_err(|e| e.to_string())?;
    
    let bvh_program = Program::builder()
        .devices(device)
        .src(BVH_KERNEL_SRC)
        .build(&context)
        .map_err(|e| e.to_string())?;
    
    let queue = Queue::new(&context, device, None)
        .map_err(|e| e.to_string())?;

    Ok(OpenCLState {
        context,
        prepare_program,
        bake_program,
        bvh_program,
        queue,
    })
}

const BVH_KERNEL_SRC: &str = r#"
__kernel void compute_aabb(
    __global const float* vertices,
    __global float* aabb_min,
    __global float* aabb_max,
    __global float* centroids,
    const int num_triangles
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles) return;
    
    int base = gid * 9;
    float3 v0 = (float3)(vertices[base+0], vertices[base+1], vertices[base+2]);
    float3 v1 = (float3)(vertices[base+3], vertices[base+4], vertices[base+5]);
    float3 v2 = (float3)(vertices[base+6], vertices[base+7], vertices[base+8]);
    
    float3 min_val = fmin(fmin(v0, v1), v2);
    float3 max_val = fmax(fmax(v0, v1), v2);
    float3 centroid = (v0 + v1 + v2) / 3.0f;
    
    aabb_min[gid*3+0] = min_val.x;
    aabb_min[gid*3+1] = min_val.y;
    aabb_min[gid*3+2] = min_val.z;
    
    aabb_max[gid*3+0] = max_val.x;
    aabb_max[gid*3+1] = max_val.y;
    aabb_max[gid*3+2] = max_val.z;
    
    centroids[gid*3+0] = centroid.x;
    centroids[gid*3+1] = centroid.y;
    centroids[gid*3+2] = centroid.z;
}

uint expandBits(uint v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

uint morton3D(float x, float y, float z, float3 scene_min, float3 scene_size) {
    x = (x - scene_min.x) / scene_size.x;
    y = (y - scene_min.y) / scene_size.y;
    z = (z - scene_min.z) / scene_size.z;
    
    x = clamp(x * 1024.0f, 0.0f, 1023.0f);
    y = clamp(y * 1024.0f, 0.0f, 1023.0f);
    z = clamp(z * 1024.0f, 0.0f, 1023.0f);
    
    uint xx = expandBits((uint)x);
    uint yy = expandBits((uint)y);
    uint zz = expandBits((uint)z);
    
    return xx * 4 + yy * 2 + zz;
}

__kernel void compute_morton_codes(
    __global const float* centroids,
    __global uint* morton_codes,
    __global int* indices,
    const float3 scene_min,
    const float3 scene_size,
    const int num_triangles
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles) return;
    
    float x = centroids[gid*3+0];
    float y = centroids[gid*3+1];
    float z = centroids[gid*3+2];
    
    uint code = morton3D(x, y, z, scene_min, scene_size);
    morton_codes[gid] = code;
    indices[gid] = gid;
}

__kernel void radix_sort_step(
    __global const uint* input_codes,
    __global const int* input_indices,
    __global uint* output_codes,
    __global int* output_indices,
    __global int* counters,
    const int bit_offset,
    const int num_items
) {
    int gid = get_global_id(0);
    if (gid >= num_items) return;
    
    uint code = input_codes[gid];
    int idx = input_indices[gid];
    uint bit = (code >> bit_offset) & 1;
    
    int pos = atomic_add(&counters[bit], 1);
    if (bit == 0) {
        output_codes[pos] = code;
        output_indices[pos] = idx;
    } else {
        int zero_count = counters[0];
        output_codes[zero_count + pos] = code;
        output_indices[zero_count + pos] = idx;
    }
}

__kernel void build_bvh_nodes(
    __global const uint* morton_codes,
    __global const int* sorted_indices,
    __global const float* aabb_min,
    __global const float* aabb_max,
    __global float* node_aabb_min,
    __global float* node_aabb_max,
    __global int* node_left,
    __global int* node_right,
    const int num_triangles
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles - 1) return;
    
    int first = 0;
    int last = num_triangles - 1;
    
    uint first_code = morton_codes[sorted_indices[first]];
    uint last_code = morton_codes[sorted_indices[last]];
    
    if (first_code == last_code) {
        int split = (first + last) / 2;
        node_left[gid] = split;
        node_right[gid] = split + 1;
        return;
    }
    
    int common_prefix = clz(first_code ^ last_code);
    
    int split = first;
    int step = last - first;
    
    do {
        step = (step + 1) >> 1;
        int new_split = split + step;
        
        if (new_split < last) {
            uint split_code = morton_codes[sorted_indices[new_split]];
            int split_prefix = clz(first_code ^ split_code);
            if (split_prefix > common_prefix) {
                split = new_split;
            }
        }
    } while (step > 1);
    
    node_left[gid] = split;
    node_right[gid] = split + 1;
}

__kernel void calculate_bounding_boxes(
    __global const float* aabb_min,
    __global const float* aabb_max,
    __global const int* node_left,
    __global const int* node_right,
    __global float* node_aabb_min,
    __global float* node_aabb_max,
    __global int* processed,
    const int num_triangles
) {
    int gid = get_global_id(0);
    if (gid >= num_triangles) return;
    
    int node_idx = gid;
    
    while (node_idx >= 0) {
        int was_processed = atomic_xchg(&processed[node_idx], 1);
        if (was_processed) {
            break;
        }
        
        int left_idx = node_left[node_idx];
        int right_idx = node_right[node_idx];
        
        float3 min_val = (float3)(1e30f, 1e30f, 1e30f);
        float3 max_val = (float3)(-1e30f, -1e30f, -1e30f);
        
        if (left_idx < num_triangles) {
            min_val = fmin(min_val, (float3)(aabb_min[left_idx*3], aabb_min[left_idx*3+1], aabb_min[left_idx*3+2]));
            max_val = fmax(max_val, (float3)(aabb_max[left_idx*3], aabb_max[left_idx*3+1], aabb_max[left_idx*3+2]));
        } else {
            int left_node = left_idx - num_triangles;
            min_val = fmin(min_val, (float3)(node_aabb_min[left_node*3], node_aabb_min[left_node*3+1], node_aabb_min[left_node*3+2]));
            max_val = fmax(max_val, (float3)(node_aabb_max[left_node*3], node_aabb_max[left_node*3+1], node_aabb_max[left_node*3+2]));
        }
        
        if (right_idx < num_triangles) {
            min_val = fmin(min_val, (float3)(aabb_min[right_idx*3], aabb_min[right_idx*3+1], aabb_min[right_idx*3+2]));
            max_val = fmax(max_val, (float3)(aabb_max[right_idx*3], aabb_max[right_idx*3+1], aabb_max[right_idx*3+2]));
        } else {
            int right_node = right_idx - num_triangles;
            min_val = fmin(min_val, (float3)(node_aabb_min[right_node*3], node_aabb_min[right_node*3+1], node_aabb_min[right_node*3+2]));
            max_val = fmax(max_val, (float3)(node_aabb_max[right_node*3], node_aabb_max[right_node*3+1], node_aabb_max[right_node*3+2]));
        }
        
        node_aabb_min[node_idx*3] = min_val.x;
        node_aabb_min[node_idx*3+1] = min_val.y;
        node_aabb_min[node_idx*3+2] = min_val.z;
        node_aabb_max[node_idx*3] = max_val.x;
        node_aabb_max[node_idx*3+1] = max_val.y;
        node_aabb_max[node_idx*3+2] = max_val.z;
        
        if (node_idx == 0) break;
        node_idx = (node_idx - 1) / 2;
    }
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
    float3 v0 = (float3)(vertices[base+0], vertices[base+1], vertices[base+2]);
    float3 v1 = (float3)(vertices[base+3], vertices[base+4], vertices[base+5]);
    float3 v2 = (float3)(vertices[base+6], vertices[base+7], vertices[base+8]);
    
    float3 centroid = (v0 + v1 + v2) / 3.0f;
    
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 normal = cross(edge1, edge2);
    
    float len = length(normal);
    if (len > 1e-8f) {
        normal = normal / len;
    }
    
    int out_base = gid * 6;
    output[out_base+0] = centroid.x;
    output[out_base+1] = centroid.y;
    output[out_base+2] = centroid.z;
    output[out_base+3] = normal.x;
    output[out_base+4] = normal.y;
    output[out_base+5] = normal.z;
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
    
    output[out_base+0] = vertices[v_base+0];
    output[out_base+1] = vertices[v_base+1];
    output[out_base+2] = vertices[v_base+2];
    output[out_base+3] = vertices[v_base+3];
    output[out_base+4] = vertices[v_base+4];
    output[out_base+5] = vertices[v_base+5];
    output[out_base+6] = vertices[v_base+6];
    output[out_base+7] = vertices[v_base+7];
    output[out_base+8] = vertices[v_base+8];
    output[out_base+9] = alphas[gid];
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
    
    output[out_base+0] = positions[gid*3+0];
    output[out_base+1] = positions[gid*3+1];
    output[out_base+2] = positions[gid*3+2];
    
    output[out_base+3] = directions[gid*3+0];
    output[out_base+4] = directions[gid*3+1];
    output[out_base+5] = directions[gid*3+2];
    
    float half_cone = (cone_angles[gid] * 0.5f) * 3.14159265f / 180.0f;
    output[out_base+6] = cos(half_cone);
    
    float falloff = distances[gid] / sqrt(1.0f / 0.01f - 1.0f);
    output[out_base+7] = falloff;
    
    output[out_base+8] = colors[gid*3+0] / 255.0f;
    output[out_base+9] = colors[gid*3+1] / 255.0f;
    output[out_base+10] = colors[gid*3+2] / 255.0f;
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
    
    float3 centroid = (float3)(tri_data[gid*6+0], tri_data[gid*6+1], tri_data[gid*6+2]);
    float3 normal = (float3)(tri_data[gid*6+3], tri_data[gid*6+4], tri_data[gid*6+5]);
    
    float len = sqrt(dot(normal, normal));
    if (len > 1e-8f) normal = normal / len;
    
    float3 result = (float3)(0.0f, 0.0f, 0.0f);
    
    for (int li = 0; li < num_lights; li++) {
        int base_l = li * 11;
        float3 light_pos = (float3)(light_data[base_l+0], light_data[base_l+1], light_data[base_l+2]);
        float3 light_dir = (float3)(light_data[base_l+3], light_data[base_l+4], light_data[base_l+5]);
        float cos_half = light_data[base_l+6];
        float falloff = light_data[base_l+7];
        float3 light_color = (float3)(light_data[base_l+8], light_data[base_l+9], light_data[base_l+10]);
        
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
        float3 offset = (float3)(centroid.x + normal.x * 0.001f, centroid.y + normal.y * 0.001f, centroid.z + normal.z * 0.1f);
        
        for (int oi = 0; oi < num_occluders; oi++) {
            int base_o = oi * 10;
            float3 v0 = (float3)(occluder_data[base_o+0], occluder_data[base_o+1], occluder_data[base_o+2]);
            float3 v1 = (float3)(occluder_data[base_o+3], occluder_data[base_o+4], occluder_data[base_o+5]);
            float3 v2 = (float3)(occluder_data[base_o+6], occluder_data[base_o+7], occluder_data[base_o+8]);
            float alpha = occluder_data[base_o+9];
            
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
    output[gid*3+0] = result.x;
    output[gid*3+1] = result.y;
    output[gid*3+2] = result.z;
}
"#;

pub fn To_console(){
    let width = super::Engine_settings.lock().unwrap().window_width as usize;
    let height = super::Engine_settings.lock().unwrap().window_height as usize;
    let screen = super::Screen.lock().unwrap();

    for y in (0..height).rev() {
        for x in 0..width {
            print!("\x1b[38;2;{};{};{}m{}\x1b[0m", screen[y][x].pixel_RGBA_color[0], screen[y][x].pixel_RGBA_color[1],
                                                   screen[y][x].pixel_RGBA_color[2],screen[y][x].pixel_symbol);
        }
        println!();
    }
    drop(width);
    drop(height);
    drop(screen);
}

fn point_in_polygon_offset(px: f32, py: f32, vertices: &[f32], offset_x: f32, offset_y: f32) -> bool {
    let mut inside = false;
    let n = vertices.len() / 2;
    
    for i in 0..n {
        let j = (i + 1) % n;
        let xi = vertices[2*i] + offset_x;
        let yi = vertices[2*i + 1] + offset_y;
        let xj = vertices[2*j] + offset_x;
        let yj = vertices[2*j + 1] + offset_y;
        
        let intersect = ((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi);
        if intersect {
            inside = !inside;
        }
    }
    inside
}

fn cross(a: &[f32;3], b: &[f32;3]) -> [f32;3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot(a: &[f32;3], b: &[f32;3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn normalize(v: [f32;3]) -> [f32;3] {
    let len = (v[0]*v[0] + v[1]*v[1] + v[2]*v[2]).sqrt();
    if len < 1e-8 {
        return [0.0, 0.0, 1.0];
    }
    [v[0]/len, v[1]/len, v[2]/len]
}

pub fn forward_from_angles(pitch_deg: f32, yaw_deg: f32) -> [f32;3] {
    let p = pitch_deg * PI / 180.0;
    let y = yaw_deg * PI / 180.0;
    let (cp, sp) = (p.cos(), p.sin());
    let (sy, cy) = (y.sin(), y.cos());
    normalize([sy * cp, -sp, cy * cp])
}

#[derive(Clone)]
pub struct CameraBasis {
    pub forward: [f32;3],
    pub right: [f32;3],
    pub up: [f32;3],
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
    let right   = [r00, r10, r20];
    let up      = [r01, r11, r21];

    CameraBasis { forward, right, up }
}

fn ray_triangle_intersect(origin: &[f32;3], dir: &[f32;3], v0: &[f32;3], v1: &[f32;3], v2: &[f32;3]) -> Option<(f32, f32, f32)> {
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

fn generate_uv_for_triangle(v0: &[f32;3], v1: &[f32;3], v2: &[f32;3]) -> ((f32, f32), (f32, f32), (f32, f32)) {
    let mut min = [v0[0], v0[1], v0[2]];
    let mut max = [v0[0], v0[1], v0[2]];
    for v in [v0, v1, v2] {
        for i in 0..3 {
            if v[i] < min[i] { min[i] = v[i]; }
            if v[i] > max[i] { max[i] = v[i]; }
        }
    }
    let mut ranges = [max[0]-min[0], max[1]-min[1], max[2]-min[2]];
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
    use std::collections::HashMap;
    use std::sync::OnceLock;
    static CACHE: OnceLock<std::sync::Mutex<HashMap<String, RgbaImage>>> = OnceLock::new();
    let cache = CACHE.get_or_init(|| std::sync::Mutex::new(HashMap::new()));
    let mut lock = cache.lock().unwrap();
    if let Some(img) = lock.get(path) {
        return Some(img.clone());
    }
    if let Ok(img) = image::open(path).and_then(|dyn_img| Ok(dyn_img.to_rgba8())) {
        lock.insert(path.to_string(), img.clone());
        Some(img)
    } else {
        None
    }
}

fn is_occluded(point: [f32;3], normal: [f32;3], light_pos: [f32;3], dist_to_light: f32, triangles: &[super::Draw_components]) -> bool {
    let offset = [point[0] + normal[0]*1e-3, point[1] + normal[1]*1e-3, point[2] + normal[2]*0.1];
    let to_light = [light_pos[0]-point[0], light_pos[1]-point[1], light_pos[2]-point[2]];
    let dir = normalize(to_light);
    for tri in triangles {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0]+tri.draw_x, verts[1]+tri.draw_y, verts[2]+tri.draw_z];
        let v1 = [verts[3]+tri.draw_x, verts[4]+tri.draw_y, verts[5]+tri.draw_z];
        let v2 = [verts[6]+tri.draw_x, verts[7]+tri.draw_y, verts[8]+tri.draw_z];
        if let Some((t, _, _)) = ray_triangle_intersect(&offset, &dir, &v0, &v1, &v2) {
            if t > 1e-3 && t < dist_to_light {
                return true;
            }
        }
    }
    false
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

fn compute_triangle_aabb(tri: &super::Draw_components) -> ([f32; 3], [f32; 3]) {
    let verts = &tri.draw_vertices;
    let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
    let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
    let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for v in [v0, v1, v2] {
        for i in 0..3 {
            if v[i] < min[i] { min[i] = v[i]; }
            if v[i] > max[i] { max[i] = v[i]; }
        }
    }
    (min, max)
}

fn build_bvh_recursive(indices: &mut [usize], aabbs: &[([f32; 3], [f32; 3])], triangles: &[Render_triangle], depth: u32, offset: usize) -> BvhNode {
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for &i in indices.iter() {
        let (aabb_min, aabb_max) = aabbs[i];
        for k in 0..3 {
            if aabb_min[k] < min[k] { min[k] = aabb_min[k]; }
            if aabb_max[k] > max[k] { max[k] = aabb_max[k]; }
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
    let axis = if extent[0] >= extent[1] && extent[0] >= extent[2] { 0 } else if extent[1] >= extent[2] { 1 } else { 2 };
    indices.sort_by(|&a, &b| {
        let ca = (aabbs[a].0[axis] + aabbs[a].1[axis]) * 0.5;
        let cb = (aabbs[b].0[axis] + aabbs[b].1[axis]) * 0.5;
        ca.partial_cmp(&cb).unwrap()
    });
    let mid = indices.len() / 2;
    let (left_indices, right_indices) = indices.split_at_mut(mid);
    let left = build_bvh_recursive(left_indices, aabbs, triangles, depth + 1, offset);
    let right = build_bvh_recursive(right_indices, aabbs, triangles, depth + 1, offset);
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
    let aabbs: Vec<([f32; 3], [f32; 3])> = triangles.iter().map(|rt| compute_triangle_aabb(&rt.triangle)).collect();
    build_bvh_recursive(&mut indices, &aabbs, triangles, 0, offset)
}

fn ray_intersect_aabb(origin: &[f32; 3], dir: &[f32; 3], aabb_min: &[f32; 3], aabb_max: &[f32; 3], max_t: f32) -> bool {
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

fn collect_candidates(origin: &[f32; 3], dir: &[f32; 3], max_t: f32, node: &BvhNode, triangles: &[Render_triangle], out: &mut Vec<usize>) {
    if !ray_intersect_aabb(origin, dir, &node.aabb_min, &node.aabb_max, max_t) {
        return;
    }
    if node.triangles.is_empty() {
        if let Some(left) = &node.left {
            collect_candidates(origin, dir, max_t, left, triangles, out);
        }
        if let Some(right) = &node.right {
            collect_candidates(origin, dir, max_t, right, triangles, out);
        }
    } else {
        for &idx in &node.triangles {
            out.push(node.triangle_offset + idx);
        }
    }
}

fn light_transmittance(point: [f32; 3], normal: [f32; 3], light_pos: [f32; 3], dist_to_light: f32, bvh: &BvhNode, triangles: &[Render_triangle]) -> f32 {
    let offset = [point[0] + normal[0] * 1e-3, point[1] + normal[1] * 1e-3, point[2] + normal[2] * 0.1];
    let to_light = [light_pos[0] - point[0], light_pos[1] - point[1], light_pos[2] - point[2]];
    let dir = normalize(to_light);
    let mut candidates = Vec::new();
    collect_candidates(&offset, &dir, dist_to_light, bvh, triangles, &mut candidates);
    let mut transmittance = 1.0;
    for &idx in &candidates {
        let rt = &triangles[idx];
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
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

fn compute_lighting(point: [f32; 3], normal: [f32; 3], bvh: &BvhNode, triangles: &[Render_triangle], lights: &[super::Light_components], ambient: [u8; 3]) -> [f32; 3] {
    let mut r = ambient[0] as f32 / 255.0;
    let mut g = ambient[1] as f32 / 255.0;
    let mut b = ambient[2] as f32 / 255.0;

    const EPSILON: f32 = 0.01;

    for light in lights {
        let light_pos = [light.light_x, light.light_y, light.light_z];
        let to_light = [light_pos[0] - point[0], light_pos[1] - point[1], light_pos[2] - point[2]];
        let dist_sq = to_light[0] * to_light[0] + to_light[1] * to_light[1] + to_light[2] * to_light[2];
        let dist = dist_sq.sqrt();
        if dist < 1e-6 { continue; }
        let to_light_dir = normalize(to_light);

        let light_forward = camera_basis(light.light_pitch, light.light_yaw, 0.0).forward;
        let point_dir_from_light = [-to_light_dir[0], -to_light_dir[1], -to_light_dir[2]];
        let cos_angle = dot(&light_forward, &point_dir_from_light);
        let half_cone = (light.light_cone_angle * 0.5) * PI / 180.0;
        let cos_half_cone = half_cone.cos();
        if cos_angle < cos_half_cone { continue; }

        let transmittance = light_transmittance(point, normal, light_pos, dist, bvh, triangles);
        if transmittance <= 0.0 { continue; }

        let falloff = light.light_distance / (1.0f32 / EPSILON - 1.0f32).sqrt();
        let attenuation = 1.0 / (1.0 + (dist / falloff).powi(2));
        if attenuation < EPSILON { continue; }

        let diff = dot(&normal, &to_light_dir).max(0.0);
        let factor = attenuation * diff * transmittance;

        r += (light.light_RGB_color[0] as f32 / 255.0) * factor;
        g += (light.light_RGB_color[1] as f32 / 255.0) * factor;
        b += (light.light_RGB_color[2] as f32 / 255.0) * factor;
    }

    [r.min(1.0), g.min(1.0), b.min(1.0)]
}

pub fn ray_tracing(start_x: f32, start_y: f32, start_z: f32, length: i128, dir: [f32; 3],
                   bvh: &BvhNode, triangles: &[Render_triangle], textures: &HashMap<String, RgbaImage>,
                   dynamic_lights: &[super::Light_components], all_lights: &[super::Light_components],
                   ambient_light: [u8; 3]) -> super::Pixel_structure {
    let origin = [start_x, start_y, start_z];
    let max_t = length as f32;
    let mut candidates = Vec::new();
    collect_candidates(&origin, &dir, max_t, bvh, triangles, &mut candidates);

    let mut hits = Vec::new();

    for &idx in &candidates {
        let rt = &triangles[idx];
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
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
                let hit_point = [origin[0] + dir[0] * t, origin[1] + dir[1] * t, origin[2] + dir[2] * t];
                let light_factor = if let Some(baked) = rt.baked_light {
                    let dynamic_contrib = compute_lighting(hit_point, normal, bvh, triangles, dynamic_lights, [0, 0, 0]);
                    let amb = [ambient_light[0] as f32 / 255.0, ambient_light[1] as f32 / 255.0, ambient_light[2] as f32 / 255.0];
                    [
                        (amb[0] + baked[0] + dynamic_contrib[0]).min(1.0),
                        (amb[1] + baked[1] + dynamic_contrib[1]).min(1.0),
                        (amb[2] + baked[2] + dynamic_contrib[2]).min(1.0),
                    ]
                } else {
                    compute_lighting(hit_point, normal, bvh, triangles, all_lights, ambient_light)
                };
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
        if a <= 0.0 { continue; }
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

fn point_in_view_frustum(point: [f32; 3], origin: [f32; 3], basis: &CameraBasis, tan_h: f32, tan_v: f32) -> bool {
    let rel = [point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]];
    let z = dot(&rel, &basis.forward);
    if z <= 0.0 { return false; }
    let x = dot(&rel, &basis.right);
    let y = dot(&rel, &basis.up);
    x.abs() <= z * tan_h && y.abs() <= z * tan_v
}

fn light_cone_intersects_frustum(light: &super::Light_components, origin: [f32; 3], basis: &CameraBasis, tan_h: f32, tan_v: f32) -> bool {
    let apex = [light.light_x, light.light_y, light.light_z];
    let dir = camera_basis(light.light_pitch, light.light_yaw, 0.0).forward;
    let half_cone = (light.light_cone_angle * 0.5) * PI / 180.0;
    let cos_h = half_cone.cos();
    let sin_h = half_cone.sin();
    let rel = [apex[0] - origin[0], apex[1] - origin[1], apex[2] - origin[2]];
    let planes: [[f32; 3]; 5] = [
        [basis.right[0] - basis.forward[0] * tan_h, basis.right[1] - basis.forward[1] * tan_h, basis.right[2] - basis.forward[2] * tan_h],
        [-basis.right[0] - basis.forward[0] * tan_h, -basis.right[1] - basis.forward[1] * tan_h, -basis.right[2] - basis.forward[2] * tan_h],
        [basis.up[0] - basis.forward[0] * tan_v, basis.up[1] - basis.forward[1] * tan_v, basis.up[2] - basis.forward[2] * tan_v],
        [-basis.up[0] - basis.forward[0] * tan_v, -basis.up[1] - basis.forward[1] * tan_v, -basis.up[2] - basis.forward[2] * tan_v],
        [-basis.forward[0], -basis.forward[1], -basis.forward[2]],
    ];
    for n in planes {
        let rel_dot = dot(&rel, &n);
        if rel_dot <= 0.0 { continue; }
        let a = dot(&dir, &n);
        let n_len_sq = dot(&n, &n);
        let b = (n_len_sq - a * a).max(0.0).sqrt();
        let min_dot = a.min(a * cos_h + b * sin_h);
        if min_dot >= 0.0 {
            return false;
        }
    }
    true
}

pub fn Render_3d_to_screen(dynamic_triangles: &[super::Draw_components], screen: &mut Vec<Vec<super::Pixel_structure>>, dynamic_lights: &[super::Light_components]) {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width;
    let height = settings.window_height;

    let cam = super::Camera.lock().unwrap();
    let (ox, oy, oz) = (cam.camera_x, cam.camera_y, cam.camera_z);
    let pitch = cam.camera_pitch;
    let yaw = cam.camera_yaw;
    let roll = cam.camera_roll;
    let fov = cam.camera_fov as f32;
    let max_dist = cam.max_dist as i128;
    let ambient_light = [cam.ambient_light[0], cam.ambient_light[1], cam.ambient_light[2]];
    drop(cam);

    let aspect = width as f32 / height as f32;
    let fov_rad = fov * PI / 180.0;
    let half_tan = (fov_rad / 2.0).tan();
    let tan_h = half_tan * aspect;
    let tan_v = half_tan;

    let basis = camera_basis(pitch, yaw, roll);
    let origin = [ox, oy, oz];

    let baked_static = Baked_static_triangles.lock().unwrap().clone();
    let static_bvh_opt = Baked_static_bvh.lock().unwrap().clone();
    let static_bvh = match static_bvh_opt {
        Some(node) => node,
        None => build_bvh(&baked_static, 0),
    };

    let dynamic_render: Vec<Render_triangle> = dynamic_triangles.iter()
        .map(|t| Render_triangle { triangle: t.clone(), baked_light: None })
        .collect();

    let mut all_triangles = baked_static.clone();
    all_triangles.extend(dynamic_render.clone());

    let combined_bvh = if dynamic_render.is_empty() {
        static_bvh
    } else if baked_static.is_empty() {
        build_bvh(&dynamic_render, 0)
    } else {
        let dynamic_bvh = build_bvh(&dynamic_render, baked_static.len());
        let top_min = [
            static_bvh.aabb_min[0].min(dynamic_bvh.aabb_min[0]),
            static_bvh.aabb_min[1].min(dynamic_bvh.aabb_min[1]),
            static_bvh.aabb_min[2].min(dynamic_bvh.aabb_min[2]),
        ];
        let top_max = [
            static_bvh.aabb_max[0].max(dynamic_bvh.aabb_max[0]),
            static_bvh.aabb_max[1].max(dynamic_bvh.aabb_max[1]),
            static_bvh.aabb_max[2].max(dynamic_bvh.aabb_max[2]),
        ];
        BvhNode {
            aabb_min: top_min,
            aabb_max: top_max,
            left: Some(Box::new(static_bvh)),
            right: Some(Box::new(dynamic_bvh)),
            triangles: Vec::new(),
            triangle_offset: 0,
        }
    };

    let static_lights = super::Static_light.lock().unwrap().clone();
    let mut all_lights: Vec<super::Light_components> = static_lights;
    all_lights.extend(dynamic_lights.iter().cloned());
    let all_lights: Vec<super::Light_components> = all_lights.into_iter()
        .filter(|l| light_cone_intersects_frustum(l, origin, &basis, tan_h, tan_v))
        .collect();
    let dynamic_lights_culled: Vec<super::Light_components> = dynamic_lights.iter()
        .filter(|l| light_cone_intersects_frustum(l, origin, &basis, tan_h, tan_v))
        .cloned()
        .collect();

    let bvh_arc = Arc::new(combined_bvh);
    let triangles_arc = Arc::new(all_triangles);

    let mut texture_map = HashMap::new();
    for rt in triangles_arc.iter() {
        let tri = &rt.triangle;
        if tri.draw_texture_path != "none" && !texture_map.contains_key(&tri.draw_texture_path) {
            if let Some(img) = load_texture(&tri.draw_texture_path) {
                texture_map.insert(tri.draw_texture_path.clone(), img);
            }
        }
    }
    let texture_map = Arc::new(texture_map);

    let num_threads = thread::available_parallelism().expect("Can't get num of threads").get() * settings.cores_multiply.clone() as usize - 1;
    let mut handles = vec![];
    drop(settings);

    let height_usize = height as usize;
    let width_usize = width as usize;
    let rows_per_thread = height_usize / num_threads;

    for t in 0..num_threads {
        let start_y = t * rows_per_thread;
        let end_y = if t == num_threads - 1 { height_usize } else { (t + 1) * rows_per_thread };

        let bvh_clone = bvh_arc.clone();
        let triangles_clone = triangles_arc.clone();
        let dynamic_lights_local = dynamic_lights_culled.to_vec();
        let all_lights_local = all_lights.to_vec();
        let ox = ox; let oy = oy; let oz = oz;
        let max_dist = max_dist;
        let width = width_usize;
        let height = height_usize;
        let aspect = aspect;
        let half_tan = half_tan;
        let basis = basis.clone();
        let texture_map = texture_map.clone();
        let ambient_light = ambient_light;

        handles.push(thread::spawn(move || {
            let mut result = Vec::new();
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
                    let pixel = ray_tracing(ox, oy, oz, max_dist, dir, &bvh_clone, &triangles_clone, &texture_map, &dynamic_lights_local, &all_lights_local, ambient_light);
                    result.push((y, x, pixel));
                }
            }
            result
        }));
    }

    for handle in handles {
        for (y, x, pixel) in handle.join().unwrap() {
            screen[y][x] = pixel;
        }
    }
}

fn build_bvh_on_gpu(triangles: &[Render_triangle]) -> BvhNode {
    let num_triangles = triangles.len();
    if num_triangles == 0 {
        return BvhNode {
            aabb_min: [0.0; 3],
            aabb_max: [0.0; 3],
            left: None,
            right: None,
            triangles: Vec::new(),
            triangle_offset: 0,
        };
    }

    let mut raw_vertices: Vec<f32> = Vec::with_capacity(num_triangles * 9);
    for tri in triangles {
        let verts = &tri.triangle.draw_vertices;
        raw_vertices.push(verts[0] + tri.triangle.draw_x);
        raw_vertices.push(verts[1] + tri.triangle.draw_y);
        raw_vertices.push(verts[2] + tri.triangle.draw_z);
        raw_vertices.push(verts[3] + tri.triangle.draw_x);
        raw_vertices.push(verts[4] + tri.triangle.draw_y);
        raw_vertices.push(verts[5] + tri.triangle.draw_z);
        raw_vertices.push(verts[6] + tri.triangle.draw_x);
        raw_vertices.push(verts[7] + tri.triangle.draw_y);
        raw_vertices.push(verts[8] + tri.triangle.draw_z);
    }

    let state_mutex = get_opencl_state().expect("Failed to get OpenCL state");
    let state_guard = state_mutex.lock().unwrap();
    
    if let Some(state) = state_guard.as_ref() {
        let vertices_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_ONLY)
            .len(raw_vertices.len())
            .copy_host_slice(&raw_vertices)
            .build().unwrap();

        let aabb_min_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles * 3)
            .build().unwrap();

        let aabb_max_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles * 3)
            .build().unwrap();

        let centroids_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles * 3)
            .build().unwrap();

        let compute_aabb_kernel = Kernel::builder()
            .program(&state.bvh_program)
            .name("compute_aabb")
            .queue(state.queue.clone())
            .arg(&vertices_buffer)
            .arg(&aabb_min_buffer)
            .arg(&aabb_max_buffer)
            .arg(&centroids_buffer)
            .arg(&(num_triangles as i32))
            .global_work_size(num_triangles)
            .build().unwrap();

        unsafe { compute_aabb_kernel.enq().unwrap(); }

        let mut aabb_min_cpu = vec![0.0f32; num_triangles * 3];
        let mut aabb_max_cpu = vec![0.0f32; num_triangles * 3];
        let mut centroids_cpu = vec![0.0f32; num_triangles * 3];

        aabb_min_buffer.read(&mut aabb_min_cpu).enq().unwrap();
        aabb_max_buffer.read(&mut aabb_max_cpu).enq().unwrap();
        centroids_buffer.read(&mut centroids_cpu).enq().unwrap();

        let mut scene_min = [f32::INFINITY; 3];
        let mut scene_max = [f32::NEG_INFINITY; 3];
        for i in 0..num_triangles {
            for j in 0..3 {
                scene_min[j] = scene_min[j].min(aabb_min_cpu[i*3+j]);
                scene_max[j] = scene_max[j].max(aabb_max_cpu[i*3+j]);
            }
        }
        let scene_size = [
            (scene_max[0] - scene_min[0]).max(1e-6),
            (scene_max[1] - scene_min[1]).max(1e-6),
            (scene_max[2] - scene_min[2]).max(1e-6),
        ];

        let morton_codes_buffer: Buffer<u32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles)
            .build().unwrap();

        let indices_buffer: Buffer<i32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles)
            .build().unwrap();

        let scene_min_f3 = Float3::new(scene_min[0], scene_min[1], scene_min[2]);
        let scene_size_f3 = Float3::new(scene_size[0], scene_size[1], scene_size[2]);

        let compute_morton_kernel = Kernel::builder()
            .program(&state.bvh_program)
            .name("compute_morton_codes")
            .queue(state.queue.clone())
            .arg(&centroids_buffer)
            .arg(&morton_codes_buffer)
            .arg(&indices_buffer)
            .arg(scene_min_f3)
            .arg(scene_size_f3)
            .arg(&(num_triangles as i32))
            .global_work_size(num_triangles)
            .build().unwrap();

        unsafe { compute_morton_kernel.enq().unwrap(); }

        let mut current_codes_buffer = morton_codes_buffer;
        let mut current_indices_buffer = indices_buffer;

        for bit in 0..30 {
            let temp_codes_buffer: Buffer<u32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(num_triangles)
                .build().unwrap();

            let temp_indices_buffer: Buffer<i32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(num_triangles)
                .build().unwrap();

            let counters_buffer: Buffer<i32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_WRITE | flags::MEM_COPY_HOST_PTR)
                .len(2)
                .copy_host_slice(&[0i32, 0i32])
                .build().unwrap();

            let radix_sort_kernel = Kernel::builder()
                .program(&state.bvh_program)
                .name("radix_sort_step")
                .queue(state.queue.clone())
                .arg(&current_codes_buffer)
                .arg(&current_indices_buffer)
                .arg(&temp_codes_buffer)
                .arg(&temp_indices_buffer)
                .arg(&counters_buffer)
                .arg(&(bit as i32))
                .arg(&(num_triangles as i32))
                .global_work_size(num_triangles)
                .build().unwrap();

            unsafe { radix_sort_kernel.enq().unwrap(); }

            current_codes_buffer = temp_codes_buffer;
            current_indices_buffer = temp_indices_buffer;
        }

        let morton_codes_buffer = current_codes_buffer;
        let indices_buffer = current_indices_buffer;

        if num_triangles == 1 {
            return BvhNode {
                aabb_min: [aabb_min_cpu[0], aabb_min_cpu[1], aabb_min_cpu[2]],
                aabb_max: [aabb_max_cpu[0], aabb_max_cpu[1], aabb_max_cpu[2]],
                left: None,
                right: None,
                triangles: vec![0],
                triangle_offset: 0,
            };
        }

        let node_aabb_min_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len((num_triangles - 1) * 3)
            .build().unwrap();

        let node_aabb_max_buffer: Buffer<f32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len((num_triangles - 1) * 3)
            .build().unwrap();

        let node_left_buffer: Buffer<i32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles - 1)
            .build().unwrap();

        let node_right_buffer: Buffer<i32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_WRITE_ONLY)
            .len(num_triangles - 1)
            .build().unwrap();

        let build_bvh_kernel = Kernel::builder()
            .program(&state.bvh_program)
            .name("build_bvh_nodes")
            .queue(state.queue.clone())
            .arg(&morton_codes_buffer)
            .arg(&indices_buffer)
            .arg(&aabb_min_buffer)
            .arg(&aabb_max_buffer)
            .arg(&node_aabb_min_buffer)
            .arg(&node_aabb_max_buffer)
            .arg(&node_left_buffer)
            .arg(&node_right_buffer)
            .arg(&(num_triangles as i32))
            .global_work_size(num_triangles - 1)
            .build().unwrap();

        unsafe { build_bvh_kernel.enq().unwrap(); }

        let processed_buffer: Buffer<i32> = Buffer::builder()
            .queue(state.queue.clone())
            .flags(flags::MEM_READ_WRITE | flags::MEM_COPY_HOST_PTR)
            .len(num_triangles - 1)
            .copy_host_slice(&vec![0i32; num_triangles - 1])
            .build().unwrap();

        let calculate_bbox_kernel = Kernel::builder()
            .program(&state.bvh_program)
            .name("calculate_bounding_boxes")
            .queue(state.queue.clone())
            .arg(&aabb_min_buffer)
            .arg(&aabb_max_buffer)
            .arg(&node_left_buffer)
            .arg(&node_right_buffer)
            .arg(&node_aabb_min_buffer)
            .arg(&node_aabb_max_buffer)
            .arg(&processed_buffer)
            .arg(&(num_triangles as i32))
            .global_work_size(num_triangles)
            .build().unwrap();

        unsafe { calculate_bbox_kernel.enq().unwrap(); }

        let mut node_aabb_min_cpu = vec![0.0f32; (num_triangles - 1) * 3];
        let mut node_aabb_max_cpu = vec![0.0f32; (num_triangles - 1) * 3];
        let mut node_left_cpu = vec![0i32; num_triangles - 1];
        let mut node_right_cpu = vec![0i32; num_triangles - 1];
        let mut indices_cpu = vec![0i32; num_triangles];

        node_aabb_min_buffer.read(&mut node_aabb_min_cpu).enq().unwrap();
        node_aabb_max_buffer.read(&mut node_aabb_max_cpu).enq().unwrap();
        node_left_buffer.read(&mut node_left_cpu).enq().unwrap();
        node_right_buffer.read(&mut node_right_cpu).enq().unwrap();
        indices_buffer.read(&mut indices_cpu).enq().unwrap();

        fn build_cpu_bvh(
            first: usize,
            last: usize,
            node_idx: usize,
            node_left: &[i32],
            node_right: &[i32],
            indices: &[i32],
            aabb_min: &[[f32; 3]],
            aabb_max: &[[f32; 3]],
            node_aabb_min: &[[f32; 3]],
            node_aabb_max: &[[f32; 3]],
            triangles: &[Render_triangle],
        ) -> BvhNode {
            if first == last {
                let tri_idx = indices[first] as usize;
                return BvhNode {
                    aabb_min: aabb_min[tri_idx],
                    aabb_max: aabb_max[tri_idx],
                    left: None,
                    right: None,
                    triangles: vec![tri_idx],
                    triangle_offset: 0,
                };
            }

            let left_child = node_left[node_idx] as usize;
            let right_child = node_right[node_idx] as usize;

            let left_node = if left_child < indices.len() {
                build_cpu_bvh(first, left_child, left_child, node_left, node_right, indices, aabb_min, aabb_max, node_aabb_min, node_aabb_max, triangles)
            } else {
                let node_idx_in_bvh = left_child - indices.len();
                build_cpu_bvh(first, node_left[node_idx_in_bvh] as usize, node_idx_in_bvh, node_left, node_right, indices, aabb_min, aabb_max, node_aabb_min, node_aabb_max, triangles)
            };

            let right_node = if right_child < indices.len() {
                build_cpu_bvh(right_child, last, right_child, node_left, node_right, indices, aabb_min, aabb_max, node_aabb_min, node_aabb_max, triangles)
            } else {
                let node_idx_in_bvh = right_child - indices.len();
                build_cpu_bvh(node_right[node_idx_in_bvh] as usize, last, node_idx_in_bvh, node_left, node_right, indices, aabb_min, aabb_max, node_aabb_min, node_aabb_max, triangles)
            };

            BvhNode {
                aabb_min: node_aabb_min[node_idx],
                aabb_max: node_aabb_max[node_idx],
                left: Some(Box::new(left_node)),
                right: Some(Box::new(right_node)),
                triangles: Vec::new(),
                triangle_offset: 0,
            }
        }

        let aabb_min_vec: Vec<[f32; 3]> = (0..num_triangles).map(|i| [aabb_min_cpu[i*3], aabb_min_cpu[i*3+1], aabb_min_cpu[i*3+2]]).collect();
        let aabb_max_vec: Vec<[f32; 3]> = (0..num_triangles).map(|i| [aabb_max_cpu[i*3], aabb_max_cpu[i*3+1], aabb_max_cpu[i*3+2]]).collect();
        let node_aabb_min_vec: Vec<[f32; 3]> = (0..num_triangles-1).map(|i| [node_aabb_min_cpu[i*3], node_aabb_min_cpu[i*3+1], node_aabb_min_cpu[i*3+2]]).collect();
        let node_aabb_max_vec: Vec<[f32; 3]> = (0..num_triangles-1).map(|i| [node_aabb_max_cpu[i*3], node_aabb_max_cpu[i*3+1], node_aabb_max_cpu[i*3+2]]).collect();

        build_cpu_bvh(
            0,
            num_triangles - 1,
            0,
            &node_left_cpu,
            &node_right_cpu,
            &indices_cpu,
            &aabb_min_vec,
            &aabb_max_vec,
            &node_aabb_min_vec,
            &node_aabb_max_vec,
            triangles,
        )
    } else {
        build_bvh(triangles, 0)
    }
}

fn Bake_scene() {
    let static_scene = super::Static_scene.lock().unwrap();

    let mut static_triangles: Vec<super::Draw_components> = Vec::new();
    for object in static_scene.iter() {
        if object.draw_type == "3d_object".to_string() {
            let verts = &object.draw_vertices;
            for i in (0..verts.len()).step_by(9) {
                if i + 8 >= verts.len() { break; }
                let tri = super::Draw_components {
                    draw_type: object.draw_type.clone(),
                    draw_x: object.draw_x,
                    draw_y: object.draw_y,
                    draw_z: object.draw_z,
                    draw_symbol: object.draw_symbol,
                    draw_vertices: vec![
                        verts[i], verts[i + 1], verts[i + 2],
                        verts[i + 3], verts[i + 4], verts[i + 5],
                        verts[i + 6], verts[i + 7], verts[i + 8],
                    ],
                    draw_RGBA_color: object.draw_RGBA_color,
                    draw_texture_path: object.draw_texture_path.clone(),
                    draw_special_name: object.draw_special_name.clone()
                };
                static_triangles.push(tri);
            }
        }
    }
    drop(static_scene);

    let static_lights = super::Static_light.lock().unwrap().clone();

    let occluders: Vec<Render_triangle> = static_triangles.iter().cloned()
        .map(|t| Render_triangle { triangle: t, baked_light: None })
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

    let mut tri_data = vec![0.0f32; num_triangles * 6];
    let mut occluder_data = vec![0.0f32; num_occluders * 10];
    let mut light_data = vec![0.0f32; num_lights * 11];
    let mut output = vec![0.0f32; num_triangles * 3];

    if num_triangles > 0 && num_lights > 0 {
        let state_mutex = get_opencl_state().expect("Failed to get OpenCL state");
        let state_guard = state_mutex.lock().unwrap();
        if let Some(state) = state_guard.as_ref() {
            let raw_vertices_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(raw_vertices.len())
                .copy_host_slice(&raw_vertices)
                .build().unwrap();

            let tri_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(tri_data.len())
                .build().unwrap();

            let prepare_tri_kernel = Kernel::builder()
                .program(&state.prepare_program)
                .name("prepare_triangle_data")
                .queue(state.queue.clone())
                .arg(&raw_vertices_buffer)
                .arg(&tri_data_buffer)
                .arg(&(num_triangles as i32))
                .global_work_size(num_triangles)
                .build().unwrap();

            unsafe { prepare_tri_kernel.enq().unwrap(); }

            let raw_occluder_vertices_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(num_occluders * 9)
                .copy_host_slice(&raw_vertices[0..num_occluders * 9])
                .build().unwrap();

            let raw_alphas_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(raw_alphas.len())
                .copy_host_slice(&raw_alphas)
                .build().unwrap();

            let occluder_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(occluder_data.len())
                .build().unwrap();

            let prepare_occluder_kernel = Kernel::builder()
                .program(&state.prepare_program)
                .name("prepare_occluder_data")
                .queue(state.queue.clone())
                .arg(&raw_occluder_vertices_buffer)
                .arg(&raw_alphas_buffer)
                .arg(&occluder_data_buffer)
                .arg(&(num_occluders as i32))
                .global_work_size(num_occluders)
                .build().unwrap();

            unsafe { prepare_occluder_kernel.enq().unwrap(); }

            let light_positions_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_positions.len())
                .copy_host_slice(&light_positions)
                .build().unwrap();

            let light_directions_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_directions.len())
                .copy_host_slice(&light_directions)
                .build().unwrap();

            let light_cone_angles_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_cone_angles.len())
                .copy_host_slice(&light_cone_angles)
                .build().unwrap();

            let light_distances_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_distances.len())
                .copy_host_slice(&light_distances)
                .build().unwrap();

            let light_colors_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_READ_ONLY)
                .len(light_colors.len())
                .copy_host_slice(&light_colors)
                .build().unwrap();

            let light_data_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(light_data.len())
                .build().unwrap();

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
                .arg(&(num_lights as i32))
                .global_work_size(num_lights)
                .build().unwrap();

            unsafe { prepare_light_kernel.enq().unwrap(); }

            let output_buffer: Buffer<f32> = Buffer::builder()
                .queue(state.queue.clone())
                .flags(flags::MEM_WRITE_ONLY)
                .len(output.len())
                .build().unwrap();

            let bake_kernel = Kernel::builder()
                .program(&state.bake_program)
                .name("bake_lighting")
                .queue(state.queue.clone())
                .arg(&tri_data_buffer)
                .arg(&occluder_data_buffer)
                .arg(&light_data_buffer)
                .arg(&(num_triangles as i32))
                .arg(&(num_occluders as i32))
                .arg(&(num_lights as i32))
                .arg(&output_buffer)
                .global_work_size(num_triangles)
                .build().unwrap();

            unsafe { bake_kernel.enq().unwrap(); }

            tri_data_buffer.read(&mut tri_data).enq().unwrap();
            occluder_data_buffer.read(&mut occluder_data).enq().unwrap();
            light_data_buffer.read(&mut light_data).enq().unwrap();
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
                let centroid = [(v0[0] + v1[0] + v2[0]) / 3.0, (v0[1] + v1[1] + v2[1]) / 3.0, (v0[2] + v1[2] + v2[2]) / 3.0];
                let baked = compute_lighting(centroid, normal, &occluders_bvh, &occluders, &static_lights, [0, 0, 0]);
                output[idx*3] = baked[0];
                output[idx*3+1] = baked[1];
                output[idx*3+2] = baked[2];
                idx += 1;
            }
        }
    }

    let mut baked_result: Vec<Render_triangle> = Vec::with_capacity(num_triangles);
    for i in 0..num_triangles {
        let baked_light = [output[i*3], output[i*3+1], output[i*3+2]];
        baked_result.push(Render_triangle {
            triangle: static_triangles[i].clone(),
            baked_light: Some(baked_light),
        });
    }

    let baked_bvh = build_bvh_on_gpu(&baked_result);
    let mut store = Baked_static_triangles.lock().unwrap();
    *store = baked_result;
    drop(store);
    let mut bvh_store = Baked_static_bvh.lock().unwrap();
    *bvh_store = Some(baked_bvh);
}

pub fn Render_image_to_console() -> Result<(),String>{
    let mut queue_2d:Vec<super::Draw_components> = Vec::new();
    let mut queue_3d:Vec<super::Draw_components> = Vec::new();
    let mut light_queue:Vec<super::Light_components> = Vec::new();

    let mut all_queue= super::Static_scene.lock().unwrap();

    for object in all_queue.iter(){
        if object.draw_type == "2d_object".to_string(){
            queue_2d.push(object.clone());
        }
    }

    drop(all_queue);

    let mut all_queue= super::Draw_queue.lock().unwrap();

    for object in all_queue.iter(){
        if object.draw_type == "2d_object".to_string(){
            queue_2d.push(object.clone());
        }
        if object.draw_type == "3d_object".to_string(){
            queue_3d.push(object.clone());
        }
    }

    drop(all_queue);
    super::Draw_queue.lock().unwrap().clear();

    let mut all_ligts = super::Light_queue.lock().unwrap();
    for light in all_ligts.iter(){
        light_queue.push(light.clone());
    }
    drop(all_ligts);
    super::Light_queue.lock().unwrap().clear();

    let mut examination = super::Is_scene_changed.lock().unwrap();
    if *examination {
        Bake_scene();
        *examination = false;
    }
    drop(examination);

    let width = super::Engine_settings.lock().unwrap().window_width as i128;
    let height = super::Engine_settings.lock().unwrap().window_height as i128;

    let new_screen = vec![vec![super::Empty_pixel.lock().unwrap().clone(); width.clone() as usize]; height.clone() as usize];
    let mut screen = super::Screen.lock().unwrap();
    *screen = new_screen;

    let mut triangles = Vec::new();
    for object in queue_3d {
        let verts = &object.draw_vertices;
        for i in (0..verts.len()).step_by(9) {
            if i + 8 >= verts.len() { break; }
            let tri = super::Draw_components {
                draw_type: object.draw_type.clone(),
                draw_x: object.draw_x,
                draw_y: object.draw_y,
                draw_z: object.draw_z,
                draw_symbol: object.draw_symbol,
                draw_vertices: vec![
                    verts[i], verts[i+1], verts[i+2],
                    verts[i+3], verts[i+4], verts[i+5],
                    verts[i+6], verts[i+7], verts[i+8],
                ],
                draw_RGBA_color: object.draw_RGBA_color,
                draw_texture_path: object.draw_texture_path.clone(),
                draw_special_name: object.draw_special_name.clone()
            };
            triangles.push(tri);
        }
    }

    Render_3d_to_screen(&triangles, &mut screen, &light_queue);

    for object in queue_2d{
        let mut biggest_x:f32 = 0.0;
        let mut biggest_y:f32 = 0.0;
        let mut smallest_x:f32 = width as f32;
        let mut smallest_y:f32 = height as f32; 
        for i in (0..(object.draw_vertices.len()-1)).step_by(2) {
            biggest_x = if object.draw_vertices[i]+object.draw_x > biggest_x {object.draw_vertices[i]+object.draw_x}else{biggest_x};
            biggest_y = if object.draw_vertices[i+1]+object.draw_y > biggest_y {object.draw_vertices[i+1]+object.draw_y}else{biggest_y};
            smallest_x = if object.draw_vertices[i]+object.draw_x < smallest_x {object.draw_vertices[i]+object.draw_x}else{smallest_x};
            smallest_y = if object.draw_vertices[i+1]+object.draw_y < smallest_y {object.draw_vertices[i+1]+object.draw_y}else{smallest_y};
        }
        let start_x = if smallest_x < 0.0 { 0 } else { smallest_x as i128 };
        let end_x = if biggest_x > width as f32 { width } else { biggest_x as i128 };
        let start_y = if smallest_y < 0.0 { 0 } else { smallest_y as i128 };
        let end_y = if biggest_y > height as f32 { height } else { biggest_y as i128 };

        let uv_range_x = if (biggest_x - smallest_x).abs() < 1e-8 { 1.0 } else { biggest_x - smallest_x };
        let uv_range_y = if (biggest_y - smallest_y).abs() < 1e-8 { 1.0 } else { biggest_y - smallest_y };

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
                        let tex_x = (u * (img.width() as f32 - 1.0)).round().clamp(0.0, img.width() as f32 - 1.0) as u32;
                        let tex_y = (v * (img.height() as f32 - 1.0)).round().clamp(0.0, img.height() as f32 - 1.0) as u32;
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
                            pixel_RGBA_color: [r, g, b, 255]
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

    drop(width);
    drop(height);
    drop(screen);

    Ok(())
}