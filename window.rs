use std::cell::RefCell;
use std::time::{Duration, Instant};

use sdl2::event::{Event, WindowEvent};
use sdl2::keyboard::Keycode;
use sdl2::pixels::PixelFormatEnum;
use sdl2::render::{Canvas, Texture, TextureCreator};
use sdl2::video::{Window, WindowContext};

use super::{Engine_settings, Screen};

struct SdlState {
    sdl: sdl2::Sdl,
    _video: sdl2::VideoSubsystem,
    event_pump: sdl2::EventPump,
    canvas: Canvas<Window>,
    texture: Texture,
}

thread_local! {
    static SDL: RefCell<Option<SdlState>> = RefCell::new(None);
    static LAST_FRAME: RefCell<Instant> = RefCell::new(Instant::now());
}

pub fn Create_window() {
    let s = Engine_settings.lock().unwrap();
    let name = s.window_name.clone();
    let width = s.window_width as u32;
    let height = s.window_height as u32;
    drop(s);

    let sdl = sdl2::init().expect("SDL init failed");
    let video = sdl.video().expect("SDL video subsystem init failed");

    let window = video
        .window(&name, width, height)
        .position_centered()
        .build()
        .expect("failed to create window");

    let canvas = window
        .into_canvas()
        .build()
        .expect("failed to create canvas");

    let texture_creator: &'static TextureCreator<WindowContext> =
        Box::leak(Box::new(canvas.texture_creator()));

    let texture = texture_creator
        .create_texture_streaming(PixelFormatEnum::RGB888, width, height)
        .expect("failed to create texture");

    let event_pump = sdl.event_pump().expect("failed to create event pump");

    SDL.with(|c| {
        *c.borrow_mut() = Some(SdlState {
            sdl,
            _video: video,
            event_pump,
            canvas,
            texture,
        });
    });
    println!("[Window]Window created");
}

pub fn With_canvas_and_pump<F, R>(f: F) -> R
where
    F: FnOnce(&mut Canvas<Window>, &mut sdl2::EventPump) -> R,
{
    SDL.with(|c| {
        let mut guard = c.borrow_mut();
        let state = guard.as_mut().expect("window not created");
        f(&mut state.canvas, &mut state.event_pump)
    })
}

pub fn Convert_frame() -> (Vec<u32>, usize, usize) {
    let screen = Screen.lock().unwrap();
    let h = screen.len();
    let w = screen.first().map_or(0, |r| r.len());
    let mut buf = vec![0u32; w * h];

    let num_threads = super::render::RENDER_THREADS
        .load(std::sync::atomic::Ordering::Relaxed)
        .min(h)
        .max(1);
    let chunk_size = (h + num_threads - 1) / num_threads;
    let buf_slice: &mut [u32] = &mut buf[..];

    std::thread::scope(|s| {
        let mut row_start = 0usize;
        for (src, dst) in screen
            .chunks(chunk_size)
            .zip(buf_slice.chunks_mut(chunk_size * w))
        {
            let cur_start = row_start;
            row_start += src.len();
            let w_loc = w;
            s.spawn(move || {
                for (y_rel, row) in src.iter().enumerate() {
                    let base = y_rel * w_loc;
                    for (x, px) in row.iter().enumerate() {
                        let [r, g, b] = px.color;
                        dst[base + x] = ((r as u32) << 16) | ((g as u32) << 8) | b as u32;
                    }
                }
                let _ = cur_start;
            });
        }
    });

    (buf, w, h)
}

pub fn Update_frame() {
    let (buf, w, h) = Convert_frame();

    SDL.with(|c| {
        let mut guard = c.borrow_mut();
        let state = guard.as_mut().expect("window not created");

        let buf_bytes: &[u8] = unsafe {
            std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len() * 4)
        };

        state
            .texture
            .update(None, buf_bytes, w * 4)
            .expect("texture update failed");

        state.canvas.clear();
        state
            .canvas
            .copy(&state.texture, None, None)
            .expect("canvas copy failed");

        {
            let SdlState { canvas, event_pump, .. } = &mut *state;
            super::gui::Update_gui(canvas, event_pump);
        }

        state.canvas.present();
    });
}

pub fn Get_pressed_keys() -> Vec<i32> {
    SDL.with(|c| {
        let mut guard = c.borrow_mut();
        let state = guard.as_mut().expect("window not created");

        for event in state.event_pump.poll_iter() {
            match event {
                Event::Quit { .. } => std::process::exit(0),
                Event::Window { win_event: WindowEvent::Close, .. } => std::process::exit(0),
                Event::KeyDown { keycode: Some(Keycode::Escape), .. } => std::process::exit(0),
                _ => {}
            }
        }

        state.event_pump
            .keyboard_state()
            .scancodes()
            .filter(|&(_sc, pressed)| pressed)
            .map(|(sc, _)| sc as i32)
            .collect()
    })
}