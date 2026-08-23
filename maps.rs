use std::fs::File;
use std::io::{self, BufRead, BufReader, Write};
use std::sync::Mutex;

use crate::{
    Draw_components, Light_components, Script, Static_light, Static_scene, Map_objects, Map_scripts,
    Draw_queue, Light_queue, tick_system,
};

fn extract_args(s: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut paren_depth = 0;
    let mut in_string = false;
    let mut escape = false;
    let chars: Vec<char> = s.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let ch = chars[i];
        if escape {
            current.push(ch);
            escape = false;
            i += 1;
            continue;
        }
        if ch == '\\' {
            escape = true;
            current.push(ch);
            i += 1;
            continue;
        }
        if ch == '"' && !in_string {
            in_string = true;
            current.push(ch);
            i += 1;
            continue;
        }
        if ch == '"' && in_string {
            in_string = false;
            current.push(ch);
            i += 1;
            continue;
        }
        if in_string {
            current.push(ch);
            i += 1;
            continue;
        }
        if ch == '(' || ch == '[' || ch == '{' {
            paren_depth += 1;
            current.push(ch);
            i += 1;
        } else if ch == ')' || ch == ']' || ch == '}' {
            paren_depth -= 1;
            current.push(ch);
            i += 1;
        } else if ch == ',' && paren_depth == 0 {
            let trimmed = current.trim();
            if !trimmed.is_empty() {
                args.push(trimmed.to_string());
            }
            current.clear();
            i += 1;
        } else {
            current.push(ch);
            i += 1;
        }
    }
    let trimmed = current.trim();
    if !trimmed.is_empty() {
        args.push(trimmed.to_string());
    }
    args
}

fn parse_f32(s: &str) -> io::Result<f32> {
    let s = s.trim();
    if s.is_empty() {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "Empty f32"));
    }
    s.parse::<f32>()
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, format!("Invalid f32: {}", e)))
}

fn parse_char(s: &str) -> io::Result<char> {
    let s = s.trim();
    if s.len() >= 3 && s.starts_with('\'') && s.ends_with('\'') {
        let inner = &s[1..s.len()-1];
        if inner == "\\n" {
            Ok('\n')
        } else if inner == "\\t" {
            Ok('\t')
        } else if inner == "\\r" {
            Ok('\r')
        } else if inner == "\\\\" {
            Ok('\\')
        } else if inner == "\\'" {
            Ok('\'')
        } else {
            let mut chars = inner.chars();
            if let Some(ch) = chars.next() {
                if chars.next().is_none() {
                    Ok(ch)
                } else {
                    Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid char literal: more than one character"))
                }
            } else {
                Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid char literal: empty"))
            }
        }
    } else {
        Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid char literal format"))
    }
}

fn parse_string(s: &str) -> io::Result<String> {
    let s = s.trim();
    if s.len() >= 2 && s.starts_with('"') && s.ends_with('"') {
        let inner = &s[1..s.len()-1];
        let mut result = String::new();
        let mut chars = inner.chars();
        while let Some(ch) = chars.next() {
            if ch == '\\' {
                if let Some(next) = chars.next() {
                    match next {
                        'n' => result.push('\n'),
                        't' => result.push('\t'),
                        'r' => result.push('\r'),
                        '\\' => result.push('\\'),
                        '"' => result.push('"'),
                        _ => result.push(next),
                    }
                }
            } else {
                result.push(ch);
            }
        }
        Ok(result)
    } else {
        Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid string literal"))
    }
}

fn parse_f32_vec(s: &str) -> io::Result<Vec<f32>> {
    let s = s.trim();
    if s.starts_with('[') && s.ends_with(']') {
        let inner = &s[1..s.len()-1];
        if inner.trim().is_empty() {
            return Ok(Vec::new());
        }
        let parts: Vec<&str> = inner.split(',').map(|x| x.trim()).filter(|x| !x.is_empty()).collect();
        let mut result = Vec::with_capacity(parts.len());
        for part in parts {
            result.push(parse_f32(part)?);
        }
        Ok(result)
    } else {
        Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid array literal"))
    }
}

fn parse_u8_array<const N: usize>(s: &str) -> io::Result<[u8; N]> {
    let s = s.trim();
    if s.starts_with('[') && s.ends_with(']') {
        let inner = &s[1..s.len()-1];
        let parts: Vec<&str> = inner.split(',').map(|x| x.trim()).filter(|x| !x.is_empty()).collect();
        if parts.len() != N {
            return Err(io::Error::new(io::ErrorKind::InvalidData, format!("Expected {} elements, got {}", N, parts.len())));
        }
        let mut result = [0u8; N];
        for (i, part) in parts.iter().enumerate() {
            let val = part.parse::<u8>()
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, format!("Invalid u8: {}", e)))?;
            result[i] = val;
        }
        Ok(result)
    } else {
        Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid array literal"))
    }
}

fn parse_draw_components(line: &str) -> io::Result<Draw_components> {
    let line = line.trim();
    if !line.starts_with("Draw_components(") || !line.ends_with(')') {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid Draw_components format"));
    }
    let args_str = &line[16..line.len()-1];
    let args = extract_args(args_str);
    if args.len() != 9 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, format!("Expected 9 arguments, got {}", args.len())));
    }
    Ok(Draw_components {
        draw_type: parse_string(&args[0])?,
        draw_x: parse_f32(&args[1])?,
        draw_y: parse_f32(&args[2])?,
        draw_z: parse_f32(&args[3])?,
        draw_symbol: parse_char(&args[4])?,
        draw_vertices: parse_f32_vec(&args[5])?,
        draw_RGBA_color: parse_u8_array::<4>(&args[6])?,
        draw_texture_path: parse_string(&args[7])?,
        draw_special_name: parse_string(&args[8])?,
    })
}

fn parse_light_components(line: &str) -> io::Result<Light_components> {
    let line = line.trim();
    if !line.starts_with("Light_components(") || !line.ends_with(')') {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid Light_components format"));
    }
    let args_str = &line[17..line.len()-1];
    let args = extract_args(args_str);
    if args.len() != 9 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, format!("Expected 9 arguments, got {}", args.len())));
    }
    Ok(Light_components {
        light_x: parse_f32(&args[0])?,
        light_y: parse_f32(&args[1])?,
        light_z: parse_f32(&args[2])?,
        light_RGB_color: parse_u8_array::<3>(&args[3])?,
        light_distance: parse_f32(&args[4])?,
        light_cone_angle: parse_f32(&args[5])?,
        light_pitch: parse_f32(&args[6])?,
        light_yaw: parse_f32(&args[7])?,
        light_special_name: parse_string(&args[8])?,
    })
}

fn parse_script(line: &str) -> io::Result<Script> {
    let line = line.trim();
    if !line.starts_with("Script(") || !line.ends_with(')') {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "Invalid Script format"));
    }
    let args_str = &line[7..line.len()-1];
    let args = extract_args(args_str);
    if args.len() != 2 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, format!("Expected 2 arguments, got {}", args.len())));
    }
    Ok(Script {
        function: parse_string(&args[0])?,
        content: parse_string(&args[1])?,
    })
}

fn format_string(s: &str) -> String {
    let mut escaped = String::new();
    for ch in s.chars() {
        match ch {
            '\n' => escaped.push_str("\\n"),
            '\t' => escaped.push_str("\\t"),
            '\r' => escaped.push_str("\\r"),
            '\\' => escaped.push_str("\\\\"),
            '"' => escaped.push_str("\\\""),
            _ => escaped.push(ch),
        }
    }
    format!("\"{}\"", escaped)
}

fn format_f32_vec(v: &[f32]) -> String {
    let parts: Vec<String> = v.iter().map(|x| format!("{}", x)).collect();
    format!("[{}]", parts.join(", "))
}

fn format_u8_array<const N: usize>(arr: &[u8; N]) -> String {
    let parts: Vec<String> = arr.iter().map(|x| format!("{}", x)).collect();
    format!("[{}]", parts.join(", "))
}

fn format_char(c: char) -> String {
    let mut s = String::new();
    match c {
        '\'' => s.push_str("'\\''"),
        '\\' => s.push_str("'\\\\'"),
        _ => s = format!("'{}'", c),
    }
    s
}

use rhai::*;

pub fn Do_all_scripts() {
    let scripts = Map_scripts.lock().unwrap().clone();
    if scripts.is_empty() {
        return;
    }

    let mut engine = Engine::new();

    engine.register_fn("get_map_object", |key: &str| {
        Map_objects.lock().unwrap().get(key).cloned().unwrap_or_default()
    });
    engine.register_fn("set_map_object", |key: &str, value: &str| {
        let mut map = Map_objects.lock().unwrap();
        map.insert(key.to_string(), value.to_string());
    });
    engine.register_fn("push_draw", |comp: Draw_components| {
        Draw_queue.lock().unwrap().push(comp);
    });
    engine.register_fn("push_light", |light: Light_components| {
        Light_queue.lock().unwrap().push(light);
    });
    engine.register_fn("get_tick", || tick_system::Get_tick());

    engine.register_fn("push_static_draw", |comp: Draw_components| {
        Static_scene.lock().unwrap().push(comp);
    });
    engine.register_fn("push_static_light", |light: Light_components| {
        Static_light.lock().unwrap().push(light);
    });
    engine.register_fn("clear_dynamic_draw", || {
        Draw_queue.lock().unwrap().clear();
    });
    engine.register_fn("clear_dynamic_light", || {
        Light_queue.lock().unwrap().clear();
    });
    engine.register_fn("clear_static_scene", || {
        Static_scene.lock().unwrap().clear();
    });
    engine.register_fn("clear_static_light", || {
        Static_light.lock().unwrap().clear();
    });

    engine.register_fn("get_camera_fov", || {
        super::Camera.lock().unwrap().camera_fov
    });
    engine.register_fn("get_camera_x", || {
        super::Camera.lock().unwrap().camera_x
    });
    engine.register_fn("get_camera_y", || {
        super::Camera.lock().unwrap().camera_y
    });
    engine.register_fn("get_camera_z", || {
        super::Camera.lock().unwrap().camera_z
    });
    engine.register_fn("get_camera_pitch", || {
        super::Camera.lock().unwrap().camera_pitch
    });
    engine.register_fn("get_camera_yaw", || {
        super::Camera.lock().unwrap().camera_yaw
    });
    engine.register_fn("get_camera_roll", || {
        super::Camera.lock().unwrap().camera_roll
    });
    engine.register_fn("get_max_dist", || {
        super::Camera.lock().unwrap().max_dist
    });
    engine.register_fn("get_ambient_light", || {
        let ambient = super::Camera.lock().unwrap().ambient_light;
        rhai::Array::from_iter(ambient.iter().map(|&v| Dynamic::from_int(v as i64)))
    });

    engine.register_fn("set_scene_changed", |val: bool| {
        let mut flag = super::Is_scene_changed.lock().unwrap();
        *flag = val;
    });

    engine.register_type::<Draw_components>();
    engine.register_type::<Light_components>();

    engine.register_fn("Draw_components", |draw_type: &str, x: f64, y: f64, z: f64, symbol: char, vertices: Array, color: Array, tex: &str, name: &str| {
        let vertices: Vec<f32> = vertices
            .into_iter()
            .map(|v| v.as_float().unwrap_or(0.0) as f32)
            .collect();

        let color: [u8; 4] = [
            color.get(0).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
            color.get(1).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
            color.get(2).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
            color.get(3).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
        ];

        Draw_components {
            draw_type: draw_type.to_string(),
            draw_x: x as f32,
            draw_y: y as f32,
            draw_z: z as f32,
            draw_symbol: symbol,
            draw_vertices: vertices,
            draw_RGBA_color: color,
            draw_texture_path: tex.to_string(),
            draw_special_name: name.to_string(),
        }
    });

    engine.register_fn("Light_components", |x: f64, y: f64, z: f64, color: Array, dist: f64, cone: f64, pitch: f64, yaw: f64, name: &str| {
        let color: [u8; 3] = [
            color.get(0).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
            color.get(1).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
            color.get(2).and_then(|v| v.as_int().ok()).unwrap_or(0) as u8,
        ];

        Light_components {
            light_x: x as f32,
            light_y: y as f32,
            light_z: z as f32,
            light_RGB_color: color,
            light_distance: dist as f32,
            light_cone_angle: cone as f32,
            light_pitch: pitch as f32,
            light_yaw: yaw as f32,
            light_special_name: name.to_string(),
        }
    });

    for script in scripts {
        if script.function == "on_tick" {
            let ast = match engine.compile(&script.content) {
                Ok(ast) => ast,
                Err(e) => {
                    eprintln!("[Script] Compilation error: {}", e);
                    continue;
                }
            };
            let mut scope = Scope::new();
            if let Err(e) = engine.run_ast_with_scope(&mut scope, &ast) {
                eprintln!("[Script] Execution error: {}", e);
            }
        }
    }
}

pub fn Save_map(path: String) -> io::Result<()> {
    let mut file = File::create(&path)?;

    writeln!(file, "// Map_objects")?;
    {
        let objects = Map_objects.lock().unwrap();
        for (key, value) in objects.iter() {
            writeln!(file, "{} = {}", format_string(key), format_string(value))?;
        }
    }
    writeln!(file)?;

    writeln!(file, "// Static_scene")?;
    {
        let scene = Static_scene.lock().unwrap();
        for comp in scene.iter() {
            let line = format!(
                "Draw_components({}, {}, {}, {}, {}, {}, {}, {}, {})",
                format_string(&comp.draw_type),
                comp.draw_x,
                comp.draw_y,
                comp.draw_z,
                format_char(comp.draw_symbol),
                format_f32_vec(&comp.draw_vertices),
                format_u8_array(&comp.draw_RGBA_color),
                format_string(&comp.draw_texture_path),
                format_string(&comp.draw_special_name),
            );
            writeln!(file, "{}", line)?;
        }
    }
    writeln!(file)?;

    writeln!(file, "// Static_light")?;
    {
        let lights = Static_light.lock().unwrap();
        for light in lights.iter() {
            let line = format!(
                "Light_components({}, {}, {}, {}, {}, {}, {}, {}, {})",
                light.light_x,
                light.light_y,
                light.light_z,
                format_u8_array(&light.light_RGB_color),
                light.light_distance,
                light.light_cone_angle,
                light.light_pitch,
                light.light_yaw,
                format_string(&light.light_special_name),
            );
            writeln!(file, "{}", line)?;
        }
    }
    writeln!(file)?;

    writeln!(file, "// Map_scripts")?;
    {
        let scripts = Map_scripts.lock().unwrap();
        for script in scripts.iter() {
            let line = format!(
                "Script({}, {})",
                format_string(&script.function),
                format_string(&script.content),
            );
            writeln!(file, "{}", line)?;
        }
    }

    println!("Successfully saved map to {}", path);
    Ok(())
}

pub fn Load_map(path: String) -> io::Result<()> {
    let file = File::open(&path)?;
    let reader = BufReader::new(file);
    let lines: Vec<String> = reader.lines().collect::<Result<_, _>>()?;

    {
        Map_scripts.lock().unwrap().clear();
        Map_objects.lock().unwrap().clear();
        Static_light.lock().unwrap().clear();
        Static_scene.lock().unwrap().clear();
    }

    let mut current_section = String::new();
    let mut error_count = 0;
    let mut i = 0;
    while i < lines.len() {
        let line = lines[i].clone();
        let trimmed = line.trim();
        if trimmed.is_empty() {
            i += 1;
            continue;
        }

        if trimmed.starts_with("// Map_objects") {
            current_section = "objects".to_string();
            i += 1;
            continue;
        } else if trimmed.starts_with("// Static_scene") {
            current_section = "scene".to_string();
            i += 1;
            continue;
        } else if trimmed.starts_with("// Static_light") {
            current_section = "lights".to_string();
            i += 1;
            continue;
        } else if trimmed.starts_with("// Map_scripts") {
            current_section = "scripts".to_string();
            i += 1;
            continue;
        }

        match current_section.as_str() {
            "objects" => {
                if let Some(eq_pos) = trimmed.find('=') {
                    let key = trimmed[..eq_pos].trim();
                    let value = trimmed[eq_pos+1..].trim();
                    if !key.is_empty() && !value.is_empty() {
                        match (parse_string(key), parse_string(value)) {
                            (Ok(k), Ok(v)) => {
                                Map_objects.lock().unwrap().insert(k, v);
                            }
                            (Err(e), _) | (_, Err(e)) => {
                                eprintln!("[LoadMap] Line {}: object parse error: {}", i+1, e);
                                error_count += 1;
                            }
                        }
                    }
                }
                i += 1;
            }
            "scene" => {
                match parse_draw_components(trimmed) {
                    Ok(comp) => Static_scene.lock().unwrap().push(comp),
                    Err(e) => {
                        eprintln!("[LoadMap] Line {}: failed to parse Draw_components: {}", i+1, e);
                        error_count += 1;
                    }
                }
                i += 1;
            }
            "lights" => {
                match parse_light_components(trimmed) {
                    Ok(light) => Static_light.lock().unwrap().push(light),
                    Err(e) => {
                        eprintln!("[LoadMap] Line {}: failed to parse Light_components: {}", i+1, e);
                        error_count += 1;
                    }
                }
                i += 1;
            }
            "scripts" => {
                let mut script_block = String::new();
                while i < lines.len() {
                    let next_line = lines[i].clone();
                    let next_trimmed = next_line.trim();
                    if next_trimmed.starts_with("// Map_") {
                        break;
                    }
                    if next_trimmed.is_empty() {
                        i += 1;
                        continue;
                    }
                    script_block.push_str(&next_line);
                    script_block.push('\n');
                    i += 1;
                }
                let mut pos = 0;
                while let Some(start) = script_block[pos..].find("Script(") {
                    let start = pos + start;
                    let mut depth = 0;
                    let mut in_string = false;
                    let mut escape = false;
                    let mut end = start;
                    for (idx, ch) in script_block[start..].char_indices() {
                        let global_idx = start + idx;
                        if escape {
                            escape = false;
                            continue;
                        }
                        if ch == '\\' {
                            escape = true;
                            continue;
                        }
                        if ch == '"' {
                            in_string = !in_string;
                            continue;
                        }
                        if in_string {
                            continue;
                        }
                        if ch == '(' {
                            depth += 1;
                        } else if ch == ')' {
                            depth -= 1;
                            if depth == 0 {
                                end = global_idx + 1;
                                break;
                            }
                        }
                    }
                    if end > start {
                        let script_str = &script_block[start..end];
                        match parse_script(script_str) {
                            Ok(script) => Map_scripts.lock().unwrap().push(script),
                            Err(e) => {
                                eprintln!("[LoadMap] Failed to parse Script: {}", e);
                                error_count += 1;
                            }
                        }
                        pos = end;
                    } else {
                        break;
                    }
                }
            }
            _ => {
                i += 1;
            }
        }
    }

    if error_count > 0 {
        eprintln!("[LoadMap] Loaded with {} errors. Some objects may be missing.", error_count);
    } else {
        println!("[LoadMap] Map loaded successfully.");
    }

    let mut examination = super::Is_scene_changed.lock().unwrap();
    *examination = true;
    drop(examination);

    Ok(())
}