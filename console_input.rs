use std::mem;
use std::ptr;

#[repr(C)]
#[derive(Copy, Clone)]
struct Termios {
    c_iflag: u32,
    c_oflag: u32,
    c_cflag: u32,
    c_lflag: u32,
    c_cc: [u8; 32],
    c_ispeed: u32,
    c_ospeed: u32,
}

unsafe extern "C" {
    fn tcgetattr(fd: i32, termios: *mut Termios) -> i32;
    fn tcsetattr(fd: i32, action: i32, termios: *const Termios) -> i32;
    fn read(fd: i32, buf: *mut u8, count: usize) -> isize;
}

const TCSANOW: i32 = 0;
const ICANON: u32 = 1 << 1;
const ECHO: u32 = 1 << 3;
const VMIN: usize = 6;
const VTIME: usize = 7;

pub fn get_pressed_keys() -> Vec<char> {
    let mut keys = Vec::new();
    let mut buffer = [0; 1];
    
    unsafe {
        let mut termios: Termios = mem::zeroed();
        tcgetattr(0, &mut termios);
        let mut original = termios;
        
        termios.c_lflag &= !(ICANON | ECHO);
        termios.c_cc[VMIN] = 0;
        termios.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &termios);
        
        loop {
            let bytes_read = read(0, buffer.as_mut_ptr(), 1);
            if bytes_read <= 0 {
                break;
            }
            keys.push(buffer[0] as char);
        }
        
        tcsetattr(0, TCSANOW, &original);
    }
    
    let mut result = Vec::new();
    for key in keys {
        if !result.contains(&key) {
            result.push(key);
        }
    }
    
    result
}