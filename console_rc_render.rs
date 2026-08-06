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

fn point_in_polygon_3d(px: f32, py: f32, pz: f32, vertices: &[f32], ox: f32, oy: f32, oz: f32) -> bool {
    let n = vertices.len() / 3;
    if n < 3 { return false; }
    
    let x1 = vertices[0] + ox; let y1 = vertices[1] + oy; let z1 = vertices[2] + oz;
    let x2 = vertices[3] + ox; let y2 = vertices[4] + oy; let z2 = vertices[5] + oz;
    let x3 = vertices[6] + ox; let y3 = vertices[7] + oy; let z3 = vertices[8] + oz;
    
    let nx = (y2-y1)*(z3-z1) - (z2-z1)*(y3-y1);
    let ny = (z2-z1)*(x3-x1) - (x2-x1)*(z3-z1);
    let nz = (x2-x1)*(y3-y1) - (y2-y1)*(x3-x1);
    
    if (nx*(px-x1) + ny*(py-y1) + nz*(pz-z1)).abs() > 0.001 { return false; }
    
    let (a1, a2) = if nx.abs() >= ny.abs() && nx.abs() >= nz.abs() { (1, 2) } 
                   else if ny.abs() >= nx.abs() && ny.abs() >= nz.abs() { (0, 2) } 
                   else { (0, 1) };
    
    let (ppx, ppy) = match (a1, a2) {
        (0, 1) => (px, py),
        (0, 2) => (px, pz),
        (1, 2) => (py, pz),
        _ => (0.0, 0.0)
    };
    
    let mut pv = Vec::new();
    for i in 0..n {
        let idx = i * 3;
        let (vx, vy, vz) = (vertices[idx]+ox, vertices[idx+1]+oy, vertices[idx+2]+oz);
        let (p, q) = match (a1, a2) {
            (0, 1) => (vx, vy),
            (0, 2) => (vx, vz),
            (1, 2) => (vy, vz),
            _ => (0.0, 0.0)
        };
        pv.push(p); pv.push(q);
    }
    
    let mut inside = false;
    for i in 0..n {
        let j = (i + 1) % n;
        let xi = pv[2*i]; let yi = pv[2*i+1];
        let xj = pv[2*j]; let yj = pv[2*j+1];
        if ((yi > ppy) != (yj > ppy)) && (ppx < (xj - xi) * (ppy - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    inside
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

pub fn ray_trace(start_x: f32, start_y: f32, start_z: f32, length: i128, pitch: f32, yaw: f32, roll: f32) -> super::Pixel_structure {
    let matrix = super::Matrix.lock().unwrap();
    let matrix_size = matrix.len() as i128;
    
    let p = pitch * PI / 180.0;
    let y = yaw * PI / 180.0;
    let r = roll * PI / 180.0;
    
    let (cp, sp) = (p.cos(), p.sin());
    let (cy, sy) = (y.cos(), y.sin());
    let (cr, sr) = (r.cos(), r.sin());
    
    let vx = sy * cp;
    let vy = -sp;
    let vz = cy * cp;
    
    let mut temp_pixel = super::Pixel_structure { pixel_symbol: 'E', pixel_RGBA_color: [0, 0, 0, 0] };
    let mut pixel_match = false;
    
    for i in 0..length {
        let end_x = start_x + (i as f32) * vx;
        let end_y = start_y + (i as f32) * vy;
        let end_z = start_z + (i as f32) * vz;
        
        let ix = end_x.round() as i128;
        let iy = end_y.round() as i128;
        let iz = end_z.round() as i128;
        
        if ix < 0 || ix >= matrix_size || iy < 0 || iy >= matrix_size || iz < 0 || iz >= matrix_size {
            break;
        }
        
        let ux = ix as usize;
        let uy = iy as usize;
        let uz = iz as usize;
        
        if !matrix[ux][uy][uz].is_empty {
            let alpha = matrix[ux][uy][uz].unit_RGBA_color[3];
            let percent = (alpha as f32) / 255.0;
            
            if alpha < 255 {
                let r_add = (matrix[ux][uy][uz].unit_RGBA_color[0] as f32 * percent) as u8;
                let g_add = (matrix[ux][uy][uz].unit_RGBA_color[1] as f32 * percent) as u8;
                let b_add = (matrix[ux][uy][uz].unit_RGBA_color[2] as f32 * percent) as u8;
                
                temp_pixel.pixel_RGBA_color[0] = if temp_pixel.pixel_RGBA_color[0] + r_add <= 255 {
                    temp_pixel.pixel_RGBA_color[0] + r_add
                } else { 255 };
                temp_pixel.pixel_RGBA_color[1] = if temp_pixel.pixel_RGBA_color[1] + g_add <= 255 {
                    temp_pixel.pixel_RGBA_color[1] + g_add
                } else { 255 };
                temp_pixel.pixel_RGBA_color[2] = if temp_pixel.pixel_RGBA_color[2] + b_add <= 255 {
                    temp_pixel.pixel_RGBA_color[2] + b_add
                } else { 255 };
            } else {
                temp_pixel.pixel_RGBA_color[0] = if temp_pixel.pixel_RGBA_color[0] + matrix[ux][uy][uz].unit_RGBA_color[0] <= 255 {
                    temp_pixel.pixel_RGBA_color[0] + matrix[ux][uy][uz].unit_RGBA_color[0]
                } else { 255 };
                temp_pixel.pixel_RGBA_color[1] = if temp_pixel.pixel_RGBA_color[1] + matrix[ux][uy][uz].unit_RGBA_color[1] <= 255 {
                    temp_pixel.pixel_RGBA_color[1] + matrix[ux][uy][uz].unit_RGBA_color[1]
                } else { 255 };
                temp_pixel.pixel_RGBA_color[2] = if temp_pixel.pixel_RGBA_color[2] + matrix[ux][uy][uz].unit_RGBA_color[2] <= 255 {
                    temp_pixel.pixel_RGBA_color[2] + matrix[ux][uy][uz].unit_RGBA_color[2]
                } else { 255 };
                pixel_match = true;
                temp_pixel.pixel_symbol = matrix[ux][uy][uz].unit_symbol;
                break;
            }
        }
    }
    
    if !pixel_match {
        temp_pixel.pixel_symbol = super::Empty_pixel.lock().unwrap().clone().pixel_symbol;
        temp_pixel.pixel_RGBA_color = [0, 0, 0, 0];
    }
    
    drop(matrix);
    temp_pixel
}

pub fn Render_matrix() {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width as usize;
    let height = settings.window_height as usize;
    drop(settings);

    let camera = super::Camera.lock().unwrap();
    let fov_rad = (camera.camera_fov as f32) * std::f32::consts::PI / 180.0;
    let aspect = width as f32 / height as f32;
    let max_dist = camera.max_dist as i128;
    drop(camera);

    let empty_pixel = super::Empty_pixel.lock().unwrap().clone();
    let mut new_screen = vec![vec![empty_pixel.clone(); width]; height];
    drop(empty_pixel);

    let mut pixel_count = 0;
    for py in 0..height {
        for px in 0..width {
            let ndc_x = (2.0 * px as f32 / width as f32) - 1.0;
            let ndc_y = (2.0 * py as f32 / height as f32) - 1.0;
            
            let camera = super::Camera.lock().unwrap();
            let pixel = ray_trace(
                camera.camera_x,
                camera.camera_y,
                camera.camera_z,
                max_dist,
                camera.camera_pitch + ndc_y * (fov_rad / 2.0),
                camera.camera_yaw + ndc_x * (fov_rad / 2.0) * aspect,
                camera.camera_roll
            );
            drop(camera);
            
            new_screen[height - 1 - py][px] = pixel;
        }
    }

    let mut screen = super::Screen.lock().unwrap();
    *screen = new_screen;
    drop(screen);
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

    for object in queue_3d {
        let mut matrix = super::Matrix.lock().unwrap();
        let matrix_size = matrix.len();
        
        let mut biggest_x:f32 = 0.0;
        let mut biggest_y:f32 = 0.0;
        let mut biggest_z:f32 = 0.0;
        let mut smallest_x:f32 = matrix_size as f32;
        let mut smallest_y:f32 = matrix_size as f32;
        let mut smallest_z:f32 = matrix_size as f32;
        
        for i in (0..object.draw_vertices.len()).step_by(3) {
            let vx = object.draw_vertices[i] + object.draw_x;
            let vy = object.draw_vertices[i+1] + object.draw_y;
            let vz = object.draw_vertices[i+2] + object.draw_z;
            
            biggest_x = if vx > biggest_x { vx } else { biggest_x };
            biggest_y = if vy > biggest_y { vy } else { biggest_y };
            biggest_z = if vz > biggest_z { vz } else { biggest_z };
            smallest_x = if vx < smallest_x { vx } else { smallest_x };
            smallest_y = if vy < smallest_y { vy } else { smallest_y };
            smallest_z = if vz < smallest_z { vz } else { smallest_z };
        }
        
        let start_x = if smallest_x < 0.0 { 0 } else { smallest_x as usize };
        let end_x = if biggest_x >= matrix_size as f32 { matrix_size } else { (biggest_x as usize) + 1 };
        let start_y = if smallest_y < 0.0 { 0 } else { smallest_y as usize };
        let end_y = if biggest_y >= matrix_size as f32 { matrix_size } else { (biggest_y as usize) + 1 };
        let start_z = if smallest_z < 0.0 { 0 } else { smallest_z as usize };
        let end_z = if biggest_z >= matrix_size as f32 { matrix_size } else { (biggest_z as usize) + 1 };
        
        for x in start_x..end_x {
            for y in start_y..end_y {
                for z in start_z..end_z {
                    if point_in_polygon_3d(x as f32, y as f32, z as f32, &object.draw_vertices, object.draw_x, object.draw_y, object.draw_z) {
                        matrix[x][y][z] = super::Matrix_unit {
                            is_empty: false,
                            unit_symbol: object.draw_symbol,
                            unit_RGBA_color: object.draw_RGBA_color,
                        };
                    }
                }
            }
        }
        drop(matrix);
    }

    Render_matrix();

    let mut screen = super::Screen.lock().unwrap();

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

    drop(screen);
    drop(width);
    drop(height);

    Ok(())
}