# AVOEngine — Полная документация (v0.2.0)

> Эта документация написана тупым ИИ, если что-то непонятно, то можете написать в discord сообщество https://discord.gg/XenUfXuHn4.

---

## Содержание

1. [Введение](#введение)
2. [Подключение](#подключение)
3. [Управление процессом и выходом](#управление-процессом-и-выходом)
4. [Глобальные настройки и состояние](#глобальные-настройки-и-состояние)
5. [Система тиков](#система-тиков)
6. [Окно, ввод и SDL](#окно-ввод-и-sdl)
7. [GUI (egui)](#gui-egui)
8. [Рендеринг](#рендеринг)
9. [Физика](#физика)
10. [Звук](#звук)
11. [Импорт glTF](#импорт-gltf)
12. [Engine_setup](#engine_setup)
13. [Подводные камни](#подводные-камни)
14. [Лицензия](#лицензия)

---

## Введение

AVOEngine — программный 3D-движок на Rust. Включает:

- Программный растеризатор с BSP-деревом, Z-буфером, альфа-смешиванием и `alpha_test`.
- Запечённое попиксельное освещение через тест «точка внутри полиэдра».
- Многопоточный рендер по строкам экрана.
- Скелетную анимацию с несколькими активными клипами и `quat_slerp`-смешиванием.
- Импорт glTF с упаковкой текстур в атлас.
- BSP-физику (sweep по осям) с трением, упругостью, массой и гравитацией.
- 3D-звук с окклюзией через BSP, затуханием и панорамированием (`kittyaudio`).
- GUI на `egui` через `egui-sdl2` с якорной системой.
- Атмосферное затухание цвета по глубине.
- Динамические декали («спреи»).

Зависимости (`Cargo.toml`): `sdl2 = 0.38`, `egui = 0.36`, `egui-sdl2 = 0.12 (canvas-backend)`, `image = 0.25`, `gltf = 1.4 (import, utils, names)`, `kittyaudio = 0.2.0`. Edition 2024.

---

## Подключение

```toml
[dependencies]
avoengine = { path = "./avoengine" }
```

`avoengine/Cargo.toml` объявляет `[lib] path = "avoengine.rs"`. Модули (`gui`, `window`, `render`, `physics`, `sound`, `gltf_parser`, `tick_system`) лежат рядом.

---

## Управление процессом и выходом

Это поведение **движка** (в `window::drain_into_delta`, которая вызывается из `window::Update_frame` и `window::Get_pressed_keys`):

| SDL-событие | Действие движка |
|-------------|------------------|
| `Quit` | `std::process::exit(0)` |
| `WindowEvent::Close` | `std::process::exit(0)` |
| `KeyDown` + `Keycode::Escape` | `std::process::exit(0)` |
| `MouseMotion` (при `Mouse_capture`) | добавление `(xrel, yrel)` в `MOUSE_DELTA` |
| `MouseWheel` (при `Mouse_capture`) | добавление `y` в `MOUSE_DELTA.2` |

`process::exit(0)` — немедленный выход, `Drop`-деструкторы не вызываются, `main` не возвращает управление. Если приложению нужна корректная очистка или код после игрового цикла — предусмотрите **собственный** флаг выхода (по любой другой клавише / условию) и завершайте `main` нормально.

Все SDL-события также копятся в `GUI_EVENTS` и на следующем `Update_frame` передаются в `gui::Update_gui`.

---

## Глобальные настройки и состояние

### Settings / Engine_settings

```rust
pub struct Settings {
    pub window_width: i128,          // 320
    pub window_height: i128,         // 240
    pub tick_speed: i128,            // 50 мс
    pub cores_multiply: u8,          // 2
    pub window_name: String,         // "avoengine"
    pub bsp_epsilon: f32,            // 1e-4
    pub bsp_split_weight: f32,       // 5.0
    pub bsp_min_leaf: usize,         // 8
    pub bsp_full_scan_limit: usize,  // 100
    pub bsp_max_depth: u32,          // 64
    pub near_plane: f32,             // 0.001
    pub cull_backfaces: bool,        // false
    pub spray_pixels_per_unit: f32,  // 512.0
    pub spray_offset: f32,           // 0.01
}
pub static Engine_settings: LazyLock<Mutex<Settings>>;
```

`Engine_setup` читает `window_width/height`, `cores_multiply`, `tick_speed` один раз при старте. Изменения после `Engine_setup` не пересоздают `Screen`, пул потоков или таймер тиков.

### Camera_structure / Camera

```rust
pub struct Camera_structure {
    pub camera_fov: u8,             // 90 — вертикальный FOV
    pub camera_x: f32,              // 0.0
    pub camera_y: f32,              // 1.0
    pub camera_z: f32,              // 0.0
    pub camera_pitch: f32,          // градусы
    pub camera_yaw: f32,            // градусы
    pub camera_roll: f32,           // градусы
    pub max_dist: u128,             // 256
    pub ambient_light: [u8; 3],     // [64, 64, 64]
}
pub static Camera: Mutex<Camera_structure>;
```

`camera_roll` влияет на `right`/`up` базис камеры во всех модулях (рендер, звук, спреи).

### Pixel_structure / Screen

```rust
pub struct Pixel_structure { pub color: [u8; 3] }
pub static Empty_pixel: Mutex<Pixel_structure>;             // по умолчанию [0, 0, 128]
pub static Screen: Mutex<Vec<Vec<Pixel_structure>>>;        // [height][width]
```

Размер `Screen` задаётся в `Setup_window_screen(width, height)`, вызываемой из `Engine_setup`.

### Draw_components / Static_scene

```rust
pub struct Draw_components {
    pub x: f32, pub y: f32, pub z: f32,
    pub pitch: f32, pub yaw: f32, pub roll: f32,  // градусы
    pub vertices: Vec<f32>,                        // тройки x, y, z
    pub RGBA_color: [u8; 4],
    pub properties: HashMap<String, String>,
    pub special_name: String,
}
pub static Static_scene: Mutex<Vec<Draw_components>>;
```

После изменения `Static_scene` вызывайте `render::Bake_static_scene()`.

### Dynamic_components / Draw_queue

```rust
pub struct Dynamic_components {
    pub x: f32, pub y: f32, pub z: f32,
    pub pitch: f32, pub yaw: f32, pub roll: f32,
    pub vertices: Vec<f32>,
    pub RGBA_color: [u8; 4],
    pub animations_in_play: Vec<String>,
    pub properties: HashMap<String, String>,
    pub special_name: String,
}
pub static Draw_queue: Mutex<Vec<Dynamic_components>>;
```

Очередь **не очищается автоматически** — жизненным циклом объектов управляет приложение.

### `properties` для Draw_components / Dynamic_components

| Ключ | Формат | Смысл |
|------|--------|-------|
| `texture` | путь | Текстура |
| `uv` | `u0,v0 u1,v1 ...` | UV по вершинам (3 пары на треугольник) |
| `alpha_test` | `u8` | Порог (0 — выключено) |
| `skeleton_id` | `u64` | (динамика) |
| `joint_indices` | `i0,i1,i2,i3;...` | (динамика) |
| `joint_weights` | `w0,w1,w2,w3;...` | (динамика) |
| `animation_speed` | `f32` | (динамика) |
| `animation_start_tick` | `u128` | (динамика) |
| `default_animation` | имя | (динамика, зацикливается) |
| `bone_translation_offsets`, `bone_rotation_offsets` | строки | Ручные смещения костей |

### Dynamic_components: полный API

```rust
impl Dynamic_components {
    pub fn new(name: String) -> Self;
    pub fn Attach_skeleton(&mut self, skel: Arc<Skeleton_data>) -> u64;
    pub fn Load_from_import(&mut self, result: &Import_result) -> u64;
    pub fn Get_skeleton(&self) -> Option<Arc<Skeleton_data>>;

    pub fn Play_animation(&mut self, name: String);
    pub fn Set_default_animation(&mut self, name: String);
    pub fn Stop_all_animations(&mut self);
    pub fn update_animations(&mut self, current_tick: u128, tick_ms: f32);

    pub fn Set_x(&mut self, delta: f32);
    pub fn Set_y(&mut self, delta: f32);
    pub fn Set_z(&mut self, delta: f32);
    pub fn Set_pitch(&mut self, delta: f32);
    pub fn Set_yaw(&mut self, delta: f32);
    pub fn Set_roll(&mut self, delta: f32);
    pub fn Set_R(&mut self, delta: i16);
    pub fn Set_G(&mut self, delta: i16);
    pub fn Set_B(&mut self, delta: i16);
    pub fn Set_A(&mut self, delta: i16);
    pub fn Set_animation_speed(&mut self, delta: f32);

    pub fn Set_bone_translation(&mut self, bone_idx: usize, dx: f32, dy: f32, dz: f32);
    pub fn Set_bone_rotation(&mut self, bone_idx: usize, dx: f32, dy: f32, dz: f32);

    pub fn Get_x(&self) -> f32;                       // аналогично y/z/pitch/yaw/roll/R/G/B/A
    pub fn Get_animation_speed(&self) -> f32;
    pub fn Get_current_animation_time(&self) -> f32;
    pub fn Get_current_animation_time_at(&self, current_tick: u128, tick_ms: f32) -> f32;

    pub fn Get_bone_names(&self) -> Vec<String>;
    pub fn Get_animation_names(&self) -> Vec<String>;
    pub fn Get_bone_index(&self, bone_name: &str) -> Option<usize>;

    pub fn Get_bone_local_translation(&self, bone_name: &str) -> Option<[f32; 3]>;
    pub fn Get_bone_local_rotation(&self, bone_name: &str) -> Option<[f32; 4]>;
    pub fn Get_bone_world_transform(&self, bone_name: &str) -> Option<[[f32; 4]; 4]>;
    pub fn Get_bone_world_position(&self, bone_name: &str) -> Option<[f32; 3]>;
    pub fn Get_bone_world_rotation(&self, bone_name: &str) -> Option<[f32; 4]>;

    pub fn compute_pose(&self) -> (Vec<[[f32; 4]; 4]>, Vec<[[f32; 4]; 4]>);
    pub fn compute_pose_at(&self, current_tick: u128, tick_ms: f32)
        -> (Vec<[[f32; 4]; 4]>, Vec<[[f32; 4]; 4]>);
}
```

**Семантика сеттеров (КРИТИЧНО):**

| Метод | Что делает |
|-------|------------|
| `Set_x/y/z`, `Set_pitch/yaw/roll` | **прибавляет** дельту |
| `Set_R/G/B/A` | **прибавляет** `i16`-дельту (clamp в `u8`) |
| `Set_animation_speed` | **прибавляет** дельту (clamp ≥ 0) |
| `Set_bone_translation/rotation` | **кумулятивно** прибавляет к смещению |
| `Play_animation` | ставит клип в очередь `animations_in_play` (один раз) |
| `Set_default_animation` | задаёт зацикливающийся клип |
| `Stop_all_animations` | очищает `animations_in_play` и сбрасывает `animation_start_tick` |

**Смешивание нескольких активных клипов:** трансляции/масштабы усредняются, повороты смешиваются последовательным `quat_slerp` с коэффициентом `1/(k+1)`.

**Скиннинг:** каждая вершина смешивается по `joint_indices`/`joint_weights` с матрицей `world[i] * inverse_bind[i]`. До 4 костей на вершину.

### Pseudo_light_map

```rust
pub static Pseudo_light_map: Mutex<Vec<Draw_components>>;
```

Каждый элемент — источник света: геометрия используется как **объём**, цвет — из `RGBA_color[0..3]`. Запекается в `render::Bake_light()`.

### Physics

```rust
pub struct Physics_components {
    pub x: f32, pub y: f32, pub z: f32,
    pub pitch: f32, pub yaw: f32, pub roll: f32,
    pub vertices: Vec<f32>,
    pub properties: HashMap<String, String>,
    pub special_name: String,
}
pub static Physics_scene: Mutex<Vec<Physics_components>>;

pub struct Dynamic_physics_components {
    pub x: f32, pub y: f32, pub z: f32,
    pub pitch: f32, pub yaw: f32, pub roll: f32,
    pub vertices: Vec<f32>,
    pub RGBA_color: [u8; 4],
    pub animations_in_play: Vec<String>,
    pub force_vectors: Vec<[f32; 3]>,
    pub impact_with: Vec<u128>,
    pub properties: HashMap<String, String>,
    pub special_name: String,
}
pub static Physics_queue: Mutex<Vec<Dynamic_physics_components>>;

pub struct Physics_properties {
    pub g: f32,                   // 9.8
    pub resistance_in_air: f32,   // 1.0
    pub atmosphere_density: f32,  // 0.1 (используется и в рендере)
}
pub static Earth: Mutex<Physics_properties>;
```

`Physics_components.properties`: `collision` (`"1"` — участвует), `resistance` (`f32`), `bounce` (`f32`), `soundproofing` (`0..1`).

`Dynamic_physics_components.properties` дополнительно: `has_gravity`, `mass`.

### Sounds

```rust
pub struct Sound_components {
    pub path: String,
    pub x: f32, pub y: f32, pub z: f32,
    pub time: i128,                            // счётчик (мс); 0 → пересоздать инстанс
    pub properties: HashMap<String, String>,   // volume, soundproofing
    pub special_name: String,                  // УНИКАЛЬНЫЙ ключ инстанса
}
pub static Sounds: Mutex<Vec<Sound_components>>;
```

### GUI

```rust
pub static GUI_OBJECTS: LazyLock<Mutex<HashMap<String, String>>>;   // {key} в шаблонах
pub static GUI_SCENE: Mutex<Vec<GuiElement>>;
```

---

## Система тиков

```rust
pub fn Init_tick_system();
pub fn Get_current_tick() -> u128;   // тик = tick_speed мс
```

Тик инициализируется в `Engine_setup`. Анимации и физика синхронизируются по тику.

---

## Окно, ввод и SDL

```rust
pub fn Create_window();
pub fn With_canvas_and_pump<F, R>(f: F) -> R
where F: FnOnce(&mut Canvas<Window>, &mut EventPump) -> R;
pub fn Convert_frame() -> (Vec<u32>, usize, usize);
pub fn Update_frame();
pub fn Get_pressed_keys() -> Vec<i32>;
pub fn Mouse_capture();
pub fn Return_mouse();
pub fn Get_mouse() -> Vec<i32>;
pub fn Return_properties() -> &'static RefCell<Option<SdlState>>;
```

### `SdlState`

```rust
pub struct SdlState {
    pub sdl: sdl2::Sdl,
    pub _video: sdl2::VideoSubsystem,
    pub event_pump: sdl2::EventPump,
    pub canvas: Canvas<Window>,
    pub texture: Texture,
    pub mouse: sdl2::mouse::MouseUtil,
    pub is_captured: bool,
}
```

Хранится в `thread_local` `RefCell<Option<SdlState>>`.

### `Update_frame`

1. `Convert_frame()` — параллельная упаковка `Screen` → `u32` (RGB888 через `(r<<16)|(g<<8)|b`). Количество потоков = `RENDER_THREADS`.
2. `Texture::update` + `Canvas::copy` + `Canvas::clear`.
3. `drain_into_delta` — опрос SDL-событий (см. раздел «Управление процессом»).
4. `gui::Update_gui(canvas, events)` — **возвращаемое `Vec<String>` отбрасывается**.
5. `canvas.present()`.

Если приложению нужны действия GUI-кнопок (`Vec<String>` с id нажатых), вызывайте `gui::Update_gui` самостоятельно (и не полагайтесь на `Update_frame` для их получения).

### `Get_pressed_keys`

- Вызывает `drain_into_delta` (попутно пополняет `GUI_EVENTS`).
- Возвращает `SDL_Scancode` нажатых клавиш как `i32` (не `Keycode`!).

### `Get_mouse`

```rust
vec![x, y, dx, dy, buttons, wheel]
```

| Поле | Смысл |
|------|-------|
| `x`, `y` | абсолютные координаты |
| `dx`, `dy` | относительное движение (только при `Mouse_capture()`, иначе 0) |
| `buttons` | маска: LMB=1, MMB=2, RMB=4, X1=8, X2=16 |
| `wheel` | накопленный `y` колеса |

**Каждый вызов обнуляет `MOUSE_DELTA`** — вызывайте один раз за кадр.

### `Mouse_capture` / `Return_mouse`

`Mouse_capture` включает relative-mouse mode (SDL2), скрывает курсор и берёт окно в grab. `Return_mouse` откатывает обратно.

### `Return_properties` (unsafe escape hatch)

Возвращает `&'static RefCell<Option<SdlState>>` через `unsafe`-каст указателя из `thread_local`. Даёт прямой доступ к `Canvas`, `EventPump`, `MouseUtil`. Не используйте `borrow_mut()` при активном внутреннем `borrow()` движка — паника.

---

## GUI (egui)

```rust
pub enum Anchor {
    TopLeft, TopRight, BottomLeft, BottomRight,
    Center, TopCenter, BottomCenter, LeftCenter, RightCenter,
}

pub enum GuiElement {
    Text {
        template: String, color: [u8; 4], font: String, size: f32,
        anchor: Anchor, offset: (f32, f32),
    },
    Button {
        id: String, label: String, size: (f32, f32),
        anchor: Anchor, offset: (f32, f32),
    },
    Image {
        path: String, size: (f32, f32),
        anchor: Anchor, offset: (f32, f32),
    },
}

pub fn Init_gui(canvas: &Canvas<Window>);
pub fn Update_gui(canvas: &mut Canvas<Window>, events: &[Event]) -> Vec<String>;
```

### `Text`

- `template` подставляет `{key}` из `GUI_OBJECTS`.
- `font`:
  - `"proportional"` / `"Proportional"` → `Proportional`
  - `"monospace"` / `"Monospace"` → `Monospace`
  - иное → `FontFamily::Name(...)`; шрифт должен быть зарегистрирован через `egui::Context::set_fonts`, иначе метка не отобразится.
- `wrap_mode(TextWrapMode::Extend)` — **без переносов**.

### `Button`

- `id` должен быть уникальным (используется как ключ `egui::Id`).
- Стиль — дефолтный egui; кастомизации цвета/шрифта нет.
- Если нажата — `id` попадает в возвращаемый `Vec<String>`.

### `Image`

- `path` кэшируется по строке пути в `TEXTURES`.
- `size` масштабирует без сохранения пропорций.

### Якоря

`resolve_anchor(anchor, offset)` → `egui::Area::anchor(align, offset)`. `offset` — смещение в пикселях относительно привязанного угла/края.

---

## Рендеринг

### Инициализация

```rust
pub fn init_render(width: usize, height: usize, num_threads: usize);
```

Задаёт `Z_BUFFER` и `RENDER_THREADS`. Вызывается из `Engine_setup`.

### Триангуляция

```rust
pub struct render_triangle {
    pub vertices: [f32; 9],
    pub uvs: [f32; 6],
    pub texture: Option<Arc<Texture>>,
    pub RGBA_color: [u8; 4],
    pub alpha_test: u8,
    pub properties: Arc<HashMap<String, String>>,
}

pub fn triangulation(objects: Vec<Draw_components>) -> Vec<render_triangle>;
```

- Поворот объекта — `rotation_matrix` + `apply_rotation` (X → Y → Z).
- UV извлекаются из `properties["uv"]` по индексу треугольника.
- Итоговый цвет пикселя: сначала `tex * RGBA_color / 255`, затем `alpha = tex_a * RGBA_a / 255`.

### BSP

```rust
pub struct Plane { pub point: [f32; 3], pub normal: [f32; 3] }

pub enum BspNode {
    Leaf    { triangles: Vec<render_triangle> },
    Internal {
        plane: Plane,
        coplanar: Vec<render_triangle>,
        front: Box<BspNode>,
        back: Box<BspNode>,
    },
}

pub fn build_bsp(tris: Vec<render_triangle>, depth: u32) -> BspNode;
pub static BSP_ROOT: Mutex<Option<Box<BspNode>>>;
```

- `choose_split_plane`: эвристика `score = splits * bsp_split_weight + |front - back|`. При `len >= bsp_full_scan_limit` — прореживание с шагом `len / bsp_full_scan_limit`.
- Отсев: `splits * 2 > len` → плоскость отбрасывается.
- `Spanning`-треугольники попадают в оба поддерева (без фактической нарезки геометрии).

### Запекание

```rust
pub fn Bake_static_scene();   // Static_scene → BSP_ROOT
pub fn Bake_light();          // Pseudo_light_map → LIGHT_SOURCES
```

Не автоматическое — вызывайте вручную после изменения соответствующих сцен.

### Освещение

```rust
pub struct Light_source {
    pub triangles: Vec<render_triangle>,
    pub color: [u8; 3],
    pub bounds_min: [f32; 3],
    pub bounds_max: [f32; 3],
    pub tri_bounds: Vec<([f32; 3], [f32; 3])>,
}
pub static LIGHT_SOURCES: Mutex<Vec<Arc<Light_source>>>;
```

Тест «внутри объёма»: AABB-отсев → чётность пересечений диагонального луча с треугольниками. Нечётное число → точка внутри → цвет источника прибавляется. Сумма по всем источникам с clamp 255.

Это объёмный тест, а не «свет по нормали». Он не затеняет по углу — только по признаку «внутри/снаружи».

### Атмосферное затухание

В растеризаторе:
```
amb_r = ambient_light[0] - 255 + z * atmosphere_density
final_r = clamp(obj_r + light_r + amb_r, 0, obj_r)
```
`z = 1/inv_z` — расстояние по forward-оси камеры. `atmosphere_density` из `Earth`.

### Спреи

```rust
pub fn Add_spray(path: String, center: [f32; 3], color: [u8; 4]);
pub fn Add_spray_at(path: String, center: [f32; 3], normal: [f32; 3], color: [u8; 4]);
pub static SPRAYS: Mutex<Vec<spray>>;
```

- `Add_spray` ищет ближайшую поверхность только в `BSP_ROOT` (статическая геометрия).
- Ориентация: up-вектор камеры проецируется на плоскость спрея (fallback — мировой up, затем мировой Z).
- Размер в мировых единицах = `tex.{w,h} / spray_pixels_per_unit`, сдвиг по нормали = `spray_offset`.
- Спреи не участвуют в освещении и в `alpha_test`.
- API удаления нет.

### `Raycast_dynamic`

```rust
pub fn Raycast_dynamic(origin: [f32; 3], dir: [f32; 3])
    -> Option<(f32, [f32; 3], [f32; 3])>;
```

- Трассирует по **динамическим** треугольникам (`prepare_dynamic_triangles`).
- Возвращает `(t, hit_point, normal)`; нормаль нормализована.
- Не проверяет `alpha_test` и не трассирует статику (это отдельная задача пользователя через `BSP_ROOT`).

### `Render_frame`

Порядок:
1. Очистка `Screen` в `Empty_pixel.color`; обнуление `Z_BUFFER`.
2. `CameraBasis` (forward/right/up с учётом roll).
3. `prepare_dynamic_triangles` — скиннинг + вращение + трансляция.
4. Параллельный рендер по строкам, каждый поток:
   - динамические треугольники,
   - BSP-дерево (обход с порядком по `signed_distance(camera, plane)`),
   - спреи.
5. В растеризаторе:
   - `near`-плоскость (`near_plane`),
   - перспективно-корректные `1/z`, `u/z`, `v/z`,
   - тест `inv_z > zbuf`,
   - сэмплинг текстуры с `rem_euclid` (тайлинг UV),
   - `alpha_test`: если `src_a >= alpha_test` — непрозрачная запись + Z; иначе пиксель пропускается.
   - `alpha == 255`: непрозрачная запись + Z.
   - `0 < alpha < 255`: альфа-смешивание, **Z-буфер не обновляется**.
   - `cull_backfaces`: если `true` и `area2 < 0` — отбрасывание.

---

## Физика

### Запекание

```rust
pub fn Bake_physics_scene();
pub static PHYSICS_BSP_ROOT: Mutex<Option<Box<render::BspNode>>>;
pub static PHYSICS_OBJECT_RESISTANCES: LazyLock<Mutex<HashMap<u128, f32>>>;
pub static PHYSICS_OBJECT_BOUNCES:     LazyLock<Mutex<HashMap<u128, f32>>>;
```

`Bake_physics_scene` фильтрует `Physics_scene` по `properties["collision"] == "1"`, триангулирует, строит BSP и заполняет карты сопротивлений/упругости по индексам объектов. Цвет (`RGBA_color`) при этом теряется (ставится `[255,255,255,255]`) — для отрисовки дублируйте в `Static_scene`.

### `Update_physics`

Пайплайн на объект:

1. `update_animations`.
2. Если был контакт в прошлом тике — горизонтальные компоненты умножаются на
   `exp(-avg_surface_resistance * obj_resistance / mass)`.
3. Иначе — воздушное трение: `exp(-resistance_in_air * obj_resistance / mass)` (если `resistance_in_air > 0`).
4. При `has_gravity == "1"` → `force_vectors.push([0, -g*dt, 0])`.
5. `vel = sum(force_vectors)`; смещение = `vel * dt`.
6. При `collision == "1"` и наличии `PHYSICS_BSP_ROOT` — sweep по осям **Y → X → Z**:
   - raycast из текущей позиции в направлении `delta[axis]`;
   - при попадании: `pos[axis] = hit[axis] - signum(delta[axis]) * 0.001`, `Add_impact(obj_id)`, `handle_collision_reflect(obj, axis, combined_bounce)`;
   - `combined_bounce = obj_bounce * surface_bounce`;
   - если `combined_bounce > 0` — добавляется отражённая скорость `-sum * combined_bounce`.
7. Иначе — простое прибавление смещения.
8. Удаление почти нулевых `force_vectors` (`sum(|f|) <= 1e-4`).
9. `impact_with` очищается автоматически в начале следующего тика.

**Ключевое:** `force_vectors` — это **аккумулятор скоростей**, а не сил. `Add_force(f)` уже делит на `mass`.

### API `Dynamic_physics_components`

То же, что `Dynamic_components`, плюс:

```rust
pub fn Set_resistance(&mut self, r: f32);        // абсолют
pub fn Get_resistance(&self) -> f32;
pub fn Set_bounce(&mut self, b: f32);            // абсолют
pub fn Get_bounce(&self) -> f32;
pub fn Set_mass(&mut self, m: f32);              // абсолют, 0 → 1.0 при чтении
pub fn Get_mass(&self) -> f32;

pub fn Add_force(&mut self, force: [f32; 3]);   // делит на mass и прибавляет
pub fn Clear_forces(&mut self);
pub fn Get_forces(&self) -> Vec<[f32; 3]>;

pub fn Add_impact(&mut self, id: u128);          // дедуплицирует
pub fn Clear_impacts(&mut self);
pub fn Get_impacts(&self) -> Vec<u128>;
```

**`Set_resistance/bounce/mass` — абсолютные**, в отличие от дельт `Set_x/y/z`.

---

## Звук

```rust
pub fn Init_sound();
pub fn Play_sound();
```

`Play_sound` на каждый `Sound_components`:

1. `distance = |src - cam|`.
2. **Окклюзия** (`compute_soundproofing`): луч от источника к камере через `PHYSICS_BSP_ROOT` (только `collision == "1"`) и через **все** треугольники `Dynamic_physics_components` (независимо от `collision`). Множитель: `∏ (1 - soundproofing).clamp(0, 1)`, где `soundproofing` из `properties` (по умолчанию 0).
3. **Затухание:** `1 / (1 + dist * 0.1)`.
4. **Панорама:** проекция `(src - cam)` на правый вектор камеры (с учётом pitch/roll), clamp `[-1, 1]`.
5. **Громкость:** `prop_volume * soundproof * attenuation`.
6. Управление инстансами: `SOUND_HANDLES: HashMap<String, SoundHandle>`, ключ — `special_name`.
   - `time == 0` → старый инстанс удаляется (перезапуск).
   - Завершившийся инстанс удаляется.
   - Новый инстанс создаётся через `mixer.play(Sound::from_path(...))`.
7. `time += tick_speed`.

`Play_sound` не блокирует поток. `kittyaudio` грузит файл при первом создании инстанса — для частых звуков кэшируйте `Sound` самостоятельно.

---

## Импорт glTF

```rust
pub fn Import_gltf(path: String) -> Option<Import_result>;

pub struct Import_result {
    pub texture_path: String,
    pub uv: String,
    pub uvs: Vec<f32>,
    pub vertices: Vec<f32>,
    pub joint_indices: Vec<[u8; 4]>,
    pub joint_weights: Vec<[f32; 4]>,
    pub bone_names: Vec<String>,
    pub animation_names: Vec<String>,
    pub skeleton: Arc<Skeleton_data>,
}
```

- **`MIRROR_X = true`** — инвертирует X позиции и компоненты кватерниона.
- **Атлас:** все базовые текстуры упаковываются в `<stem>_atlas.png` рядом с моделью; UV пересчитываются в атласные координаты.
- **Скиннинг при импорте:** каждая вершина привязывается к **одной** кости (владелец узла меша) с весом 1.0. Полноценный мультискиннинг glTF не загружается — заполняйте `joint_indices`/`joint_weights` вручную, если нужно.
- Кость с индексом > 255 игнорируется.
- **Утилиты** (публичные): `mat4_identity`, `mat4_mul`, `mat4_apply`, `mat4_inverse`, `quat_to_mat3`, `trs_to_matrix`, `quat_mul`, `quat_slerp`, `lerp3`, `eval_translation`, `eval_rotation`, `eval_scale`.
- `Keyframe` объявлен, но не используется.

---

## Engine_setup

```rust
pub fn Engine_setup();
```

Последовательность:

1. Читает `window_width`, `window_height`, `cores_multiply` из `Engine_settings`.
2. `Setup_window_screen(w, h)` — создаёт `Screen`.
3. `num_threads = max(1, cores * cores_multiply - 1)`.
4. `render::init_render(w, h, num_threads)`.
5. `window::Create_window()`.
6. `window::With_canvas_and_pump(|canvas, _| gui::Init_gui(canvas))`.
7. `tick_system::Init_tick_system()`.
8. `sound::Init_sound()`.

**Не вызывает** `Bake_static_scene` / `Bake_light` / `Bake_physics_scene` — это делает приложение после заполнения сцен.

---

## Подводные камни

1. **`Set_x/y/z/pitch/yaw/roll/R/G/B/A/animation_speed` — дельта**, а `Set_resistance/bounce/mass` — абсолют.
2. **`force_vectors` — скорости, не силы.** `Add_force` уже делит на массу.
3. **`Clear_forces` полностью гасит импульс** — не вызывайте каждый тик, если нужен `bounce`.
4. **`Update_frame` отбрасывает `Vec<String>` действий GUI.** Для кнопок — вызывайте `gui::Update_gui` отдельно.
5. **`Get_pressed_keys` и `Get_mouse` вызывают `drain_into_delta`** — пополняют `GUI_EVENTS` и обнуляют `MOUSE_DELTA`. `Get_mouse` — один раз за кадр.
6. **ESC / крестик / `Quit` → `process::exit(0)`.** Без `Drop`; код после цикла не выполнится. Для корректной очистки — собственный флаг выхода.
7. **Запекание вручную:** `Bake_static_scene`, `Bake_light`, `Bake_physics_scene`.
8. **`Convert_frame` использует `RGB888`** — альфа теряется.
9. **Кэши текстур и скелетов не очищаются** (`TEXTURE_CACHE`, `SKELETON_REGISTRY`) — память растёт при частой замене ассетов.
10. **`0 < alpha < 255` не обновляет Z-буфер** — полупрозрачные объекты не перекрывают последующие.
11. **`alpha_test` — жёсткий порог**: `a >= alpha_test` → запись; иначе пропуск.
12. **`cull_backfaces` по умолчанию `false`** — задние грани видны.
13. **`Text` в GUI не переносит строки** (`TextWrapMode::Extend`).
14. **`font` в `Text`:** только `proportional`/`monospace` работают «из коробки»; прочие требуют ручной регистрации в `egui::Context`.
15. **Импорт glTF: одна кость на вершину**, `MIRROR_X = true`, ограничение `bone_idx <= 255`.
16. **Спреи не участвуют в освещении и `alpha_test`.**
17. **`Raycast_dynamic` не проверяет `alpha_test`** и не трассирует статику.
18. **`special_name` звука — уникальный ключ инстанса.** Коллизии → перезапись.
19. **Окклюзия звука:** статика только с `collision == "1"`; динамика — все `Dynamic_physics_components` независимо от их `collision`.
20. **`Physics_scene` при запекании теряет цвет** (`RGBA_color` → белый).
21. **Столкновения только со статикой.** Динамические объекты друг с другом не сталкиваются.
22. **Sweep по осям без CCD.** Быстрые объекты туннелируются.
23. **`update_animations` вызывается и в `Update_physics`, и в `prepare_dynamic_triangles`** (дважды за тик; в норме идемпотентно).
24. **`Empty_pixel` по умолчанию `[0, 0, 128]`; `ambient_light` — `[64, 64, 64]`.**
25. **`Return_properties` — unsafe escape hatch.** Несовместимое заимствование → паника.
26. **`RENDER_THREADS = max(1, cores * cores_multiply - 1)`.**

---

## Лицензия

AVOEngine распространяется под **GNU Lesser General Public License v3.0** (LGPL-3.0). Полный текст — в `LICENSE.md`.

Copyright (C) 2026 Vladislav Kagaqkin.