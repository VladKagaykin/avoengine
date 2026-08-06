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

fn point_in_polygon_offset(px: f64, py: f64, vertices: &[f64], offset_x: f64, offset_y: f64) -> bool {
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

pub fn Render_image_to_console() -> Result<(),String>{
    let mut queue_2d:Vec<super::Draw_components> = Vec::new();

    let all_queue= super::Draw_queue.lock().unwrap();

    for object in all_queue.iter(){
        if object.draw_type == "2d_object".to_string(){
            queue_2d.push(object.clone());
        }
    }

    let width = super::Engine_settings.lock().unwrap().window_width as i128;
    let height = super::Engine_settings.lock().unwrap().window_height as i128;

    let mut screen = super::Screen.lock().unwrap();

    for object in queue_2d{
        let mut biggest_x:f64 = 0.0;
        let mut biggest_y:f64 = 0.0;
        let mut smallest_x:f64 = width as f64;
        let mut smallest_y:f64 = height as f64; 
        for i in (0..(object.draw_vertices.len()-1)).step_by(2) {
            biggest_x = if object.draw_vertices[i]+object.draw_x > biggest_x {object.draw_vertices[i]+object.draw_x}else{biggest_x};
            biggest_y = if object.draw_vertices[i+1]+object.draw_y > biggest_y {object.draw_vertices[i+1]+object.draw_y}else{biggest_y};
            smallest_x = if object.draw_vertices[i]+object.draw_x < smallest_x {object.draw_vertices[i]+object.draw_x}else{smallest_x};
            smallest_y = if object.draw_vertices[i+1]+object.draw_y < smallest_y {object.draw_vertices[i+1]+object.draw_y}else{smallest_y};
        }
        let start_x = if smallest_x < 0.0 { 0 } else { smallest_x as i128 };
        let end_x = if biggest_x > width as f64 { width } else { biggest_x as i128 };
        let start_y = if smallest_y < 0.0 { 0 } else { smallest_y as i128 };
        let end_y = if biggest_y > height as f64 { height } else { biggest_y as i128 };
        for x in start_x..end_x {
            for y in start_y..end_y {
                if point_in_polygon_offset(x as f64, y as f64, &object.draw_vertices, object.draw_x, object.draw_y) {
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

    Ok(())
}