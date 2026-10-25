use super::{Screen,Draw_components,Static_scene};
use std::sync::Arc;
use std::collections::HashMap;

use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

pub static RENDER_THREADS: AtomicUsize = AtomicUsize::new(1);

pub static BSP_ROOT: Mutex<Option<Box<BspNode>>> = Mutex::new(None);

#[derive(Clone)]
pub struct render_triangle {
    pub vertices: [f32; 9],
    pub RGBA_color: [u8; 4],
    pub properties: Arc<HashMap<String, String>>
}

pub fn triangulation(objects: Vec<Draw_components>) -> Vec<render_triangle>{
    let mut triangles: Vec<render_triangle> = Vec::new();
    for object in 0..objects.len(){
        let shared_properties = Arc::new(objects[object].properties.clone());
        for vertex in (0..(objects[object].vertices.len())).step_by(9){
            triangles.push(render_triangle{vertices: [objects[object].vertices[vertex]+objects[object].x,
                                                          objects[object].vertices[vertex+1]+objects[object].y,
                                                          objects[object].vertices[vertex+2]+objects[object].z,
                                                          objects[object].vertices[vertex+3]+objects[object].x,
                                                          objects[object].vertices[vertex+4]+objects[object].y,
                                                          objects[object].vertices[vertex+5]+objects[object].z,
                                                          objects[object].vertices[vertex+6]+objects[object].x,
                                                          objects[object].vertices[vertex+7]+objects[object].y,
                                                          objects[object].vertices[vertex+8]+objects[object].z,],
                                           RGBA_color: objects[object].RGBA_color,
                                           properties: Arc::clone(&shared_properties)});
        }
    }
    return triangles;
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

fn intersect_near(a: [f32; 3], b: [f32; 3], near: f32) -> [f32; 3] {
    let t = (near - a[2]) / (b[2] - a[2]);
    [a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), near]
}

fn clip_near(tri: &[[f32; 3]; 3], near: f32) -> Vec<[f32; 3]> {
    let mut out = Vec::with_capacity(4);
    for i in 0..3 {
        let a = tri[i];
        let b = tri[(i + 1) % 3];
        let a_in = a[2] > near;
        let b_in = b[2] > near;
        if b_in {
            if !a_in {
                out.push(intersect_near(a, b, near));
            }
            out.push(b);
        } else if a_in {
            out.push(intersect_near(a, b, near));
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
    focal: f32, width: usize, height: usize,
    row_offset: usize,
    screen_chunk: &mut [Vec<super::Pixel_structure>],
    zbuf_chunk: &mut [Vec<f32>],
    color: [u8; 3],
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

    let px_start = min_x as f32 + 0.5;
    let want_positive = area2 > 0.0;

    for py in min_yr..=max_yr {
        let py_local = (py - ro) as usize;
        let fy = py as f32 + 0.5;

        let mut e12 = a12 * (fy - s1[1]) - b12 * (px_start - s1[0]);
        let mut e20 = a20 * (fy - s2[1]) - b20 * (px_start - s2[0]);
        let mut e01 = a01 * (fy - s0[1]) - b01 * (px_start - s0[0]);
        let mut inv_z = (e12 * iz0 + e20 * iz1 + e01 * iz2) * inv_area2;

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
                    row_zbuf[ux] = inv_z;
                    row_screen[ux].color = color;
                }
            }
            e12 += de12_dx;
            e20 += de20_dx;
            e01 += de01_dx;
            inv_z += d_inv_z_dx;
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
    let color = [tri.RGBA_color[0], tri.RGBA_color[1], tri.RGBA_color[2]];

    let v0 = to_view_space(verts[0], basis);
    let v1 = to_view_space(verts[1], basis);
    let v2 = to_view_space(verts[2], basis);

    let clipped = clip_near(&[v0, v1, v2], NEAR_PLANE);
    if clipped.len() < 3 { return; }

    for i in 1..clipped.len() - 1 {
        rasterize_triangle(
            clipped[0], clipped[i], clipped[i + 1],
            focal, width, height, row_offset,
            screen_chunk, zbuf_chunk, color,
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
            s.spawn(move || {
                render_node(root_ref, basis_ref, focal, width, height, cur_start, scr, zb);
            });
        }
    });
}