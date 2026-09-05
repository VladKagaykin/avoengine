use winit::window::Window;
use winit::event_loop::EventLoop;
use winit::dpi::LogicalSize;
use winit::window::WindowAttributes;
use softbuffer::{Context, Surface};
use std::num::NonZeroU32;
use winit::keyboard::KeyCode;
use std::cell::RefCell;
use std::collections::HashMap;
use ocl::{Platform, Device, Context as OclContext, Program, Kernel, Queue, Buffer, flags};
use std::sync::{OnceLock, Mutex};

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

const SCALE_KERNEL_SRC: &str = r#"
__kernel void scale_image(
    __global const uchar* src,
    int src_width,
    int src_height,
    __global uchar* dst,
    int dst_width,
    int dst_height
) {
    int gid = get_global_id(0);
    if (gid >= dst_width * dst_height) return;
    int x = gid % dst_width;
    int y = gid / dst_width;
    float scale_x = (float)src_width / dst_width;
    float scale_y = (float)src_height / dst_height;
    int sx = (int)(x * scale_x);
    int sy = (int)(y * scale_y);
    if (sx >= src_width) sx = src_width - 1;
    if (sy >= src_height) sy = src_height - 1;
    int src_idx = (sy * src_width + sx) * 4;
    int dst_idx = gid * 4;
    dst[dst_idx]     = src[src_idx + 2]; // B
    dst[dst_idx + 1] = src[src_idx + 1]; // G
    dst[dst_idx + 2] = src[src_idx];     // R
    dst[dst_idx + 3] = src[src_idx + 3]; // A
}
"#;

struct OpenCLWindowState {
    context: OclContext,
    queue: Queue,
    program: Program,
}

static OPENCL_WINDOW_STATE: OnceLock<Mutex<Option<OpenCLWindowState>>> = OnceLock::new();

fn init_opencl_for_window() -> Result<OpenCLWindowState, String> {
    let platforms = Platform::list();
    if platforms.is_empty() { return Err("No OpenCL platforms".to_string()); }
    let mut selected_device = None;
    let mut selected_platform = None;
    for platform in platforms.iter() {
        if let Ok(mut devices) = Device::list(*platform, Some(flags::DeviceType::GPU)) {
            if let Some(dev) = devices.pop() {
                selected_device = Some(dev);
                selected_platform = Some(*platform);
                break;
            }
        }
    }
    if selected_device.is_none() {
        for platform in platforms.iter() {
            if let Ok(mut devices) = Device::list(*platform, Some(flags::DeviceType::CPU)) {
                if let Some(dev) = devices.pop() {
                    selected_device = Some(dev);
                    selected_platform = Some(*platform);
                    break;
                }
            }
        }
    }
    if selected_device.is_none() {
        for platform in platforms.iter() {
            if let Ok(mut devices) = Device::list(*platform, None) {
                if let Some(dev) = devices.pop() {
                    selected_device = Some(dev);
                    selected_platform = Some(*platform);
                    break;
                }
            }
        }
    }
    let device = selected_device.ok_or("No OpenCL device")?;
    let platform = selected_platform.unwrap();
    let context = OclContext::builder()
        .platform(platform)
        .devices(device)
        .build()
        .map_err(|e| format!("Context error: {}", e))?;
    let program = Program::builder()
        .devices(device)
        .src(SCALE_KERNEL_SRC)
        .build(&context)
        .map_err(|e| format!("Program build error: {}", e))?;
    let queue = Queue::new(&context, device, None).map_err(|e| format!("Queue error: {}", e))?;
    Ok(OpenCLWindowState { context, queue, program })
}

fn get_opencl_window_state() -> Result<&'static Mutex<Option<OpenCLWindowState>>, String> {
    OPENCL_WINDOW_STATE.get_or_init(|| match init_opencl_for_window() {
        Ok(state) => Mutex::new(Some(state)),
        Err(e) => {
            eprintln!("OpenCL window init failed: {}", e);
            Mutex::new(None)
        }
    });
    OPENCL_WINDOW_STATE.get().ok_or_else(|| "OpenCL not initialized".to_string())
}

pub fn update_frame(window: &Window) -> Result<(), Box<dyn std::error::Error>> {
    let screen = super::Screen.lock().unwrap();
    let height = screen.len();
    if height == 0 { return Ok(()); }
    let width = screen[0].len();
    if width == 0 { return Ok(()); }

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

    let state_lock = match get_opencl_window_state() {
        Ok(v) => v,
        Err(_) => {
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
            return Ok(());
        }
    };

    let state_guard = state_lock.lock().unwrap();
    let state = match state_guard.as_ref() {
        Some(s) => s,
        None => {
            drop(state_guard);
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
            return Ok(());
        }
    };

    let mut src_flat = Vec::with_capacity(width * height * 4);
    for row in screen.iter().rev() {
        for pixel in row {
            src_flat.extend_from_slice(&pixel.pixel_RGBA_color);
        }
    }

    let src_buffer: Buffer<u8> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_READ_ONLY)
        .len(src_flat.len())
        .copy_host_slice(&src_flat)
        .build()?;

    let dst_len = buffer_data.len() * 4;
    let dst_bytes = unsafe { std::slice::from_raw_parts_mut(buffer_data.as_mut_ptr() as *mut u8, dst_len) };
    let dst_buffer: Buffer<u8> = Buffer::builder()
        .queue(state.queue.clone())
        .flags(flags::MEM_WRITE_ONLY)
        .len(dst_len)
        .build()?;

    let kernel = Kernel::builder()
        .program(&state.program)
        .name("scale_image")
        .queue(state.queue.clone())
        .arg(&src_buffer)
        .arg(&(width as i32))
        .arg(&(height as i32))
        .arg(&dst_buffer)
        .arg(&(window_width as i32))
        .arg(&(window_height as i32))
        .global_work_size((window_width * window_height) as usize)
        .build()?;

    unsafe { kernel.enq()?; }

    dst_buffer.read(dst_bytes).enq()?;

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