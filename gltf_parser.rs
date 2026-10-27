use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use gltf::animation::util::ReadOutputs;

const MIRROR_X: bool = true;

fn mirror_x_vec3(v: [f32; 3]) -> [f32; 3] {
    if MIRROR_X { [-v[0], v[1], v[2]] } else { v }
}

fn mirror_x_quat(q: [f32; 4]) -> [f32; 4] {
    if MIRROR_X { [q[0], -q[1], -q[2], q[3]] } else { q }
}

pub struct Bone_data {
    pub name: String,
    pub parent: Option<usize>,
    pub translation: [f32; 3],
    pub rotation: [f32; 4],
    pub scale: [f32; 3],
    pub inverse_bind: [[f32; 4]; 4],
}

pub struct Keyframe {
    pub time: f32,
    pub translation: [f32; 3],
    pub rotation: [f32; 4],
    pub scale: [f32; 3],
}

pub struct Bone_tracks {
    pub translation: Vec<(f32, [f32; 3])>,
    pub rotation: Vec<(f32, [f32; 4])>,
    pub scale: Vec<(f32, [f32; 3])>,
}

impl Bone_tracks {
    pub fn new() -> Self {
        Self {
            translation: Vec::new(),
            rotation: Vec::new(),
            scale: Vec::new(),
        }
    }
}

pub struct Animation_data {
    pub name: String,
    pub duration: f32,
    pub tracks: HashMap<usize, Bone_tracks>,
}

pub struct Skeleton_data {
    pub bones: Vec<Bone_data>,
    pub animations: HashMap<String, Animation_data>,
}

pub struct Import_result {
    pub texture_path: String,
    pub uv: String,
    pub uvs: Vec<f32>,
    pub vertices: Vec<f32>,
    pub joint_indices: Vec<[u8; 4]>,
    pub joint_weights: Vec<[f32; 4]>,
    pub bone_names: Vec<String>,
    pub animation_names: Vec<String>,
    pub skeleton: Arc<Skeleton_data>,
}

#[derive(Clone, Copy)]
struct AtlasCell {
    x: usize,
    y: usize,
    w: usize,
    h: usize,
}

pub fn mat4_identity() -> [[f32; 4]; 4] {
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

pub fn mat4_mul(a: [[f32; 4]; 4], b: [[f32; 4]; 4]) -> [[f32; 4]; 4] {
    let mut r = [[0.0f32; 4]; 4];
    for i in 0..4 {
        for j in 0..4 {
            for k in 0..4 {
                r[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    r
}

pub fn mat4_apply(m: [[f32; 4]; 4], v: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3],
        m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3],
        m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3],
    ]
}

pub fn mat4_inverse(m: [[f32; 4]; 4]) -> [[f32; 4]; 4] {
    let a = m[0][0]; let b = m[0][1]; let c = m[0][2]; let t0 = m[0][3];
    let d = m[1][0]; let e = m[1][1]; let f = m[1][2]; let t1 = m[1][3];
    let g = m[2][0]; let h = m[2][1]; let i = m[2][2]; let t2 = m[2][3];

    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if det.abs() < 1e-12 {
        return mat4_identity();
    }
    let inv_det = 1.0 / det;

    let ia = (e * i - f * h) * inv_det;
    let ib = -(b * i - c * h) * inv_det;
    let ic = (b * f - c * e) * inv_det;
    let id = -(d * i - f * g) * inv_det;
    let ie = (a * i - c * g) * inv_det;
    let if_ = -(a * f - c * d) * inv_det;
    let ig = (d * h - e * g) * inv_det;
    let ih = -(a * h - b * g) * inv_det;
    let ii = (a * e - b * d) * inv_det;

    let it0 = -(ia * t0 + ib * t1 + ic * t2);
    let it1 = -(id * t0 + ie * t1 + if_ * t2);
    let it2 = -(ig * t0 + ih * t1 + ii * t2);

    [
        [ia, ib, ic, it0],
        [id, ie, if_, it1],
        [ig, ih, ii, it2],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

pub fn quat_to_mat3(q: [f32; 4]) -> [[f32; 3]; 3] {
    let [x, y, z, w] = q;
    let x2 = x + x;
    let y2 = y + y;
    let z2 = z + z;
    let xx = x * x2;
    let xy = x * y2;
    let xz = x * z2;
    let yy = y * y2;
    let yz = y * z2;
    let zz = z * z2;
    let wx = w * x2;
    let wy = w * y2;
    let wz = w * z2;

    [
        [1.0 - (yy + zz), xy - wz, xz + wy],
        [xy + wz, 1.0 - (xx + zz), yz - wx],
        [xz - wy, yz + wx, 1.0 - (xx + yy)],
    ]
}

pub fn trs_to_matrix(t: [f32; 3], q: [f32; 4], s: [f32; 3]) -> [[f32; 4]; 4] {
    let r = quat_to_mat3(q);
    [
        [r[0][0] * s[0], r[0][1] * s[1], r[0][2] * s[2], t[0]],
        [r[1][0] * s[0], r[1][1] * s[1], r[1][2] * s[2], t[1]],
        [r[2][0] * s[0], r[2][1] * s[1], r[2][2] * s[2], t[2]],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

pub fn quat_mul(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    let [ax, ay, az, aw] = a;
    let [bx, by, bz, bw] = b;
    [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]
}

pub fn quat_slerp(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    let mut dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    let mut bq = b;
    if dot < 0.0 {
        bq = [-b[0], -b[1], -b[2], -b[3]];
        dot = -dot;
    }
    if dot > 0.9995 {
        let r = [
            a[0] + t * (bq[0] - a[0]),
            a[1] + t * (bq[1] - a[1]),
            a[2] + t * (bq[2] - a[2]),
            a[3] + t * (bq[3] - a[3]),
        ];
        let len = (r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]).sqrt();
        if len < 1e-8 {
            return [0.0, 0.0, 0.0, 1.0];
        }
        return [r[0] / len, r[1] / len, r[2] / len, r[3] / len];
    }
    let theta_0 = dot.acos();
    let theta = theta_0 * t;
    let sin_theta = theta.sin();
    let sin_theta_0 = theta_0.sin();
    let s0 = (theta_0 - theta).cos() - dot * sin_theta / sin_theta_0;
    let s1 = sin_theta / sin_theta_0;
    [
        a[0] * s0 + bq[0] * s1,
        a[1] * s0 + bq[1] * s1,
        a[2] * s0 + bq[2] * s1,
        a[3] * s0 + bq[3] * s1,
    ]
}

pub fn lerp3(a: [f32; 3], b: [f32; 3], t: f32) -> [f32; 3] {
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    ]
}

pub fn eval_translation(track: &[(f32, [f32; 3])], time: f32, default: [f32; 3]) -> [f32; 3] {
    if track.is_empty() {
        return default;
    }
    if time <= track[0].0 {
        return track[0].1;
    }
    let last = &track[track.len() - 1];
    if time >= last.0 {
        return last.1;
    }
    for i in 0..track.len() - 1 {
        let (t0, v0) = track[i];
        let (t1, v1) = track[i + 1];
        if time >= t0 && time <= t1 {
            if (t1 - t0).abs() < 1e-8 {
                return v1;
            }
            let f = (time - t0) / (t1 - t0);
            return lerp3(v0, v1, f);
        }
    }
    default
}

pub fn eval_rotation(track: &[(f32, [f32; 4])], time: f32, default: [f32; 4]) -> [f32; 4] {
    if track.is_empty() {
        return default;
    }
    if time <= track[0].0 {
        return track[0].1;
    }
    let last = &track[track.len() - 1];
    if time >= last.0 {
        return last.1;
    }
    for i in 0..track.len() - 1 {
        let (t0, v0) = track[i];
        let (t1, v1) = track[i + 1];
        if time >= t0 && time <= t1 {
            if (t1 - t0).abs() < 1e-8 {
                return v1;
            }
            let f = (time - t0) / (t1 - t0);
            return quat_slerp(v0, v1, f);
        }
    }
    default
}

pub fn eval_scale(track: &[(f32, [f32; 3])], time: f32, default: [f32; 3]) -> [f32; 3] {
    if track.is_empty() {
        return default;
    }
    if time <= track[0].0 {
        return track[0].1;
    }
    let last = &track[track.len() - 1];
    if time >= last.0 {
        return last.1;
    }
    for i in 0..track.len() - 1 {
        let (t0, v0) = track[i];
        let (t1, v1) = track[i + 1];
        if time >= t0 && time <= t1 {
            if (t1 - t0).abs() < 1e-8 {
                return v1;
            }
            let f = (time - t0) / (t1 - t0);
            return lerp3(v0, v1, f);
        }
    }
    default
}

fn find_parent_index(joint_node_index: usize, joints: &[gltf::Node]) -> Option<usize> {
    for (i, candidate) in joints.iter().enumerate() {
        for child in candidate.children() {
            if child.index() == joint_node_index {
                return Some(i);
            }
        }
    }
    None
}

fn compute_world_transforms(bones: &[Bone_data]) -> Vec<[[f32; 4]; 4]> {
    let n = bones.len();
    let mut world = vec![mat4_identity(); n];
    let mut computed = vec![false; n];
    let mut visiting = vec![false; n];

    fn resolve(
        i: usize,
        bones: &[Bone_data],
        world: &mut Vec<[[f32; 4]; 4]>,
        computed: &mut Vec<bool>,
        visiting: &mut Vec<bool>,
    ) {
        if computed[i] {
            return;
        }
        if visiting[i] {
            world[i] = trs_to_matrix(bones[i].translation, bones[i].rotation, bones[i].scale);
            computed[i] = true;
            return;
        }
        visiting[i] = true;
        let local = trs_to_matrix(bones[i].translation, bones[i].rotation, bones[i].scale);
        world[i] = match bones[i].parent {
            Some(p) if p < bones.len() && p != i => {
                resolve(p, bones, world, computed, visiting);
                mat4_mul(world[p], local)
            }
            _ => local,
        };
        computed[i] = true;
        visiting[i] = false;
    }

    for i in 0..n {
        resolve(i, bones, &mut world, &mut computed, &mut visiting);
    }
    world
}

fn convert_to_rgba(data: &gltf::image::Data) -> Option<(usize, usize, Vec<u8>)> {
    let w = data.width as usize;
    let h = data.height as usize;
    let pixels = &data.pixels;
    let mut rgba: Vec<u8> = Vec::with_capacity(w * h * 4);

    match data.format {
        gltf::image::Format::R8 => {
            for &p in pixels {
                rgba.extend_from_slice(&[p, p, p, 255]);
            }
        }
        gltf::image::Format::R8G8 => {
            for c in pixels.chunks(2) {
                rgba.extend_from_slice(&[c[0], c[1], 0, 255]);
            }
        }
        gltf::image::Format::R8G8B8 => {
            for c in pixels.chunks(3) {
                rgba.extend_from_slice(&[c[0], c[1], c[2], 255]);
            }
        }
        gltf::image::Format::R8G8B8A8 => {
            rgba.extend_from_slice(pixels);
        }
        gltf::image::Format::R16 => {
            for c in pixels.chunks(2) {
                let v = c[0];
                rgba.extend_from_slice(&[v, v, v, 255]);
            }
        }
        gltf::image::Format::R16G16 => {
            for c in pixels.chunks(4) {
                rgba.extend_from_slice(&[c[0], c[2], 0, 255]);
            }
        }
        gltf::image::Format::R16G16B16 => {
            for c in pixels.chunks(6) {
                rgba.extend_from_slice(&[c[0], c[2], c[4], 255]);
            }
        }
        gltf::image::Format::R16G16B16A16 => {
            for c in pixels.chunks(8) {
                rgba.extend_from_slice(&[c[0], c[2], c[4], c[6]]);
            }
        }
        _ => return None,
    }

    Some((w, h, rgba))
}

fn build_atlas(
    doc: &gltf::Document,
    images: &[gltf::image::Data],
    model_path: &Path,
) -> Option<(String, HashMap<usize, AtlasCell>, usize, usize)> {
    let mut image_indices: Vec<usize> = Vec::new();
    for mesh in doc.meshes() {
        for prim in mesh.primitives() {
            if let Some(info) = prim.material().pbr_metallic_roughness().base_color_texture() {
                let idx = info.texture().source().index();
                if !image_indices.contains(&idx) {
                    image_indices.push(idx);
                }
            }
        }
    }

    if image_indices.is_empty() {
        return None;
    }

    let mut loaded: Vec<(usize, usize, Vec<u8>)> = Vec::new();
    for &idx in &image_indices {
        let data = images.get(idx)?;
        let (w, h, rgba) = convert_to_rgba(data)?;
        loaded.push((w, h, rgba));
    }

    let max_w = loaded.iter().map(|(w, _, _)| *w).max().unwrap_or(1);
    let max_h = loaded.iter().map(|(_, h, _)| *h).max().unwrap_or(1);

    let n = loaded.len();
    let cols = (n as f64).sqrt().ceil() as usize;
    let rows = (n + cols - 1) / cols;

    let atlas_w = cols * max_w;
    let atlas_h = rows * max_h;

    let mut atlas = vec![0u8; atlas_w * atlas_h * 4];
    let mut cells: HashMap<usize, AtlasCell> = HashMap::new();

    for (i, (w, h, rgba)) in loaded.iter().enumerate() {
        let col = i % cols;
        let row = i / cols;
        let ox = col * max_w;
        let oy = row * max_h;

        for y in 0..*h {
            for x in 0..*w {
                let src = (y * w + x) * 4;
                let dst = ((oy + y) * atlas_w + (ox + x)) * 4;
                atlas[dst..dst + 4].copy_from_slice(&rgba[src..src + 4]);
            }
        }

        cells.insert(image_indices[i], AtlasCell { x: ox, y: oy, w: *w, h: *h });
    }

    let stem = model_path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("model");
    let dir = model_path.parent().unwrap_or_else(|| Path::new("."));
    let atlas_path: PathBuf = dir.join(format!("{}_atlas.png", stem));

    let img = image::RgbaImage::from_raw(atlas_w as u32, atlas_h as u32, atlas)?;
    img.save(&atlas_path).ok()?;

    Some((atlas_path.to_str()?.to_string(), cells, atlas_w, atlas_h))
}

pub fn Import_gltf(path: String) -> Option<Import_result> {
    let (doc, buffers, images) = gltf::import(&path).ok()?;

    let all_nodes: Vec<gltf::Node> = doc.nodes().collect();

    let model_path = Path::new(&path);
    let atlas_result = build_atlas(&doc, &images, model_path);

    let mut bones: Vec<Bone_data> = Vec::new();
    let mut node_to_bone: HashMap<usize, usize> = HashMap::new();
    let mut bone_names: Vec<String> = Vec::new();

    for (i, node) in all_nodes.iter().enumerate() {
        node_to_bone.insert(node.index(), i);
        let name = node
            .name()
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("bone_{}", i));
        let parent = find_parent_index(node.index(), &all_nodes);
        let (t, r, s) = node.transform().decomposed();
        bones.push(Bone_data {
            name: name.clone(),
            parent,
            translation: mirror_x_vec3(t),
            rotation: mirror_x_quat(r),
            scale: s,
            inverse_bind: mat4_identity(),
        });
        bone_names.push(name);
    }

    let bind_world = compute_world_transforms(&bones);
    for i in 0..bones.len() {
        bones[i].inverse_bind = mat4_inverse(bind_world[i]);
    }

    let mut vertices: Vec<f32> = Vec::new();
    let mut uvs_out: Vec<f32> = Vec::new();
    let mut joint_indices_out: Vec<[u8; 4]> = Vec::new();
    let mut joint_weights_out: Vec<[f32; 4]> = Vec::new();

    for node in all_nodes.iter() {
        let mesh = match node.mesh() {
            Some(m) => m,
            None => continue,
        };
        let bone_idx = match node_to_bone.get(&node.index()) {
            Some(&b) => b,
            None => continue,
        };
        if bone_idx > 255 {
            continue;
        }
        let bind_mat = bind_world[bone_idx];

        for prim in mesh.primitives() {
            let reader = prim.reader(|b| Some(&buffers[b.index()]));
            let positions: Vec<[f32; 3]> = match reader.read_positions() {
                Some(p) => p.collect(),
                None => continue,
            };
            let prim_uvs: Vec<[f32; 2]> = reader
                .read_tex_coords(0)
                .map(|t| t.into_f32().collect())
                .unwrap_or_default();
            let indices: Vec<u32> = reader
                .read_indices()
                .map(|i| i.into_u32().collect())
                .unwrap_or_else(|| (0..positions.len() as u32).collect());

            let cell = atlas_result.as_ref().and_then(|(_, cells, _, _)| {
                prim.material()
                    .pbr_metallic_roughness()
                    .base_color_texture()
                    .and_then(|info| {
                        let idx = info.texture().source().index();
                        cells.get(&idx).copied()
                    })
            });

            for tri in indices.chunks(3) {
                if tri.len() < 3 {
                    continue;
                }
                for &idx in tri {
                    let ii = idx as usize;
                    if ii >= positions.len() {
                        continue;
                    }
                    let p_local = mirror_x_vec3(positions[ii]);
                    let p_world = mat4_apply(bind_mat, p_local);
                    vertices.extend_from_slice(&p_world);

                    if !prim_uvs.is_empty() && ii < prim_uvs.len() {
                        if let (Some(c), Some((_, _, aw, ah))) = (cell, atlas_result.as_ref()) {
                            let u = prim_uvs[ii][0].clamp(0.0, 0.9999);
                            let v = prim_uvs[ii][1].clamp(0.0, 0.9999);
                            let au = (c.x as f32 + u * c.w as f32) / *aw as f32;
                            let av = (c.y as f32 + v * c.h as f32) / *ah as f32;
                            uvs_out.extend_from_slice(&[au, av]);
                        } else {
                            uvs_out.extend_from_slice(&prim_uvs[ii]);
                        }
                    } else {
                        uvs_out.extend_from_slice(&[0.0, 0.0]);
                    }

                    joint_indices_out.push([bone_idx as u8, 0, 0, 0]);
                    joint_weights_out.push([1.0, 0.0, 0.0, 0.0]);
                }
            }
        }
    }

    let uv_string = uvs_out
        .chunks(2)
        .map(|p| format!("{},{}", p[0], p[1]))
        .collect::<Vec<_>>()
        .join(" ");

    let mut animations: HashMap<String, Animation_data> = HashMap::new();
    let mut animation_names: Vec<String> = Vec::new();

    for anim in doc.animations() {
        let name = anim
            .name()
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("anim_{}", animation_names.len()));

        let mut tracks: HashMap<usize, Bone_tracks> = HashMap::new();
        let mut duration: f32 = 0.0;

        for channel in anim.channels() {
            let target = channel.target();
            let node_index = target.node().index();
            let bone_idx = match node_to_bone.get(&node_index) {
                Some(&b) => b,
                None => continue,
            };

            let sreader = channel.reader(|b| Some(&buffers[b.index()]));
            let input: Vec<f32> = match sreader.read_inputs() {
                Some(it) => it.collect(),
                None => continue,
            };
            if input.is_empty() {
                continue;
            }
            for &t in &input {
                if t > duration {
                    duration = t;
                }
            }

            let output = sreader.read_outputs();
            let out = match output {
                Some(o) => o,
                None => continue,
            };

            let entry = tracks.entry(bone_idx).or_insert_with(Bone_tracks::new);

            match (target.property(), out) {
                (gltf::animation::Property::Translation, ReadOutputs::Translations(iter)) => {
                    let vals: Vec<[f32; 3]> = iter.collect();
                    for (i, &t) in input.iter().enumerate() {
                        if let Some(v) = vals.get(i) {
                            entry.translation.push((t, mirror_x_vec3(*v)));
                        }
                    }
                }
                (gltf::animation::Property::Rotation, ReadOutputs::Rotations(rots)) => {
                    let vals: Vec<[f32; 4]> = rots.into_f32().collect();
                    for (i, &t) in input.iter().enumerate() {
                        if let Some(v) = vals.get(i) {
                            entry.rotation.push((t, mirror_x_quat(*v)));
                        }
                    }
                }
                (gltf::animation::Property::Scale, ReadOutputs::Scales(iter)) => {
                    let vals: Vec<[f32; 3]> = iter.collect();
                    for (i, &t) in input.iter().enumerate() {
                        if let Some(v) = vals.get(i) {
                            entry.scale.push((t, *v));
                        }
                    }
                }
                _ => {}
            }
        }

        animations.insert(
            name.clone(),
            Animation_data {
                name: name.clone(),
                duration,
                tracks,
            },
        );
        animation_names.push(name);
    }

    let texture_path = atlas_result
        .as_ref()
        .map(|(p, _, _, _)| p.clone())
        .unwrap_or_default();

    Some(Import_result {
        texture_path,
        uv: uv_string,
        uvs: uvs_out,
        vertices,
        joint_indices: joint_indices_out,
        joint_weights: joint_weights_out,
        bone_names,
        animation_names,
        skeleton: Arc::new(Skeleton_data { bones, animations }),
    })
}