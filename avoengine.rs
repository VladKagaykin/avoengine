use std::sync::Mutex;

pub struct Settings{
    pub window_width: i128,
    pub window_height: i128,
    pub tick_speed: i128
}
pub static Engine_settings: Mutex<Settings> = Mutex::new(Settings{window_width: 58, window_height: 44, tick_speed: 1});
pub struct Draw_components{
    pub draw_type: String,
    pub draw_x: f64,
    pub draw_y: f64,
    pub draw_z: f64,
    pub draw_symbol: char,
    pub draw_vertices: Vec<f64>
}
pub static Draw_queue: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());
pub fn Setup_window(width: &i128, height:&i128){
    //put cod here in near future
    println!("Created window {} x {} pixels", width, height);
}
pub fn Engine_setup(){
    //also put codE here in near future
    println!("Engine setup is complete");
}