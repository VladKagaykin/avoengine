use std::cell::RefCell;
use std::time::{Duration, Instant};

use minifb::{Key, Window, WindowOptions};

use super::{Engine_settings, Screen};
//КОД ИЗ ИНЕТА
thread_local! {
    static WIN: RefCell<Option<Window>> = RefCell::new(None);
    static LAST_FRAME: RefCell<Instant> = RefCell::new(Instant::now());
}

pub fn Create_window() {
    let s = Engine_settings.lock().unwrap();
    let win = Window::new(
        &s.window_name,
        s.window_width as usize,
        s.window_height as usize,
        WindowOptions::default(),
    )
    .expect("failed to create window");
    drop(s);
    WIN.with(|w| *w.borrow_mut() = Some(win));
    println!("[Window]Window created")
}

pub fn Convert_frame() -> (Vec<u32>, usize, usize) {
    let screen = Screen.lock().unwrap();
    let h = screen.len();
    let w = screen.first().map_or(0, |r| r.len());
    let mut buf = vec![0u32; w * h];

    let num_threads = super::render::RENDER_THREADS.load(std::sync::atomic::Ordering::Relaxed).min(h).max(1);
    let chunk_size = (h + num_threads - 1) / num_threads;
    let buf_slice: &mut [u32] = &mut buf[..];

    std::thread::scope(|s| {
        let mut row_start = 0usize;
        for (src, dst) in screen.chunks(chunk_size).zip(buf_slice.chunks_mut(chunk_size * w)) {
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

    WIN.with(|w_| {
        let mut guard = w_.borrow_mut();
        let win = guard.as_mut().expect("window not created");
        if !win.is_open() || win.is_key_down(Key::Escape) {
            std::process::exit(0);
        }
        win.update_with_buffer(&buf, w, h)
            .expect("update_with_buffer failed");
    });
}