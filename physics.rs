use rapier3d::prelude::*;
use rapier3d::na::UnitQuaternion;
use std::collections::HashMap;
use std::sync::Mutex;
use crate::{Draw_components, Draw_queue, Static_scene, Engine_settings};

struct PhysicsWorld {
    gravity: Vector<Real>,
    integration_parameters: IntegrationParameters,
    physics_pipeline: PhysicsPipeline,
    island_manager: IslandManager,
    broad_phase: DefaultBroadPhase,
    narrow_phase: NarrowPhase,
    bodies: RigidBodySet,
    colliders: ColliderSet,
    impulse_joints: ImpulseJointSet,
    multibody_joints: MultibodyJointSet,
    ccd_solver: CCDSolver,
    dynamic_handles: HashMap<String, RigidBodyHandle>,
    static_handles: HashMap<String, RigidBodyHandle>,
    dynamic_forces: HashMap<String, Vec<[f32; 7]>>,
    static_baked: bool,
}

pub static Physics_world: Mutex<Option<PhysicsWorld>> = Mutex::new(None);
pub static Physics_initialized: Mutex<bool> = Mutex::new(false);

fn parse_physics_props(props: &HashMap<String, String>) -> (bool, bool, f32, f32, f32, Vec<[f32; 7]>) {
    let mut merged: HashMap<String, String> = props.clone();
    if let Some(sp) = props.get("special_properties") {
        for pair in sp.split(',') {
            let pair = pair.trim();
            if let Some(colon_pos) = pair.find(':') {
                let key = pair[..colon_pos].trim().to_string();
                let val = pair[colon_pos + 1..].trim().to_string();
                if !key.is_empty() {
                    merged.insert(key, val);
                }
            }
        }
    }

    let has_gravity: bool = merged.get("has_gravity")
        .and_then(|v| v.parse::<u8>().ok())
        .map(|v| v == 1)
        .unwrap_or(false);

    let impact: bool = merged.get("impact")
        .and_then(|v| v.parse::<u8>().ok())
        .map(|v| v == 1)
        .unwrap_or(false);

    let friction: f32 = merged.get("u")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0.5);

    let bounce: f32 = merged.get("bounce")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0.3);

    let mass: f32 = merged.get("mass")
        .and_then(|v| v.parse().ok())
        .unwrap_or(1.0);

    let mut forces: Vec<[f32; 7]> = Vec::new();
    let mut idx = 0;
    loop {
        let key = if idx == 0 { "force".to_string() } else { format!("force{}", idx) };
        if let Some(val) = merged.get(&key) {
            let parts: Vec<&str> = val.split(',').collect();
            if parts.len() == 7 {
                let f: Vec<f32> = parts.iter().filter_map(|p| p.trim().parse().ok()).collect();
                if f.len() == 7 {
                    forces.push([f[0], f[1], f[2], f[3], f[4], f[5], f[6]]);
                }
            }
            idx += 1;
        } else {
            break;
        }
    }

    (has_gravity, impact, friction, bounce, mass, forces)
}

fn vertices_to_points(vertices: &[f32]) -> Vec<Point<Real>> {
    vertices.chunks(3)
        .filter(|c| c.len() == 3)
        .map(|c| point![c[0], c[1], c[2]])
        .collect()
}

fn build_collider(vertices: &[f32], is_static: bool) -> Option<ColliderBuilder> {
    let points = vertices_to_points(vertices);
    if points.is_empty() {
        return None;
    }

    if is_static {
        let indices: Vec<[u32; 3]> = (0..points.len() as u32 / 3)
            .map(|i| [i * 3, i * 3 + 1, i * 3 + 2])
            .collect();
        Some(ColliderBuilder::trimesh(points, indices))
    } else {
        ColliderBuilder::convex_hull(&points)
            .or_else(|| Some(ColliderBuilder::ball(0.1)))
    }
}

fn create_static_bodies(world: &mut PhysicsWorld) {
    if world.static_baked {
        return;
    }
    world.static_baked = true;

    let static_scene = Static_scene.lock().unwrap();
    for (i, obj) in static_scene.iter().enumerate() {
        if obj.draw_type != "3d_object" {
            continue;
        }

        let (_, _impact, friction, bounce, _mass, _) = parse_physics_props(&obj.properties);
        
        let explicit_no_impact = obj.properties.get("special_properties")
            .map(|s| s.contains("impact:0"))
            .unwrap_or(false);
            
        if explicit_no_impact {
            continue;
        }

        let name = if obj.draw_special_name.is_empty() {
            format!("static_{}", i)
        } else {
            obj.draw_special_name.clone()
        };

        if world.static_handles.contains_key(&name) {
            continue;
        }

        let body = RigidBodyBuilder::fixed()
            .translation(vector![obj.draw_x, obj.draw_y, obj.draw_z])
            .build();
        let handle = world.bodies.insert(body);

        if let Some(collider_builder) = build_collider(&obj.draw_vertices, true) {
            let collider = collider_builder
                .friction(friction)
                .restitution(bounce)
                .build();
            world.colliders.insert_with_parent(collider, handle, &mut world.bodies);
        }

        world.static_handles.insert(name, handle);
    }
}

fn sync_dynamic_bodies(world: &mut PhysicsWorld) {
    let draw_queue = Draw_queue.lock().unwrap();
    for (i, obj) in draw_queue.iter().enumerate() {
        let (has_gravity, impact, friction, bounce, mass, forces) = parse_physics_props(&obj.properties);
        if !impact {
            continue;
        }

        let name = if obj.draw_special_name.is_empty() {
            format!("body_{}", i)
        } else {
            obj.draw_special_name.clone()
        };

        if world.dynamic_handles.contains_key(&name) {
            if !forces.is_empty() {
                world.dynamic_forces.insert(name, forces);
            }
            continue;
        }

        let pitch: f32 = obj.properties.get("pitch").and_then(|v| v.parse().ok()).unwrap_or(0.0);
        let yaw: f32 = obj.properties.get("yaw").and_then(|v| v.parse().ok()).unwrap_or(0.0);
        let roll: f32 = obj.properties.get("roll").and_then(|v| v.parse().ok()).unwrap_or(0.0);

        let rotation = UnitQuaternion::from_euler_angles(
            roll.to_radians(),
            pitch.to_radians(),
            yaw.to_radians(),
        );

        let gravity_scale = if has_gravity { 1.0 } else { 0.0 };

        let body = RigidBodyBuilder::dynamic()
            .translation(vector![obj.draw_x, obj.draw_y, obj.draw_z])
            .rotation(rotation.scaled_axis())
            .gravity_scale(gravity_scale)
            .ccd_enabled(true)
            .build();
        let handle = world.bodies.insert(body);

        if let Some(collider_builder) = build_collider(&obj.draw_vertices, false) {
            let collider = collider_builder
                .friction(friction)
                .restitution(bounce)
                .mass(mass)
                .build();
            world.colliders.insert_with_parent(collider, handle, &mut world.bodies);
        }

        if !forces.is_empty() {
            world.dynamic_forces.insert(name.clone(), forces);
        }

        world.dynamic_handles.insert(name, handle);
    }
}

fn apply_forces(world: &mut PhysicsWorld) {
    let forces_to_apply: Vec<(String, Vec<[f32; 7]>)> = world.dynamic_forces.drain().collect();

    for (name, forces) in forces_to_apply {
        if let Some(&handle) = world.dynamic_handles.get(&name) {
            let body = &mut world.bodies[handle];
            let body_pos = *body.translation();

            for force in forces {
                let point_local = vector![force[0], force[1], force[2]];
                let angle_x = force[3].to_radians();
                let angle_y = force[4].to_radians();
                let angle_z = force[5].to_radians();
                let strength = force[6];

                let rotation = Rotation::from_euler_angles(angle_x, angle_y, angle_z);
                let impulse = rotation * vector![strength, 0.0, 0.0];
                
                let world_point = point![
                    body_pos.x + point_local.x,
                    body_pos.y + point_local.y,
                    body_pos.z + point_local.z
                ];

                body.apply_impulse_at_point(impulse, world_point, true);
            }
        }
    }
}

fn read_positions_back(world: &PhysicsWorld) {
    let mut draw_queue = Draw_queue.lock().unwrap();
    for obj in draw_queue.iter_mut() {
        let name = if obj.draw_special_name.is_empty() {
            String::new()
        } else {
            obj.draw_special_name.clone()
        };

        if let Some(&handle) = world.dynamic_handles.get(&name) {
            let body = &world.bodies[handle];
            let pos = body.translation();
            obj.draw_x = pos.x;
            obj.draw_y = pos.y;
            obj.draw_z = pos.z;

            let (roll, pitch, yaw) = body.rotation().euler_angles();
            obj.properties.insert("pitch".to_string(), pitch.to_degrees().to_string());
            obj.properties.insert("yaw".to_string(), yaw.to_degrees().to_string());
            obj.properties.insert("roll".to_string(), roll.to_degrees().to_string());
        }
    }
}

pub fn init_physics() {
    let mut initialized = Physics_initialized.lock().unwrap();
    if *initialized {
        return;
    }

    let settings = Engine_settings.lock().unwrap();
    let dt = settings.tick_speed as f32 / 1000.0;
    drop(settings);

    let mut integration_parameters = IntegrationParameters::default();
    integration_parameters.dt = dt;

    let world = PhysicsWorld {
        gravity: vector![0.0, -9.8, 0.0],
        integration_parameters,
        physics_pipeline: PhysicsPipeline::new(),
        island_manager: IslandManager::new(),
        broad_phase: DefaultBroadPhase::new(),
        narrow_phase: NarrowPhase::new(),
        bodies: RigidBodySet::new(),
        colliders: ColliderSet::new(),
        impulse_joints: ImpulseJointSet::new(),
        multibody_joints: MultibodyJointSet::new(),
        ccd_solver: CCDSolver::new(),
        dynamic_handles: HashMap::new(),
        static_handles: HashMap::new(),
        dynamic_forces: HashMap::new(),
        static_baked: false,
    };

    *Physics_world.lock().unwrap() = Some(world);
    *initialized = true;
    println!("[Physics] Rapier physics engine initialized.");
}

pub fn physics_step() {
    {
        let initialized = Physics_initialized.lock().unwrap();
        if !*initialized {
            drop(initialized);
            init_physics();
        }
    }

    let mut world_lock = Physics_world.lock().unwrap();
    let world = match world_lock.as_mut() {
        Some(w) => w,
        None => return,
    };

    create_static_bodies(world);
    sync_dynamic_bodies(world);
    apply_forces(world);

    world.physics_pipeline.step(
        &world.gravity,
        &world.integration_parameters,
        &mut world.island_manager,
        &mut world.broad_phase,
        &mut world.narrow_phase,
        &mut world.bodies,
        &mut world.colliders,
        &mut world.impulse_joints,
        &mut world.multibody_joints,
        &mut world.ccd_solver,
        None,
        &(),
        &(),
    );

    read_positions_back(world);
}

pub fn set_gravity(g: f32) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        w.gravity = vector![0.0, -g, 0.0];
    }
}

pub fn set_time_step(dt: f32) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        w.integration_parameters.dt = dt;
    }
}

pub fn add_force_to_body(id: &str, force: [f32; 7]) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        w.dynamic_forces.entry(id.to_string()).or_default().push(force);
    }
}

pub fn clear_forces(id: &str) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        w.dynamic_forces.remove(id);
    }
}

pub fn set_body_velocity(id: &str, vx: f32, vy: f32, vz: f32) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        if let Some(&handle) = w.dynamic_handles.get(id) {
            w.bodies[handle].set_linvel(vector![vx, vy, vz], true);
        }
    }
}

pub fn set_body_angular_velocity(id: &str, wx: f32, wy: f32, wz: f32) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        if let Some(&handle) = w.dynamic_handles.get(id) {
            w.bodies[handle].set_angvel(vector![wx, wy, wz], true);
        }
    }
}

pub fn get_body_position(id: &str) -> Option<(f32, f32, f32)> {
    let w = Physics_world.lock().unwrap();
    if let Some(world) = w.as_ref() {
        let handle = world.dynamic_handles.get(id)
            .or_else(|| world.static_handles.get(id));
        if let Some(&h) = handle {
            let pos = world.bodies[h].translation();
            return Some((pos.x, pos.y, pos.z));
        }
    }
    None
}

pub fn get_body_rotation(id: &str) -> Option<(f32, f32, f32)> {
    let w = Physics_world.lock().unwrap();
    if let Some(world) = w.as_ref() {
        if let Some(&handle) = world.dynamic_handles.get(id) {
            let (roll, pitch, yaw) = world.bodies[handle].rotation().euler_angles();
            return Some((pitch.to_degrees(), yaw.to_degrees(), roll.to_degrees()));
        }
    }
    None
}

pub fn remove_body(id: &str) {
    if let Some(w) = Physics_world.lock().unwrap().as_mut() {
        if let Some(handle) = w.dynamic_handles.remove(id) {
            w.bodies.remove(handle, &mut w.island_manager, &mut w.colliders, &mut w.impulse_joints, &mut w.multibody_joints, true);
        }
        if let Some(handle) = w.static_handles.remove(id) {
            w.bodies.remove(handle, &mut w.island_manager, &mut w.colliders, &mut w.impulse_joints, &mut w.multibody_joints, true);
        }
        w.dynamic_forces.remove(id);
    }
}