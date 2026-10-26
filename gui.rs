use std::collections::HashMap;
use std::sync::{LazyLock, Mutex};

use sdl2::event::Event;
use sdl2::keyboard::Keycode;
use sdl2::render::Canvas;
use sdl2::video::Window;
use sdl2::EventPump;

#[derive(Clone, Copy)]
pub enum Anchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center,
    TopCenter,
    BottomCenter,
    LeftCenter,
    RightCenter,
}

pub enum GuiElement {
    Text {
        template: String,
        color: [u8; 4],
        font: String,
        size: f32,
        anchor: Anchor,
        offset: (f32, f32),
    },
    Button {
        id: String,
        label: String,
        size: (f32, f32),
        anchor: Anchor,
        offset: (f32, f32),
    },
    Image {
        path: String,
        size: (f32, f32),
        anchor: Anchor,
        offset: (f32, f32),
    },
}

pub static GUI_OBJECTS: LazyLock<Mutex<HashMap<String, String>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

pub static GUI_SCENE: Mutex<Vec<GuiElement>> = Mutex::new(Vec::new());

static TEXTURES: LazyLock<Mutex<HashMap<String, egui::TextureHandle>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

thread_local! {
    static EGUI: std::cell::RefCell<Option<egui_sdl2::EguiCanvas>> =
        std::cell::RefCell::new(None);
}

pub fn Init_gui(canvas: &Canvas<Window>) {
    let egui = egui_sdl2::EguiCanvas::new(canvas);
    EGUI.with(|e| {
        *e.borrow_mut() = Some(egui);
    });
    println!("[Gui]Initialithated gui");
}

pub fn Update_gui(canvas: &mut Canvas<Window>, event_pump: &mut EventPump) -> Vec<String> {
    let mut actions = Vec::new();

    EGUI.with(|e| {
        let mut egui_opt = e.borrow_mut();
        let egui = egui_opt.as_mut().expect("GUI not initialized");

        for event in event_pump.poll_iter() {
            egui.on_event(&*canvas, &event);
            match event {
                Event::Quit { .. } => std::process::exit(0),
                Event::KeyDown {
                    keycode: Some(Keycode::Escape),
                    ..
                } => std::process::exit(0),
                _ => {}
            }
        }

        egui.run(|ctx| {
            let scene = GUI_SCENE.lock().unwrap();
            let objects = GUI_OBJECTS.lock().unwrap();

            for (i, element) in scene.iter().enumerate() {
                match element {
                    GuiElement::Text {
                        template,
                        color,
                        font,
                        size,
                        anchor,
                        offset,
                    } => {
                        let text = format_text(template, &objects);
                        let text_color = egui::Color32::from_rgba_unmultiplied(
                            color[0], color[1], color[2], color[3],
                        );
                        let font_id = egui::FontId::new(*size, font_family(font));
                        let (align, offset_vec) = resolve_anchor(*anchor, *offset);

                        egui::Area::new(egui::Id::new(format!("text_{}", i)))
                            .anchor(align, offset_vec)
                            .show(ctx, |ui| {
                                ui.add(egui::Label::new(
                                    egui::RichText::new(text)
                                        .color(text_color)
                                        .font(font_id),
                                ));
                            });
                    }
                    GuiElement::Button {
                        id,
                        label,
                        size,
                        anchor,
                        offset,
                    } => {
                        let (align, offset_vec) = resolve_anchor(*anchor, *offset);
                        let mut clicked = false;

                        egui::Area::new(egui::Id::new(format!("btn_{}", id)))
                            .anchor(align, offset_vec)
                            .show(ctx, |ui| {
                                let btn = ui.add_sized(*size, egui::Button::new(label));
                                if btn.clicked() {
                                    clicked = true;
                                }
                            });

                        if clicked {
                            actions.push(id.clone());
                        }
                    }
                    GuiElement::Image {
                        path,
                        size,
                        anchor,
                        offset,
                    } => {
                        let (align, offset_vec) = resolve_anchor(*anchor, *offset);
                        let texture = load_texture(ctx, path);
                        let size_vec = egui::Vec2::new(size.0, size.1);

                        egui::Area::new(egui::Id::new(format!("img_{}", i)))
                            .anchor(align, offset_vec)
                            .show(ctx, |ui| {
                                ui.add(egui::Image::new((texture.id(), size_vec)));
                            });
                    }
                }
            }
        });

        egui.paint(canvas);
    });

    actions
}

fn resolve_anchor(anchor: Anchor, offset: (f32, f32)) -> (egui::Align2, egui::Vec2) {
    let align = match anchor {
        Anchor::TopLeft => egui::Align2::LEFT_TOP,
        Anchor::TopRight => egui::Align2::RIGHT_TOP,
        Anchor::BottomLeft => egui::Align2::LEFT_BOTTOM,
        Anchor::BottomRight => egui::Align2::RIGHT_BOTTOM,
        Anchor::Center => egui::Align2::CENTER_CENTER,
        Anchor::TopCenter => egui::Align2::CENTER_TOP,
        Anchor::BottomCenter => egui::Align2::CENTER_BOTTOM,
        Anchor::LeftCenter => egui::Align2::LEFT_CENTER,
        Anchor::RightCenter => egui::Align2::RIGHT_CENTER,
    };
    (align, egui::Vec2::new(offset.0, offset.1))
}

fn font_family(name: &str) -> egui::FontFamily {
    match name {
        "proportional" | "Proportional" => egui::FontFamily::Proportional,
        "monospace" | "Monospace" => egui::FontFamily::Monospace,
        other => egui::FontFamily::Name(other.into()),
    }
}

fn format_text(template: &str, objects: &HashMap<String, String>) -> String {
    let mut result = template.to_string();
    for (key, value) in objects {
        result = result.replace(&format!("{{{}}}", key), value);
    }
    result
}

fn load_texture(ctx: &egui::Context, path: &str) -> egui::TextureHandle {
    let mut textures = TEXTURES.lock().unwrap();
    if let Some(tex) = textures.get(path) {
        return tex.clone();
    }

    let img = image::open(path)
        .expect("Failed to load image")
        .to_rgba8();
    let size = [img.width() as usize, img.height() as usize];
    let pixels = img.into_raw();
    let color_image = egui::ColorImage::from_rgba_unmultiplied(size, &pixels);
    let texture = ctx.load_texture(path, color_image, egui::TextureOptions::default());

    textures.insert(path.to_string(), texture.clone());
    texture
}