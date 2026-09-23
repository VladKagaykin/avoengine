use std::fs;
use std::path::Path;

pub fn load_obj_and_texture(folder_path: &str) -> (Vec<f32>, Vec<f32>, String, String) {
    let mut obj_path = String::new();
    let mut tex_path = String::new();
    
    let properties_path = Path::new(folder_path).join("properties.txt");
    let properties_content = fs::read_to_string(properties_path).unwrap_or_default();

    for entry in fs::read_dir(folder_path).unwrap() {
        let path = entry.unwrap().path();
        if let Some(ext) = path.extension() {
            let ext_str = ext.to_str().unwrap().to_lowercase();
            if ext_str == "obj" {
                obj_path = path.to_str().unwrap().to_string();
            } else if ext_str == "png" || ext_str == "jpg" || ext_str == "jpeg" {
                tex_path = path.to_str().unwrap().to_string();
            }
        }
    }
    
    let content = fs::read_to_string(&obj_path).unwrap();
    let mut v_pos = Vec::<[f32; 3]>::new();
    let mut v_tex = Vec::<[f32; 2]>::new();
    let mut out_vertices = Vec::new();
    let mut out_uvs = Vec::new();
    
    for line in content.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.is_empty() {
            continue;
        }
        match parts[0] {
            "v" => {
                let x = parts[1].parse().unwrap();
                let y = parts[2].parse().unwrap();
                let z = parts[3].parse().unwrap();
                v_pos.push([x, y, z]);
            }
            "vt" => {
                let u = parts[1].parse().unwrap();
                let v = parts[2].parse().unwrap();
                v_tex.push([u, v]);
            }
            "f" => {
                let mut face_v = Vec::new();
                let mut face_vt = Vec::new();
                for i in 1..parts.len() {
                    let indices: Vec<&str> = parts[i].split('/').collect();
                    let v_idx = indices[0].parse::<usize>().unwrap() - 1;
                    face_v.push(v_idx);
                    if indices.len() > 1 && !indices[1].is_empty() {
                        let vt_idx = indices[1].parse::<usize>().unwrap() - 1;
                        face_vt.push(vt_idx);
                    } else {
                        face_vt.push(0);
                    }
                }
                for i in 1..face_v.len() - 1 {
                    out_vertices.extend_from_slice(&v_pos[face_v[0]]);
                    out_vertices.extend_from_slice(&v_pos[face_v[i]]);
                    out_vertices.extend_from_slice(&v_pos[face_v[i + 1]]);
                    out_uvs.extend_from_slice(&v_tex[face_vt[0]]);
                    out_uvs.extend_from_slice(&v_tex[face_vt[i]]);
                    out_uvs.extend_from_slice(&v_tex[face_vt[i + 1]]);
                }
            }
            _ => {}
        }
    }
    
    (out_vertices, out_uvs, tex_path, properties_content)
}