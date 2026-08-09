use std::time::{Duration, Instant};
use std::sync::Mutex;

static tick: Mutex<u128> = Mutex::new(0);
static tick_speed: Mutex<i128> = Mutex::new(50);
static last_tick_time: Mutex<Option<Instant>> = Mutex::new(None);

pub fn Init_tick_system(){
    let tick_speed_from_settings = super::Engine_settings.lock().unwrap();
    *tick_speed.lock().unwrap() = tick_speed_from_settings.tick_speed.clone();
    *last_tick_time.lock().unwrap() = Some(Instant::now());
    drop(tick_speed_from_settings)
}

pub fn Tick_update() {
    let now = Instant::now();
    let mut last_time_guard = last_tick_time.lock().unwrap();
    let last_time = match *last_time_guard {
        Some(time) => time,
        None => {
            *last_time_guard = Some(now);
            return;
        }
    };
    
    if now.duration_since(last_time) >= Duration::from_millis(*tick_speed.lock().unwrap() as u64) {
        *tick.lock().unwrap() += 1;
        *last_time_guard = Some(now);
    }
}

pub fn Get_tick() -> u128{
    *tick.lock().unwrap()
}