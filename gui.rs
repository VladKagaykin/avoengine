use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::{LazyLock, Mutex};
use image::codecs::gif::GifDecoder;
use image::{AnimationDecoder, Rgba, RgbaImage};
use rusttype::{point, Font as RtFont, Scale};
use super::tick_system::Get_tick;
use super::{Draw_components, Draw_queue, Engine_settings, Pixel_structure, Screen};

pub static Page_elements: LazyLock<Mutex<HashMap<String, String>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

#[derive(Clone, PartialEq)]
pub enum GuiKind {
    Text,
    Image,
    Gif,
}

#[derive(Clone)]
pub struct GuiElement {
    pub kind: GuiKind,
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub color: [u8; 4],
    pub font_size: f32,
    pub font_path: String,
    pub content: String,
    pub image: Option<RgbaImage>,
    pub frames: Vec<RgbaImage>,
    pub frame_delays: Vec<u32>,
    pub current_frame: usize,
    pub text_texture: Option<RgbaImage>,
}

#[derive(Clone)]
pub struct GuiPage {
    pub name: String,
    pub raw: String,
    pub elements: Vec<GuiElement>,
}

pub fn Load_gui_page(path: &str) -> Result<GuiPage, String> {
    let raw = std::fs::read_to_string(path).map_err(|e| e.to_string())?;
    let data = Page_elements.lock().unwrap().clone();
    let substituted = substitute_placeholders(&raw, &data);
    let mut elements = parse_page(&substituted);

    for element in elements.iter_mut() {
        update_element_frame(element);
        update_element_text_texture(element);
    }

    let name = sanitize_name(
        &std::path::Path::new(path)
            .file_stem()
            .map(|s| s.to_string_lossy().to_string())
            .unwrap_or_else(|| "page".to_string()),
    );

    Ok(GuiPage {
        name,
        raw,
        elements,
    })
}

pub fn Update_gui_page(page: &mut GuiPage) {
    let data = Page_elements.lock().unwrap().clone();
    let substituted = substitute_placeholders(&page.raw, &data);
    page.elements = parse_page(&substituted);

    for element in page.elements.iter_mut() {
        update_element_frame(element);
        update_element_text_texture(element);
    }
}

pub fn Overlay_gui_page(page: &GuiPage, add_to_dynamic_queue: bool) {
    if add_to_dynamic_queue {
        overlay_dynamic_queue(page);
    } else {
        overlay_main_buffer(page);
    }
}

fn substitute_placeholders(input: &str, data: &HashMap<String, String>) -> String {
    let mut out = input.to_string();

    for (key, value) in data {
        let double_placeholder = "{{".to_string() + key.as_str() + "}}";
        let single_placeholder = "{".to_string() + key.as_str() + "}";

        out = out.replace(&double_placeholder, value.as_str());
        out = out.replace(&single_placeholder, value.as_str());
    }

    out
}

fn parse_page(text: &str) -> Vec<GuiElement> {
    let mut elements = Vec::new();
    let mut current_font = String::new();

    for raw_line in text.lines() {
        let line = raw_line.trim();

        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        let tokens: Vec<&str> = line.split_whitespace().collect();

        if tokens.is_empty() {
            continue;
        }

        match tokens[0] {
            "font" if tokens.len() >= 2 => {
                current_font = tokens[1..].join(" ");
            }
            "text" if tokens.len() >= 8 => {
                let x = parse_relative(tokens[1]);
                let y = parse_relative(tokens[2]);
                let w = parse_relative(tokens[3]);
                let h = parse_relative(tokens[4]);
                let color = parse_color(tokens[5]);
                let font_size = parse_relative(tokens[6]);
                let mut content = tokens[7..].join(" ");
                content = content.replace("\\n", "\n");

                elements.push(GuiElement {
                    kind: GuiKind::Text,
                    x,
                    y,
                    w,
                    h,
                    color,
                    font_size,
                    font_path: current_font.clone(),
                    content,
                    image: None,
                    frames: Vec::new(),
                    frame_delays: Vec::new(),
                    current_frame: 0,
                    text_texture: None,
                });
            }
            "image" if tokens.len() >= 7 => {
                let x = parse_relative(tokens[1]);
                let y = parse_relative(tokens[2]);
                let w = parse_relative(tokens[3]);
                let h = parse_relative(tokens[4]);
                let color = parse_color(tokens[5]);
                let content = tokens[6..].join(" ");
                let loaded_image = load_image(&content);

                elements.push(GuiElement {
                    kind: GuiKind::Image,
                    x,
                    y,
                    w,
                    h,
                    color,
                    font_size: 0.0,
                    font_path: String::new(),
                    content,
                    image: loaded_image,
                    frames: Vec::new(),
                    frame_delays: Vec::new(),
                    current_frame: 0,
                    text_texture: None,
                });
            }
            "gif" if tokens.len() >= 7 => {
                let x = parse_relative(tokens[1]);
                let y = parse_relative(tokens[2]);
                let w = parse_relative(tokens[3]);
                let h = parse_relative(tokens[4]);
                let color = parse_color(tokens[5]);
                let content = tokens[6..].join(" ");
                let (frames, frame_delays, first_image) = load_gif_frames(&content);

                elements.push(GuiElement {
                    kind: GuiKind::Gif,
                    x,
                    y,
                    w,
                    h,
                    color,
                    font_size: 0.0,
                    font_path: String::new(),
                    content,
                    image: first_image,
                    frames,
                    frame_delays,
                    current_frame: 0,
                    text_texture: None,
                });
            }
            _ => {}
        }
    }

    elements
}

fn parse_relative(token: &str) -> f32 {
    let t = token.trim();

    if let Some(percent) = t.strip_suffix('%') {
        return percent.trim().parse::<f32>().unwrap_or(0.0) / 100.0;
    }

    t.parse::<f32>().unwrap_or(0.0)
}

fn parse_color(token: &str) -> [u8; 4] {
    let t = token.trim();

    if t.contains(',') {
        let parts: Vec<&str> = t.split(',').collect();

        if parts.len() >= 3 {
            let r = parts[0].trim().parse::<u8>().unwrap_or(255);
            let g = parts[1].trim().parse::<u8>().unwrap_or(255);
            let b = parts[2].trim().parse::<u8>().unwrap_or(255);
            let a = if parts.len() >= 4 {
                parts[3].trim().parse::<u8>().unwrap_or(255)
            } else {
                255
            };

            return [r, g, b, a];
        }
    }

    if let Some(color) = parse_hex_color(t) {
        return color;
    }

    if t.eq_ignore_ascii_case("transparent") {
        return [0, 0, 0, 0];
    }

    [255, 255, 255, 255]
}

fn parse_hex_color(token: &str) -> Option<[u8; 4]> {
    let s = token.trim();
    let s = s.strip_prefix('#').unwrap_or(s);
    let s = s.strip_prefix("0x").unwrap_or(s);

    if s.len() == 6 {
        let r = u8::from_str_radix(&s[0..2], 16).ok()?;
        let g = u8::from_str_radix(&s[2..4], 16).ok()?;
        let b = u8::from_str_radix(&s[4..6], 16).ok()?;
        return Some([r, g, b, 255]);
    }

    if s.len() == 8 {
        let r = u8::from_str_radix(&s[0..2], 16).ok()?;
        let g = u8::from_str_radix(&s[2..4], 16).ok()?;
        let b = u8::from_str_radix(&s[4..6], 16).ok()?;
        let a = u8::from_str_radix(&s[6..8], 16).ok()?;
        return Some([r, g, b, a]);
    }

    None
}

fn load_image(path: &str) -> Option<RgbaImage> {
    if path_is_none(path) {
        return None;
    }

    image::open(path).ok().map(|img| img.to_rgba8())
}

fn load_gif_frames(path: &str) -> (Vec<RgbaImage>, Vec<u32>, Option<RgbaImage>) {
    if path_is_none(path) {
        return (Vec::new(), Vec::new(), None);
    }

    if let Ok(file) = std::fs::File::open(path) {
        if let Ok(decoder) = GifDecoder::new(std::io::BufReader::new(file)) {
            let mut frames = Vec::new();
            let mut delays = Vec::new();

            for frame_result in decoder.into_frames() {
                if let Ok(frame) = frame_result {
                    let (num, den) = frame.delay().numer_denom_ms();
                    let ms = if den == 0 {
                        100
                    } else {
                        num / den.max(1)
                    };

                    delays.push(ms.max(1));
                    frames.push(frame.into_buffer());
                }
            }

            if !frames.is_empty() {
                let first = Some(frames[0].clone());
                return (frames, delays, first);
            }
        }
    }

    if let Some(img) = load_image(path) {
        return (vec![img.clone()], vec![100], Some(img));
    }

    (Vec::new(), Vec::new(), None)
}

fn update_element_frame(element: &mut GuiElement) {
    if element.kind != GuiKind::Gif || element.frames.is_empty() {
        return;
    }

    let tick = Get_tick();

    if element.frame_delays.is_empty() {
        element.current_frame = tick as usize % element.frames.len();
        return;
    }

    let total: u32 = element.frame_delays.iter().sum();

    if total == 0 {
        element.current_frame = tick as usize % element.frames.len();
        return;
    }

    let tick_speed = {
        let settings = Engine_settings.lock().unwrap();

        if settings.tick_speed < 1 {
            1u64
        } else {
            settings.tick_speed as u64
        }
    };

    let mut ms = ((tick as u64).wrapping_mul(tick_speed)) as u32;
    ms %= total;

    let mut acc = 0u32;

    for (i, delay) in element.frame_delays.iter().enumerate() {
        acc = acc.saturating_add(*delay);

        if ms < acc {
            element.current_frame = i;
            return;
        }
    }

    element.current_frame = 0;
}

fn update_element_text_texture(element: &mut GuiElement) {
    if element.kind != GuiKind::Text {
        return;
    }

    let height_f = {
        let settings = Engine_settings.lock().unwrap();
        settings.window_height.max(0) as f32
    };

    let pixel_size = resolve_font_size(element.font_size, height_f);
    let font_path = if path_is_none(&element.font_path) {
        "data/Trebuchet MS.ttf".to_string()
    } else {
        element.font_path.clone()
    };

    let font = load_font_from_file(&font_path);

    element.text_texture = font.and_then(|f| {
        render_text_rgba(&f, &element.content, pixel_size, element.color)
    });
}

fn resolve_font_size(value: f32, window_height: f32) -> f32 {
    if value <= 0.0 {
        return 24.0;
    }

    if value <= 1.0 {
        return (value * window_height).max(1.0);
    }

    value.max(1.0)
}

fn load_font_from_file(path: &str) -> Option<RtFont<'static>> {
    if path_is_none(path) {
        return None;
    }

    let bytes = std::fs::read(path).ok()?;
    RtFont::try_from_vec(bytes)
}

fn render_text_rgba(
    font: &RtFont<'static>,
    text: &str,
    pixel_size: f32,
    color: [u8; 4],
) -> Option<RgbaImage> {
    if text.is_empty() || !pixel_size.is_finite() || pixel_size <= 0.0 {
        return None;
    }

    let scale = Scale::uniform(pixel_size);
    let v_metrics = font.v_metrics(scale);

    let mut x = 0.0f32;
    let mut glyphs = Vec::new();

    let mut min_x = f32::INFINITY;
    let mut max_x = f32::NEG_INFINITY;
    let mut min_y = f32::INFINITY;
    let mut max_y = f32::NEG_INFINITY;

    for ch in text.chars() {
        let scaled = font.glyph(ch).scaled(scale);
        let advance = scaled.h_metrics().advance_width;
        let positioned = scaled.positioned(point(x, v_metrics.ascent));

        if let Some(bb) = positioned.pixel_bounding_box() {
            min_x = min_x.min(bb.min.x as f32);
            max_x = max_x.max(bb.max.x as f32);
            min_y = min_y.min(bb.min.y as f32);
            max_y = max_y.max(bb.max.y as f32);
        }

        glyphs.push(positioned);
        x += advance;
    }

    if min_x.is_infinite() || max_x.is_infinite() || min_y.is_infinite() || max_y.is_infinite() {
        return None;
    }

    let padding = 1u32;
    let width = ((max_x - min_x).ceil() as u32).max(1) + padding * 2;
    let height = ((max_y - min_y).ceil() as u32).max(1) + padding * 2;

    let mut canvas = RgbaImage::new(width, height);

    let global_min_x = min_x.floor() as i32;
    let global_min_y = min_y.floor() as i32;
    let base_alpha = color[3] as f32 / 255.0;

    for glyph in glyphs {
        if let Some(bb) = glyph.pixel_bounding_box() {
            let base_x = bb.min.x - global_min_x + padding as i32;
            let base_y = bb.min.y - global_min_y + padding as i32;

            glyph.draw(|gx, gy, coverage| {
                let alpha = (coverage.clamp(0.0, 1.0) * base_alpha * 255.0).round() as u8;

                if alpha == 0 {
                    return;
                }

                let px = base_x + gx as i32;
                let py = base_y + gy as i32;

                if px >= 0 && py >= 0 && (px as u32) < width && (py as u32) < height {
                    canvas.put_pixel(
                        px as u32,
                        py as u32,
                        Rgba([color[0], color[1], color[2], alpha]),
                    );
                }
            });
        }
    }

    Some(canvas)
}

fn overlay_main_buffer(page: &GuiPage) {
    let mut screen_guard = Screen.lock().unwrap();
    let screen = &mut *screen_guard;

    if screen.is_empty() || screen[0].is_empty() {
        return;
    }

    let screen_h = screen.len();
    let screen_w = screen[0].len();
    let width_f = screen_w as f32;
    let height_f = screen_h as f32;

    for element in &page.elements {
        let x = rel_to_abs(element.x, width_f).round() as i64;
        let y = rel_to_abs(element.y, height_f).round() as i64;
        let mut w = rel_to_abs(element.w, width_f).round() as i64;
        let mut h = rel_to_abs(element.h, height_f).round() as i64;

        if element.kind != GuiKind::Text {
            if let Some(img) = element
                .frames
                .get(element.current_frame)
                .or(element.image.as_ref())
            {
                if element.w <= 0.0 {
                    w = img.width() as i64;
                }

                if element.h <= 0.0 {
                    h = img.height() as i64;
                }
            }
        } else if let Some(img) = &element.text_texture {
            if element.w <= 0.0 {
                w = img.width() as i64;
            }

            if element.h <= 0.0 {
                h = img.height() as i64;
            }
        }

        if w <= 0 {
            w = 1;
        }

        if h <= 0 {
            h = 1;
        }

        match element.kind {
            GuiKind::Text => {
                if let Some(img) = &element.text_texture {
                    draw_text_image_screen(screen, screen_w, screen_h, x, y, w, h, img);
                } else {
                    draw_text_screen(
                        screen,
                        screen_w,
                        screen_h,
                        x,
                        y,
                        w,
                        h,
                        &element.content,
                        element.color,
                    );
                }
            }
            GuiKind::Image => {
                if let Some(img) = &element.image {
                    blit_screen(
                        screen,
                        screen_w,
                        screen_h,
                        x,
                        y,
                        w,
                        h,
                        img,
                        element.color,
                    );
                }
            }
            GuiKind::Gif => {
                if let Some(img) = element
                    .frames
                    .get(element.current_frame)
                    .or(element.image.as_ref())
                {
                    blit_screen(
                        screen,
                        screen_w,
                        screen_h,
                        x,
                        y,
                        w,
                        h,
                        img,
                        element.color,
                    );
                }
            }
        }
    }
}

fn draw_text_image_screen(
    screen: &mut Vec<Vec<Pixel_structure>>,
    screen_w: usize,
    screen_h: usize,
    x: i64,
    y: i64,
    w: i64,
    h: i64,
    img: &RgbaImage,
) {
    let img_w = img.width() as i64;
    let img_h = img.height() as i64;

    if img_w <= 0 || img_h <= 0 {
        return;
    }

    let limit_w = if w <= 0 { img_w } else { w };
    let limit_h = if h <= 0 { img_h } else { h };

    for py in 0..img_h {
        if py >= limit_h {
            break;
        }

        let window_y = y + py;

        if window_y < 0 || window_y >= screen_h as i64 {
            continue;
        }

        let screen_y = (screen_h as i64 - 1 - window_y) as usize;

        for px in 0..img_w {
            if px >= limit_w {
                break;
            }

            let window_x = x + px;

            if window_x < 0 || window_x >= screen_w as i64 {
                continue;
            }

            let p = img.get_pixel(px as u32, py as u32);
            blend_pixel(&mut screen[screen_y][window_x as usize], [p[0], p[1], p[2], p[3]], ' ');
        }
    }
}

fn draw_text_screen(
    screen: &mut Vec<Vec<Pixel_structure>>,
    screen_w: usize,
    screen_h: usize,
    x: i64,
    y: i64,
    w: i64,
    h: i64,
    text: &str,
    color: [u8; 4],
) {
    if w <= 0 || h <= 0 {
        return;
    }

    let mut col = 0i64;
    let mut row = 0i64;

    for ch in text.chars() {
        if ch == '\n' {
            col = 0;
            row += 1;
            continue;
        }

        if col >= w {
            col = 0;
            row += 1;
        }

        if row >= h {
            break;
        }

        if ch == '\r' {
            continue;
        }

        if ch != ' ' {
            let window_x = x + col;
            let window_y = y + row;

            if window_x >= 0
                && window_x < screen_w as i64
                && window_y >= 0
                && window_y < screen_h as i64
            {
                let screen_y = screen_h as i64 - 1 - window_y;

                if screen_y >= 0 && screen_y < screen_h as i64 {
                    blend_pixel(
                        &mut screen[screen_y as usize][window_x as usize],
                        color,
                        ch,
                    );
                }
            }
        }

        col += 1;
    }
}

fn blit_screen(
    screen: &mut Vec<Vec<Pixel_structure>>,
    screen_w: usize,
    screen_h: usize,
    x: i64,
    y: i64,
    w: i64,
    h: i64,
    img: &RgbaImage,
    tint: [u8; 4],
) {
    if w <= 0 || h <= 0 {
        return;
    }

    let img_w = img.width() as i64;
    let img_h = img.height() as i64;

    if img_w <= 0 || img_h <= 0 {
        return;
    }

    for dy in 0..h {
        let window_y = y + dy;

        if window_y < 0 || window_y >= screen_h as i64 {
            continue;
        }

        let screen_y = screen_h as i64 - 1 - window_y;

        if screen_y < 0 || screen_y >= screen_h as i64 {
            continue;
        }

        let sy = ((dy as f32 / h as f32) * img_h as f32).floor() as i64;
        let sy = sy.clamp(0, img_h - 1);

        for dx in 0..w {
            let window_x = x + dx;

            if window_x < 0 || window_x >= screen_w as i64 {
                continue;
            }

            let sx = ((dx as f32 / w as f32) * img_w as f32).floor() as i64;
            let sx = sx.clamp(0, img_w - 1);

            let p = img.get_pixel(sx as u32, sy as u32);
            let src = tint_pixel([p[0], p[1], p[2], p[3]], tint);

            blend_pixel(
                &mut screen[screen_y as usize][window_x as usize],
                src,
                ' ',
            );
        }
    }
}

fn tint_pixel(src: [u8; 4], tint: [u8; 4]) -> [u8; 4] {
    [
        ((src[0] as u16 * tint[0] as u16) / 255) as u8,
        ((src[1] as u16 * tint[1] as u16) / 255) as u8,
        ((src[2] as u16 * tint[2] as u16) / 255) as u8,
        ((src[3] as u16 * tint[3] as u16) / 255) as u8,
    ]
}

fn blend_pixel(dest: &mut Pixel_structure, src: [u8; 4], symbol: char) {
    let alpha = src[3] as f32 / 255.0;

    if alpha <= 0.0 {
        return;
    }

    if alpha >= 1.0 {
        dest.pixel_RGBA_color = src;
        dest.pixel_symbol = symbol;
        return;
    }

    let inv = 1.0 - alpha;

    dest.pixel_RGBA_color[0] =
        (dest.pixel_RGBA_color[0] as f32 * inv + src[0] as f32 * alpha) as u8;
    dest.pixel_RGBA_color[1] =
        (dest.pixel_RGBA_color[1] as f32 * inv + src[1] as f32 * alpha) as u8;
    dest.pixel_RGBA_color[2] =
        (dest.pixel_RGBA_color[2] as f32 * inv + src[2] as f32 * alpha) as u8;
    dest.pixel_RGBA_color[3] = 255;
    dest.pixel_symbol = symbol;
}

fn overlay_dynamic_queue(page: &GuiPage) {
    let settings = Engine_settings.lock().unwrap();
    let width_f = settings.window_width.max(0) as f32;
    let height_f = settings.window_height.max(0) as f32;
    drop(settings);

    if width_f <= 0.0 || height_f <= 0.0 {
        return;
    }

    let prefix = format!("gui::{}::", page.name);

    let mut queue_guard = Draw_queue.lock().unwrap();
    let queue = &mut *queue_guard;

    queue.retain(|c| !c.draw_special_name.starts_with(prefix.as_str()));

    for (index, element) in page.elements.iter().enumerate() {
        let x = rel_to_abs(element.x, width_f);
        let y = rel_to_abs(element.y, height_f);
        let mut w = rel_to_abs(element.w, width_f);
        let mut h = rel_to_abs(element.h, height_f);

        if element.kind != GuiKind::Text {
            if let Some(img) = element
                .frames
                .get(element.current_frame)
                .or(element.image.as_ref())
            {
                if element.w <= 0.0 {
                    w = img.width() as f32;
                }

                if element.h <= 0.0 {
                    h = img.height() as f32;
                }
            }
        } else if let Some(img) = &element.text_texture {
            if element.w <= 0.0 {
                w = img.width() as f32;
            }

            if element.h <= 0.0 {
                h = img.height() as f32;
            }
        }

        if w <= 0.0 {
            w = 1.0;
        }

        if h <= 0.0 {
            h = 1.0;
        }

        match element.kind {
            GuiKind::Text => {
                if let Some(texture) = save_text_texture(&page.name, index, element) {
                    let special = format!("{}{}_text", prefix, index);
                    push_rect(
                        queue,
                        special,
                        x,
                        y,
                        w,
                        h,
                        [255, 255, 255, 255],
                        ' ',
                        texture,
                    );
                }
            }
            GuiKind::Image => {
                if !path_is_none(&element.content) {
                    let special = format!("{}{}_0", prefix, index);
                    push_rect(
                        queue,
                        special,
                        x,
                        y,
                        w,
                        h,
                        element.color,
                        ' ',
                        element.content.clone(),
                    );
                }
            }
            GuiKind::Gif => {
                let texture = texture_path_for_element(&page.name, index, element);

                if !path_is_none(&texture) {
                    let special = format!("{}{}_0", prefix, index);
                    push_rect(queue, special, x, y, w, h, element.color, ' ', texture);
                }
            }
        }
    }

    if let Ok(mut changed) = super::console_rc_render::Dynamic_changed.lock() {
        *changed = true;
    }
}

fn save_text_texture(page_name: &str, index: usize, element: &GuiElement) -> Option<String> {
    let img = element.text_texture.as_ref()?;

    let mut hasher = std::hash::DefaultHasher::new();
    element.content.hash(&mut hasher);
    element.font_path.hash(&mut hasher);
    element.font_size.to_bits().hash(&mut hasher);
    element.color.hash(&mut hasher);

    let hash = hasher.finish();
    let file_name = format!(
        "avoengine_gui_text_{}_{}_{}.png",
        page_name, index, hash
    );
    let path = std::env::temp_dir().join(file_name);

    if !path.exists() {
        let _ = img.save(&path);
    }

    if path.exists() {
        Some(path.to_string_lossy().to_string())
    } else {
        None
    }
}

fn texture_path_for_element(page_name: &str, index: usize, element: &GuiElement) -> String {
    if element.kind == GuiKind::Gif && element.frames.len() > 1 {
        if let Some(frame) = element.frames.get(element.current_frame) {
            let file_name = format!(
                "avoengine_gui_{}_{}_{}.png",
                page_name, index, element.current_frame
            );
            let path = std::env::temp_dir().join(file_name);

            if !path.exists() {
                let _ = frame.save(&path);
            }

            if path.exists() {
                return path.to_string_lossy().to_string();
            }
        }
    }

    element.content.clone()
}

fn push_rect(
    queue: &mut Vec<Draw_components>,
    special: String,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    color: [u8; 4],
    symbol: char,
    texture: String,
) {
    let texture_path = if path_is_none(&texture) {
        "none".to_string()
    } else {
        texture
    };

    queue.push(Draw_components {
        draw_type: "2d_object".to_string(),
        draw_x: x,
        draw_y: y,
        draw_z: 0.0,
        draw_symbol: symbol,
        draw_vertices: vec![0.0, 0.0, w, 0.0, w, h, 0.0, h],
        draw_RGBA_color: color,
        draw_texture_path: texture_path,
        draw_uvs: Vec::new(),
        properties: HashMap::new(),
        draw_special_name: special,
    });
}

fn rel_to_abs(value: f32, total: f32) -> f32 {
    if total <= 0.0 {
        return 0.0;
    }

    if value.abs() <= 1.0 {
        value * total
    } else {
        value
    }
}

fn path_is_none(p: &str) -> bool {
    let t = p.trim();
    t.is_empty() || t.eq_ignore_ascii_case("none")
}

fn sanitize_name(input: &str) -> String {
    input
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '_' || c == '-' {
                c
            } else {
                '_'
            }
        })
        .collect()
}