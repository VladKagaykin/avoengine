use super::{Earth, Light_components, Static_light, Light_queue, Camera, Empty_pixel};
use super::render::{render_triangle, BSP_ROOT, BspNode};
use std::sync::{Arc, Mutex, LazyLock};

const LIGHTMAP_SCALE: f32 = 2.0;
const MIN_LIGHTMAP_SIZE: usize = 1;
const MAX_LIGHTMAP_SIZE: usize = 256;
const RAY_EPSILON: f32 = 1e-4;

pub struct Lightmap {
    pub width: usize,
    pub height: usize,
    pub pixels: Vec<[u8; 3]>,
}

pub static LIGHTMAP_CACHE: LazyLock<Mutex<Vec<Arc<Lightmap>>>> =
    LazyLock::new(|| Mutex::new(Vec::new()));

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn normalize(v: [f32; 3]) -> [f32; 3] {
    let len = dot(v, v).sqrt();
    if len < 1e-12 { return [0.0, 0.0, 0.0]; }
    [v[0] / len, v[1] / len, v[2] / len]
}

fn orthonormal_basis(n: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    let up = if n[1].abs() < 0.99 { [0.0, 1.0, 0.0] } else { [1.0, 0.0, 0.0] };
    let t = normalize(cross(up, n));
    let b = cross(n, t);
    (t, b)
}

fn barycentric(p: [f32; 2], a: [f32; 2], b: [f32; 2], c: [f32; 2]) -> [f32; 3] {
    let v0 = [b[0] - a[0], b[1] - a[1]];
    let v1 = [c[0] - a[0], c[1] - a[1]];
    let v2 = [p[0] - a[0], p[1] - a[1]];
    let den = v0[0] * v1[1] - v1[0] * v0[1];
    if den.abs() < 1e-12 { return [1.0, 0.0, 0.0]; }
    let inv = 1.0 / den;
    let bc1 = (v2[0] * v1[1] - v1[0] * v2[1]) * inv;
    let bc2 = (v0[0] * v2[1] - v2[0] * v0[1]) * inv;
    let bc0 = 1.0 - bc1 - bc2;
    [bc0, bc1, bc2]
}

fn ray_triangle_intersect(
    orig: [f32; 3], dir: [f32; 3],
    a: [f32; 3], b: [f32; 3], c: [f32; 3],
) -> Option<f32> {
    let e1 = sub(b, a);
    let e2 = sub(c, a);
    let h = cross(dir, e2);
    let det = dot(e1, h);
    if det.abs() < 1e-8 { return None; }
    let f = 1.0 / det;
    let s = sub(orig, a);
    let u = f * dot(s, h);
    if u < 0.0 || u > 1.0 { return None; }
    let q = cross(s, e1);
    let v = f * dot(dir, q);
    if v < 0.0 || u + v > 1.0 { return None; }
    let t = f * dot(e2, q);
    if t > RAY_EPSILON { Some(t) } else { None }
}

fn is_occluded(pos: [f32; 3], light_pos: [f32; 3], all_tris: &[render_triangle]) -> bool {
    let to_light = sub(light_pos, pos);
    let dist_sq = dot(to_light, to_light);
    if dist_sq < RAY_EPSILON * RAY_EPSILON { return false; }
    let dist = dist_sq.sqrt();
    let dir = [to_light[0] / dist, to_light[1] / dist, to_light[2] / dist];

    for tri in all_tris {
        let a = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
        let b = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
        let c = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];
        if let Some(t) = ray_triangle_intersect(pos, dir, a, b, c) {
            if t < dist - RAY_EPSILON {
                return true;
            }
        }
    }
    false
}

fn spotlight_dir(light: &Light_components) -> [f32; 3] {
    let yaw = light.yaw.to_radians();
    let pitch = light.pitch.to_radians();
    [
        pitch.cos() * yaw.sin(),
        pitch.sin(),
        pitch.cos() * yaw.cos(),
    ]
}

fn light_contribution(
    pos: [f32; 3], normal: [f32; 3], light: &Light_components,
    all_tris: Option<&[render_triangle]>,
) -> [f32; 3] {
    let lpos = [light.x, light.y, light.z];
    let to_light = sub(lpos, pos);
    let dist_sq = dot(to_light, to_light);
    let dist = dist_sq.sqrt();
    if dist < RAY_EPSILON { return [0.0, 0.0, 0.0]; }

    let dir = [to_light[0] / dist, to_light[1] / dist, to_light[2] / dist];

    if light.cone_angle > 0.5 && light.cone_angle < 359.5 {
        let ldir = spotlight_dir(light);
        let to_surface = [-dir[0], -dir[1], -dir[2]];
        let cos_angle = dot(to_surface, ldir);
        let half = (light.cone_angle * 0.5).to_radians();
        if cos_angle < half.cos() { return [0.0, 0.0, 0.0]; }
    }

    let ndotl = dot(normal, dir);
    if ndotl <= 0.0 { return [0.0, 0.0, 0.0]; }

    if let Some(tris) = all_tris {
        if is_occluded(pos, lpos, tris) { return [0.0, 0.0, 0.0]; }
    }

    let atten = ndotl;
    [
        light.RGB_color[0] as f32 * atten,
        light.RGB_color[1] as f32 * atten,
        light.RGB_color[2] as f32 * atten,
    ]
}

fn compute_static_light_at(
    pos: [f32; 3], normal: [f32; 3],
    lights: &[Light_components], all_tris: &[render_triangle],
    ambient: [u8; 3],
) -> [u8; 3] {
    let mut r = ambient[0] as f32;
    let mut g = ambient[1] as f32;
    let mut b = ambient[2] as f32;

    for light in lights {
        let c = light_contribution(pos, normal, light, Some(all_tris));
        r += c[0];
        g += c[1];
        b += c[2];
    }

    [
        r.clamp(0.0, 255.0) as u8,
        g.clamp(0.0, 255.0) as u8,
        b.clamp(0.0, 255.0) as u8,
    ]
}

fn compute_lightmap_for_triangle(
    tri: &render_triangle,
    lights: &[Light_components],
    all_tris: &[render_triangle],
    ambient: [u8; 3],
) -> Option<(Lightmap, [f32; 6])> {
    let p0 = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
    let p1 = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
    let p2 = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];
    let e1 = sub(p1, p0);
    let e2 = sub(p2, p0);
    let n_raw = cross(e1, e2);
    let n_len = dot(n_raw, n_raw).sqrt();
    if n_len < 1e-6 { return None; }
    let normal = [n_raw[0] / n_len, n_raw[1] / n_len, n_raw[2] / n_len];
    let (tangent, bitangent) = orthonormal_basis(normal);
    let uv0 = [dot(p0, tangent), dot(p0, bitangent)];
    let uv1 = [dot(p1, tangent), dot(p1, bitangent)];
    let uv2 = [dot(p2, tangent), dot(p2, bitangent)];
    let umin = uv0[0].min(uv1[0]).min(uv2[0]);
    let umax = uv0[0].max(uv1[0]).max(uv2[0]);
    let vmin = uv0[1].min(uv1[1]).min(uv2[1]);
    let vmax = uv0[1].max(uv1[1]).max(uv2[1]);
    let du = umax - umin;
    let dv = vmax - vmin;
    if du < 1e-6 && dv < 1e-6 { return None; }
    let du_safe = du.max(1e-6);
    let dv_safe = dv.max(1e-6);
    let w = ((du_safe * LIGHTMAP_SCALE).ceil() as usize)
        .clamp(MIN_LIGHTMAP_SIZE, MAX_LIGHTMAP_SIZE);
    let h = ((dv_safe * LIGHTMAP_SCALE).ceil() as usize)
        .clamp(MIN_LIGHTMAP_SIZE, MAX_LIGHTMAP_SIZE);
    let lm_uv0 = [(uv0[0] - umin) / du_safe, (uv0[1] - vmin) / dv_safe];
    let lm_uv1 = [(uv1[0] - umin) / du_safe, (uv1[1] - vmin) / dv_safe];
    let lm_uv2 = [(uv2[0] - umin) / du_safe, (uv2[1] - vmin) / dv_safe];
    let mut pixels = vec![[0u8; 3]; w * h];
    for j in 0..h {
        for i in 0..w {
            let u = (i as f32 + 0.5) / w as f32;
            let v = (j as f32 + 0.5) / h as f32;
            let bc = barycentric([u, v], lm_uv0, lm_uv1, lm_uv2);
            let world = [
                p0[0] * bc[0] + p1[0] * bc[1] + p2[0] * bc[2],
                p0[1] * bc[0] + p1[1] * bc[1] + p2[1] * bc[2],
                p0[2] * bc[0] + p1[2] * bc[1] + p2[2] * bc[2],
            ];
            pixels[j * w + i] = compute_static_light_at(world, normal, lights, all_tris, ambient);
        }
    }
    for _ in 0..3 {
        let src = pixels.clone();
        for y in 0..h {
            for x in 0..w {
                let mut r = 0u32;
                let mut g = 0u32;
                let mut b = 0u32;
                let mut count = 0u32;
                for dy in -1i32..=1 {
                    for dx in -1i32..=1 {
                        let nx = x as i32 + dx;
                        let ny = y as i32 + dy;
                        if nx >= 0 && nx < w as i32 && ny >= 0 && ny < h as i32 {
                            let idx = ny as usize * w + nx as usize;
                            r += src[idx][0] as u32;
                            g += src[idx][1] as u32;
                            b += src[idx][2] as u32;
                            count += 1;
                        }
                    }
                }
                let idx = y * w + x;
                pixels[idx] = [
                    (r / count) as u8,
                    (g / count) as u8,
                    (b / count) as u8,
                ];
            }
        }
    }
    Some((
        Lightmap { width: w, height: h, pixels },
        [lm_uv0[0], lm_uv0[1], lm_uv1[0], lm_uv1[1], lm_uv2[0], lm_uv2[1]],
    ))
}

pub fn Bake_light() {
    let lights = Static_light.lock().unwrap().clone();
    if lights.is_empty() {
        println!("[Light] No static lights to bake");
        return;
    }

    let ambient = Camera.lock().unwrap().ambient_light;

    let mut all_tris: Vec<render_triangle> = Vec::new();
    super::render::for_each_triangle(|t| all_tris.push(t.clone()));

    let mut baked_count = 0usize;
    let mut cache = LIGHTMAP_CACHE.lock().unwrap();
    cache.clear();

    super::render::for_each_triangle_mut(|t| {
        if let Some((lm, uvs)) = compute_lightmap_for_triangle(t, &lights, &all_tris, ambient) {
            let arc = Arc::new(lm);
            cache.push(Arc::clone(&arc));
            t.lightmap = Some(arc);
            t.lightmap_uvs = uvs;
            baked_count += 1;
        }
    });

    println!("[Light] Baked {} static lights into {} triangles", lights.len(), baked_count);
}

pub fn Compute_dynamic_light_from_slice(
    pos: [f32; 3], normal: [f32; 3], lights: &[Light_components],
) -> [u8; 3] {
    let mut r = 0.0f32;
    let mut g = 0.0f32;
    let mut b = 0.0f32;

    for light in lights {
        let c = light_contribution(pos, normal, light, None);
        r += c[0];
        g += c[1];
        b += c[2];
    }

    [
        r.clamp(0.0, 255.0) as u8,
        g.clamp(0.0, 255.0) as u8,
        b.clamp(0.0, 255.0) as u8,
    ]
}

pub fn Compute_dynamic_light_at(pos: [f32; 3], normal: [f32; 3]) -> [u8; 3] {
    let lights = Light_queue.lock().unwrap();
    Compute_dynamic_light_from_slice(pos, normal, &lights)
}

pub fn Add_dynamic_light(light: Light_components) {
    Light_queue.lock().unwrap().push(light);
}

pub fn Clear_dynamic_lights() {
    Light_queue.lock().unwrap().clear();
}

pub fn Get_fog_color() -> [u8; 3] {
    Empty_pixel.lock().unwrap().color
}

pub fn Get_fog_density() -> f32 {
    Earth.lock().unwrap().atmosphere_density
}

pub fn Get_dynamic_lights_snapshot() -> Vec<Light_components> {
    Light_queue.lock().unwrap().clone()
}