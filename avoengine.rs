use std::sync::Mutex;

pub mod console_rc_render;
pub mod tick_system;
pub mod console_input;
pub mod window_processing;

pub struct Settings{
    pub window_width: i128,
    pub window_height: i128,
    pub tick_speed: i128, // милисекунды 
    pub cores_multiply: u8
}
pub static Engine_settings: Mutex<Settings> = Mutex::new(Settings{window_width: 58, window_height: 44, tick_speed: 50,
                                                                  cores_multiply: 2});

#[derive(Clone)]
pub struct Draw_components{
    pub draw_type: String,
    pub draw_x: f32,
    pub draw_y: f32,
    pub draw_z: f32,
    pub draw_symbol: char,
    pub draw_vertices: Vec<f32>,
    pub draw_RGBA_color: [u8;4],
    pub draw_texture_path: String,
    pub draw_special_name: String
}
pub static Draw_queue: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());
pub static Static_scene: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());
pub static Is_scene_changed: Mutex<bool> = Mutex::new(false);

#[derive(Clone)]
pub struct Light_components{
    pub light_x: f32,
    pub light_y: f32,
    pub light_z: f32,
    pub light_RGB_color: [u8;3],
    pub light_distance: f32,
    pub light_cone_angle: f32,
    pub light_pitch: f32,
    pub light_yaw: f32,
    pub light_special_name: String
}

pub static Light_queue: Mutex<Vec<Light_components>> = Mutex::new(Vec::new());
pub static Static_light: Mutex<Vec<Light_components>> = Mutex::new(Vec::new());

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
    pub max_dist: u128,
    pub ambient_light: [u8;3]
}
pub static Camera: Mutex<Camera_structure> = Mutex::new(Camera_structure{camera_fov: 58, camera_x: 0.0, camera_y: 1.0,
                                                                         camera_z: 0.0, camera_pitch: 0.0, camera_yaw: 0.0,
                                                                         camera_roll: 0.0, max_dist: 256, ambient_light: [128,128,128]});

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