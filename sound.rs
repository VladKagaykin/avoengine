use std::collections::HashMap;
use std::sync::{Mutex, LazyLock};
use kittyaudio::{Mixer, Sound, SoundHandle};

use super::{Camera, Sounds, Engine_settings};
use super::physics::{PHYSICS_BSP_ROOT, Physics_queue};
use super::render::BspNode;

static AUDIO_MIXER: Mutex<Option<Mixer>> = Mutex::new(None);
static SOUND_HANDLES: LazyLock<Mutex<HashMap<String, SoundHandle>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

pub fn Init_sound() {
    let mut mixer = Mixer::new();
    mixer.init();
    *AUDIO_MIXER.lock().unwrap() = Some(mixer);
    println!("[Sound]Initialized sound system");
}

fn ray_tri(orig: [f32; 3], dir: [f32; 3], p0: [f32; 3], p1: [f32; 3], p2: [f32; 3]) -> Option<f32> {
    let e1 = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]];
    let e2 = [p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]];
    let h = [
        dir[1] * e2[2] - dir[2] * e2[1],
        dir[2] * e2[0] - dir[0] * e2[2],
        dir[0] * e2[1] - dir[1] * e2[0],
    ];
    let a = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if a.abs() < 1e-8 { return None; }
    let f = 1.0 / a;
    let s = [orig[0] - p0[0], orig[1] - p0[1], orig[2] - p0[2]];
    let u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
    if u < 0.0 || u > 1.0 { return None; }
    let q = [
        s[1] * e1[2] - s[2] * e1[1],
        s[2] * e1[0] - s[0] * e1[2],
        s[0] * e1[1] - s[1] * e1[0],
    ];
    let v = f * (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]);
    if v < 0.0 || u + v > 1.0 { return None; }
    let t = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
    if t <= 1e-4 { return None; }
    Some(t)
}

fn transform_point(p: [f32; 3], pitch: f32, yaw: f32, roll: f32) -> [f32; 3] {
    let (sp, cp) = pitch.sin_cos();
    let (sy, cy) = yaw.sin_cos();
    let (sr, cr) = roll.sin_cos();
    let [x, y, z] = p;
    let x1 = x * cr - y * sr;
    let y1 = x * sr + y * cr;
    let z1 = z;
    let x2 = x1;
    let y2 = y1 * cp - z1 * sp;
    let z2 = y1 * sp + z1 * cp;
    let x3 = x2 * cy + z2 * sy;
    let z3 = -x2 * sy + z2 * cy;
    [x3, y2, z3]
}

fn try_collect_tri(
    tri_vertices: &[f32],
    properties: &HashMap<String, String>,
    orig: [f32; 3],
    dir: [f32; 3],
    max_t: f32,
    hits: &mut HashMap<u128, f32>,
) {
    if tri_vertices.len() < 9 { return; }
    let p0 = [tri_vertices[0], tri_vertices[1], tri_vertices[2]];
    let p1 = [tri_vertices[3], tri_vertices[4], tri_vertices[5]];
    let p2 = [tri_vertices[6], tri_vertices[7], tri_vertices[8]];
    if let Some(t) = ray_tri(orig, dir, p0, p1, p2) {
        if t <= max_t {
            let id = properties.get("_physics_object_id")
                .and_then(|s| s.parse::<u128>().ok())
                .unwrap_or(u128::MAX);
            if id != u128::MAX {
                let sp = properties.get("soundproofing")
                    .and_then(|s| s.parse::<f32>().ok())
                    .unwrap_or(0.0);
                hits.entry(id).or_insert(sp);
            }
        }
    }
}

fn walk_bsp_collect(
    node: &BspNode,
    orig: [f32; 3],
    dir: [f32; 3],
    max_t: f32,
    hits: &mut HashMap<u128, f32>,
) {
    match node {
        BspNode::Leaf { triangles } => {
            for tri in triangles {
                try_collect_tri(&tri.vertices, &tri.properties, orig, dir, max_t, hits);
            }
        }
        BspNode::Internal { coplanar, front, back, .. } => {
            for tri in coplanar {
                try_collect_tri(&tri.vertices, &tri.properties, orig, dir, max_t, hits);
            }
            walk_bsp_collect(front, orig, dir, max_t, hits);
            walk_bsp_collect(back, orig, dir, max_t, hits);
        }
    }
}

fn compute_soundproofing(from: [f32; 3], to: [f32; 3]) -> f32 {
    let delta = [to[0] - from[0], to[1] - from[1], to[2] - from[2]];
    let len_sq = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    if len_sq < 1e-8 { return 1.0; }
    let len = len_sq.sqrt();
    let dir = [delta[0] / len, delta[1] / len, delta[2] / len];

    let mut hits: HashMap<u128, f32> = HashMap::new();

    {
        let bsp = PHYSICS_BSP_ROOT.lock().unwrap();
        if let Some(root) = bsp.as_ref() {
            walk_bsp_collect(root, from, dir, len, &mut hits);
        }
    }

    {
        let queue = Physics_queue.lock().unwrap();
        for (idx, obj) in queue.iter().enumerate() {
            let mut hit = false;
            for tri in obj.vertices.chunks(9) {
                if tri.len() < 9 { break; }
                let p0 = transform_point([tri[0], tri[1], tri[2]], obj.pitch, obj.yaw, obj.roll);
                let p1 = transform_point([tri[3], tri[4], tri[5]], obj.pitch, obj.yaw, obj.roll);
                let p2 = transform_point([tri[6], tri[7], tri[8]], obj.pitch, obj.yaw, obj.roll);
                let p0 = [p0[0] + obj.x, p0[1] + obj.y, p0[2] + obj.z];
                let p1 = [p1[0] + obj.x, p1[1] + obj.y, p1[2] + obj.z];
                let p2 = [p2[0] + obj.x, p2[1] + obj.y, p2[2] + obj.z];
                if let Some(t) = ray_tri(from, dir, p0, p1, p2) {
                    if t <= len { hit = true; break; }
                }
            }
            if hit {
                let sp = obj.properties.get("soundproofing")
                    .and_then(|s| s.parse::<f32>().ok())
                    .unwrap_or(0.0);
                hits.insert(u128::MAX - idx as u128, sp);
            }
        }
    }

    let mut factor = 1.0f32;
    for (_, sp) in hits.iter() {
        factor *= (1.0 - sp).clamp(0.0, 1.0);
    }
    factor.max(0.0)
}

fn compute_distance_attenuation(dist: f32) -> f32 {
    1.0 / (1.0 + dist * 0.1)
}

fn compute_camera_right(cam_yaw_rad: f32, cam_pitch_rad: f32, cam_roll_rad: f32) -> [f32; 3] {
    let (sy, cy) = cam_yaw_rad.sin_cos();
    let right_base = [cy, 0.0, -sy];

    if cam_roll_rad.abs() < 1e-6 {
        return right_base;
    }

    let (sp, cp) = cam_pitch_rad.sin_cos();
    let forward = [cp * sy, sp, cp * cy];
    let up_base = [
        forward[1] * right_base[2] - forward[2] * right_base[1],
        forward[2] * right_base[0] - forward[0] * right_base[2],
        forward[0] * right_base[1] - forward[1] * right_base[0],
    ];
    let (sr, cr) = cam_roll_rad.sin_cos();
    [
        right_base[0] * cr + up_base[0] * sr,
        right_base[1] * cr + up_base[1] * sr,
        right_base[2] * cr + up_base[2] * sr,
    ]
}

fn compute_pan(
    cam_pos: [f32; 3],
    cam_yaw_rad: f32,
    cam_pitch_rad: f32,
    cam_roll_rad: f32,
    src_pos: [f32; 3],
) -> f32 {
    let right = compute_camera_right(cam_yaw_rad, cam_pitch_rad, cam_roll_rad);
    let dx = src_pos[0] - cam_pos[0];
    let dy = src_pos[1] - cam_pos[1];
    let dz = src_pos[2] - cam_pos[2];
    let len = (dx * dx + dy * dy + dz * dz).sqrt();
    if len < 1e-6 { return 0.0; }
    ((dx * right[0] + dy * right[1] + dz * right[2]) / len).clamp(-1.0, 1.0)
}

pub fn Play_sound() {
    let mut mixer_lock = AUDIO_MIXER.lock().unwrap();
    let mixer = match mixer_lock.as_mut() {
        Some(m) => m,
        None => return,
    };

    let (cx, cy, cz, cyaw, cpitch, croll) = {
        let c = Camera.lock().unwrap();
        (
            c.camera_x, c.camera_y, c.camera_z,
            c.camera_yaw, c.camera_pitch, c.camera_roll,
        )
    };

    let yaw_rad   = cyaw.to_radians();
    let pitch_rad = cpitch.to_radians();
    let roll_rad  = croll.to_radians();

    let tick_ms = Engine_settings.lock().unwrap().tick_speed.max(1) as f32;

    let mut handles = SOUND_HANDLES.lock().unwrap();
    let mut sounds = Sounds.lock().unwrap();

    let mut active: Vec<String> = Vec::new();

    for sound in sounds.iter_mut() {
        let name = sound.special_name.clone();
        active.push(name.clone());

        let prop_volume = sound.properties.get("volume")
            .and_then(|s| s.parse::<f32>().ok())
            .unwrap_or(1.0)
            .clamp(0.0, 1.0);

        let cam_pos = [cx, cy, cz];
        let src_pos = [sound.x, sound.y, sound.z];

        let distance = {
            let dx = src_pos[0] - cam_pos[0];
            let dy = src_pos[1] - cam_pos[1];
            let dz = src_pos[2] - cam_pos[2];
            (dx * dx + dy * dy + dz * dz).sqrt()
        };

        let soundproof = compute_soundproofing(src_pos, cam_pos);
        let attenuation = compute_distance_attenuation(distance);
        let final_volume = prop_volume * soundproof * attenuation;
        let pan = compute_pan(cam_pos, yaw_rad, pitch_rad, roll_rad, src_pos);

        if sound.time == 0 {
            handles.remove(&name);
        }

        if let Some(h) = handles.get(&name) {
            if h.finished() {
                handles.remove(&name);
            } else {
                h.set_volume(final_volume);
                h.set_panning(pan);
            }
        } else {
            match Sound::from_path(&sound.path) {
                Ok(snd) => {
                    let h = mixer.play(snd);
                    h.set_volume(final_volume);
                    h.set_panning(pan);
                    handles.insert(name, h);
                }
                Err(e) => eprintln!("[Sound] Failed to load {}: {:?}", sound.path, e),
            }
        }

        sound.time += tick_ms as i128;
    }

    handles.retain(|k, h| {
        if h.finished() {
            false
        } else {
            active.contains(k)
        }
    });
}