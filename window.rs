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
    println!("[Window]Wndow created")
}

pub fn Convert_frame() -> (Vec<u32>, usize, usize) {
    let screen = Screen.lock().unwrap();
    let h = screen.len();
    let w = screen.first().map_or(0, |r| r.len());
    let mut buf = vec![0u32; w * h];
    for (y, row) in screen.iter().enumerate() {
        for (x, px) in row.iter().enumerate() {
            let [r, g, b] = px.color;
            buf[y * w + x] = ((r as u32) << 16) | ((g as u32) << 8) | b as u32;
        }
    }
    (buf, w, h)
}

pub fn Update_frame() {
    let tick_ms = {
        let s = Engine_settings.lock().unwrap();
        s.tick_speed as u64
    };
    LAST_FRAME.with(|lf| {
        let mut last = lf.borrow_mut();
        let elapsed = last.elapsed();
        let target = Duration::from_millis(tick_ms);
        if elapsed < target {
            std::thread::sleep(target - elapsed);
        }
        *last = Instant::now();
    });

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