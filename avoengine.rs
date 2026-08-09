use std::sync::Mutex;
pub mod console_rc_render;
pub mod tick_system;

pub struct Settings{
    pub window_width: i128,
    pub window_height: i128,
    pub tick_speed: i128, // милисекунды 
}
pub static Engine_settings: Mutex<Settings> = Mutex::new(Settings{window_width: 58, window_height: 44, tick_speed: 50});

#[derive(Clone)]
pub struct Draw_components{
    pub draw_type: String,
    pub draw_x: f32,
    pub draw_y: f32,
    pub draw_z: f32,
    pub draw_symbol: char,
    pub draw_vertices: Vec<f32>,
    pub draw_RGBA_color: [u8;4]
}
pub static Draw_queue: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());

#[derive(Clone)]
pub struct Pixel_structure{
    pub pixel_symbol: char,
    pub pixel_RGBA_color: [u8;4] 
}

pub static Empty_pixel: Mutex<Pixel_structure> = Mutex::new(Pixel_structure{pixel_symbol: ' ', pixel_RGBA_color: [0, 0, 0, 0]});
pub static Screen: Mutex<Vec<Vec<Pixel_structure>>> = Mutex::new(Vec::new());

#[derive(Clone)]
pub struct Camera_structure{
    pub camera_fov: u8,
    pub camera_x: f32,
    pub camera_y: f32,
    pub camera_z: f32,
    pub camera_pitch: f32,
    pub camera_yaw: f32,
    pub camera_roll: f32,
    pub max_dist: u128
}
pub static Camera: Mutex<Camera_structure> = Mutex::new(Camera_structure{camera_fov: 70, camera_x: 0.0, camera_y: 0.0,
                                                                         camera_z: 0.0, camera_pitch: 0.0, camera_yaw: 0.0,
                                                                         camera_roll: 0.0, max_dist: 256});

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