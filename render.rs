use super::{Screen,Draw_components,Static_scene};
use super::{tick_system, Engine_settings, gltf_parser};
use std::sync::Arc;
use std::collections::HashMap;

use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

pub static RENDER_THREADS: AtomicUsize = AtomicUsize::new(1);

pub static BSP_ROOT: Mutex<Option<Box<BspNode>>> = Mutex::new(None);

use std::sync::LazyLock;

pub struct Texture {
    pub width: usize,
    pub height: usize,
    pub pixels: Vec<[u8; 4]>
}

pub static TEXTURE_CACHE: LazyLock<Mutex<HashMap<String, Arc<Texture>>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

fn load_texture(path: &str) -> Option<Arc<Texture>> {
    let mut cache = TEXTURE_CACHE.lock().unwrap();
    if let Some(t) = cache.get(path) { return Some(Arc::clone(t)); }

    let img = image::open(path).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    let tex = Arc::new(Texture {
        width: w as usize,
        height: h as usize,
        pixels: img.pixels().map(|p| p.0).collect(),
    });
    cache.insert(path.to_string(), Arc::clone(&tex));
    Some(tex)
}

#[derive(Clone)]
pub struct render_triangle {
    pub vertices: [f32; 9],
    pub uvs:      [f32; 6],
    pub texture:  Option<Arc<Texture>>,
    pub RGBA_color: [u8; 4],
    pub alpha_test: u8,
    pub properties: Arc<HashMap<String, String>>
}

fn parse_floats(s: &str) -> Vec<f32> {
    s.split(|c: char| c == ',' || c.is_whitespace())
        .filter_map(|x| x.parse().ok())
        .collect()
}

pub fn triangulation(objects: Vec<Draw_components>) -> Vec<render_triangle> {
    let mut triangles = Vec::new();
    for obj in &objects {
        let props = Arc::new(obj.properties.clone());
        let uvs = props.get("uv").map(|s| parse_floats(s)).unwrap_or_default();
        let tex = props.get("texture")
            .and_then(|p| load_texture(p));
        let alpha_test = props.get("alpha_test")
            .and_then(|s| s.parse().ok())
            .unwrap_or(0u8);

        for (tri_i, vertex) in (0..obj.vertices.len()).step_by(9).enumerate() {
            let uv_off = tri_i * 6;
            let tri_uvs = if uvs.len() >= uv_off + 6 {
                [uvs[uv_off],   uvs[uv_off+1],
                 uvs[uv_off+2], uvs[uv_off+3],
                 uvs[uv_off+4], uvs[uv_off+5]]
            } else {
                [0.0; 6]
            };

            triangles.push(render_triangle {
                vertices: [
                    obj.vertices[vertex]   + obj.x, obj.vertices[vertex+1] + obj.y, obj.vertices[vertex+2] + obj.z,
                    obj.vertices[vertex+3] + obj.x, obj.vertices[vertex+4] + obj.y, obj.vertices[vertex+5] + obj.z,
                    obj.vertices[vertex+6] + obj.x, obj.vertices[vertex+7] + obj.y, obj.vertices[vertex+8] + obj.z,
                ],
                uvs: tri_uvs,
                texture: tex.clone(),
                RGBA_color: obj.RGBA_color,
                alpha_test,
                properties: Arc::clone(&props),
            });
        }
    }
    triangles
}

#[derive(Clone, Copy)]
pub struct Plane{
    pub point: [f32; 3],
    pub normal: [f32; 3]
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0]-b[0], a[1]-b[1], a[2]-b[2]]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    ]
}

fn plane_from_triangle(tri: &render_triangle) -> Option<Plane>{
    let p0 = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
    let p1 = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
    let p2 = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];

    let e1 = sub(p1, p0);
    let e2 = sub(p2, p0);

    let n_raw = cross(e1, e2);

    let len_sq = dot(n_raw, n_raw);
    if len_sq < 1e-12 {
        return None;
    }

    let len = len_sq.sqrt();
    let normal = [n_raw[0] / len, n_raw[1] / len, n_raw[2] / len];

    Some(Plane { point: p0, normal })
}

fn signed_distance(point: [f32; 3], plane: &Plane) -> f32{
    dot(sub(point, plane.point), plane.normal)
}

#[derive(Clone)]
enum TriClass {
    Front,
    Back,
    Coplanar,
    Spanning
}

fn classify(tri: &render_triangle, plane: &Plane, eps: f32) -> TriClass {
    let mut front = 0;
    let mut back  = 0;

    for i in [0, 3, 6] {
        let v = [tri.vertices[i], tri.vertices[i+1], tri.vertices[i+2]];
        let d = signed_distance(v, plane);
        if d > eps {
            front += 1;
        } else if d < -eps {
            back += 1;
        }
    }

    if front == 0 && back == 0 {TriClass::Coplanar }
    else if front == 0 {TriClass::Back }
    else if back  == 0 {TriClass::Front }
    else {TriClass::Spanning }
}

#[derive(Clone, Copy)]
pub struct SplitStats {
    pub splits:   usize,
    pub front:    usize,
    pub back:     usize,
    pub coplanar: usize,
    pub score:    f32
}

fn evaluate(
    plane: &Plane,
    tris: &[render_triangle],
    eps: f32,
    split_weight: f32,
) -> SplitStats {
    let mut splits   = 0;
    let mut front    = 0;
    let mut back     = 0;
    let mut coplanar = 0;

    for t in tris {
        match classify(t, plane, eps) {
            TriClass::Front    => front    += 1,
            TriClass::Back     => back     += 1,
            TriClass::Coplanar => coplanar += 1,
            TriClass::Spanning => splits   += 1,
        }
    }

    let balance = (front as i64 - back as i64).unsigned_abs() as f32;
    let score = splits as f32 * split_weight + balance;

    SplitStats { splits, front, back, coplanar, score }
}

// !!!ОБЯЗАТЕЛЬНО ПРОТЕСТИРОВАТЬ ЗНАЧЕНИЯ(возможно сделать динамическими в зависимости от сцены)!!!
const EPS: f32 = 1e-4;
const SPLIT_WEIGHT: f32 = 5.0;
const MIN_LEAF: usize = 8;
const FULL_SCAN_LIMIT: usize = 100;

pub fn choose_split_plane(tris: &[render_triangle]) -> Option<Plane> {
    if tris.len() < MIN_LEAF {
        return None;
    }

    let step = if tris.len() < FULL_SCAN_LIMIT {
        1
    } else {
        tris.len() / FULL_SCAN_LIMIT
    };

    let mut best_plane: Option<Plane> = None;
    let mut best_score = f32::INFINITY;

    for (i, tri) in tris.iter().enumerate() {
        if i % step != 0 { continue; }

        let plane = match plane_from_triangle(tri) {
            Some(p) => p,
            None => continue, 
        };

        let stats = evaluate(&plane, tris, EPS, SPLIT_WEIGHT);

        if stats.splits * 2 > tris.len() { continue; }

        if stats.score < best_score {
            best_score = stats.score;
            best_plane = Some(plane);
        }
    }

    if let Some(plane) = best_plane {
        let stats = evaluate(&plane, tris, EPS, SPLIT_WEIGHT);
        if stats.front == 0 || stats.back == 0 {
            return None;
        }
    }

    return best_plane;
}

pub enum BspNode {
    Leaf {
        triangles: Vec<render_triangle>
    },
    Internal {
        plane: Plane,
        coplanar: Vec<render_triangle>,
        front: Box<BspNode>,
        back:  Box<BspNode>
    }
}

pub fn build_bsp(tris: Vec<render_triangle>, depth: u32) -> BspNode {
    //!!!ТАКЖЕ ЭКСПЕРЕМЕНТИРОВАТЬ С ЗНАЧЕНИЕМ, ЛУЧШЕ ВЫВЕСТИ В ENGINE_SETTINGS!!!
    const MAX_DEPTH: u32 = 64;

    if depth >= MAX_DEPTH {
        return BspNode::Leaf { triangles: tris };
    }

    let plane = match choose_split_plane(&tris) {
        Some(p) => p,
        None => return BspNode::Leaf { triangles: tris }
    };

    let mut coplanar = Vec::new();
    let mut front    = Vec::new();
    let mut back     = Vec::new();

    for tri in tris {
        match classify(&tri, &plane, EPS) {
            TriClass::Coplanar => coplanar.push(tri),
            TriClass::Front    => front.push(tri),
            TriClass::Back     => back.push(tri),
            TriClass::Spanning => {
                front.push(tri.clone());
                back.push(tri);
            }
        }
    }

    BspNode::Internal {
        plane,
        coplanar,
        front: Box::new(build_bsp(front, depth + 1)),
        back:  Box::new(build_bsp(back,  depth + 1))
    }
}

pub fn Bake_static_scene() {
    let scene = Static_scene.lock().unwrap().clone();
    let triangles = triangulation(scene);
    let tree = build_bsp(triangles, 0);
    *BSP_ROOT.lock().unwrap() = Some(Box::new(tree));
    println!("[Render] Created BSP tree")
}

pub static Z_BUFFER: Mutex<Vec<Vec<f32>>> = Mutex::new(Vec::new());

const NEAR_PLANE: f32 = 0.001;

const CULL_BACKFACES: bool = false;

const SPRAY_PIXELS_PER_UNIT: f32 = 512.0;
const SPRAY_OFFSET: f32 = 0.01;

#[derive(Clone)]
pub struct spray {
    pub surface: [render_triangle; 2]
}

pub static SPRAYS: Mutex<Vec<spray>> = Mutex::new(Vec::new());

fn normalize(v: [f32; 3]) -> [f32; 3] {
    let len = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
    if len < 1e-12 { return [0.0, 0.0, 0.0]; }
    [v[0] / len, v[1] / len, v[2] / len]
}

fn point_in_triangle(p: [f32; 3], tri: &render_triangle, eps: f32) -> bool {
    let a = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
    let b = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
    let c = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];

    let v0 = sub(c, a);
    let v1 = sub(b, a);
    let v2 = sub(p, a);

    let d00 = dot(v0, v0);
    let d01 = dot(v0, v1);
    let d02 = dot(v0, v2);
    let d11 = dot(v1, v1);
    let d12 = dot(v1, v2);

    let denom = d00 * d11 - d01 * d01;
    if denom.abs() < 1e-12 { return false; }
    let inv = 1.0 / denom;

    let u = (d11 * d02 - d01 * d12) * inv;
    let v = (d00 * d12 - d01 * d02) * inv;

    u >= -eps && v >= -eps && u + v <= 1.0 + eps
}

fn tri_plane_dist_sq(p: [f32; 3], tri: &render_triangle) -> Option<f32> {
    let plane = plane_from_triangle(tri)?;
    let d = signed_distance(p, &plane);
    Some(d * d)
}

fn find_nearest_recursive<'a>(
    node: &'a BspNode,
    pos: [f32; 3],
    best: &mut Option<(&'a render_triangle, f32)>,
) {
    match node {
        BspNode::Leaf { triangles } => {
            for t in triangles {
                if point_in_triangle(pos, t, 1e-3) {
                    if let Some(d_sq) = tri_plane_dist_sq(pos, t) {
                        if best.map_or(true, |(_, bd)| d_sq < bd) {
                            *best = Some((t, d_sq));
                        }
                    }
                }
            }
        }
        BspNode::Internal { plane, coplanar, front, back } => {
            for t in coplanar {
                if point_in_triangle(pos, t, 1e-3) {
                    if let Some(d_sq) = tri_plane_dist_sq(pos, t) {
                        if best.map_or(true, |(_, bd)| d_sq < bd) {
                            *best = Some((t, d_sq));
                        }
                    }
                }
            }
            let d_to_plane = signed_distance(pos, plane);
            let (first, second) = if d_to_plane > 0.0 {
                (back.as_ref(), front.as_ref())
            } else {
                (front.as_ref(), back.as_ref())
            };
            find_nearest_recursive(first, pos, best);
            let curr_best = best.map_or(f32::INFINITY, |(_, d)| d);
            if d_to_plane * d_to_plane < curr_best {
                find_nearest_recursive(second, pos, best);
            }
        }
    }
}

pub fn Add_spray_at(path: String, center_position: [f32; 3], mut n: [f32; 3], RGBA_color: [u8; 4]) {
    let tex = match load_texture(&path) {
        Some(t) => t,
        None => {
            println!("[Render] Spray texture not found: {}", path);
            return;
        }
    };

    let (cam_pos, view_up) = {
        let cam = super::Camera.lock().unwrap();
        let pos = [cam.camera_x, cam.camera_y, cam.camera_z];
        let yaw = cam.camera_yaw.to_radians();
        let pitch = cam.camera_pitch.to_radians();
        let roll = cam.camera_roll.to_radians();

        let forward = [
            pitch.cos() * yaw.sin(),
            pitch.sin(),
            pitch.cos() * yaw.cos(),
        ];
        let right_base = [yaw.cos(), 0.0, -yaw.sin()];
        let up_base = cross(forward, right_base);

        let vu = if roll.abs() > 1e-6 {
            let cr = roll.cos();
            let sr = roll.sin();
            [
                up_base[0] * cr - right_base[0] * sr,
                up_base[1] * cr - right_base[1] * sr,
                up_base[2] * cr - right_base[2] * sr,
            ]
        } else {
            up_base
        };
        (pos, vu)
    };

    if dot(n, sub(cam_pos, center_position)) < 0.0 {
        n = [-n[0], -n[1], -n[2]];
    }

    let mut up_ref = {
        let d = dot(view_up, n);
        sub(view_up, [n[0] * d, n[1] * d, n[2] * d])
    };

    if dot(up_ref, up_ref) < 1e-6 {
        let wu = [0.0, 1.0, 0.0];
        let d = dot(wu, n);
        up_ref = sub(wu, [n[0] * d, n[1] * d, n[2] * d]);

        if dot(up_ref, up_ref) < 1e-6 {
            let wz = [0.0, 0.0, 1.0];
            let d = dot(wz, n);
            up_ref = sub(wz, [n[0] * d, n[1] * d, n[2] * d]);
        }
    }

    let right = normalize(cross(up_ref, n));
    let up = normalize(cross(n, right));

    let cx = center_position[0] + n[0] * SPRAY_OFFSET;
    let cy = center_position[1] + n[1] * SPRAY_OFFSET;
    let cz = center_position[2] + n[2] * SPRAY_OFFSET;

    let half_w = (tex.width as f32 / SPRAY_PIXELS_PER_UNIT) * 0.5;
    let half_h = (tex.height as f32 / SPRAY_PIXELS_PER_UNIT) * 0.5;

    let tl = [cx - right[0]*half_w + up[0]*half_h, cy - right[1]*half_w + up[1]*half_h, cz - right[2]*half_w + up[2]*half_h];
    let tr = [cx + right[0]*half_w + up[0]*half_h, cy + right[1]*half_w + up[1]*half_h, cz + right[2]*half_w + up[2]*half_h];
    let br = [cx + right[0]*half_w - up[0]*half_h, cy + right[1]*half_w - up[1]*half_h, cz + right[2]*half_w - up[2]*half_h];
    let bl = [cx - right[0]*half_w - up[0]*half_h, cy - right[1]*half_w - up[1]*half_h, cz - right[2]*half_w - up[2]*half_h];

    let empty_props = Arc::new(HashMap::new());

    let tri1 = render_triangle {
        vertices: [
            tl[0], tl[1], tl[2],
            tr[0], tr[1], tr[2],
            br[0], br[1], br[2],
        ],
        uvs: [0.0, 0.0, 1.0, 0.0, 1.0, 1.0],
        texture: Some(Arc::clone(&tex)),
        RGBA_color,
        alpha_test: 0,
        properties: Arc::clone(&empty_props),
    };

    let tri2 = render_triangle {
        vertices: [
            tl[0], tl[1], tl[2],
            br[0], br[1], br[2],
            bl[0], bl[1], bl[2],
        ],
        uvs: [0.0, 0.0, 1.0, 1.0, 0.0, 1.0],
        texture: Some(Arc::clone(&tex)),
        RGBA_color,
        alpha_test: 0,
        properties: Arc::clone(&empty_props),
    };

    let mut sprays = SPRAYS.lock().unwrap();
    sprays.push(spray { surface: [tri1, tri2] });
}

pub fn Add_spray(path: String, center_position: [f32; 3], RGBA_color: [u8; 4]) {
    if load_texture(&path).is_none() {
        println!("[Render] Spray texture not found: {}", path);
        return;
    }

    let n_opt = {
        let bsp_guard = BSP_ROOT.lock().unwrap();
        if let Some(root) = bsp_guard.as_ref() {
            let mut best: Option<(&render_triangle, f32)> = None;
            find_nearest_recursive(root, center_position, &mut best);
            best.and_then(|(tri, _)| plane_from_triangle(tri)).map(|p| p.normal)
        } else {
            None
        }
    };

    let n = match n_opt {
        Some(n) => n,
        None => {
            let triangles = prepare_dynamic_triangles();
            let mut found = None;
            for tri in &triangles {
                if point_in_triangle(center_position, tri, 1e-3) {
                    if let Some(p) = plane_from_triangle(tri) {
                        found = Some(p.normal);
                        break;
                    }
                }
            }
            match found {
                Some(n) => n,
                None => {
                    println!("[Render] Add_spray: no surface at position");
                    return;
                }
            }
        }
    };

    Add_spray_at(path, center_position, n, RGBA_color);
}

pub static SKELETON_REGISTRY: LazyLock<Mutex<HashMap<u64, Arc<gltf_parser::Skeleton_data>>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

pub static NEXT_SKELETON_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

#[derive(Clone)]
pub struct Dynamic_components {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub vertices: Vec<f32>,
    pub RGBA_color: [u8; 4],
    pub animations_in_play: Vec<String>,
    pub properties: HashMap<String, String>,
    pub special_name: String
}

pub static Draw_queue: Mutex<Vec<Dynamic_components>> = Mutex::new(Vec::new());

fn parse_joint_indices_str(s: &str) -> Vec<[u8; 4]> {
    s.split(';').filter_map(|entry| {
        let parts: Vec<u8> = entry.split(',').filter_map(|p| p.parse().ok()).collect();
        if parts.len() == 4 { Some([parts[0], parts[1], parts[2], parts[3]]) } else { None }
    }).collect()
}

fn parse_joint_weights_str(s: &str) -> Vec<[f32; 4]> {
    s.split(';').filter_map(|entry| {
        let parts: Vec<f32> = entry.split(',').filter_map(|p| p.parse().ok()).collect();
        if parts.len() == 4 { Some([parts[0], parts[1], parts[2], parts[3]]) } else { None }
    }).collect()
}

fn parse_translation_offsets_str(s: &str) -> HashMap<usize, [f32; 3]> {
    s.split(';').filter_map(|entry| {
        let (idx_str, vals_str) = entry.split_once(':')?;
        let idx: usize = idx_str.parse().ok()?;
        let vals: Vec<f32> = vals_str.split(',').filter_map(|p| p.parse().ok()).collect();
        if vals.len() == 3 { Some((idx, [vals[0], vals[1], vals[2]])) } else { None }
    }).collect()
}

fn parse_rotation_offsets_str(s: &str) -> HashMap<usize, [f32; 4]> {
    s.split(';').filter_map(|entry| {
        let (idx_str, vals_str) = entry.split_once(':')?;
        let idx: usize = idx_str.parse().ok()?;
        let vals: Vec<f32> = vals_str.split(',').filter_map(|p| p.parse().ok()).collect();
        if vals.len() == 4 { Some((idx, [vals[0], vals[1], vals[2], vals[3]])) } else { None }
    }).collect()
}

fn serialize_translation_offsets_str(m: &HashMap<usize, [f32; 3]>) -> String {
    m.iter().map(|(k, v)| format!("{}:{},{},{}", k, v[0], v[1], v[2])).collect::<Vec<_>>().join(";")
}

fn serialize_rotation_offsets_str(m: &HashMap<usize, [f32; 4]>) -> String {
    m.iter().map(|(k, v)| format!("{}:{},{},{},{}", k, v[0], v[1], v[2], v[3])).collect::<Vec<_>>().join(";")
}

fn compute_bone_world(
    i: usize,
    parents: &[Option<usize>],
    local: &[[[f32; 4]; 4]],
    world: &mut [[[f32; 4]; 4]],
    computed: &mut [bool],
) {
    if computed[i] { return; }
    match parents[i] {
        Some(p) => {
            compute_bone_world(p, parents, local, world, computed);
            world[i] = gltf_parser::mat4_mul(world[p], local[i]);
        }
        None => {
            world[i] = local[i];
        }
    }
    computed[i] = true;
}

impl Dynamic_components {
    pub fn new(special_name: String) -> Self {
        let mut properties = HashMap::new();
        properties.insert("animation_speed".to_string(), "1.0".to_string());
        properties.insert("animation_start_tick".to_string(), "0".to_string());
        Self {
            x: 0.0,
            y: 0.0,
            z: 0.0,
            vertices: Vec::new(),
            RGBA_color: [255, 255, 255, 255],
            animations_in_play: Vec::new(),
            properties,
            special_name,
        }
    }

    pub fn Attach_skeleton(&mut self, skel: Arc<gltf_parser::Skeleton_data>) -> u64 {
        let id = NEXT_SKELETON_ID.fetch_add(1, Ordering::Relaxed);
        SKELETON_REGISTRY.lock().unwrap().insert(id, skel);
        self.properties.insert("skeleton_id".to_string(), id.to_string());
        id
    }

    pub fn Load_from_import(&mut self, result: &gltf_parser::Import_result) -> u64 {
        self.vertices = result.vertices.clone();
        self.properties.insert("uv".to_string(), result.uv.clone());
        self.properties.insert("texture".to_string(), result.texture_path.clone());

        let ji: String = result.joint_indices.iter()
            .map(|j| format!("{},{},{},{}", j[0], j[1], j[2], j[3]))
            .collect::<Vec<_>>().join(";");
        self.properties.insert("joint_indices".to_string(), ji);

        let jw: String = result.joint_weights.iter()
            .map(|w| format!("{},{},{},{}", w[0], w[1], w[2], w[3]))
            .collect::<Vec<_>>().join(";");
        self.properties.insert("joint_weights".to_string(), jw);

        let id = NEXT_SKELETON_ID.fetch_add(1, Ordering::Relaxed);
        SKELETON_REGISTRY.lock().unwrap().insert(id, Arc::clone(&result.skeleton));
        self.properties.insert("skeleton_id".to_string(), id.to_string());
        id
    }

    pub fn Get_skeleton(&self) -> Option<Arc<gltf_parser::Skeleton_data>> {
        let id_str = self.properties.get("skeleton_id")?;
        let id: u64 = id_str.parse().ok()?;
        SKELETON_REGISTRY.lock().unwrap().get(&id).cloned()
    }

    fn get_animation_start_tick(&self) -> u128 {
        self.properties.get("animation_start_tick").and_then(|s| s.parse().ok()).unwrap_or(0)
    }

    fn get_animation_speed(&self) -> f32 {
        self.properties.get("animation_speed").and_then(|s| s.parse().ok()).unwrap_or(1.0)
    }

    fn get_uvs(&self) -> Vec<f32> {
        self.properties.get("uv").map(|s| parse_floats(s)).unwrap_or_default()
    }

    fn get_joint_indices(&self) -> Vec<[u8; 4]> {
        self.properties.get("joint_indices").map(|s| parse_joint_indices_str(s)).unwrap_or_default()
    }

    fn get_joint_weights(&self) -> Vec<[f32; 4]> {
        self.properties.get("joint_weights").map(|s| parse_joint_weights_str(s)).unwrap_or_default()
    }

    fn get_translation_offsets(&self) -> HashMap<usize, [f32; 3]> {
        self.properties.get("bone_translation_offsets")
            .map(|s| parse_translation_offsets_str(s))
            .unwrap_or_default()
    }

    fn get_rotation_offsets(&self) -> HashMap<usize, [f32; 4]> {
        self.properties.get("bone_rotation_offsets")
            .map(|s| parse_rotation_offsets_str(s))
            .unwrap_or_default()
    }

    pub fn Play_animation(&mut self, name: String) {
        let skel = match self.Get_skeleton() { Some(s) => s, None => return };
        if !skel.animations.contains_key(&name) { return; }
        if self.animations_in_play.contains(&name) { return; }
        if self.animations_in_play.is_empty() {
            let tick = tick_system::Get_current_tick();
            self.properties.insert("animation_start_tick".to_string(), tick.to_string());
        }
        self.animations_in_play.push(name);
    }

    pub fn Set_default_animation(&mut self, name: String) {
        let skel = match self.Get_skeleton() { Some(s) => s, None => return };
        if skel.animations.contains_key(&name) {
            self.properties.insert("default_animation".to_string(), name);
            if self.animations_in_play.is_empty() {
                let tick = tick_system::Get_current_tick();
                self.properties.insert("animation_start_tick".to_string(), tick.to_string());
            }
        }
    }

    pub fn Stop_all_animations(&mut self) {
        self.animations_in_play.clear();
    }

    pub fn Set_x(&mut self, delta: f32) { self.x += delta; }
    pub fn Set_y(&mut self, delta: f32) { self.y += delta; }
    pub fn Set_z(&mut self, delta: f32) { self.z += delta; }

    pub fn Set_R(&mut self, delta: i16) { self.RGBA_color[0] = (self.RGBA_color[0] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_G(&mut self, delta: i16) { self.RGBA_color[1] = (self.RGBA_color[1] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_B(&mut self, delta: i16) { self.RGBA_color[2] = (self.RGBA_color[2] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_A(&mut self, delta: i16) { self.RGBA_color[3] = (self.RGBA_color[3] as i16 + delta).clamp(0, 255) as u8; }

    pub fn Set_animation_speed(&mut self, delta: f32) {
        let cur = self.get_animation_speed();
        self.properties.insert("animation_speed".to_string(), (cur + delta).max(0.0).to_string());
    }

    pub fn Set_bone_translation(&mut self, bone_idx: usize, dx: f32, dy: f32, dz: f32) {
        let mut m = self.get_translation_offsets();
        let e = m.entry(bone_idx).or_insert([0.0; 3]);
        e[0] += dx; e[1] += dy; e[2] += dz;
        self.properties.insert("bone_translation_offsets".to_string(), serialize_translation_offsets_str(&m));
    }

    pub fn Set_bone_rotation(&mut self, bone_idx: usize, dx: f32, dy: f32, dz: f32) {
        let hx = (dx * 0.5).sin(); let cx = (dx * 0.5).cos();
        let hy = (dy * 0.5).sin(); let cy = (dy * 0.5).cos();
        let hz = (dz * 0.5).sin(); let cz = (dz * 0.5).cos();
        let q = [
            hx * cy * cz - cx * hy * hz,
            cx * hy * cz + hx * cy * hz,
            cx * cy * hz - hx * hy * cz,
            cx * cy * cz + hx * hy * hz,
        ];
        let mut m = self.get_rotation_offsets();
        let e = m.entry(bone_idx).or_insert([0.0, 0.0, 0.0, 1.0]);
        *e = gltf_parser::quat_mul(*e, q);
        self.properties.insert("bone_rotation_offsets".to_string(), serialize_rotation_offsets_str(&m));
    }

    pub fn Get_x(&self) -> f32 { self.x }
    pub fn Get_y(&self) -> f32 { self.y }
    pub fn Get_z(&self) -> f32 { self.z }
    pub fn Get_R(&self) -> u8 { self.RGBA_color[0] }
    pub fn Get_G(&self) -> u8 { self.RGBA_color[1] }
    pub fn Get_B(&self) -> u8 { self.RGBA_color[2] }
    pub fn Get_A(&self) -> u8 { self.RGBA_color[3] }
    pub fn Get_animation_speed(&self) -> f32 { self.get_animation_speed() }

    pub fn Get_current_animation_time(&self) -> f32 {
        let tick_ms = Engine_settings.lock().unwrap().tick_speed.max(1) as f32 / 1000.0;
        self.Get_current_animation_time_at(tick_system::Get_current_tick(), tick_ms)
    }

    pub fn Get_current_animation_time_at(&self, current_tick: u128, tick_ms: f32) -> f32 {
        let anim_name: String = match self.animations_in_play.first()
            .or(self.properties.get("default_animation")) {
            Some(n) => n.clone(),
            None => return 0.0,
        };
        let skel = match self.Get_skeleton() { Some(s) => s, None => return 0.0 };
        let anim = match skel.animations.get(&anim_name) { Some(a) => a, None => return 0.0 };
        let elapsed = current_tick.saturating_sub(self.get_animation_start_tick()) as f32
            * tick_ms * self.get_animation_speed();
        if anim.duration > 0.0 { elapsed % anim.duration } else { 0.0 }
    }

    pub fn Get_bone_names(&self) -> Vec<String> {
        self.Get_skeleton().map(|s| s.bones.iter().map(|b| b.name.clone()).collect()).unwrap_or_default()
    }

    pub fn Get_animation_names(&self) -> Vec<String> {
        self.Get_skeleton().map(|s| s.animations.keys().cloned().collect()).unwrap_or_default()
    }

    pub fn Get_bone_index(&self, bone_name: &str) -> Option<usize> {
        self.Get_skeleton().and_then(|s| s.bones.iter().position(|b| b.name == bone_name))
    }

    pub fn Get_bone_local_translation(&self, bone_name: &str) -> Option<[f32; 3]> {
        let skel = self.Get_skeleton()?;
        let idx = skel.bones.iter().position(|b| b.name == bone_name)?;
        let bone = &skel.bones[idx];
        let time = self.Get_current_animation_time();
        let anim_name: Option<String> = self.animations_in_play.first()
            .or(self.properties.get("default_animation")).cloned();
        let base = if let Some(name) = anim_name {
            if let Some(anim) = skel.animations.get(&name) {
                if let Some(tr) = anim.tracks.get(&idx) {
                    gltf_parser::eval_translation(&tr.translation, time, bone.translation)
                } else { bone.translation }
            } else { bone.translation }
        } else { bone.translation };
        let off = self.get_translation_offsets().get(&idx).copied().unwrap_or([0.0; 3]);
        Some([base[0] + off[0], base[1] + off[1], base[2] + off[2]])
    }

    pub fn Get_bone_local_rotation(&self, bone_name: &str) -> Option<[f32; 4]> {
        let skel = self.Get_skeleton()?;
        let idx = skel.bones.iter().position(|b| b.name == bone_name)?;
        let bone = &skel.bones[idx];
        let time = self.Get_current_animation_time();
        let anim_name: Option<String> = self.animations_in_play.first()
            .or(self.properties.get("default_animation")).cloned();
        let base = if let Some(name) = anim_name {
            if let Some(anim) = skel.animations.get(&name) {
                if let Some(tr) = anim.tracks.get(&idx) {
                    gltf_parser::eval_rotation(&tr.rotation, time, bone.rotation)
                } else { bone.rotation }
            } else { bone.rotation }
        } else { bone.rotation };
        if let Some(off) = self.get_rotation_offsets().get(&idx) {
            Some(gltf_parser::quat_mul(base, *off))
        } else {
            Some(base)
        }
    }

    pub fn compute_pose(&self) -> (Vec<[[f32; 4]; 4]>, Vec<[[f32; 4]; 4]>) {
        let tick_ms = Engine_settings.lock().unwrap().tick_speed.max(1) as f32 / 1000.0;
        self.compute_pose_at(tick_system::Get_current_tick(), tick_ms)
    }

    pub fn compute_pose_at(&self, current_tick: u128, tick_ms: f32) -> (Vec<[[f32; 4]; 4]>, Vec<[[f32; 4]; 4]>) {
        let skel = match self.Get_skeleton() { Some(s) => s, None => return (Vec::new(), Vec::new()) };
        let n = skel.bones.len();
        if n == 0 { return (Vec::new(), Vec::new()); }

        let trans_offsets = self.get_translation_offsets();
        let rot_offsets = self.get_rotation_offsets();
        let start_tick = self.get_animation_start_tick();
        let speed = self.get_animation_speed();

        let mut active_names: Vec<String> = self.animations_in_play.clone();
        if active_names.is_empty() {
            if let Some(d) = self.properties.get("default_animation") {
                active_names.push(d.clone());
            }
        }

        let times: Vec<f32> = active_names.iter().map(|name| {
            if let Some(anim) = skel.animations.get(name) {
                let elapsed = current_tick.saturating_sub(start_tick) as f32 * tick_ms * speed;
                if anim.duration > 0.0 { elapsed % anim.duration } else { 0.0 }
            } else { 0.0 }
        }).collect();

        let mut local = vec![gltf_parser::mat4_identity(); n];

        for i in 0..n {
            let bone = &skel.bones[i];

            let mut sum_t = [0.0f32; 3];
            let mut sum_s = [0.0f32; 3];
            let mut weight_count = 0usize;
            let mut base_r = bone.rotation;

            for (k, name) in active_names.iter().enumerate() {
                let anim = match skel.animations.get(name) { Some(a) => a, None => continue };
                let at = times[k];
                if let Some(tr) = anim.tracks.get(&i) {
                    let t = gltf_parser::eval_translation(&tr.translation, at, bone.translation);
                    let s = gltf_parser::eval_scale(&tr.scale, at, bone.scale);
                    let r = gltf_parser::eval_rotation(&tr.rotation, at, bone.rotation);
                    sum_t[0] += t[0]; sum_t[1] += t[1]; sum_t[2] += t[2];
                    sum_s[0] += s[0]; sum_s[1] += s[1]; sum_s[2] += s[2];
                    if weight_count == 0 {
                        base_r = r;
                    } else {
                        base_r = gltf_parser::quat_slerp(base_r, r, 1.0 / (weight_count as f32 + 1.0));
                    }
                    weight_count += 1;
                }
            }

            let (mut t, mut r, s) = if weight_count > 0 {
                let wc = weight_count as f32;
                (
                    [sum_t[0] / wc, sum_t[1] / wc, sum_t[2] / wc],
                    base_r,
                    [sum_s[0] / wc, sum_s[1] / wc, sum_s[2] / wc],
                )
            } else {
                (bone.translation, bone.rotation, bone.scale)
            };

            if let Some(off) = trans_offsets.get(&i) {
                t[0] += off[0]; t[1] += off[1]; t[2] += off[2];
            }
            if let Some(off) = rot_offsets.get(&i) {
                r = gltf_parser::quat_mul(r, *off);
            }

            local[i] = gltf_parser::trs_to_matrix(t, r, s);
        }

        let parents: Vec<Option<usize>> = skel.bones.iter().map(|b| b.parent).collect();
        let mut world = vec![gltf_parser::mat4_identity(); n];
        let mut computed = vec![false; n];
        for i in 0..n {
            compute_bone_world(i, &parents, &local, &mut world, &mut computed);
        }

        (local, world)
    }

    pub fn Get_bone_world_transform(&self, bone_name: &str) -> Option<[[f32; 4]; 4]> {
        let skel = self.Get_skeleton()?;
        let idx = skel.bones.iter().position(|b| b.name == bone_name)?;
        let (_, world) = self.compute_pose();
        world.get(idx).copied()
    }

    pub fn Get_bone_world_position(&self, bone_name: &str) -> Option<[f32; 3]> {
        let m = self.Get_bone_world_transform(bone_name)?;
        Some([m[0][3] + self.x, m[1][3] + self.y, m[2][3] + self.z])
    }

    pub fn Get_bone_world_rotation(&self, bone_name: &str) -> Option<[f32; 4]> {
        let m = self.Get_bone_world_transform(bone_name)?;
        let trace = m[0][0] + m[1][1] + m[2][2];
        if trace > 0.0 {
            let s = (trace + 1.0).sqrt() * 2.0;
            Some([
                (m[2][1] - m[1][2]) / s,
                (m[0][2] - m[2][0]) / s,
                (m[1][0] - m[0][1]) / s,
                0.25 * s,
            ])
        } else if m[0][0] > m[1][1] && m[0][0] > m[2][2] {
            let s = (1.0 + m[0][0] - m[1][1] - m[2][2]).sqrt() * 2.0;
            Some([
                0.25 * s,
                (m[0][1] + m[1][0]) / s,
                (m[0][2] + m[2][0]) / s,
                (m[2][1] - m[1][2]) / s,
            ])
        } else if m[1][1] > m[2][2] {
            let s = (1.0 + m[1][1] - m[0][0] - m[2][2]).sqrt() * 2.0;
            Some([
                (m[0][1] + m[1][0]) / s,
                0.25 * s,
                (m[1][2] + m[2][1]) / s,
                (m[0][2] - m[2][0]) / s,
            ])
        } else {
            let s = (1.0 + m[2][2] - m[0][0] - m[1][1]).sqrt() * 2.0;
            Some([
                (m[0][2] + m[2][0]) / s,
                (m[1][2] + m[2][1]) / s,
                0.25 * s,
                (m[1][0] - m[0][1]) / s,
            ])
        }
    }
}

struct CameraBasis {
    right: [f32; 3],
    up: [f32; 3],
    forward: [f32; 3],
    position: [f32; 3],
}

fn compute_camera_basis() -> CameraBasis {
    let cam = super::Camera.lock().unwrap();
    let position = [cam.camera_x, cam.camera_y, cam.camera_z];
    let yaw = cam.camera_yaw.to_radians();
    let pitch = cam.camera_pitch.to_radians();
    let roll = cam.camera_roll.to_radians();
    drop(cam);

    let forward = [
        pitch.cos() * yaw.sin(),
        pitch.sin(),
        pitch.cos() * yaw.cos(),
    ];

    let right_base = [yaw.cos(), 0.0, -yaw.sin()];
    let up_base = cross(forward, right_base);

    let (right, up) = if roll.abs() > 1e-6 {
        let cr = roll.cos();
        let sr = roll.sin();
        (
            [
                right_base[0] * cr + up_base[0] * sr,
                right_base[1] * cr + up_base[1] * sr,
                right_base[2] * cr + up_base[2] * sr,
            ],
            [
                up_base[0] * cr - right_base[0] * sr,
                up_base[1] * cr - right_base[1] * sr,
                up_base[2] * cr - right_base[2] * sr,
            ],
        )
    } else {
        (right_base, up_base)
    };

    CameraBasis { right, up, forward, position }
}

fn to_view_space(p: [f32; 3], basis: &CameraBasis) -> [f32; 3] {
    let v = sub(p, basis.position);
    [dot(v, basis.right), dot(v, basis.up), dot(v, basis.forward)]
}

fn intersect_near(
    a: [f32; 3], b: [f32; 3],
    uva: [f32; 2], uvb: [f32; 2],
    near: f32,
) -> ([f32; 3], [f32; 2]) {
    let t = (near - a[2]) / (b[2] - a[2]);
    (
        [a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), near],
        [uva[0] + t * (uvb[0] - uva[0]), uva[1] + t * (uvb[1] - uva[1])],
    )
}

fn clip_near(
    tri: &[([f32; 3], [f32; 2]); 3],
    near: f32,
) -> Vec<([f32; 3], [f32; 2])> {
    let mut out = Vec::with_capacity(4);
    for i in 0..3 {
        let (a, uva) = tri[i];
        let (b, uvb) = tri[(i + 1) % 3];
        let a_in = a[2] > near;
        let b_in = b[2] > near;
        if b_in {
            if !a_in {
                out.push(intersect_near(a, b, uva, uvb, near));
            }
            out.push((b, uvb));
        } else if a_in {
            out.push(intersect_near(a, b, uva, uvb, near));
        }
    }
    out
}

fn project(v: [f32; 3], focal: f32, width: f32, height: f32) -> ([f32; 2], f32) {
    let inv_z = 1.0 / v[2];
    (
        [
            width * 0.5 + v[0] * inv_z * focal,
            height * 0.5 - v[1] * inv_z * focal, // минус: ось Y на экране вниз
        ],
        inv_z,
    )
}

#[allow(clippy::too_many_arguments)]
fn rasterize_triangle(
    v0: [f32; 3], v1: [f32; 3], v2: [f32; 3],
    uv0: [f32; 2], uv1: [f32; 2], uv2: [f32; 2],
    texture: Option<&Texture>,
    focal: f32, width: usize, height: usize,
    row_offset: usize,
    screen_chunk: &mut [Vec<super::Pixel_structure>],
    zbuf_chunk: &mut [Vec<f32>],
    rgba: [u8; 4],
    alpha_test: u8,
) {
    let (s0, iz0) = project(v0, focal, width as f32, height as f32);
    let (s1, iz1) = project(v1, focal, width as f32, height as f32);
    let (s2, iz2) = project(v2, focal, width as f32, height as f32);

    let area2 = (s1[0] - s0[0]) * (s2[1] - s0[1]) - (s2[0] - s0[0]) * (s1[1] - s0[1]);
    if area2.abs() < 1e-6 { return; }
    if CULL_BACKFACES && area2 < 0.0 { return; }

    let chunk_h = screen_chunk.len() as i32;
    let ro = row_offset as i32;

    let min_x = s0[0].min(s1[0]).min(s2[0]).max(0.0).floor() as i32;
    let max_x = s0[0].max(s1[0]).max(s2[0]).min((width - 1) as f32).ceil() as i32;
    let min_y = s0[1].min(s1[1]).min(s2[1]).max(0.0).floor() as i32;
    let max_y = s0[1].max(s1[1]).max(s2[1]).min((height - 1) as f32).ceil() as i32;

    let min_yr = min_y.max(ro);
    let max_yr = max_y.min(ro + chunk_h - 1);
    if min_x > max_x || min_yr > max_yr { return; }

    let a12 = s2[0] - s1[0]; let b12 = s2[1] - s1[1];
    let a20 = s0[0] - s2[0]; let b20 = s0[1] - s2[1];
    let a01 = s1[0] - s0[0]; let b01 = s1[1] - s0[1];

    let de12_dx = -b12; let de20_dx = -b20; let de01_dx = -b01;
    let inv_area2 = 1.0 / area2;
    let d_inv_z_dx = (de12_dx * iz0 + de20_dx * iz1 + de01_dx * iz2) * inv_area2;

    let u0z = uv0[0] * iz0; let v0z = uv0[1] * iz0;
    let u1z = uv1[0] * iz1; let v1z = uv1[1] * iz1;
    let u2z = uv2[0] * iz2; let v2z = uv2[1] * iz2;

    let d_uz_dx = (de12_dx * u0z + de20_dx * u1z + de01_dx * u2z) * inv_area2;
    let d_vz_dx = (de12_dx * v0z + de20_dx * v1z + de01_dx * v2z) * inv_area2;

    let px_start = min_x as f32 + 0.5;
    let want_positive = area2 > 0.0;

    for py in min_yr..=max_yr {
        let py_local = (py - ro) as usize;
        let fy = py as f32 + 0.5;

        let mut e12 = a12 * (fy - s1[1]) - b12 * (px_start - s1[0]);
        let mut e20 = a20 * (fy - s2[1]) - b20 * (px_start - s2[0]);
        let mut e01 = a01 * (fy - s0[1]) - b01 * (px_start - s0[0]);
        let mut inv_z = (e12 * iz0 + e20 * iz1 + e01 * iz2) * inv_area2;
        let mut uz = (e12 * u0z + e20 * u1z + e01 * u2z) * inv_area2;
        let mut vz = (e12 * v0z + e20 * v1z + e01 * v2z) * inv_area2;

        let row_screen = &mut screen_chunk[py_local];
        let row_zbuf = &mut zbuf_chunk[py_local];

        for px in min_x..=max_x {
            let inside = if want_positive {
                e12 >= 0.0 && e20 >= 0.0 && e01 >= 0.0
            } else {
                e12 <= 0.0 && e20 <= 0.0 && e01 <= 0.0
            };
            if inside {
                let ux = px as usize;
                if inv_z > row_zbuf[ux] {
                    let (src_rgb, src_a) = if let Some(tex) = texture {
                        let u = uz / inv_z;
                        let v = vz / inv_z;
                        let tw = tex.width as i32;
                        let th = tex.height as i32;
                        let tx = ((u * tw as f32).floor() as i32).rem_euclid(tw) as usize;
                        let ty = ((v * th as f32).floor() as i32).rem_euclid(th) as usize;
                        let t = tex.pixels[ty * tex.width + tx];
                        let a = (t[3] as u16 * rgba[3] as u16 / 255) as u8;
                        let r = (t[0] as u32 * rgba[0] as u32 / 255) as u8;
                        let g = (t[1] as u32 * rgba[1] as u32 / 255) as u8;
                        let b = (t[2] as u32 * rgba[2] as u32 / 255) as u8;
                        ([r, g, b], a)
                    } else {
                        ([rgba[0], rgba[1], rgba[2]], rgba[3])
                    };

                    if alpha_test > 0 {
                        if src_a >= alpha_test {
                            row_zbuf[ux] = inv_z;
                            row_screen[ux].color = src_rgb;
                        }
                    } else if src_a == 255 {
                        row_zbuf[ux] = inv_z;
                        row_screen[ux].color = src_rgb;
                    } else if src_a > 0 {
                        let a = src_a as u32;
                        let ia = 255 - a;
                        let dst = row_screen[ux].color;
                        row_screen[ux].color = [
                            ((src_rgb[0] as u32 * a + dst[0] as u32 * ia) / 255) as u8,
                            ((src_rgb[1] as u32 * a + dst[1] as u32 * ia) / 255) as u8,
                            ((src_rgb[2] as u32 * a + dst[2] as u32 * ia) / 255) as u8,
                        ];
                    }
                }
            }
            e12 += de12_dx;
            e20 += de20_dx;
            e01 += de01_dx;
            inv_z += d_inv_z_dx;
            uz += d_uz_dx;
            vz += d_vz_dx;
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn draw_render_triangle(
    tri: &render_triangle,
    basis: &CameraBasis,
    focal: f32, width: usize, height: usize,
    row_offset: usize,
    screen_chunk: &mut [Vec<super::Pixel_structure>],
    zbuf_chunk: &mut [Vec<f32>],
) {
    let verts = [
        [tri.vertices[0], tri.vertices[1], tri.vertices[2]],
        [tri.vertices[3], tri.vertices[4], tri.vertices[5]],
        [tri.vertices[6], tri.vertices[7], tri.vertices[8]],
    ];
    let uvs = [
        [tri.uvs[0], tri.uvs[1]],
        [tri.uvs[2], tri.uvs[3]],
        [tri.uvs[4], tri.uvs[5]],
    ];
    let rgba = tri.RGBA_color;

    let v0 = to_view_space(verts[0], basis);
    let v1 = to_view_space(verts[1], basis);
    let v2 = to_view_space(verts[2], basis);

    let clipped = clip_near(&[(v0, uvs[0]), (v1, uvs[1]), (v2, uvs[2])], NEAR_PLANE);
    if clipped.len() < 3 { return; }

    let tex = tri.texture.as_deref();

    for i in 1..clipped.len() - 1 {
        rasterize_triangle(
            clipped[0].0, clipped[i].0, clipped[i + 1].0,
            clipped[0].1, clipped[i].1, clipped[i + 1].1,
            tex,
            focal, width, height, row_offset,
            screen_chunk, zbuf_chunk, rgba, tri.alpha_test,
        );
    }
}

#[allow(clippy::too_many_arguments)]
fn render_node(
    node: &BspNode,
    basis: &CameraBasis,
    focal: f32, width: usize, height: usize,
    row_offset: usize,
    screen_chunk: &mut [Vec<super::Pixel_structure>],
    zbuf_chunk: &mut [Vec<f32>],
) {
    match node {
        BspNode::Leaf { triangles } => {
            for t in triangles {
                draw_render_triangle(t, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
            }
        }
        BspNode::Internal { plane, coplanar, front, back } => {
            let d = signed_distance(basis.position, plane);
            if d > 0.0 {
                render_node(back, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
                for t in coplanar {
                    draw_render_triangle(t, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
                }
                render_node(front, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
            } else {
                render_node(front, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
                for t in coplanar {
                    draw_render_triangle(t, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
                }
                render_node(back, basis, focal, width, height, row_offset, screen_chunk, zbuf_chunk);
            }
        }
    }
}

fn prepare_dynamic_triangles() -> Vec<render_triangle> {
    let queue = Draw_queue.lock().unwrap();
    let current_tick = tick_system::Get_current_tick();
    let tick_ms = Engine_settings.lock().unwrap().tick_speed.max(1) as f32 / 1000.0;

    let mut result: Vec<render_triangle> = Vec::new();

    for obj in queue.iter() {
        let n_tris = obj.vertices.len() / 9;
        if n_tris == 0 { continue; }

        let tex = obj.properties.get("texture").and_then(|p| load_texture(p));
        let props = Arc::new(obj.properties.clone());

        let skel = match obj.Get_skeleton() { Some(s) => s, None => continue };
        let has_skin = !skel.bones.is_empty();

        let (_, world) = obj.compute_pose_at(current_tick, tick_ms);
        let skin_mats: Vec<[[f32; 4]; 4]> = if has_skin {
            (0..skel.bones.len())
                .map(|i| gltf_parser::mat4_mul(world[i], skel.bones[i].inverse_bind))
                .collect()
        } else { Vec::new() };

        let joint_indices = obj.get_joint_indices();
        let joint_weights = obj.get_joint_weights();
        let uvs = obj.get_uvs();

        for t in 0..n_tris {
            let base = t * 9;
            let mut transformed = [0.0f32; 9];

            for v in 0..3 {
                let vi = t * 3 + v;
                let px = obj.vertices[base + v * 3];
                let py = obj.vertices[base + v * 3 + 1];
                let pz = obj.vertices[base + v * 3 + 2];

                let (sx, sy, sz) = if has_skin && vi < joint_indices.len() && vi < joint_weights.len() {
                    let idx = &joint_indices[vi];
                    let w = &joint_weights[vi];
                    let mut acc = [0.0f32; 3];
                    for k in 0..4 {
                        let wk = w[k];
                        if wk <= 0.0 { continue; }
                        let ji = idx[k] as usize;
                        if ji >= skin_mats.len() { continue; }
                        let p = gltf_parser::mat4_apply(skin_mats[ji], [px, py, pz]);
                        acc[0] += p[0] * wk;
                        acc[1] += p[1] * wk;
                        acc[2] += p[2] * wk;
                    }
                    (acc[0], acc[1], acc[2])
                } else {
                    (px, py, pz)
                };

                transformed[v * 3] = sx + obj.x;
                transformed[v * 3 + 1] = sy + obj.y;
                transformed[v * 3 + 2] = sz + obj.z;
            }

            let uv_base = t * 6;
            let tri_uvs = if uvs.len() >= uv_base + 6 {
                [uvs[uv_base], uvs[uv_base+1], uvs[uv_base+2], uvs[uv_base+3], uvs[uv_base+4], uvs[uv_base+5]]
            } else {
                [0.0; 6]
            };

            result.push(render_triangle {
                vertices: transformed,
                uvs: tri_uvs,
                texture: tex.clone(),
                RGBA_color: obj.RGBA_color,
                alpha_test: 0,
                properties: Arc::clone(&props),
            });
        }
    }

    result
}

pub fn Raycast_dynamic(origin: [f32; 3], dir: [f32; 3]) -> Option<(f32, [f32; 3], [f32; 3])> {
    let triangles = prepare_dynamic_triangles();

    let mut best_t: f32 = f32::INFINITY;
    let mut best_hit: Option<([f32; 3], [f32; 3])> = None;

    for tri in &triangles {
        let p0 = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
        let p1 = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
        let p2 = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];

        let e1 = sub(p1, p0);
        let e2 = sub(p2, p0);
        let h = cross(dir, e2);
        let a = dot(e1, h);
        if a.abs() < 1e-8 { continue; }
        let f = 1.0 / a;
        let s = sub(origin, p0);
        let u = f * dot(s, h);
        if !(0.0..=1.0).contains(&u) { continue; }
        let q = cross(s, e1);
        let v = f * dot(dir, q);
        if v < 0.0 || u + v > 1.0 { continue; }
        let t = f * dot(e2, q);
        if t > 1e-4 && t < best_t {
            best_t = t;
            let nrm = cross(e1, e2);
            let len = (nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]).sqrt();
            let normal = if len > 1e-8 {
                [nrm[0]/len, nrm[1]/len, nrm[2]/len]
            } else {
                [0.0, 1.0, 0.0]
            };
            let hp = [
                origin[0] + dir[0]*t,
                origin[1] + dir[1]*t,
                origin[2] + dir[2]*t,
            ];
            best_hit = Some((hp, normal));
        }
    }

    best_hit.map(|(p, n)| (best_t, p, n))
}

pub fn init_render(width: usize, height: usize, num_threads: usize) {
    {
        let mut zbuf = Z_BUFFER.lock().unwrap();
        *zbuf = vec![vec![0.0f32; width]; height];
    }
    RENDER_THREADS.store(num_threads.max(1), Ordering::Relaxed);
    println!("[Render] Render initialized with {} threads", num_threads.max(1));
}

pub fn Render_frame() {
    let empty_color = super::Empty_pixel.lock().unwrap().color;
    let basis = compute_camera_basis();
    let fov_v_rad = {
        let cam = super::Camera.lock().unwrap();
        (cam.camera_fov as f32).to_radians()
    };

    let dynamic_triangles = prepare_dynamic_triangles();

    let mut screen = super::Screen.lock().unwrap();
    let mut zbuf = Z_BUFFER.lock().unwrap();

    let height = screen.len();
    let width = if height > 0 { screen[0].len() } else { 0 };
    if width == 0 || height == 0 { return; }

    if zbuf.len() != height || zbuf[0].len() != width {
        *zbuf = vec![vec![0.0f32; width]; height];
    }

    let focal = (height as f32 * 0.5) / (fov_v_rad * 0.5).tan();
    if !focal.is_finite() || focal <= 0.0 { return; }

    for row in screen.iter_mut() {
        for px in row.iter_mut() { px.color = empty_color; }
    }
    for row in zbuf.iter_mut() {
        for v in row.iter_mut() { *v = 0.0; }
    }

    let bsp_guard = BSP_ROOT.lock().unwrap();
    let root = match bsp_guard.as_ref() {
        Some(b) => b.as_ref(),
        None => return,
    };

    let sprays_guard = SPRAYS.lock().unwrap();
    let sprays_slice: &[spray] = &sprays_guard[..];
    let dyn_slice: &[render_triangle] = &dynamic_triangles;

    let num_threads = RENDER_THREADS.load(Ordering::Relaxed).min(height).max(1);
    let chunk_size = (height + num_threads - 1) / num_threads;

    let screen_slice: &mut [Vec<super::Pixel_structure>] = &mut screen[..];
    let zbuf_slice: &mut [Vec<f32>] = &mut zbuf[..];

    std::thread::scope(|s| {
        let mut row_start = 0usize;
        for (scr, zb) in screen_slice.chunks_mut(chunk_size).zip(zbuf_slice.chunks_mut(chunk_size)) {
            let cur_start = row_start;
            row_start += scr.len();
            let root_ref = root;
            let basis_ref = &basis;
            let sprays_ref = sprays_slice;
            let dyn_ref = dyn_slice;
            s.spawn(move || {
                render_node(root_ref, basis_ref, focal, width, height, cur_start, scr, zb);
                for sp in sprays_ref {
                    for t in &sp.surface {
                        draw_render_triangle(t, basis_ref, focal, width, height, cur_start, scr, zb);
                    }
                }
                for t in dyn_ref {
                    draw_render_triangle(t, basis_ref, focal, width, height, cur_start, scr, zb);
                }
            });
        }
    });
}