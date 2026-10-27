use std::sync::Mutex;
use std::time::Instant;

static Tick_state: Mutex<Option<(Instant, u128)>> = Mutex::new(None);

pub fn Init_tick_system() {
    let tick_ms = super::Engine_settings.lock().unwrap().tick_speed.max(1) as u128;
    let mut state = Tick_state.lock().unwrap();
    *state = Some((Instant::now(), tick_ms));
    println!("[Tick system]Initialized with {} ms per tick ({} ticks per second)",
        tick_ms,
        1000 / tick_ms
    );
}

pub fn Get_current_tick() -> u128 {
    let state = Tick_state.lock().unwrap();
    match *state {
        Some((start, tick_ms)) => start.elapsed().as_millis() / tick_ms,
        None => 0,
    }
}