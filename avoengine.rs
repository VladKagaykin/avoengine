use std::sync::{Mutex, LazyLock};
use std::collections::HashMap;

pub mod window;
pub mod render;
pub mod gui;
pub mod tick_system;
pub mod gltf_parser;
pub mod physics;

// структура настроек движка
pub struct Settings {
    pub window_width: i128,
    pub window_height: i128,
    pub tick_speed: i128, // милисекунды
    pub cores_multiply: u8, // потом как-нибудь автоматизировать данный ужас
    pub window_name: String
}

// настройки движка по умолчанию(lazylock для функции to_string)
pub static Engine_settings: LazyLock<Mutex<Settings>> = LazyLock::new(|| {
    Mutex::new(Settings {
        window_width: 640,
        window_height: 480,
        tick_speed: 50,
        cores_multiply: 2,
        window_name: "avoengine".to_string(),
    })
});

// структура 3д компонентов, всё жизненно необходимое в структуре, остальное в свойствах
#[derive(Clone)]
pub struct Draw_components {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub vertices: Vec<f32>,
    pub RGBA_color: [u8; 4],
    pub properties: HashMap<String, String>,
    pub special_name: String,
}

// незапечённая сцена(без флага, запекать будет пользователь)
pub static Static_scene: Mutex<Vec<Draw_components>> = Mutex::new(Vec::new());

// сначала разобраться с статикой, потом динамику(в том числе и физику), потом свет 
// #[derive(Clone)]
// pub struct Light_components {
//     pub light_x: f32,
//     pub light_y: f32,
//     pub light_z: f32,
//     pub light_RGB_color: [u8; 3],
//     pub light_distance: f32,
//     pub light_cone_angle: f32,
//     pub light_pitch: f32,
//     pub light_yaw: f32,
//     pub light_special_name: String,
// }

// pub static Light_queue: Mutex<Vec<Light_components>> = Mutex::new(Vec::new());
// pub static Static_light: Mutex<Vec<Light_components>> = Mutex::new(Vec::new());

//структура пискеля, выводимого на экран
#[derive(Clone)]
pub struct Pixel_structure {
    pub color: [u8; 3],
}
//пустой пиксель, которым заполняется экран
pub static Empty_pixel: Mutex<Pixel_structure> = Mutex::new(Pixel_structure {
    color: [0, 0, 128] // синий для проверки
});
// матрица того, что видно на экране
pub static Screen: Mutex<Vec<Vec<Pixel_structure>>> = Mutex::new(Vec::new());
// структура камеры: здесь только жизненно необходимые вещи, остальное уже проблема пользователя(по крайней мере я так думаю на момент написания комментария)
#[derive(Clone)]
pub struct Camera_structure {
    pub camera_fov: u8,
    pub camera_x: f32,
    pub camera_y: f32,
    pub camera_z: f32,
    pub camera_pitch: f32,
    pub camera_yaw: f32,
    pub camera_roll: f32,
    pub max_dist: u128,
    pub ambient_light: [u8; 3],
}
// стандартные настройки камеры
pub static Camera: Mutex<Camera_structure> = Mutex::new(Camera_structure {
    camera_fov: 90,
    camera_x: 0.0,
    camera_y: 1.0,
    camera_z: 0.0,
    camera_pitch: 0.0,
    camera_yaw: 0.0,
    camera_roll: 0.0,
    max_dist: 256,
    ambient_light: [128, 128, 128],
});
// параметры физики(в том числе и визуальной)
#[derive(Clone)]
pub struct Physics_properties {
    pub g: f32,
    pub resistance_in_air: f32,
    pub atmosphere_density: f32,
    pub atmosphere_color: [u8;3]
}
//стандартная физика
pub static Earth: Mutex<Physics_properties> = Mutex::new(Physics_properties {
    g: 9.8,
    resistance_in_air: 1.0,
    atmosphere_density: 0.1,
    atmosphere_color: [255,255,255]
});
// структура физической карты(хитбоксы) то же самое, что и у графических элементов, но без графики
#[derive(Clone)]
pub struct Physics_components {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub vertices: Vec<f32>,
    pub properties: HashMap<String, String>,
    pub special_name: String,
}
//по прежнему как и с графикой
pub static Physics_scene: Mutex<Vec<Physics_components>> = Mutex::new(Vec::new());
//создание пустого экрана
pub fn Setup_window_screen(width: &i128, height: &i128) {
    //создание времменного экрана нужного размера и сразу заполнение пустыми пикселями
    let new_screen = vec![
        vec![Empty_pixel.lock().unwrap().clone(); *width as usize];
        *height as usize
    ];
    //присвоение основному экрану временного
    let mut screen = Screen.lock().unwrap();
    *screen = new_screen;
    println!("[Engine]Created screen {} x {} pixels", *width as usize, *height as usize);
}
//функция основной настройки движка
pub fn Engine_setup() {
    println!("[Engine]Start engine setup");
    let (w, h, cm) = {
        let s = Engine_settings.lock().unwrap();
        (s.window_width, s.window_height, s.cores_multiply)
    };
    Setup_window_screen(&w, &h);
    let cores = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1);
    let num_threads = cores.saturating_mul(cm as usize).saturating_sub(1).max(1);
    render::init_render(w as usize, h as usize, num_threads);
    window::Create_window();
    window::With_canvas_and_pump(|canvas, _| {
        gui::Init_gui(canvas);
    });
    tick_system::Init_tick_system();
    println!("[Engine]Engine setup is complete");
}