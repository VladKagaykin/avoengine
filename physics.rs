use std::collections::HashMap;
use std::sync::{Mutex, Arc, LazyLock};
use std::sync::atomic::Ordering;

use super::{gltf_parser, tick_system, Engine_settings, render};

#[derive(Clone)]
pub struct Dynamic_physics_components{
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub pitch: f32,
    pub yaw: f32,
    pub roll: f32,
    pub vertices: Vec<f32>,
    pub RGBA_color: [u8; 4],
    pub animations_in_play: Vec<String>,
    pub force_vectors: Vec<[f32;3]>,
    pub impact_with: Vec<u128>,
    pub properties: HashMap<String, String>,
    pub special_name: String
}

pub static Physics_queue: Mutex<Vec<Dynamic_physics_components>> = Mutex::new(Vec::new());

pub static PHYSICS_BSP_ROOT: Mutex<Option<Box<render::BspNode>>> = Mutex::new(None);

pub static PHYSICS_OBJECT_RESISTANCES: LazyLock<Mutex<HashMap<u128, f32>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

pub static PHYSICS_OBJECT_BOUNCES: LazyLock<Mutex<HashMap<u128, f32>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

fn parse_floats(s: &str) -> Vec<f32> {
    s.split(|c: char| c == ',' || c.is_whitespace())
        .filter_map(|x| x.parse().ok())
        .collect()
}

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

impl Dynamic_physics_components {
    pub fn new(special_name: String) -> Self {
        let mut properties = HashMap::new();
        properties.insert("animation_speed".to_string(), "1.0".to_string());
        properties.insert("animation_start_tick".to_string(), "0".to_string());
        properties.insert("resistance".to_string(), "1.0".to_string());
        properties.insert("bounce".to_string(), "0.0".to_string());
        properties.insert("mass".to_string(), "1.0".to_string());
        Self {
            x: 0.0,
            y: 0.0,
            z: 0.0,
            pitch: 0.0,
            yaw: 0.0,
            roll: 0.0,
            vertices: Vec::new(),
            RGBA_color: [255, 255, 255, 255],
            animations_in_play: Vec::new(),
            force_vectors: Vec::new(),
            impact_with: Vec::new(),
            properties,
            special_name,
        }
    }

    pub fn Attach_skeleton(&mut self, skel: Arc<gltf_parser::Skeleton_data>) -> u64 {
        let id = render::NEXT_SKELETON_ID.fetch_add(1, Ordering::Relaxed);
        render::SKELETON_REGISTRY.lock().unwrap().insert(id, skel);
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

        let id = render::NEXT_SKELETON_ID.fetch_add(1, Ordering::Relaxed);
        render::SKELETON_REGISTRY.lock().unwrap().insert(id, Arc::clone(&result.skeleton));
        self.properties.insert("skeleton_id".to_string(), id.to_string());
        id
    }

    pub fn Get_skeleton(&self) -> Option<Arc<gltf_parser::Skeleton_data>> {
        let id_str = self.properties.get("skeleton_id")?;
        let id: u64 = id_str.parse().ok()?;
        render::SKELETON_REGISTRY.lock().unwrap().get(&id).cloned()
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
        let tick = tick_system::Get_current_tick();
        self.properties.insert("animation_start_tick".to_string(), tick.to_string());
    }

    pub fn update_animations(&mut self, current_tick: u128, tick_ms: f32) {
        if self.animations_in_play.is_empty() { return; }
        let skel = match self.Get_skeleton() { Some(s) => s, None => return };

        let start_tick = self.get_animation_start_tick();
        let speed      = self.get_animation_speed();
        let elapsed    = current_tick.saturating_sub(start_tick) as f32 * tick_ms * speed;

        self.animations_in_play.retain(|name| {
            match skel.animations.get(name) {
                Some(anim) => anim.duration <= 0.0 || elapsed < anim.duration,
                None => false,
            }
        });

        if self.animations_in_play.is_empty() {
            self.properties.insert(
                "animation_start_tick".to_string(),
                current_tick.to_string(),
            );
        }
    }

    pub fn Set_x(&mut self, delta: f32) { self.x += delta; }
    pub fn Set_y(&mut self, delta: f32) { self.y += delta; }
    pub fn Set_z(&mut self, delta: f32) { self.z += delta; }

    pub fn Set_pitch(&mut self, delta: f32) { self.pitch += delta; }
    pub fn Set_yaw(&mut self, delta: f32) { self.yaw += delta; }
    pub fn Set_roll(&mut self, delta: f32) { self.roll += delta; }

    pub fn Set_R(&mut self, delta: i16) { self.RGBA_color[0] = (self.RGBA_color[0] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_G(&mut self, delta: i16) { self.RGBA_color[1] = (self.RGBA_color[1] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_B(&mut self, delta: i16) { self.RGBA_color[2] = (self.RGBA_color[2] as i16 + delta).clamp(0, 255) as u8; }
    pub fn Set_A(&mut self, delta: i16) { self.RGBA_color[3] = (self.RGBA_color[3] as i16 + delta).clamp(0, 255) as u8; }

    pub fn Set_animation_speed(&mut self, delta: f32) {
        let cur = self.get_animation_speed();
        self.properties.insert("animation_speed".to_string(), (cur + delta).max(0.0).to_string());
    }

    pub fn Set_resistance(&mut self, r: f32) {
        self.properties.insert("resistance".to_string(), r.max(0.0).to_string());
    }

    pub fn Get_resistance(&self) -> f32 {
        self.properties.get("resistance")
            .and_then(|s| s.parse::<f32>().ok())
            .unwrap_or(1.0f32)
            .max(0.0f32)
    }

    pub fn Set_bounce(&mut self, b: f32) {
        self.properties.insert("bounce".to_string(), b.max(0.0).to_string());
    }

    pub fn Get_bounce(&self) -> f32 {
        self.properties.get("bounce")
            .and_then(|s| s.parse::<f32>().ok())
            .unwrap_or(0.0f32)
            .max(0.0f32)
    }

    pub fn Set_mass(&mut self, m: f32) {
        self.properties.insert("mass".to_string(), m.max(0.0).to_string());
    }

    pub fn Get_mass(&self) -> f32 {
        let m = self.properties.get("mass")
            .and_then(|s| s.parse::<f32>().ok())
            .unwrap_or(1.0f32);
        if m > 1e-6 { m } else { 1.0 }
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
    pub fn Get_pitch(&self) -> f32 { self.pitch }
    pub fn Get_yaw(&self) -> f32 { self.yaw }
    pub fn Get_roll(&self) -> f32 { self.roll }
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
        let (anim_name, is_once) = match (
            self.animations_in_play.first(),
            self.properties.get("default_animation"),
        ) {
            (Some(n), _)    => (n.clone(), true),
            (None, Some(d)) => (d.clone(), false),
            (None, None)    => return 0.0,
        };
        let skel = match self.Get_skeleton() { Some(s) => s, None => return 0.0 };
        let anim = match skel.animations.get(&anim_name) { Some(a) => a, None => return 0.0 };
        let elapsed = current_tick.saturating_sub(self.get_animation_start_tick()) as f32
            * tick_ms * self.get_animation_speed();
        if anim.duration <= 0.0 { return 0.0; }
        if is_once { elapsed.min(anim.duration) } else { elapsed % anim.duration }
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

        let mut active: Vec<(String, bool)> = self.animations_in_play.iter()
            .map(|n| (n.clone(), true))
            .collect();
        if active.is_empty() {
            if let Some(d) = self.properties.get("default_animation") {
                active.push((d.clone(), false));
            }
        }

        let times: Vec<f32> = active.iter().map(|(name, is_once)| {
            if let Some(anim) = skel.animations.get(name) {
                let elapsed = current_tick.saturating_sub(start_tick) as f32 * tick_ms * speed;
                if anim.duration <= 0.0 {
                    0.0
                } else if *is_once {
                    elapsed.min(anim.duration)
                } else {
                    elapsed % anim.duration
                }
            } else { 0.0 }
        }).collect();

        let mut local = vec![gltf_parser::mat4_identity(); n];

        for i in 0..n {
            let bone = &skel.bones[i];

            let mut sum_t = [0.0f32; 3];
            let mut sum_s = [0.0f32; 3];
            let mut weight_count = 0usize;
            let mut base_r = bone.rotation;

            for (k, (name, _is_once)) in active.iter().enumerate() {
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

    pub fn Add_force(&mut self, force: [f32; 3]) {
        let m = self.Get_mass();
        self.force_vectors.push([force[0] / m, force[1] / m, force[2] / m]);
    }

    pub fn Clear_forces(&mut self) {
        self.force_vectors.clear();
    }

    pub fn Get_forces(&self) -> Vec<[f32; 3]> {
        self.force_vectors.clone()
    }

    pub fn Add_impact(&mut self, id: u128) {
        if !self.impact_with.contains(&id) {
            self.impact_with.push(id);
        }
    }

    pub fn Clear_impacts(&mut self) {
        self.impact_with.clear();
    }

    pub fn Get_impacts(&self) -> Vec<u128> {
        self.impact_with.clone()
    }
}

fn to_draw_components(p: &super::Physics_components, id: usize) -> super::Draw_components {
    let mut props = p.properties.clone();
    props.insert("_physics_object_id".to_string(), id.to_string());
    super::Draw_components {
        x: p.x,
        y: p.y,
        z: p.z,
        pitch: p.pitch,
        yaw: p.yaw,
        roll: p.roll,
        vertices: p.vertices.clone(),
        RGBA_color: [255, 255, 255, 255],
        properties: props,
        special_name: p.special_name.clone(),
    }
}

pub fn Bake_physics_scene() {
    let scene = super::Physics_scene.lock().unwrap().clone();
    let draw_components: Vec<super::Draw_components> = scene.iter()
        .enumerate()
        .filter(|(_, p)| p.properties.get("collision").map(|v| v == "1").unwrap_or(false))
        .map(|(idx, p)| to_draw_components(p, idx))
        .collect();
    let triangles = render::triangulation(draw_components);
    let tree = render::build_bsp(triangles, 0);
    *PHYSICS_BSP_ROOT.lock().unwrap() = Some(Box::new(tree));

    let mut resistances = HashMap::new();
    let mut bounces = HashMap::new();
    for (idx, p) in scene.iter().enumerate() {
        if p.properties.get("collision").map(|v| v == "1").unwrap_or(false) {
            let r = p.properties.get("resistance")
                .and_then(|s| s.parse::<f32>().ok())
                .unwrap_or(1.0)
                .max(0.0);
            let b = p.properties.get("bounce")
                .and_then(|s| s.parse::<f32>().ok())
                .unwrap_or(1.0)
                .max(0.0);
            resistances.insert(idx as u128, r);
            bounces.insert(idx as u128, b);
        }
    }
    *PHYSICS_OBJECT_RESISTANCES.lock().unwrap() = resistances;
    *PHYSICS_OBJECT_BOUNCES.lock().unwrap() = bounces;

    println!("[Physics] Created physics BSP tree");
}

fn ray_triangle_intersect(
    orig: [f32; 3],
    dir: [f32; 3],
    tri: &render::render_triangle,
) -> Option<(f32, [f32; 3])> {
    let p0 = [tri.vertices[0], tri.vertices[1], tri.vertices[2]];
    let p1 = [tri.vertices[3], tri.vertices[4], tri.vertices[5]];
    let p2 = [tri.vertices[6], tri.vertices[7], tri.vertices[8]];

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

    let nrm = [
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
    ];
    let len = (nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]).sqrt();
    let normal = if len > 1e-8 {
        [nrm[0] / len, nrm[1] / len, nrm[2] / len]
    } else {
        [0.0, 1.0, 0.0]
    };
    Some((t, normal))
}

fn tri_object_id(tri: &render::render_triangle) -> u128 {
    tri.properties.get("_physics_object_id")
        .and_then(|s| s.parse::<u128>().ok())
        .unwrap_or(u128::MAX)
}

fn raycast_bsp_recursive(
    node: &render::BspNode,
    orig: [f32; 3],
    dir: [f32; 3],
    max_t: f32,
    best: &mut Option<(f32, [f32; 3], u128)>,
) {
    match node {
        render::BspNode::Leaf { triangles, .. } => {
            for tri in triangles {
                if let Some((t, n)) = ray_triangle_intersect(orig, dir, tri) {
                    if t <= max_t && best.map_or(true, |(bt, _, _)| t < bt) {
                        *best = Some((t, n, tri_object_id(tri)));
                    }
                }
            }
        }
        render::BspNode::Internal { plane, coplanar, front, back, .. } => {
            for tri in coplanar {
                if let Some((t, n)) = ray_triangle_intersect(orig, dir, tri) {
                    if t <= max_t && best.map_or(true, |(bt, _, _)| t < bt) {
                        *best = Some((t, n, tri_object_id(tri)));
                    }
                }
            }
            let d = (orig[0] - plane.point[0]) * plane.normal[0]
                  + (orig[1] - plane.point[1]) * plane.normal[1]
                  + (orig[2] - plane.point[2]) * plane.normal[2];
            let denom = dir[0] * plane.normal[0]
                      + dir[1] * plane.normal[1]
                      + dir[2] * plane.normal[2];
            let t_plane = if denom.abs() > 1e-8 { -d / denom } else { -1.0 };
            if d > 0.0 {
                raycast_bsp_recursive(front, orig, dir, max_t, best);
                if t_plane > 0.0 && t_plane <= max_t {
                    raycast_bsp_recursive(back, orig, dir, max_t, best);
                }
            } else {
                raycast_bsp_recursive(back, orig, dir, max_t, best);
                if t_plane > 0.0 && t_plane <= max_t {
                    raycast_bsp_recursive(front, orig, dir, max_t, best);
                }
            }
        }
    }
}

fn raycast_bsp(
    root: &render::BspNode,
    orig: [f32; 3],
    delta: [f32; 3],
) -> Option<([f32; 3], [f32; 3], u128)> {
    let len_sq = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    if len_sq < 1e-12 { return None; }
    let len = len_sq.sqrt();
    let dir = [delta[0] / len, delta[1] / len, delta[2] / len];
    let mut best: Option<(f32, [f32; 3], u128)> = None;
    raycast_bsp_recursive(root, orig, dir, len, &mut best);
    best.map(|(t, n, oid)| {
        let hit = [
            orig[0] + dir[0] * t,
            orig[1] + dir[1] * t,
            orig[2] + dir[2] * t,
        ];
        (hit, n, oid)
    })
}

fn handle_collision_reflect(
    obj: &mut Dynamic_physics_components,
    axis: usize,
    combined_bounce: f32,
) {
    let sum: f32 = obj.force_vectors.iter().map(|f| f[axis]).sum();
    for f in obj.force_vectors.iter_mut() {
        f[axis] = 0.0;
    }
    if combined_bounce > 0.0 && sum.abs() > 1e-8 {
        let mut reflected = [0.0f32; 3];
        reflected[axis] = -sum * combined_bounce;
        obj.force_vectors.push(reflected);
    }
}

fn rotation_matrix(pitch_deg: f32, yaw_deg: f32, roll_deg: f32) -> [[f32; 3]; 3] {
    let p = pitch_deg.to_radians();
    let y = yaw_deg.to_radians();
    let r = roll_deg.to_radians();
    let (cp, sp) = (p.cos(), p.sin());
    let (cy, sy) = (y.cos(), y.sin());
    let (cr, sr) = (r.cos(), r.sin());
    [
        [cy * cr + sy * sp * sr, -cy * sr + sy * sp * cr, sy * cp],
        [cp * sr,                 cp * cr,                -sp],
        [-sy * cr + cy * sp * sr, sy * sr + cy * sp * cr, cy * cp],
    ]
}

fn apply_rotation(m: &[[f32; 3]; 3], v: [f32; 3]) -> [f32; 3] {
    [
        m[0][0]*v[0] + m[0][1]*v[1] + m[0][2]*v[2],
        m[1][0]*v[0] + m[1][1]*v[1] + m[1][2]*v[2],
        m[2][0]*v[0] + m[2][1]*v[1] + m[2][2]*v[2],
    ]
}

fn compute_rotated_vertices(obj: &Dynamic_physics_components) -> Vec<[f32; 3]> {
    if obj.vertices.len() < 3 {
        return vec![[0.0, 0.0, 0.0]];
    }
    let has_rot = obj.pitch != 0.0 || obj.yaw != 0.0 || obj.roll != 0.0;
    let rot = if has_rot { Some(rotation_matrix(obj.pitch, obj.yaw, obj.roll)) } else { None };
    let mut locals: Vec<[f32; 3]> = obj.vertices.chunks_exact(3)
        .map(|c| [c[0], c[1], c[2]])
        .collect();
    locals.sort_unstable_by(|a, b| {
        a[0].total_cmp(&b[0])
            .then(a[1].total_cmp(&b[1]))
            .then(a[2].total_cmp(&b[2]))
    });
    locals.dedup_by(|a, b| {
        a[0].to_bits() == b[0].to_bits()
            && a[1].to_bits() == b[1].to_bits()
            && a[2].to_bits() == b[2].to_bits()
    });
    locals.into_iter().map(|v| {
        if let Some(r) = &rot { apply_rotation(r, v) } else { v }
    }).collect()
}

fn compute_world_aabb(obj: &Dynamic_physics_components) -> ([f32; 3], [f32; 3]) {
    if obj.vertices.len() < 3 {
        return ([obj.x, obj.y, obj.z], [obj.x, obj.y, obj.z]);
    }
    let has_rot = obj.pitch != 0.0 || obj.yaw != 0.0 || obj.roll != 0.0;
    let rot = if has_rot { Some(rotation_matrix(obj.pitch, obj.yaw, obj.roll)) } else { None };
    let mut mn = [f32::INFINITY; 3];
    let mut mx = [f32::NEG_INFINITY; 3];
    for c in obj.vertices.chunks_exact(3) {
        let local = [c[0], c[1], c[2]];
        let r = if let Some(rm) = &rot { apply_rotation(rm, local) } else { local };
        let w = [r[0] + obj.x, r[1] + obj.y, r[2] + obj.z];
        for k in 0..3 {
            if w[k] < mn[k] { mn[k] = w[k]; }
            if w[k] > mx[k] { mx[k] = w[k]; }
        }
    }
    (mn, mx)
}

fn try_collide_axis(
    obj: &mut Dynamic_physics_components,
    root: &render::BspNode,
    bounces: &HashMap<u128, f32>,
    axis: usize,
    delta: f32,
    rotated_verts: &[[f32; 3]],
) -> bool {
    if delta.abs() < 1e-8 { return false; }

    let mut dir = [0.0f32; 3];
    dir[axis] = delta;

    let mut min_t = f32::INFINITY;
    let mut hit_oid: Option<u128> = None;
    let mut any_hit = false;

    for rv in rotated_verts {
        let v_world = [rv[0] + obj.x, rv[1] + obj.y, rv[2] + obj.z];
        if let Some((hp, _, oid)) = raycast_bsp(root, v_world, dir) {
            let t = ((hp[0] - v_world[0]).powi(2)
                   + (hp[1] - v_world[1]).powi(2)
                   + (hp[2] - v_world[2]).powi(2)).sqrt();
            if !any_hit || t < min_t {
                any_hit = true;
                min_t = t;
                hit_oid = Some(oid);
            }
        }
    }

    if !any_hit {
        match axis {
            0 => obj.x += delta,
            1 => obj.y += delta,
            2 => obj.z += delta,
            _ => {}
        }
        return false;
    }

    let move_by = delta.signum() * (min_t - 0.001).max(0.0);
    match axis {
        0 => obj.x += move_by,
        1 => obj.y += move_by,
        2 => obj.z += move_by,
        _ => {}
    }

    if let Some(oid) = hit_oid {
        if oid != u128::MAX { obj.Add_impact(oid); }
        let surf_bounce = if oid != u128::MAX {
            bounces.get(&oid).copied().unwrap_or(1.0).max(0.0)
        } else { 1.0 };
        let combined = obj.Get_bounce() * surf_bounce;
        handle_collision_reflect(obj, axis, combined);
    }

    true
}

fn resolve_dynamic_collisions(queue: &mut Vec<Dynamic_physics_components>) {
    let n = queue.len();
    if n < 2 { return; }

    let active: Vec<bool> = queue.iter()
        .map(|o| o.properties.get("collision").map(|v| v == "1").unwrap_or(false))
        .collect();

    let aabbs: Vec<([f32; 3], [f32; 3])> = queue.iter().map(compute_world_aabb).collect();

    for i in 0..n {
        if !active[i] { continue; }
        for j in (i + 1)..n {
            if !active[j] { continue; }

            let (a_min, a_max) = aabbs[i];
            let (b_min, b_max) = aabbs[j];

            let ox = a_max[0].min(b_max[0]) - a_min[0].max(b_min[0]);
            let oy = a_max[1].min(b_max[1]) - a_min[1].max(b_min[1]);
            let oz = a_max[2].min(b_max[2]) - a_min[2].max(b_min[2]);

            if ox <= 0.0 || oy <= 0.0 || oz <= 0.0 { continue; }

            let (axis, depth) = if ox <= oy && ox <= oz { (0usize, ox) }
                                else if oy <= oz { (1usize, oy) }
                                else { (2usize, oz) };

            let ca = (a_min[axis] + a_max[axis]) * 0.5;
            let cb = (b_min[axis] + b_max[axis]) * 0.5;
            let sign = if cb >= ca { 1.0f32 } else { -1.0f32 };

            let (left, right) = queue.split_at_mut(j);
            let oi = &mut left[i];
            let oj = &mut right[0];

            let mi = oi.Get_mass();
            let mj = oj.Get_mass();
            let total = mi + mj;
            if total <= 1e-8 { continue; }

            let share_i = mj / total;
            let share_j = mi / total;

            match axis {
                0 => { oi.x -= sign * depth * share_i; oj.x += sign * depth * share_j; }
                1 => { oi.y -= sign * depth * share_i; oj.y += sign * depth * share_j; }
                2 => { oi.z -= sign * depth * share_i; oj.z += sign * depth * share_j; }
                _ => {}
            }

            let vi: f32 = oi.force_vectors.iter().map(|f| f[axis]).sum();
            let vj: f32 = oj.force_vectors.iter().map(|f| f[axis]).sum();

            let approaching = (sign > 0.0 && vi > vj) || (sign < 0.0 && vi < vj);
            if !approaching { continue; }

            let combined_bounce = oi.Get_bounce() * oj.Get_bounce();
            let v_avg = (mi * vi + mj * vj) / total;
            let vi_new = v_avg + combined_bounce * (vj - vi) * share_i;
            let vj_new = v_avg + combined_bounce * (vi - vj) * share_j;

            for f in oi.force_vectors.iter_mut() { f[axis] = 0.0; }
            for f in oj.force_vectors.iter_mut() { f[axis] = 0.0; }
            let mut fi = [0.0f32; 3]; fi[axis] = vi_new;
            let mut fj = [0.0f32; 3]; fj[axis] = vj_new;
            oi.force_vectors.push(fi);
            oj.force_vectors.push(fj);
        }
    }
}

pub fn Update_physics() {
    let (g, resistance_air) = {
        let e = super::Earth.lock().unwrap();
        (e.g, e.resistance_in_air)
    };

    let tick_ms = Engine_settings.lock().unwrap().tick_speed.max(1) as f32;
    let dt = tick_ms / 1000.0;

    let bsp_guard = PHYSICS_BSP_ROOT.lock().unwrap();
    let resistances_guard = PHYSICS_OBJECT_RESISTANCES.lock().unwrap();
    let bounces_guard = PHYSICS_OBJECT_BOUNCES.lock().unwrap();
    let mut queue = Physics_queue.lock().unwrap();

    for obj in queue.iter_mut() {
        let current_tick = tick_system::Get_current_tick();
        obj.update_animations(current_tick, dt);

        let mass = obj.Get_mass();

        let was_in_contact = !obj.impact_with.is_empty();
        let prev_avg_surface_resistance: Option<f32> = if was_in_contact {
            let mut total = 0.0f32;
            let mut count = 0usize;
            for oid in &obj.impact_with {
                if let Some(r) = resistances_guard.get(oid) {
                    total += *r;
                    count += 1;
                }
            }
            if count > 0 { Some(total / count as f32) } else { None }
        } else {
            None
        };
        obj.Clear_impacts();

        let obj_resistance = obj.Get_resistance();

        if let Some(avg_surface) = prev_avg_surface_resistance {
            if obj_resistance > 0.0 {
                let friction_decay = (-avg_surface * obj_resistance / mass).exp();
                for f in obj.force_vectors.iter_mut() {
                    f[0] *= friction_decay;
                    f[2] *= friction_decay;
                }
            }
        } else if obj_resistance > 0.0 && resistance_air > 0.0 {
            let drag_decay = (-resistance_air * obj_resistance / mass).exp();
            for f in obj.force_vectors.iter_mut() {
                f[0] *= drag_decay;
                f[2] *= drag_decay;
            }
        }

        let has_gravity = obj.properties.get("has_gravity").map(|v| v == "1").unwrap_or(false);
        if has_gravity {
            obj.force_vectors.push([0.0, -g * dt, 0.0]);
        }

        let mut vel = [0.0f32; 3];
        for f in &obj.force_vectors {
            vel[0] += f[0];
            vel[1] += f[1];
            vel[2] += f[2];
        }

        let delta = [vel[0] * dt, vel[1] * dt, vel[2] * dt];
        let collision = obj.properties.get("collision").map(|v| v == "1").unwrap_or(false);

        if collision && bsp_guard.is_some() {
            let root = bsp_guard.as_ref().unwrap();
            let rotated = compute_rotated_vertices(obj);

            if delta[1].abs() > 1e-8 {
                try_collide_axis(obj, root, &bounces_guard, 1, delta[1], &rotated);
            }
            if delta[0].abs() > 1e-8 {
                try_collide_axis(obj, root, &bounces_guard, 0, delta[0], &rotated);
            }
            if delta[2].abs() > 1e-8 {
                try_collide_axis(obj, root, &bounces_guard, 2, delta[2], &rotated);
            }
        } else {
            obj.x += delta[0];
            obj.y += delta[1];
            obj.z += delta[2];
        }

        obj.force_vectors.retain(|f| f[0].abs() + f[1].abs() + f[2].abs() > 1e-4);
    }

    resolve_dynamic_collisions(&mut queue);
}