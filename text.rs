use image::{DynamicImage, ImageFormat, Rgba, RgbaImage};
use rusttype::{point, Font as RtFont, Scale};
use std::fmt;
use std::io::Cursor;

#[derive(Debug)]
pub enum FontError {
    InvalidMemory,
    InvalidFont,
    InvalidScale,
    MissingGlyph(char),
    Image(image::ImageError),
}

impl fmt::Display for FontError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidMemory => write!(f, "invalid memory or empty font data"),
            Self::InvalidFont => write!(f, "failed to parse font"),
            Self::InvalidScale => write!(f, "invalid pixel size"),
            Self::MissingGlyph(c) => write!(f, "font has no glyph for character {c:?}"),
            Self::Image(err) => write!(f, "image encoding error: {err}"),
        }
    }
}

impl std::error::Error for FontError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Image(err) => Some(err),
            _ => None,
        }
    }
}

impl From<image::ImageError> for FontError {
    fn from(err: image::ImageError) -> Self {
        Self::Image(err)
    }
}

pub struct Font {
    inner: RtFont<'static>,
}

impl Font {
    pub fn from_vec(bytes: Vec<u8>) -> Result<Self, FontError> {
        if bytes.is_empty() {
            return Err(FontError::InvalidMemory);
        }

        let inner = RtFont::try_from_vec(bytes).ok_or(FontError::InvalidFont)?;
        Ok(Self { inner })
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self, FontError> {
        Self::from_vec(bytes.to_vec())
    }

    pub unsafe fn from_memory(ptr: *const u8, len: usize) -> Result<Self, FontError> {
        if ptr.is_null() || len == 0 {
            return Err(FontError::InvalidMemory);
        }

        let slice = unsafe { std::slice::from_raw_parts(ptr, len) };
        Self::from_bytes(slice)
    }

    pub unsafe fn from_address(address: usize, len: usize) -> Result<Self, FontError> {
        unsafe { Self::from_memory(address as *const u8, len) }
    }

    pub fn render_char_png(
        &self,
        letter: char,
        pixel_size: f32,
    ) -> Result<Vec<u8>, FontError> {
        if !pixel_size.is_finite() || pixel_size <= 0.0 {
            return Err(FontError::InvalidScale);
        }

        let scale = Scale::uniform(pixel_size);

        let glyph = self.inner.glyph(letter);

        if glyph.id() == rusttype::GlyphId(0) {
            return Err(FontError::MissingGlyph(letter));
        }

        let v_metrics = self.inner.v_metrics(scale);

        let scaled = glyph.scaled(scale);
        let advance = scaled.h_metrics().advance_width;

        let positioned = scaled.positioned(point(0.0, v_metrics.ascent));

        let rgba = if let Some(bb) = positioned.pixel_bounding_box() {
            let width = bb.width().max(1) as u32;
            let height = bb.height().max(1) as u32;

            let mut image = RgbaImage::new(width, height);

            positioned.draw(|x, y, coverage| {
                if x >= width || y >= height {
                    return;
                }

                let alpha = (coverage.clamp(0.0, 1.0) * 255.0).round() as u8;

                if alpha != 0 {
                    image.put_pixel(x, y, Rgba([0, 0, 0, alpha]));
                }
            });

            image
        } else {
            let width = advance.round().max(1.0) as u32;
            let height = (v_metrics.ascent - v_metrics.descent).ceil().max(1.0) as u32;

            RgbaImage::new(width, height)
        };

        let mut png = Vec::new();

        {
            let mut cursor = Cursor::new(&mut png);
            DynamicImage::ImageRgba8(rgba).write_to(&mut cursor, ImageFormat::Png)?;
        }

        Ok(png)
    }
}

pub unsafe fn load_font_from_memory(
    ptr: *const u8,
    len: usize,
) -> Result<Font, FontError> {
    unsafe { Font::from_memory(ptr, len) }
}

pub fn render_char_png(
    font: &Font,
    letter: char,
    pixel_size: f32,
) -> Result<Vec<u8>, FontError> {
    font.render_char_png(letter, pixel_size)
}