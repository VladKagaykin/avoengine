use std::f32::consts::PI;
use std::thread;
use image::RgbaImage;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock, Mutex};
use ocl::{ProQue, Platform, Device, flags};

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
    pub baked_light: Option<[f32;3]>,
}

static Baked_static_triangles: Mutex<Vec<Render_triangle>> = Mutex::new(Vec::new());
static Baked_static_bvh: Mutex<Option<BvhNode>> = Mutex::new(None);

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

        let falloff = light.light_distance / (1.0 / EPSILON - 1.0).sqrt();
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

const KERNEL_SRC: &str = r#"
#define PI 3.14159265358979323846

uchar4 unpackColor(uint packed) {
    uchar4 c;
    c.x = (packed >> 24) & 0xFF;
    c.y = (packed >> 16) & 0xFF;
    c.z = (packed >> 8) & 0xFF;
    c.w = packed & 0xFF;
    return c;
}

uint packColor(uchar4 c) {
    return (c.x << 24) | (c.y << 16) | (c.z << 8) | c.w;
}

bool rayTriangleIntersect(float3 origin, float3 dir, float3 v0, float3 v1, float3 v2, float* t, float* u, float* v) {
    float eps = 1e-6;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 h = cross(dir, edge2);
    float a = dot(edge1, h);
    if (fabs(a) < eps) return false;
    float f = 1.0 / a;
    float3 s = origin - v0;
    *u = f * dot(s, h);
    if (*u < 0.0 || *u > 1.0) return false;
    float3 q = cross(s, edge1);
    *v = f * dot(dir, q);
    if (*v < 0.0 || *u + *v > 1.0) return false;
    *t = f * dot(edge2, q);
    return *t > eps;
}

__kernel void render_kernel(
    __global const float* triVerts,
    __global const float* triUVs,
    __global const uint* triColors,
    __global const int* triTextureIndex,
    __global const uchar* triSymbol,
    __global const float* lightData,
    __global const uchar* textureData,
    __global const int* textureMeta,
    const int numTriangles,
    const int numLights,
    const int numTextures,
    const float camOriginX, const float camOriginY, const float camOriginZ,
    const float camForwardX, const float camForwardY, const float camForwardZ,
    const float camRightX, const float camRightY, const float camRightZ,
    const float camUpX, const float camUpY, const float camUpZ,
    const float tan_h,
    const float tan_v,
    const int width,
    const int height,
    const float maxDist,
    const float ambientLightR, const float ambientLightG, const float ambientLightB,
    __global uint* outputColor,
    __global uchar* outputSymbol
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x >= width || y >= height) return;

    float3 camOrigin = (float3)(camOriginX, camOriginY, camOriginZ);
    float3 camForward = (float3)(camForwardX, camForwardY, camForwardZ);
    float3 camRight = (float3)(camRightX, camRightY, camRightZ);
    float3 camUp = (float3)(camUpX, camUpY, camUpZ);
    float3 ambientLight = (float3)(ambientLightR, ambientLightG, ambientLightB);

    float nx = (2.0f * (x + 0.5f) / width) - 1.0f;
    float ny = 1.0f - (2.0f * (y + 0.5f) / height);
    float px = nx * tan_h;
    float py = ny * tan_v;
    float3 dir = normalize(camForward + camRight * px + camUp * py);
    float3 origin = camOrigin;

    #define MAX_HITS 64
    float hitT[MAX_HITS];
    uint hitColorPacked[MAX_HITS];
    float hitAlpha[MAX_HITS];
    uchar hitSymbol[MAX_HITS];
    int hitCount = 0;

    for (int i = 0; i < numTriangles; i++) {
        int base = i * 9;
        float3 v0 = (float3)(triVerts[base], triVerts[base+1], triVerts[base+2]);
        float3 v1 = (float3)(triVerts[base+3], triVerts[base+4], triVerts[base+5]);
        float3 v2 = (float3)(triVerts[base+6], triVerts[base+7], triVerts[base+8]);
        float t, u, v;
        if (rayTriangleIntersect(origin, dir, v0, v1, v2, &t, &u, &v)) {
            if (t > 0.0f && t < maxDist) {
                uint packedBaseColor = triColors[i];
                uchar4 baseColor = unpackColor(packedBaseColor);
                float alpha = baseColor.w / 255.0f;
                uchar4 col = baseColor;

                int texIndex = triTextureIndex[i];
                if (texIndex >= 0) {
                    int tw = textureMeta[texIndex*3];
                    int th = textureMeta[texIndex*3+1];
                    int offset = textureMeta[texIndex*3+2];
                    float w = 1.0f - u - v;
                    float uvx = w * triUVs[i*6] + u * triUVs[i*6+2] + v * triUVs[i*6+4];
                    float uvy = w * triUVs[i*6+1] + u * triUVs[i*6+3] + v * triUVs[i*6+5];
                    int tx = (int)(uvx * (tw - 1));
                    int ty = (int)(uvy * (th - 1));
                    tx = clamp(tx, 0, tw-1);
                    ty = clamp(ty, 0, th-1);
                    int texelIndex = offset + (ty * tw + tx) * 4;
                    uchar4 texel = (uchar4)(textureData[texelIndex], textureData[texelIndex+1], textureData[texelIndex+2], textureData[texelIndex+3]);
                    col = (uchar4)(
                        (uchar)((texel.x/255.0f) * (baseColor.x/255.0f) * 255.0f),
                        (uchar)((texel.y/255.0f) * (baseColor.y/255.0f) * 255.0f),
                        (uchar)((texel.z/255.0f) * (baseColor.z/255.0f) * 255.0f),
                        (uchar)((texel.w/255.0f) * alpha * 255.0f)
                    );
                    alpha = col.w / 255.0f;
                }

                float3 normal = normalize(cross(v1 - v0, v2 - v0));
                float3 point = origin + dir * t;
                float3 lightSum = ambientLight;

                for (int j = 0; j < numLights; j++) {
                    int lbase = j * 10;
                    float3 lightPos = (float3)(lightData[lbase], lightData[lbase+1], lightData[lbase+2]);
                    float3 lightColor = (float3)(lightData[lbase+3], lightData[lbase+4], lightData[lbase+5]);
                    float pitch = lightData[lbase+6];
                    float yaw = lightData[lbase+7];
                    float coneAngle = lightData[lbase+8];
                    float lightDist = lightData[lbase+9];

                    float3 toLight = lightPos - point;
                    float distSq = dot(toLight, toLight);
                    float dist = sqrt(distSq);
                    if (dist < 1e-6) continue;
                    float3 toLightDir = normalize(toLight);

                    float p = pitch * PI / 180.0f;
                    float y = yaw * PI / 180.0f;
                    float3 lightForward = (float3)(sin(y)*sin(p), cos(p), cos(y)*sin(p));
                    float3 pointDirFromLight = -toLightDir;
                    float cosAngle = dot(lightForward, pointDirFromLight);
                    float halfCone = (coneAngle * 0.5f) * PI / 180.0f;
                    float cosHalfCone = cos(halfCone);
                    if (cosAngle < cosHalfCone) continue;

                    bool occluded = false;
                    float3 offset = point + normal * 0.001f;
                    for (int k = 0; k < numTriangles; k++) {
                        int kbase = k * 9;
                        float3 sv0 = (float3)(triVerts[kbase], triVerts[kbase+1], triVerts[kbase+2]);
                        float3 sv1 = (float3)(triVerts[kbase+3], triVerts[kbase+4], triVerts[kbase+5]);
                        float3 sv2 = (float3)(triVerts[kbase+6], triVerts[kbase+7], triVerts[kbase+8]);
                        float st, su, sv;
                        if (rayTriangleIntersect(offset, toLightDir, sv0, sv1, sv2, &st, &su, &sv)) {
                            if (st > 0.001f && st < dist) {
                                uint packedOccColor = triColors[k];
                                uchar4 occColor = unpackColor(packedOccColor);
                                float occAlpha = occColor.w / 255.0f;
                                if (occAlpha >= 1.0f) {
                                    occluded = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (occluded) continue;

                    float falloff = lightDist / sqrt(1.0f/0.01f - 1.0f);
                    float attenuation = 1.0f / (1.0f + (dist/falloff)*(dist/falloff));
                    if (attenuation < 0.01f) continue;

                    float diff = fmax(dot(normal, toLightDir), 0.0f);
                    float factor = attenuation * diff;
                    lightSum += lightColor * factor;
                }

                float3 lit = (float3)(col.x/255.0f, col.y/255.0f, col.z/255.0f);
                lit = lit * lightSum;
                lit = fmin(lit, 1.0f);
                col = (uchar4)((uchar)(lit.x*255.0f), (uchar)(lit.y*255.0f), (uchar)(lit.z*255.0f), col.w);

                if (hitCount < MAX_HITS) {
                    hitT[hitCount] = t;
                    hitColorPacked[hitCount] = packColor(col);
                    hitAlpha[hitCount] = alpha;
                    hitSymbol[hitCount] = triSymbol[i];
                    hitCount++;
                }
            }
        }
    }

    // Сортировка хитов по расстоянию (пузырьком, так как максимум 64)
    for (int i = 1; i < hitCount; i++) {
        float keyT = hitT[i];
        uint keyC = hitColorPacked[i];
        float keyA = hitAlpha[i];
        uchar keyS = hitSymbol[i];
        int j = i - 1;
        while (j >= 0 && hitT[j] > keyT) {
            hitT[j+1] = hitT[j];
            hitColorPacked[j+1] = hitColorPacked[j];
            hitAlpha[j+1] = hitAlpha[j];
            hitSymbol[j+1] = hitSymbol[j];
            j--;
        }
        hitT[j+1] = keyT;
        hitColorPacked[j+1] = keyC;
        hitAlpha[j+1] = keyA;
        hitSymbol[j+1] = keyS;
    }

    // Находим ближайший непрозрачный хит
    int opaqueIndex = -1;
    for (int i = 0; i < hitCount; i++) {
        if (hitAlpha[i] >= 1.0f) {
            opaqueIndex = i;
            break;
        }
    }

    uchar4 finalColor;
    uchar finalSymbol = ' ';

    if (opaqueIndex != -1) {
        // Базовый цвет - непрозрачный
        finalColor = unpackColor(hitColorPacked[opaqueIndex]);
        finalSymbol = hitSymbol[opaqueIndex];
        // Смешиваем все прозрачные хиты перед непрозрачным (с ближнего к дальнему)
        for (int i = opaqueIndex - 1; i >= 0; i--) {
            float a = hitAlpha[i];
            if (a <= 0.0f || a >= 1.0f) continue; // только прозрачные
            uchar4 fg = unpackColor(hitColorPacked[i]);
            // Смешивание fg поверх finalColor
            float invA = 1.0f - a;
            finalColor = (uchar4)(
                (uchar)(fg.x * a + finalColor.x * invA),
                (uchar)(fg.y * a + finalColor.y * invA),
                (uchar)(fg.z * a + finalColor.z * invA),
                255
            );
            finalSymbol = hitSymbol[i];
        }
    } else {
        // Нет непрозрачных: смешиваем прозрачные с фоном (чёрный), от дальнего к ближнему
        finalColor = (uchar4)(0,0,0,255);
        for (int i = hitCount - 1; i >= 0; i--) {
            float a = hitAlpha[i];
            if (a <= 0.0f) continue;
            uchar4 fg = unpackColor(hitColorPacked[i]);
            float invA = 1.0f - a;
            finalColor = (uchar4)(
                (uchar)(fg.x * a + finalColor.x * invA),
                (uchar)(fg.y * a + finalColor.y * invA),
                (uchar)(fg.z * a + finalColor.z * invA),
                255
            );
            finalSymbol = hitSymbol[i];
        }
    }

    int pixelIndex = y * width + x;
    outputColor[pixelIndex] = packColor(finalColor);
    outputSymbol[pixelIndex] = finalSymbol;
}
"#;

pub fn Render_3d_to_screen(dynamic_triangles: &[super::Draw_components], screen: &mut Vec<Vec<super::Pixel_structure>>, dynamic_lights: &[super::Light_components]) {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width;
    let height = settings.window_height;
    drop(settings);

    let cam = super::Camera.lock().unwrap();
    let (ox, oy, oz) = (cam.camera_x, cam.camera_y, cam.camera_z);
    let pitch = cam.camera_pitch;
    let yaw = cam.camera_yaw;
    let roll = cam.camera_roll;
    let fov = cam.camera_fov as f32;
    let max_dist = cam.max_dist as f32;
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

    let _combined_bvh = if dynamic_render.is_empty() {
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
    let _dynamic_lights_culled: Vec<super::Light_components> = dynamic_lights.iter()
        .filter(|l| light_cone_intersects_frustum(l, origin, &basis, tan_h, tan_v))
        .cloned()
        .collect();

    let mut texture_map = HashMap::new();
    for rt in all_triangles.iter() {
        let tri = &rt.triangle;
        if tri.draw_texture_path != "none" && !texture_map.contains_key(&tri.draw_texture_path) {
            if let Some(img) = load_texture(&tri.draw_texture_path) {
                texture_map.insert(tri.draw_texture_path.clone(), img);
            }
        }
    }

    let num_triangles = all_triangles.len();
    let num_lights = all_lights.len();
    let num_textures = texture_map.len();

    if num_triangles == 0 {
        // Если нет треугольников, просто очищаем экран
        let empty_pixel = super::Empty_pixel.lock().unwrap().clone();
        for row in screen.iter_mut() {
            for pixel in row.iter_mut() {
                *pixel = empty_pixel.clone();
            }
        }
        return;
    }

    let mut tri_verts: Vec<f32> = Vec::with_capacity(num_triangles * 9);
    let mut tri_uvs: Vec<f32> = Vec::with_capacity(num_triangles * 6);
    let mut tri_colors: Vec<u32> = Vec::with_capacity(num_triangles);
    let mut tri_tex_index: Vec<i32> = Vec::with_capacity(num_triangles);
    let mut tri_symbol: Vec<u8> = Vec::with_capacity(num_triangles);

    let mut texture_data: Vec<u8> = Vec::new();
    let mut texture_meta: Vec<i32> = Vec::with_capacity(num_textures * 3);
    let mut tex_offset_map: HashMap<String, usize> = HashMap::new();

    for (path, img) in texture_map.iter() {
        let offset = texture_data.len();
        let (w, h) = (img.width() as i32, img.height() as i32);
        texture_meta.push(w);
        texture_meta.push(h);
        texture_meta.push(offset as i32);
        tex_offset_map.insert(path.clone(), offset);
        texture_data.extend_from_slice(img.as_raw());
    }

    for rt in all_triangles.iter() {
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        tri_verts.extend_from_slice(&[v0[0], v0[1], v0[2], v1[0], v1[1], v1[2], v2[0], v2[1], v2[2]]);

        let (uv0, uv1, uv2) = if tri.draw_texture_path != "none" {
            generate_uv_for_triangle(&v0, &v1, &v2)
        } else {
            ((0.0, 0.0), (0.0, 0.0), (0.0, 0.0))
        };
        tri_uvs.extend_from_slice(&[uv0.0, uv0.1, uv1.0, uv1.1, uv2.0, uv2.1]);

        let c = tri.draw_RGBA_color;
        let packed = ((c[0] as u32) << 24) | ((c[1] as u32) << 16) | ((c[2] as u32) << 8) | (c[3] as u32);
        tri_colors.push(packed);

        let tex_idx = if tri.draw_texture_path != "none" {
            if let Some(idx) = texture_map.keys().position(|p| p == &tri.draw_texture_path) {
                idx as i32
            } else {
                -1
            }
        } else {
            -1
        };
        tri_tex_index.push(tex_idx);
        tri_symbol.push(tri.draw_symbol as u8);
    }

    let mut light_data: Vec<f32> = Vec::with_capacity(num_lights * 10);
    for light in all_lights.iter() {
        light_data.push(light.light_x);
        light_data.push(light.light_y);
        light_data.push(light.light_z);
        light_data.push(light.light_RGB_color[0] as f32 / 255.0);
        light_data.push(light.light_RGB_color[1] as f32 / 255.0);
        light_data.push(light.light_RGB_color[2] as f32 / 255.0);
        light_data.push(light.light_pitch);
        light_data.push(light.light_yaw);
        light_data.push(light.light_cone_angle);
        light_data.push(light.light_distance);
    }

    // Если нет света, создаём фиктивный буфер с одним элементом
    if light_data.is_empty() {
        light_data.push(0.0);
    }
    if texture_data.is_empty() {
        texture_data.push(0);
    }
    if texture_meta.is_empty() {
        texture_meta.push(0);
    }

    let platforms = Platform::list();
    let mut selected_platform: Option<Platform> = None;
    let mut selected_device: Option<Device> = None;

    for platform in platforms.iter() {
        if let Ok(devices) = Device::list(platform, Some(flags::DeviceType::GPU)) {
            if let Some(device) = devices.into_iter().next() {
                selected_platform = Some(platform.clone());
                selected_device = Some(device);
                break;
            }
        }
    }

    if selected_device.is_none() {
        for platform in platforms.iter() {
            if let Ok(devices) = Device::list(platform, Some(flags::DeviceType::CPU)) {
                if let Some(device) = devices.into_iter().next() {
                    selected_platform = Some(platform.clone());
                    selected_device = Some(device);
                    break;
                }
            }
        }
    }

    let (platform, device) = match (selected_platform, selected_device) {
        (Some(p), Some(d)) => (p, d),
        _ => panic!("No OpenCL device found"),
    };

    let pro_que = ProQue::builder()
        .src(KERNEL_SRC)
        .dims((width as usize, height as usize))
        .platform(platform)
        .device(device)
        .build()
        .expect("Failed to build OpenCL program");

    let buffer_tri_verts = pro_que.buffer_builder::<f32>().len(tri_verts.len()).copy_host_slice(&tri_verts).build().unwrap();
    let buffer_tri_uvs = pro_que.buffer_builder::<f32>().len(tri_uvs.len()).copy_host_slice(&tri_uvs).build().unwrap();
    let buffer_tri_colors = pro_que.buffer_builder::<u32>().len(tri_colors.len()).copy_host_slice(&tri_colors).build().unwrap();
    let buffer_tri_tex_index = pro_que.buffer_builder::<i32>().len(tri_tex_index.len()).copy_host_slice(&tri_tex_index).build().unwrap();
    let buffer_tri_symbol = pro_que.buffer_builder::<u8>().len(tri_symbol.len()).copy_host_slice(&tri_symbol).build().unwrap();
    let buffer_light_data = pro_que.buffer_builder::<f32>().len(light_data.len()).copy_host_slice(&light_data).build().unwrap();
    let buffer_texture_data = pro_que.buffer_builder::<u8>().len(texture_data.len()).copy_host_slice(&texture_data).build().unwrap();
    let buffer_texture_meta = pro_que.buffer_builder::<i32>().len(texture_meta.len()).copy_host_slice(&texture_meta).build().unwrap();

    let buffer_output_color = pro_que.buffer_builder::<u32>().len((width as usize) * (height as usize)).build().unwrap();
    let buffer_output_symbol = pro_que.buffer_builder::<u8>().len((width as usize) * (height as usize)).build().unwrap();

    let kernel = pro_que.kernel_builder("render_kernel")
        .arg(&buffer_tri_verts)
        .arg(&buffer_tri_uvs)
        .arg(&buffer_tri_colors)
        .arg(&buffer_tri_tex_index)
        .arg(&buffer_tri_symbol)
        .arg(&buffer_light_data)
        .arg(&buffer_texture_data)
        .arg(&buffer_texture_meta)
        .arg(&(num_triangles as i32))
        .arg(&(num_lights as i32))
        .arg(&(num_textures as i32))
        .arg(&(origin[0]))
        .arg(&(origin[1]))
        .arg(&(origin[2]))
        .arg(&(basis.forward[0]))
        .arg(&(basis.forward[1]))
        .arg(&(basis.forward[2]))
        .arg(&(basis.right[0]))
        .arg(&(basis.right[1]))
        .arg(&(basis.right[2]))
        .arg(&(basis.up[0]))
        .arg(&(basis.up[1]))
        .arg(&(basis.up[2]))
        .arg(&tan_h)
        .arg(&tan_v)
        .arg(&(width as i32))
        .arg(&(height as i32))
        .arg(&max_dist)
        .arg(&(ambient_light[0] as f32 / 255.0))
        .arg(&(ambient_light[1] as f32 / 255.0))
        .arg(&(ambient_light[2] as f32 / 255.0))
        .arg(&buffer_output_color)
        .arg(&buffer_output_symbol)
        .build()
        .expect("Failed to build kernel");

    unsafe {
        kernel.enq().expect("Failed to enqueue kernel");
    }

    let mut output_colors = vec![0u32; (width as usize) * (height as usize)];
    let mut output_symbols = vec![0u8; (width as usize) * (height as usize)];
    buffer_output_color.read(&mut output_colors).enq().expect("Failed to read color buffer");
    buffer_output_symbol.read(&mut output_symbols).enq().expect("Failed to read symbol buffer");

    for y in 0..height as usize {
        for x in 0..width as usize {
            let idx = y * width as usize + x;
            let packed = output_colors[idx];
            let r = (packed >> 24) as u8;
            let g = (packed >> 16) as u8;
            let b = (packed >> 8) as u8;
            let a = (packed & 0xFF) as u8;
            screen[y][x] = super::Pixel_structure {
                pixel_symbol: output_symbols[idx] as char,
                pixel_RGBA_color: [r, g, b, a],
            };
        }
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

    let occluders_bvh = build_bvh(&occluders, 0);

    let mut baked_result: Vec<Render_triangle> = Vec::with_capacity(static_triangles.len());
    for tri in &static_triangles {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];

        let edge1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
        let edge2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
        let normal = normalize(cross(&edge1, &edge2));
        let centroid = [(v0[0] + v1[0] + v2[0]) / 3.0, (v0[1] + v1[1] + v2[1]) / 3.0, (v0[2] + v1[2] + v2[2]) / 3.0];

        let baked_light = compute_lighting(centroid, normal, &occluders_bvh, &occluders, &static_lights, [0, 0, 0]);
        baked_result.push(Render_triangle { triangle: tri.clone(), baked_light: Some(baked_light) });
    }

    let baked_bvh = build_bvh(&baked_result, 0);
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