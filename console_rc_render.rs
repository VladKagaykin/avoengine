use std::f32::consts::PI;

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

fn ray_triangle_intersect(origin: &[f32;3], dir: &[f32;3], v0: &[f32;3], v1: &[f32;3], v2: &[f32;3]) -> Option<f32> {
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
        Some(t)
    } else {
        None
    }
}

pub fn ray_tracing(start_x: f32, start_y: f32, start_z: f32, length: i128, pitch: f32, yaw: f32, roll: f32,
                   triangles: &[super::Draw_components]) -> super::Pixel_structure {
    let p = pitch * PI / 180.0;
    let y = yaw * PI / 180.0;
    let r = roll * PI / 180.0;
    
    let (cp, sp) = (p.cos(), p.sin());
    let (cy, sy) = (y.cos(), y.sin());
    let (cr, sr) = (r.cos(), r.sin());
    
    let dir = [sy * cp, -sp, cy * cp];
    let len = (dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]).sqrt();
    let dir = [dir[0]/len, dir[1]/len, dir[2]/len];
    
    let origin = [start_x, start_y, start_z];
    let max_t = length as f32;
    let mut best_t = max_t;
    let mut best_pixel = super::Empty_pixel.lock().unwrap().clone();
    
    for tri in triangles {
        let verts = &tri.draw_vertices;
        if verts.len() < 9 {
            continue;
        }
        let v0 = [verts[0] + tri.draw_x, verts[1] + tri.draw_y, verts[2] + tri.draw_z];
        let v1 = [verts[3] + tri.draw_x, verts[4] + tri.draw_y, verts[5] + tri.draw_z];
        let v2 = [verts[6] + tri.draw_x, verts[7] + tri.draw_y, verts[8] + tri.draw_z];
        if let Some(t) = ray_triangle_intersect(&origin, &dir, &v0, &v1, &v2) {
            if t > 0.0 && t < best_t {
                best_t = t;
                best_pixel = super::Pixel_structure {
                    pixel_symbol: tri.draw_symbol,
                    pixel_RGBA_color: tri.draw_RGBA_color,
                };
            }
        }
    }
    best_pixel
}

pub fn Render_3d_to_screen(triangles: &[super::Draw_components], screen: &mut Vec<Vec<super::Pixel_structure>>) {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width;
    let height = settings.window_height;
    drop(settings);

    let cam = super::Camera.lock().unwrap();
    let (ox, oy, oz) = (cam.camera_x, cam.camera_y, cam.camera_z);
    let pitch = cam.camera_pitch;    
    let yaw = cam.camera_yaw;          
    let roll = cam.camera_roll;      
    let fov = cam.camera_fov as f32;   
    let max_dist = cam.max_dist as i128;
    drop(cam);

    let half_fov_deg = fov / 2.0;
    let aspect = width as f32 / height as f32;

    for y in 0..height {
        for x in 0..width {
            let nx = (x as f32 / width as f32) * 2.0 - 1.0;
            let ny = -((y as f32 / height as f32) * 2.0 - 1.0);

            let delta_yaw_deg = nx * half_fov_deg * aspect;
            let delta_pitch_deg = ny * half_fov_deg;

            let pixel = ray_tracing(
                ox, oy, oz,
                max_dist,
                pitch + delta_pitch_deg,  
                yaw + delta_yaw_deg,       
                roll,                      
                triangles,
            );
            screen[y as usize][x as usize] = pixel;
        }
    }
}

pub fn Render_image_to_console() -> Result<(),String>{
    let mut queue_2d:Vec<super::Draw_components> = Vec::new();
    let mut queue_3d:Vec<super::Draw_components> = Vec::new();

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
            };
            triangles.push(tri);
        }
    }

    Render_3d_to_screen(&triangles, &mut screen);

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
        for x in start_x..end_x {
            for y in start_y..end_y {
                if point_in_polygon_offset(x as f32, y as f32, &object.draw_vertices, object.draw_x, object.draw_y) {
                    screen[y as usize][x as usize] = super::Pixel_structure {
                        pixel_symbol: object.draw_symbol,
                        pixel_RGBA_color: [object.draw_RGBA_color[0], object.draw_RGBA_color[1], 
                                           object.draw_RGBA_color[2], object.draw_RGBA_color[3]]
                    };
                }
            }
        }
    }

    drop(width);
    drop(height);
    drop(screen);

    Ok(())
}