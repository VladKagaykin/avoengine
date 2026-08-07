use std::sync::Mutex;
pub mod console_rc_render;

pub struct Settings{
    pub window_width: i128,
    pub window_height: i128,
    pub tick_speed: i128
}
pub static Engine_settings: Mutex<Settings> = Mutex::new(Settings{window_width: 58, window_height: 44, tick_speed: 1});

#[derive(Clone)]
pub struct Draw_components{
    pub draw_type: String,
    pub draw_x: f64,
    pub draw_y: f64,
    pub draw_z: f64,
    pub draw_symbol: char,
    pub draw_vertices: Vec<f64>,
    pub draw_RGBA_color: [u8;4]
}
pub static Draw_queue: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());

#[derive(Clone)]
pub struct Pixel_structure{
    pub pixel_symbol: char,
    pub pixel_RGBA_color: [u8;4] 
}

pub static Empty_pixel: Mutex<Pixel_structure> = Mutex::new(Pixel_structure{pixel_symbol: '░', pixel_RGBA_color: [0, 0, 0, 0]});
pub static Screen: Mutex<Vec<Vec<Pixel_structure>>> = Mutex::new(Vec::new());

pub fn Setup_window(width: &i128, height:&i128){
    let new_screen = vec![vec![Empty_pixel.lock().unwrap().clone(); *width as usize]; *height as usize];
    
    let mut screen = Screen.lock().unwrap();
    *screen = new_screen;
    
    println!("Created window {} x {} pixels", *width as usize, *height as usize);
}
pub fn Engine_setup(){
    //also put codE here in near future
    println!("Engine setup is complete");
}