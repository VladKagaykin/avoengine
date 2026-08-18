use winit::window::Window;
use winit::event_loop::EventLoop;
use winit::dpi::LogicalSize;
use winit::window::WindowAttributes;
use softbuffer::{Context, Surface};
use std::num::NonZeroU32;
use winit::keyboard::KeyCode;
use std::cell::RefCell;
use std::collections::HashMap;

thread_local! {
    static PRESSED_KEYS: RefCell<HashMap<KeyCode, bool>> = RefCell::new(HashMap::new());
}

pub fn create_window(name: String) -> (Window, EventLoop<()>) {
    let settings = super::Engine_settings.lock().unwrap();
    let width = settings.window_width.clone() as u32;
    let height = settings.window_height.clone() as u32;

    drop(settings);
    let mut event_loop = EventLoop::new().unwrap();

    let window = event_loop.create_window(
    WindowAttributes::default()
        .with_title(name)
        .with_inner_size(LogicalSize::new(width, height))
    ).unwrap();
    (window, event_loop)
}

pub fn update_frame(window: &Window) -> Result<(), Box<dyn std::error::Error>> {
    let screen = super::Screen.lock().unwrap();
    
    let height = screen.len();
    if height == 0 {
        return Ok(());
    }
    let width = screen[0].len();
    if width == 0 {
        return Ok(());
    }
    
    let context = Context::new(window)?;
    let mut surface = Surface::new(&context, window)?;
    
    let window_size = window.inner_size();
    let window_width = window_size.width;
    let window_height = window_size.height;
    
    surface.resize(
        NonZeroU32::new(window_width).unwrap(), 
        NonZeroU32::new(window_height).unwrap()
    )?;
    
    let mut buffer = surface.buffer_mut()?;
    let buffer_data = buffer.as_mut();
    
    for i in 0..buffer_data.len() {
        buffer_data[i] = 0xFF000000;
    }
    let scale_x = window_width as f32 / width as f32;
    let scale_y = window_height as f32 / height as f32;
    
    for (screen_y, row) in screen.iter().enumerate() {
        let flipped_y = height - 1 - screen_y;
        let row = &screen[flipped_y];
        
        for (screen_x, pixel) in row.iter().enumerate() {
            let start_x = (screen_x as f32 * scale_x) as usize;
            let start_y = (screen_y as f32 * scale_y) as usize;
            let end_x = ((screen_x + 1) as f32 * scale_x) as usize;
            let end_y = ((screen_y + 1) as f32 * scale_y) as usize;
            
            let color = pixel.pixel_RGBA_color;
            let pixel_value = u32::from_ne_bytes([color[2], color[1], color[0], color[3]]);
            
            for y in start_y..end_y.min(window_height as usize) {
                let row_start = y * window_width as usize;
                for x in start_x..end_x.min(window_width as usize) {
                    if row_start + x < buffer_data.len() {
                        buffer_data[row_start + x] = pixel_value;
                    }
                }
            }
        }
    }
    
    buffer.present()?;
    Ok(())
}

pub fn update_key_state(key_code: KeyCode, pressed: bool) {
    PRESSED_KEYS.with(|pressed_keys| {
        let mut pressed_keys = pressed_keys.borrow_mut();
        if pressed {
            pressed_keys.insert(key_code, true);
        } else {
            pressed_keys.remove(&key_code);
        }
    });
}

pub fn get_pressed_keys(_window: &Window) -> Vec<KeyCode> {
    PRESSED_KEYS.with(|pressed_keys| {
        let pressed_keys = pressed_keys.borrow();
        let mut result = Vec::new();
        
        for (key_code, &is_pressed) in pressed_keys.iter() {
            if is_pressed {
                result.push(key_code.clone());
            }
        }
        
        result
    })
}