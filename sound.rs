use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread::{self, JoinHandle};
use std::time::Duration;

static DEVICE: OnceLock<cpal::Device> = OnceLock::new();
static PLAYBACKS: Mutex<Vec<(Arc<AtomicBool>, JoinHandle<()>)>> = Mutex::new(Vec::new());
static SOUND_CACHE: OnceLock<Mutex<HashMap<String, (u32, u16, Vec<f32>)>>> = OnceLock::new();

fn get_cache() -> &'static Mutex<HashMap<String, (u32, u16, Vec<f32>)>> {
    SOUND_CACHE.get_or_init(|| Mutex::new(HashMap::new()))
}

pub fn Init_sound() -> Result<(), Box<dyn Error>> {
    let device = DEVICE.get_or_init(|| {
        let host = cpal::default_host();
        host.default_output_device()
            .expect("No default output device available")
    });

    let device_name = device
        .description()
        .map(|d| d.to_string())
        .unwrap_or_else(|_| "Unknown".to_string());
    println!("Audio device: {}", device_name);

    if let Ok(configs) = device.supported_output_configs() {
        for config in configs {
            println!(
                "Supported config: channels = {}, sample_rate = {}..={}",
                config.channels(),
                config.min_sample_rate(),
                config.max_sample_rate()
            );
        }
    }
    if let Ok(default) = device.default_output_config() {
        println!(
            "Default output config: channels = {}, sample_rate = {}",
            default.channels(),
            default.sample_rate()
        );
    }
    Ok(())
}

pub fn Load_sound(file_path: &str) -> Result<(), Box<dyn Error>> {
    let (sample_rate, channels, samples) = read_wav(file_path)?;
    let cache = get_cache();
    let mut map = cache.lock().unwrap();
    map.insert(file_path.to_string(), (sample_rate, channels, samples));
    Ok(())
}

pub fn Play_sound(file_path: &str, volume: f32) -> Result<(), Box<dyn Error>> {
    let device = DEVICE.get().ok_or("Sound not initialized. Call Init_sound() first.")?;

    let cache = get_cache();
    let (sample_rate, channels, samples) = {
        let map = cache.lock().unwrap();
        map.get(file_path)
            .ok_or_else(|| format!("Sound not loaded: {}", file_path))?
            .clone()
    };

    let mut samples = samples;
    for s in &mut samples {
        *s *= volume;
    }
    let total_samples = samples.len();

    let config = cpal::StreamConfig {
        channels,
        sample_rate,
        buffer_size: cpal::BufferSize::Default,
    };

    let samples_arc = Arc::new(Mutex::new(samples));
    let index_arc = Arc::new(Mutex::new(0));
    let stop_flag = Arc::new(AtomicBool::new(false));
    let stop_flag_for_thread = stop_flag.clone();
    let device_clone = device.clone();

    let handle = thread::spawn(move || {
        let stream = match device_clone.build_output_stream(
            config,
            {
                let samples_cb = samples_arc.clone();
                let index_cb = index_arc.clone();
                move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
                    let mut idx = index_cb.lock().unwrap();
                    let samples = samples_cb.lock().unwrap();
                    let mut write_pos = 0;
                    while write_pos < data.len() && *idx < samples.len() {
                        data[write_pos] = samples[*idx];
                        *idx += 1;
                        write_pos += 1;
                    }
                    while write_pos < data.len() {
                        data[write_pos] = 0.0;
                        write_pos += 1;
                    }
                }
            },
            |err| eprintln!("Audio stream error: {}", err),
            None,
        ) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("Failed to build stream: {}", e);
                return;
            }
        };

        if let Err(e) = stream.play() {
            eprintln!("Failed to start stream: {}", e);
            return;
        }

        while *index_arc.lock().unwrap() < total_samples
            && !stop_flag_for_thread.load(Ordering::Relaxed)
        {
            thread::sleep(Duration::from_millis(50));
        }

        drop(stream);
    });

    {
        let mut list = PLAYBACKS.lock().unwrap();
        list.retain(|(_, h)| !h.is_finished());
        list.push((stop_flag, handle));
    }

    Ok(())
}

pub fn stop_all_sounds() -> Result<(), Box<dyn Error>> {
    let mut list = PLAYBACKS.lock().unwrap();
    for (flag, _) in list.iter() {
        flag.store(true, Ordering::Relaxed);
    }
    for (_, handle) in list.drain(..) {
        let _ = handle.join();
    }
    Ok(())
}

fn read_wav(path: &str) -> Result<(u32, u16, Vec<f32>), Box<dyn Error>> {
    let mut file = File::open(path)?;

    let mut riff = [0u8; 4];
    file.read_exact(&mut riff)?;
    if &riff != b"RIFF" {
        return Err("Not a RIFF file".into());
    }
    file.seek(SeekFrom::Current(4))?;

    let mut wave = [0u8; 4];
    file.read_exact(&mut wave)?;
    if &wave != b"WAVE" {
        return Err("Not a WAVE format".into());
    }

    let mut sample_rate = 0u32;
    let mut channels = 0u16;
    let mut data_start: u64 = 0;
    let mut data_len = 0u32;

    loop {
        let mut chunk_id = [0u8; 4];
        if file.read_exact(&mut chunk_id).is_err() {
            break;
        }
        let mut chunk_size = [0u8; 4];
        file.read_exact(&mut chunk_size)?;
        let size = u32::from_le_bytes(chunk_size);

        match &chunk_id {
            b"fmt " => {
                let mut fmt_data = vec![0u8; size as usize];
                file.read_exact(&mut fmt_data)?;
                if fmt_data.len() < 16 {
                    return Err("fmt chunk too short".into());
                }
                let audio_format = u16::from_le_bytes([fmt_data[0], fmt_data[1]]);
                if audio_format != 1 {
                    return Err("Only PCM (format 1) supported".into());
                }
                channels = u16::from_le_bytes([fmt_data[2], fmt_data[3]]);
                sample_rate = u32::from_le_bytes([
                    fmt_data[4], fmt_data[5], fmt_data[6], fmt_data[7],
                ]);
                let bits_per_sample = u16::from_le_bytes([fmt_data[14], fmt_data[15]]);
                if bits_per_sample != 16 {
                    return Err("Only 16‑bit PCM supported".into());
                }
            }
            b"data" => {
                data_start = file.stream_position()?;
                data_len = size;
                break;
            }
            _ => {
                file.seek(SeekFrom::Current(size as i64))?;
            }
        }
    }

    if data_len == 0 {
        return Err("No data chunk found".into());
    }

    file.seek(SeekFrom::Start(data_start))?;
    let sample_count = data_len as usize / 2;
    let mut raw_samples = vec![0i16; sample_count];

    let mut buffer = vec![0u8; data_len as usize];
    file.read_exact(&mut buffer)?;
    for i in 0..sample_count {
        let offset = i * 2;
        raw_samples[i] = i16::from_le_bytes([buffer[offset], buffer[offset + 1]]);
    }

    let samples_f32: Vec<f32> = raw_samples
        .into_iter()
        .map(|s| s as f32 / i16::MAX as f32)
        .collect();

    Ok((sample_rate, channels, samples_f32))
}