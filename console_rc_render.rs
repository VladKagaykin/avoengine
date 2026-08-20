use std::f32::consts::PI;
use std::thread;
use image::RgbaImage;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock, Mutex};

#[derive(Clone)]
pub struct Render_triangle {
    pub triangle: super::Draw_components,
    pub baked_light: Option<[f32;3]>,
}

static Baked_static_triangles: Mutex<Vec<Render_triangle>> = Mutex::new(Vec::new());

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

fn light_transmittance(point: [f32;3], normal: [f32;3], light_pos: [f32;3], dist_to_light: f32, triangles: &[Render_triangle]) -> f32 {
    let offset = [point[0] + normal[0]*1e-3, point[1] + normal[1]*1e-3, point[2] + normal[2]*0.1];
    let to_light = [light_pos[0]-point[0], light_pos[1]-point[1], light_pos[2]-point[2]];
    let dir = normalize(to_light);
    let mut transmittance = 1.0;
    for rt in triangles {
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0]+tri.draw_x, verts[1]+tri.draw_y, verts[2]+tri.draw_z];
        let v1 = [verts[3]+tri.draw_x, verts[4]+tri.draw_y, verts[5]+tri.draw_z];
        let v2 = [verts[6]+tri.draw_x, verts[7]+tri.draw_y, verts[8]+tri.draw_z];
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

fn compute_lighting(point: [f32;3], normal: [f32;3], triangles: &[Render_triangle], lights: &[super::Light_components], ambient: [u8;3]) -> [f32;3] {
    let mut r = ambient[0] as f32 / 255.0;
    let mut g = ambient[1] as f32 / 255.0;
    let mut b = ambient[2] as f32 / 255.0;

    const EPSILON: f32 = 0.01;

    for light in lights {
        let light_pos = [light.light_x, light.light_y, light.light_z];
        let to_light = [light_pos[0] - point[0], light_pos[1] - point[1], light_pos[2] - point[2]];
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

        let transmittance = light_transmittance(point, normal, light_pos, dist, triangles);
        if transmittance <= 0.0 {
            continue;
        }

        let falloff = light.light_distance / (1.0 / EPSILON - 1.0).sqrt();
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

pub fn ray_tracing(start_x: f32, start_y: f32, start_z: f32, length: i128, dir: [f32;3],
                   triangles: &[Render_triangle], textures: &HashMap<String, RgbaImage>,
                   dynamic_lights: &[super::Light_components], all_lights: &[super::Light_components],
                   ambient_light: [u8;3]) -> super::Pixel_structure {
    let origin = [start_x, start_y, start_z];
    let max_t = length as f32;
    let mut best_t = max_t;
    let mut best_pixel = super::Empty_pixel.lock().unwrap().clone();
    let mut best_alpha = 0.0;
    
    for rt in triangles {
        let tri = &rt.triangle;
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        if let Some((t, u, v)) = ray_triangle_intersect(&origin, &dir, &v0, &v1, &v2) {
            if t > 0.0 && t < best_t {
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

                let edge1 = [v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]];
                let edge2 = [v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]];
                let normal = normalize(cross(&edge1, &edge2));
                let hit_point = [origin[0] + dir[0]*t, origin[1] + dir[1]*t, origin[2] + dir[2]*t];
                let light_factor = if let Some(baked) = rt.baked_light {
                    let dynamic_contrib = compute_lighting(hit_point, normal, triangles, dynamic_lights, [0,0,0]);
                    let amb = [ambient_light[0] as f32/255.0, ambient_light[1] as f32/255.0, ambient_light[2] as f32/255.0];
                    [
                        (amb[0] + baked[0] + dynamic_contrib[0]).min(1.0),
                        (amb[1] + baked[1] + dynamic_contrib[1]).min(1.0),
                        (amb[2] + baked[2] + dynamic_contrib[2]).min(1.0),
                    ]
                } else {
                    compute_lighting(hit_point, normal, triangles, all_lights, ambient_light)
                };
                let lit_r = (r as f32 * light_factor[0]).round() as u8;
                let lit_g = (g as f32 * light_factor[1]).round() as u8;
                let lit_b = (b as f32 * light_factor[2]).round() as u8;

                let alpha = a;
                let fg = [lit_r, lit_g, lit_b, 255];
                if alpha >= 1.0 {
                    best_t = t;
                    best_pixel = super::Pixel_structure {
                        pixel_symbol: tri.draw_symbol,
                        pixel_RGBA_color: fg,
                    };
                    best_alpha = 1.0;
                } else if alpha > 0.0 {
                    let bg = &best_pixel.pixel_RGBA_color;
                    let mut blended = [0u8; 4];
                    for i in 0..3 {
                        blended[i] = ((bg[i] as f32 * (1.0 - alpha)) + (fg[i] as f32 * alpha)) as u8;
                    }
                    blended[3] = 255;
                    best_t = t;
                    best_pixel = super::Pixel_structure {
                        pixel_symbol: tri.draw_symbol,
                        pixel_RGBA_color: blended,
                    };
                    best_alpha += alpha * (1.0 - best_alpha);
                }
            }
        }
    }
    best_pixel
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

    let basis = camera_basis(pitch, yaw, roll);

    let baked_static = Baked_static_triangles.lock().unwrap().clone();
    let mut triangles: Vec<Render_triangle> = Vec::with_capacity(dynamic_triangles.len() + baked_static.len());
    for t in dynamic_triangles {
        triangles.push(Render_triangle { triangle: t.clone(), baked_light: None });
    }
    triangles.extend(baked_static);

    let static_lights = super::Static_light.lock().unwrap().clone();
    let mut all_lights: Vec<super::Light_components> = static_lights;
    all_lights.extend(dynamic_lights.iter().cloned());

    let mut texture_map = HashMap::new();
    for rt in &triangles {
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
        
        let triangles = triangles.to_vec();
        let dynamic_lights_local = dynamic_lights.to_vec();
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
                        basis.forward[0] + basis.right[0]*px + basis.up[0]*py,
                        basis.forward[1] + basis.right[1]*px + basis.up[1]*py,
                        basis.forward[2] + basis.right[2]*px + basis.up[2]*py,
                    ]);
                    let pixel = ray_tracing(ox, oy, oz, max_dist, dir, &triangles, &texture_map, &dynamic_lights_local, &all_lights_local, ambient_light);
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

fn Bake_scene(){
    let static_scene = super::Static_scene.lock().unwrap();

    let mut static_triangles: Vec<super::Draw_components> = Vec::new();
    for object in static_scene.iter(){
        if object.draw_type == "3d_object".to_string(){
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
                static_triangles.push(tri);
            }
        }
    }
    drop(static_scene);

    let static_lights = super::Static_light.lock().unwrap().clone();

    let occluders: Vec<Render_triangle> = static_triangles.iter().cloned()
        .map(|t| Render_triangle { triangle: t, baked_light: None })
        .collect();

    let mut baked_result: Vec<Render_triangle> = Vec::with_capacity(static_triangles.len());
    for tri in &static_triangles {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 { continue; }
        let v0 = [verts[0]+tri.draw_x, verts[1]+tri.draw_y, verts[2]+tri.draw_z];
        let v1 = [verts[3]+tri.draw_x, verts[4]+tri.draw_y, verts[5]+tri.draw_z];
        let v2 = [verts[6]+tri.draw_x, verts[7]+tri.draw_y, verts[8]+tri.draw_z];

        let edge1 = [v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]];
        let edge2 = [v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]];
        let normal = normalize(cross(&edge1, &edge2));
        let centroid = [(v0[0]+v1[0]+v2[0])/3.0, (v0[1]+v1[1]+v2[1])/3.0, (v0[2]+v1[2]+v2[2])/3.0];

        let baked_light = compute_lighting(centroid, normal, &occluders, &static_lights, [0,0,0]);
        baked_result.push(Render_triangle { triangle: tri.clone(), baked_light: Some(baked_light) });
    }

    let mut store = Baked_static_triangles.lock().unwrap();
    *store = baked_result;
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
        // if object.draw_type == "3d_object".to_string(){
        //     queue_3d.push(object.clone());
        // }
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