use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread::{self, JoinHandle};
use std::time::Duration;

static DEVICE: OnceLock<cpal::Device> = OnceLock::new();
static PLAYBACKS: Mutex<Vec<(Arc<AtomicBool>, JoinHandle<()>)>> = Mutex::new(Vec::new());
static PLAYBACKS_3D: Mutex<Vec<(Arc<AtomicBool>, JoinHandle<()>, Arc<cpal::Stream>)>> = Mutex::new(Vec::new());
static SOUND_CACHE: OnceLock<Mutex<HashMap<String, (u32, u16, Vec<f32>)>>> = OnceLock::new();
static BAKED_SOUND_BVH: Mutex<Option<SoundBvhNode>> = Mutex::new(None);
static BAKED_SOUND_TRIANGLES: Mutex<Vec<SoundTriangle>> = Mutex::new(Vec::new());
static SOUND_BVH_BAKED: AtomicBool = AtomicBool::new(false);

fn get_cache() -> &'static Mutex<HashMap<String, (u32, u16, Vec<f32>)>> {
    SOUND_CACHE.get_or_init(|| Mutex::new(HashMap::new()))
}

#[derive(Clone)]
struct SoundTriangle {
    v0: [f32; 3],
    v1: [f32; 3],
    v2: [f32; 3],
    soundproofing: f32,
}

#[derive(Clone)]
struct SoundBvhNode {
    aabb_min: [f32; 3],
    aabb_max: [f32; 3],
    left: Option<Box<SoundBvhNode>>,
    right: Option<Box<SoundBvhNode>>,
    triangles: Vec<usize>,
}

pub fn Init_sound() -> Result<(), Box<dyn Error>> {
    let device = DEVICE.get_or_init(|| {
        let host = cpal::default_host();
        host.default_output_device()
            .expect("No default output device available")
    });
    let device_name = device.description().map(|d| d.to_string()).unwrap_or_else(|_| "Unknown".to_string());
    println!("Audio device: {}", device_name);
    Ok(())
}

pub fn Load_sound(file_path: &str) -> Result<(), Box<dyn Error>> {
    let (sample_rate, channels, samples) = read_wav(file_path)?;
    let cache = get_cache();
    let mut map = cache.lock().unwrap();
    map.insert(file_path.to_string(), (sample_rate, channels, samples));
    Ok(())
}

pub fn Play_sound(file_path: &str, volume: f32) -> Result<(), Box<dyn Error>> {
    play_stereo_sound_fixed(file_path, volume, volume)
}

pub fn play_stereo_sound_fixed(file_path: &str, left_vol: f32, right_vol: f32) -> Result<(), Box<dyn Error>> {
    let device = DEVICE.get().ok_or("Sound not initialized.")?;
    let cache = get_cache();
    let (sample_rate, file_channels, samples) = {
        let map = cache.lock().unwrap();
        map.get(file_path).ok_or("Sound not loaded")?.clone()
    };

    let output_channels = 2usize;
    let mut output_samples = Vec::with_capacity((samples.len() / file_channels as usize) * output_channels);
    if file_channels == 1 {
        for &s in &samples {
            output_samples.push(s * left_vol);
            output_samples.push(s * right_vol);
        }
    } else if file_channels == 2 {
        let mut iter = samples.iter();
        while let (Some(l), Some(r)) = (iter.next(), iter.next()) {
            output_samples.push(*l * left_vol);
            output_samples.push(*r * right_vol);
        }
    } else {
        return Err("Unsupported channels".into());
    }

    let total_samples = output_samples.len();
    let samples_arc = Arc::new(Mutex::new(output_samples));
    let index_arc = Arc::new(Mutex::new(0));
    let index_arc2 = index_arc.clone();
    let stop_flag = Arc::new(AtomicBool::new(false));
    let stop_flag_for_thread = stop_flag.clone();
    let device_clone = device.clone();

    let config = cpal::StreamConfig {
        channels: output_channels as u16,
        sample_rate,
        buffer_size: cpal::BufferSize::Default,
    };

    let stream = device_clone.build_output_stream(
        config,
        move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
            let mut idx = index_arc.lock().unwrap();
            let samples = samples_arc.lock().unwrap();
            let mut write_pos = 0;
            while write_pos < data.len() && *idx < samples.len() {
                data[write_pos] = samples[*idx];
                *idx += 1;
                write_pos += 1;
            }
            while write_pos < data.len() {
                data[write_pos] = 0.0;
                write_pos += 1;
            }
        },
        |err| eprintln!("Audio stream error: {}", err),
        None,
    )?;

    stream.play()?;

    let handle = thread::spawn(move || {
        while *index_arc2.lock().unwrap() < total_samples && !stop_flag_for_thread.load(Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(50));
        }
        drop(stream);
    });

    {
        let mut list = PLAYBACKS.lock().unwrap();
        list.retain(|(_, h)| !h.is_finished());
        list.push((stop_flag, handle));
    }

    Ok(())
}

pub fn Stop_all_sounds() -> Result<(), Box<dyn Error>> {
    let mut list = PLAYBACKS.lock().unwrap();
    for (flag, _) in list.iter() {
        flag.store(true, Ordering::Relaxed);
    }
    for (_, handle) in list.drain(..) {
        let _ = handle.join();
    }
    let mut list3d = PLAYBACKS_3D.lock().unwrap();
    for (flag, _, _) in list3d.iter() {
        flag.store(true, Ordering::Relaxed);
    }
    for (_, handle, _) in list3d.drain(..) {
        let _ = handle.join();
    }
    Ok(())
}

fn read_wav(path: &str) -> Result<(u32, u16, Vec<f32>), Box<dyn Error>> {
    let mut file = File::open(path)?;
    let mut riff = [0u8; 4];
    file.read_exact(&mut riff)?;
    if &riff != b"RIFF" {
        return Err("Not a RIFF file".into());
    }
    file.seek(SeekFrom::Current(4))?;
    let mut wave = [0u8; 4];
    file.read_exact(&mut wave)?;
    if &wave != b"WAVE" {
        return Err("Not a WAVE format".into());
    }
    let mut sample_rate = 0u32;
    let mut channels = 0u16;
    let mut data_start: u64 = 0;
    let mut data_len = 0u32;

    loop {
        let mut chunk_id = [0u8; 4];
        if file.read_exact(&mut chunk_id).is_err() {
            break;
        }
        let mut chunk_size = [0u8; 4];
        file.read_exact(&mut chunk_size)?;
        let size = u32::from_le_bytes(chunk_size);

        match &chunk_id {
            b"fmt " => {
                let mut fmt_data = vec![0u8; size as usize];
                file.read_exact(&mut fmt_data)?;
                if fmt_data.len() < 16 {
                    return Err("fmt chunk too short".into());
                }
                let audio_format = u16::from_le_bytes([fmt_data[0], fmt_data[1]]);
                if audio_format != 1 {
                    return Err("Only PCM (format 1) supported".into());
                }
                channels = u16::from_le_bytes([fmt_data[2], fmt_data[3]]);
                sample_rate = u32::from_le_bytes([
                    fmt_data[4], fmt_data[5], fmt_data[6], fmt_data[7],
                ]);
                let bits_per_sample = u16::from_le_bytes([fmt_data[14], fmt_data[15]]);
                if bits_per_sample != 16 {
                    return Err("Only 16‑bit PCM supported".into());
                }
            }
            b"data" => {
                data_start = file.stream_position()?;
                data_len = size;
                break;
            }
            _ => {
                file.seek(SeekFrom::Current(size as i64))?;
            }
        }
    }

    if data_len == 0 {
        return Err("No data chunk found".into());
    }

    file.seek(SeekFrom::Start(data_start))?;
    let sample_count = data_len as usize / 2;
    let mut buffer = vec![0u8; data_len as usize];
    file.read_exact(&mut buffer)?;
    let mut raw_samples = vec![0i16; sample_count];
    for i in 0..sample_count {
        let offset = i * 2;
        raw_samples[i] = i16::from_le_bytes([buffer[offset], buffer[offset + 1]]);
    }

    let samples_f32: Vec<f32> = raw_samples
        .into_iter()
        .map(|s| s as f32 / i16::MAX as f32)
        .collect();

    Ok((sample_rate, channels, samples_f32))
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

fn camera_basis(pitch_deg: f32, yaw_deg: f32, roll_deg: f32) -> ([f32; 3], [f32; 3], [f32; 3]) {
    use std::f32::consts::PI;
    let p = pitch_deg * PI / 180.0;
    let y = yaw_deg * PI / 180.0;
    let r = roll_deg * PI / 180.0;

    let (cp, sp) = (p.cos(), p.sin());
    let (cy, sy) = (y.cos(), y.sin());
    let (cr, sr) = (r.cos(), r.sin());

    let mut mat = [[0.0; 3]; 3];
    mat[0][0] = cy * cr + sy * sp * sr;
    mat[0][1] = -cy * sr + sy * sp * cr;
    mat[0][2] = sy * cp;
    mat[1][0] = cp * sr;
    mat[1][1] = cp * cr;
    mat[1][2] = -sp;
    mat[2][0] = -sy * cr + cy * sp * sr;
    mat[2][1] = sy * sr + cy * sp * cr;
    mat[2][2] = cy * cp;

    let forward = [-mat[0][2], -mat[1][2], -mat[2][2]];
    let right = [mat[0][0], mat[1][0], mat[2][0]];
    let up = [mat[0][1], mat[1][1], mat[2][1]];

    (forward, right, up)
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

fn compute_triangle_aabb(tri: &SoundTriangle) -> ([f32; 3], [f32; 3]) {
    let mut min = [tri.v0[0], tri.v0[1], tri.v0[2]];
    let mut max = [tri.v0[0], tri.v0[1], tri.v0[2]];
    for v in [tri.v1, tri.v2] {
        for i in 0..3 {
            if v[i] < min[i] { min[i] = v[i]; }
            if v[i] > max[i] { max[i] = v[i]; }
        }
    }
    (min, max)
}

fn ray_intersect_aabb(
    origin: &[f32; 3],
    dir: &[f32; 3],
    aabb_min: &[f32; 3],
    aabb_max: &[f32; 3],
    max_t: f32,
) -> bool {
    let mut tmin: f32 = 0.0;
    let mut tmax = max_t;
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

fn build_sound_bvh_recursive(
    indices: &mut [usize],
    triangles: &[SoundTriangle],
    depth: u32,
) -> SoundBvhNode {
    let mut min = [f32::INFINITY; 3];
    let mut max = [f32::NEG_INFINITY; 3];
    for &i in indices.iter() {
        let (aabb_min, aabb_max) = compute_triangle_aabb(&triangles[i]);
        for k in 0..3 {
            if aabb_min[k] < min[k] { min[k] = aabb_min[k]; }
            if aabb_max[k] > max[k] { max[k] = aabb_max[k]; }
        }
    }
    if indices.len() <= 8 || depth > 20 {
        return SoundBvhNode {
            aabb_min: min,
            aabb_max: max,
            left: None,
            right: None,
            triangles: indices.to_vec(),
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
        let ca = (compute_triangle_aabb(&triangles[a]).0[axis] + compute_triangle_aabb(&triangles[a]).1[axis]) * 0.5;
        let cb = (compute_triangle_aabb(&triangles[b]).0[axis] + compute_triangle_aabb(&triangles[b]).1[axis]) * 0.5;
        ca.partial_cmp(&cb).unwrap()
    });
    let mid = indices.len() / 2;
    let (left_indices, right_indices) = indices.split_at_mut(mid);
    let left = build_sound_bvh_recursive(left_indices, triangles, depth + 1);
    let right = build_sound_bvh_recursive(right_indices, triangles, depth + 1);
    SoundBvhNode {
        aabb_min: min,
        aabb_max: max,
        left: Some(Box::new(left)),
        right: Some(Box::new(right)),
        triangles: Vec::new(),
    }
}

fn build_sound_bvh(triangles: &[SoundTriangle]) -> SoundBvhNode {
    let mut indices: Vec<usize> = (0..triangles.len()).collect();
    build_sound_bvh_recursive(&mut indices, triangles, 0)
}

fn collect_sound_candidates(
    origin: &[f32; 3],
    dir: &[f32; 3],
    max_t: f32,
    node: &SoundBvhNode,
    triangles: &[SoundTriangle],
    out: &mut Vec<usize>,
) {
    if !ray_intersect_aabb(origin, dir, &node.aabb_min, &node.aabb_max, max_t) {
        return;
    }
    if node.triangles.is_empty() {
        if let Some(left) = &node.left {
            collect_sound_candidates(origin, dir, max_t, left, triangles, out);
        }
        if let Some(right) = &node.right {
            collect_sound_candidates(origin, dir, max_t, right, triangles, out);
        }
    } else {
        for &idx in &node.triangles {
            out.push(idx);
        }
    }
}

fn bake_sound_scene() {
    let scene = super::Static_sound_scene.lock().unwrap();
    let mut triangles = Vec::new();
    for obj in scene.iter() {
        let verts = &obj.vertices;
        for i in (0..verts.len()).step_by(9) {
            if i + 8 >= verts.len() {
                break;
            }
            let v0 = [verts[i] + obj.x, verts[i+1] + obj.y, verts[i+2] + obj.z];
            let v1 = [verts[i+3] + obj.x, verts[i+4] + obj.y, verts[i+5] + obj.z];
            let v2 = [verts[i+6] + obj.x, verts[i+7] + obj.y, verts[i+8] + obj.z];
            let sp = obj.soundproofing as f32 / 255.0;
            triangles.push(SoundTriangle { v0, v1, v2, soundproofing: sp });
        }
    }
    let bvh = build_sound_bvh(&triangles);
    *BAKED_SOUND_TRIANGLES.lock().unwrap() = triangles;
    *BAKED_SOUND_BVH.lock().unwrap() = Some(bvh);
    SOUND_BVH_BAKED.store(true, Ordering::Relaxed);
}

pub fn ensure_sound_bvh_baked() {
    if !SOUND_BVH_BAKED.load(Ordering::Relaxed) {
        bake_sound_scene();
    }
}

pub fn Play_sound_3d(x: f32, y: f32, z: f32, file_path: &str, volume: f32) -> Result<(), Box<dyn Error>> {
    ensure_sound_bvh_baked();

    let gain_control = Arc::new(Mutex::new((0.0, 0.0)));
    let stop_flag = Arc::new(AtomicBool::new(false));

    let stream = play_stereo_sound_dynamic(file_path, gain_control.clone(), stop_flag.clone())?;

    let source_pos = Arc::new(Mutex::new([x, y, z]));
    let gain_update = gain_control.clone();
    let stop_update = stop_flag.clone();
    let update_handle = thread::spawn(move || {
        while !stop_update.load(Ordering::Relaxed) {
            let (origin, right) = {
                let cam = super::Camera.lock().unwrap();
                let (_, right_vec, _) = camera_basis(cam.camera_pitch, cam.camera_yaw, cam.camera_roll);
                ([cam.camera_x, cam.camera_y, cam.camera_z], right_vec)
            };
            let source = *source_pos.lock().unwrap();

            let to_source = [source[0] - origin[0], source[1] - origin[1], source[2] - origin[2]];
            let dist = (to_source[0]*to_source[0] + to_source[1]*to_source[1] + to_source[2]*to_source[2]).sqrt();

            let distance_attenuation = if dist < 1e-6 { 1.0 } else { 1.0 / (1.0 + dist * dist * 0.01) };

            let dir_to_source = normalize(to_source);
            let pan = dot(&dir_to_source, &right).max(-1.0).min(1.0);
            let left_gain = (1.0 - pan) * 0.5;
            let right_gain = (1.0 + pan) * 0.5;

            let mut occlusion_attenuation = 1.0;
            let bvh_opt = BAKED_SOUND_BVH.lock().unwrap();
            if let Some(bvh) = bvh_opt.as_ref() {
                let triangles = BAKED_SOUND_TRIANGLES.lock().unwrap();
                let mut candidates = Vec::new();
                let dir_to_cam = normalize([origin[0] - source[0], origin[1] - source[1], origin[2] - source[2]]);
                collect_sound_candidates(&source, &dir_to_cam, dist, bvh, &triangles, &mut candidates);
                for &idx in &candidates {
                    let tri = &triangles[idx];
                    if let Some((t, _, _)) = ray_triangle_intersect(&source, &dir_to_cam, &tri.v0, &tri.v1, &tri.v2) {
                        if t > 1e-3 && t < dist {
                            occlusion_attenuation *= 1.0 - tri.soundproofing;
                            if occlusion_attenuation < 0.01 {
                                occlusion_attenuation = 0.0;
                                break;
                            }
                        }
                    }
                }
            }

            let total_attenuation = distance_attenuation * occlusion_attenuation;
            let left_vol = volume * total_attenuation * left_gain;
            let right_vol = volume * total_attenuation * right_gain;

            let mut g = gain_update.lock().unwrap();
            *g = (left_vol, right_vol);
        }
    });

    {
        let mut list = PLAYBACKS_3D.lock().unwrap();
        list.push((stop_flag, update_handle, stream));
    }

    Ok(())
}

fn play_stereo_sound_dynamic(
    file_path: &str,
    gain_control: Arc<Mutex<(f32, f32)>>,
    stop_flag: Arc<AtomicBool>,
) -> Result<Arc<cpal::Stream>, Box<dyn Error>> {
    let device = DEVICE.get().ok_or("Sound not initialized.")?;
    let cache = get_cache();
    let (sample_rate, file_channels, samples) = {
        let map = cache.lock().unwrap();
        map.get(file_path).ok_or("Sound not loaded")?.clone()
    };

    let output_channels = 2usize;
    let mut output_samples = Vec::with_capacity((samples.len() / file_channels as usize) * output_channels);
    if file_channels == 1 {
        for &s in &samples {
            output_samples.push(s);
            output_samples.push(s);
        }
    } else if file_channels == 2 {
        output_samples = samples.clone();
    } else {
        return Err("Unsupported channels".into());
    }

    let total_samples = output_samples.len();
    let samples_arc = Arc::new(Mutex::new(output_samples));
    let index_arc = Arc::new(Mutex::new(0));
    let index_arc2 = index_arc.clone();
    let stop_flag_clone = stop_flag.clone();
    let gain_control_clone = gain_control.clone();

    let config = cpal::StreamConfig {
        channels: output_channels as u16,
        sample_rate,
        buffer_size: cpal::BufferSize::Fixed(2048),
    };

    let device_clone = device.clone();
    let stream = device_clone.build_output_stream(
        config,
        move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
            let mut idx = index_arc2.lock().unwrap();
            let samples = samples_arc.lock().unwrap();
            let (left_gain, right_gain) = *gain_control_clone.lock().unwrap();
            let mut write_pos = 0;
            while write_pos < data.len() && *idx < samples.len() {
                let sample = samples[*idx];
                if write_pos % 2 == 0 {
                    data[write_pos] = sample * left_gain;
                } else {
                    data[write_pos] = sample * right_gain;
                }
                *idx += 1;
                write_pos += 1;
            }
            while write_pos < data.len() {
                data[write_pos] = 0.0;
                write_pos += 1;
            }
            if *idx >= total_samples {
                stop_flag_clone.store(true, Ordering::Relaxed);
            }
        },
        |err| eprintln!("Audio stream error: {}", err),
        None,
    )?;

    stream.play()?;
    Ok(Arc::new(stream))
}