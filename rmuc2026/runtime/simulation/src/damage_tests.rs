use super::*;
use rapier3d::na::{Quaternion, UnitQuaternion, Vector3};

fn rotation(r: &Robot) -> UnitQuaternion<f64> {
    let q = r.quaternion;
    UnitQuaternion::new_normalize(Quaternion::new(q[3], q[0], q[1], q[2]))
}

fn fixture(team: &str, slot: u32) -> (Engine, usize) {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let i = m
        .robots
        .iter()
        .position(|r| r.team == team && r.slot == slot)
        .unwrap();
    m.robots[i].x = 6.8;
    m.robots[i].y = 9.8;
    m.robots[i].z = height_at(&m.arena, 6.8, 9.8);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    // Flat, empty field after the engineer's initial defensive buff has expired.
    m.tick = 4000;
    m.update_rulebook_buffs();
    (m, i)
}

// Independent reference from the Blender mount and the front surface of ArmorHitFace.
fn model_face(
    r: &Robot,
    face: usize,
    horizontal: f64,
    vertical: f64,
) -> (Vector3<f64>, Vector3<f64>) {
    let designs = config().robot_designs;
    let d = &designs[&r.slot.to_string()];
    let a = face as f64 * std::f64::consts::FRAC_PI_2;
    let tilt = 15.0_f64.to_radians();
    let normal = Vector3::new(a.cos() * tilt.cos(), a.sin() * tilt.cos(), tilt.sin());
    let extra = d["armor_mount_offset_m"]
        .as_f64()
        .unwrap_or(if r.slot == 3 { 0.03 } else { 0.0 });
    let skin = d["armor_hit_face_offset_m"].as_f64().unwrap_or(0.011);
    let distance = r.chassis_size_m[face % 2] / 2.0 - d["armor_inset_m"].as_f64().unwrap() + extra;
    let point = Vector3::new(a.cos() * distance, a.sin() * distance, r.armor_center_z_m)
        + normal * skin
        + Vector3::new(-a.sin(), a.cos(), 0.0) * horizontal
        + Vector3::new(-a.cos() * tilt.sin(), -a.sin() * tilt.sin(), tilt.cos()) * vertical;
    (
        Vector3::new(r.x, r.y, r.z) + rotation(r) * point,
        rotation(r) * normal,
    )
}

fn queue_shot(
    m: &mut Engine,
    victim: usize,
    point: Vector3<f64>,
    normal: Vector3<f64>,
    heavy: bool,
) {
    let team = if m.robots[victim].team == "red" {
        "blue"
    } else {
        "red"
    };
    let slot = if heavy { 1 } else { 4 };
    let source = m
        .robots
        .iter()
        .position(|r| r.team == team && r.slot == slot)
        .unwrap();
    m.robots[source].last_shot_time = m.time();
    let origin = point + normal * 0.12;
    let velocity = -normal * if heavy { 12.0 } else { 25.0 };
    m.projectiles.push(Projectile {
        id: 999,
        source: m.robots[source].id.clone(),
        team: team.into(),
        role: if heavy {
            "hero".into()
        } else {
            "infantry".into()
        },
        x: origin.x,
        y: origin.y,
        z: origin.z,
        vx: velocity.x,
        vy: velocity.y,
        vz: velocity.z,
        remaining: 1.0,
        damage: if heavy { 200.0 } else { 20.0 },
        radius: if heavy { 0.02125 } else { 0.0084 },
        deployed: false,
    });
}

fn shoot(m: &mut Engine, victim: usize, point: Vector3<f64>, normal: Vector3<f64>, heavy: bool) {
    queue_shot(m, victim, point, normal, heavy);
    m.step(&json!({}));
}

#[test]
fn every_visible_ground_armor_face_takes_17mm_and_42mm_damage() {
    let mut failures = Vec::new();
    for team in ["red", "blue"] {
        for slot in [1, 2, 3, 4, 7] {
            let (mut m, i) = fixture(team, slot);
            for heavy in [false, true] {
                for face in 0..4 {
                    for (horizontal, vertical) in [(0.0, 0.0), (0.0, 0.045), (0.045, -0.045)] {
                        let max_hp = m.robots[i].max_hp;
                        m.robots[i].alive = true;
                        m.robots[i].hp = max_hp;
                        m.robots[i].weak = false;
                        m.robots[i].invulnerable_until = 0.0;
                        m.robots[i].armor_hit_times.fill(-1000.0);
                        m.update_rulebook_buffs();
                        let (point, normal) = model_face(&m.robots[i], face, horizontal, vertical);
                        shoot(&mut m, i, point, normal, heavy);
                        let expected = max_hp - if heavy { 200.0_f64.min(max_hp) } else { 20.0 };
                        if (m.robots[i].hp - expected).abs() > 1e-6 {
                            failures.push(format!("{team}-{slot} {}mm face={face} h={horizontal} v={vertical}: hp={} expected={expected}",if heavy {42} else {17},m.robots[i].hp));
                        }
                        m.projectiles.clear();
                    }
                }
            }
        }
    }
    assert!(
        failures.is_empty(),
        "{} armor failures:\n{}",
        failures.len(),
        failures.join("\n")
    );
}

#[test]
fn natural_and_paid_respawns_are_protected_during_the_first_physics_tick() {
    for paid in [false, true] {
        for slot in [1, 2, 3, 4, 7] {
            let (mut m, i) = fixture("blue", slot);
            m.robots[i].alive = false;
            m.robots[i].hp = 0.0;
            m.robots[i].respawn_required = 100.0;
            m.step(&json!({}));
            if !paid {
                m.robots[i].respawn_progress = 100.0 - DT;
            }
            m.teams.get_mut("blue").unwrap().resources = 10000.0;
            // Purchase RPCs run at 10 Hz.
            if paid && m.tick % 2 != 0 {
                m.tick += 1;
            }
            let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
            queue_shot(&mut m, i, point, normal, false);
            let commands = if paid {
                json!({"blue":{slot.to_string():{"revive":true}}})
            } else {
                json!({})
            };
            m.step(&commands);
            let expected = if paid {
                m.robots[i].max_hp
            } else {
                (m.robots[i].max_hp * 0.1).round()
            };
            assert_eq!(
                m.robots[i].hp, expected,
                "slot {slot}, paid={paid}: a respawn-frame shot must not deal damage"
            );
            assert!(m.robots[i].alive && m.robots[i].invulnerable);
        }
    }
}

#[test]
fn expired_respawn_protection_does_not_block_another_tick_of_damage() {
    let (mut m, i) = fixture("blue", 4);
    m.robots[i].invulnerable = true;
    m.robots[i].invulnerable_until = m.time();
    let hp = m.robots[i].hp;
    let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(m.robots[i].hp, hp - 20.0);
    assert!(!m.robots[i].invulnerable);
}

#[test]
fn hit_speed_uses_the_moving_armor_point_on_a_spinning_robot() {
    for (horizontal, speed, damage) in [(0.06, 12.4, 0.0), (-0.06, 11.6, 20.0)] {
        let (mut m, i) = fixture("blue", 4);
        let position = Vector3::new(m.robots[i].x, m.robots[i].y, m.robots[i].z);
        let body = m
            .physics
            .as_mut()
            .unwrap()
            .bodies
            .iter_mut()
            .find(|(_, b)| (b.translation() - position).norm() < 1e-6)
            .unwrap()
            .1;
        body.set_angvel(Vector3::new(0.0, 0.0, 12.0), true);
        m.robots[i].angular_velocity = [0.0, 0.0, 12.0];
        let hp = m.robots[i].hp;
        let (point, normal) = model_face(&m.robots[i], 0, horizontal, 0.0);
        queue_shot(&mut m, i, point, normal, false);
        let mut p = m.projectiles.pop().unwrap();
        p.vx = -normal.x * speed;
        p.vy = -normal.y * speed;
        p.vz = -normal.z * speed;
        m.rulebook_robot_hit(i, &p, 0.0, point.x, point.y, point.z);
        assert_eq!(
            m.robots[i].hp,
            hp - damage,
            "rotating armor, horizontal={horizontal}, bullet speed={speed}"
        );
    }
}

#[test]
fn wheel_legged_armor_takes_damage_at_every_leg_height() {
    let (mut m, i) = fixture("blue", 3);
    for length in [0.12, 0.20, 0.34] {
        for _ in 0..40 {
            m.step(&json!({"blue":{"3":{"leg_height":length}}}));
        }
        let hp = m.robots[i].hp;
        let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
        queue_shot(&mut m, i, point, normal, false);
        m.step(&json!({"blue":{"3":{"leg_height":length}}}));
        assert_eq!(m.robots[i].hp, hp - 20.0, "leg height {length}");
    }
}

#[test]
fn body_hits_and_friendly_fire_do_not_deduct_hp() {
    for slot in [1, 2, 3, 4, 7] {
        let (mut m, i) = fixture("blue", slot);
        let r = &m.robots[i];
        let body_z = if slot == 3 {
            0.30
        } else {
            0.065 + r.chassis_size_m[2] / 2.0
        };
        let normal = rotation(r) * Vector3::new(1.0, 1.0, 0.0).normalize();
        let center = Vector3::new(r.x, r.y, r.z) + rotation(r) * Vector3::new(0.0, 0.0, body_z);
        let hp = r.hp;
        queue_shot(&mut m, i, center, normal, false);
        let p = m.projectiles.last_mut().unwrap();
        let origin = center + normal * 0.6;
        p.x = origin.x;
        p.y = origin.y;
        p.z = origin.z;
        m.step(&json!({}));
        assert_eq!(m.robots[i].hp, hp, "body shot at slot {slot}");
        assert!(m.projectiles.is_empty());
    }
    let (mut m, i) = fixture("blue", 3);
    let hp = m.robots[i].hp;
    let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
    queue_shot(&mut m, i, point, normal, false);
    let p = m.projectiles.last_mut().unwrap();
    p.team = "blue".into();
    p.source = "blue-4".into();
    m.step(&json!({}));
    assert_eq!(m.robots[i].hp, hp);
    assert!(m.projectiles.is_empty());
}

#[test]
fn a_wall_in_front_of_the_armor_blocks_damage() {
    let (mut m, i) = fixture("blue", 3);
    let (point, normal) = model_face(&m.robots[i], 2, 0.0, 0.0);
    let mut wall = m.arena.obstacles[0].clone();
    wall.x = point.x + 0.03;
    wall.y = point.y - 0.2;
    wall.w = 0.025;
    wall.h = 0.4;
    wall.bottom = 0.0;
    wall.height = 2.0;
    wall.rotation = 0.0;
    m.arena.obstacles.push(wall);
    m.physics = Some(physics::PhysicsWorld::new(
        &m.arena,
        &m.robots,
        &config().robot_designs,
    ));
    let hp = m.robots[i].hp;
    shoot(&mut m, i, point, normal, false);
    assert_eq!(m.robots[i].hp, hp);
    assert!(m.projectiles.is_empty());
}

#[test]
fn defense_and_attack_multipliers_are_applied_once_and_use_the_strongest_buff() {
    for (tech, crossing, attack, expected) in [
        (0.0, 0.0, 1.0, 20.0),
        (0.25, 0.0, 1.0, 15.0),
        (0.25, 0.5, 1.5, 15.0),
    ] {
        let (mut m, i) = fixture("blue", 4);
        m.teams.get_mut("blue").unwrap().tech_defense = tech;
        m.robots[i].crossing_defense = crossing;
        m.robots[i].crossing_defense_until = m.time() + 10.0;
        m.update_rulebook_buffs();
        let hp = m.robots[i].hp;
        let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
        queue_shot(&mut m, i, point, normal, false);
        m.projectiles.last_mut().unwrap().damage *= attack;
        m.step(&json!({}));
        assert_eq!(m.robots[i].hp, hp - expected);
    }
    let (mut m, i) = fixture("blue", 2);
    m.tick = 1000;
    m.update_rulebook_buffs();
    let hp = m.robots[i].hp;
    let (point, normal) = model_face(&m.robots[i], 2, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(
        m.robots[i].hp,
        hp - 10.0,
        "engineer receives 50% defense during the first 180 s"
    );
}

#[test]
fn a_plate_cannot_be_double_counted_and_a_kill_counts_only_remaining_hp() {
    let (mut m, i) = fixture("blue", 4);
    for _ in 0..2 {
        let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
        queue_shot(&mut m, i, point, normal, false);
    }
    m.step(&json!({}));
    assert_eq!(m.robots[i].hp, 130.0);
    let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(
        m.robots[i].hp, 110.0,
        "50ms later the plate accepts the next hit"
    );
    m.robots[i].hp = 7.0;
    let damage = m.teams["red"].attack_damage;
    let (point, normal) = model_face(&m.robots[i], 0, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(m.robots[i].hp, 0.0);
    assert!(!m.robots[i].alive);
    assert_eq!(m.teams["red"].attack_damage - damage, 7.0);
    assert_eq!(m.teams["red"].kills, 1);
}

#[test]
fn resource_protection_and_assembly_budget_are_only_for_the_engineer() {
    let (mut m, i) = fixture("blue", 2);
    let zone = m
        .arena
        .zones
        .iter()
        .find(|z| z.id == "blue_resource")
        .unwrap()
        .clone();
    m.robots[i].x = zone.x;
    m.robots[i].y = zone.y;
    m.robots[i].z = height_at(&m.arena, zone.x, zone.y);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    assert!(m.robots[i].invulnerable);
    let hp = m.robots[i].hp;
    let (point, normal) = model_face(&m.robots[i], 2, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(m.robots[i].hp, hp);
    let zone = m
        .arena
        .zones
        .iter()
        .find(|z| z.id == "blue_assembly")
        .unwrap()
        .clone();
    m.robots[i].x = zone.x;
    m.robots[i].y = zone.y;
    m.robots[i].z = height_at(&m.arena, zone.x, zone.y);
    for _ in 0..50 {
        m.step(&json!({}));
    } // Let the resource contact's 2s retention expire.
    assert!(m.robots[i].invulnerable);
    m.robots[i].assembly_invulnerable_spent = 180.0 - DT / 2.0;
    m.step(&json!({}));
    assert!(
        !m.robots[i].invulnerable,
        "assembly protection ends at the 180s budget"
    );
    let hp = m.robots[i].hp;
    let (point, normal) = model_face(&m.robots[i], 2, 0.0, 0.0);
    shoot(&mut m, i, point, normal, false);
    assert_eq!(m.robots[i].hp, hp - 20.0);
}

fn hit_structure(m: &mut Engine, outpost: bool, heavy: bool) {
    let b = if outpost {
        &m.outposts["blue"]
    } else {
        &m.bases["blue"]
    };
    let plate = structures::plates(b,outpost).into_iter().find(|p| p.face==if outpost || b.armor_open {0} else {3}).unwrap();
    let normal = Vector3::from(plate.normal);
    let point = Vector3::from(plate.center) + Vector3::new(0.0,0.03,0.0);
    let origin = point + normal * 0.12;
    let velocity = -normal * if heavy { 12.0 } else { 25.0 };
    let source = if heavy { 0 } else { 3 };
    m.robots[source].last_shot_time = m.time();
    m.projectiles.push(Projectile {
        id: 900,
        source: m.robots[source].id.clone(),
        team: "red".into(),
        role: if heavy {
            "hero".into()
        } else {
            "infantry".into()
        },
        x: origin.x,
        y: origin.y,
        z: origin.z,
        vx: velocity.x,
        vy: velocity.y,
        vz: velocity.z,
        remaining: 1.0,
        damage: if heavy { 200.0 } else { 20.0 },
        radius: if heavy { 0.02125 } else { 0.0084 },
        deployed: false,
    });
    m.step(&json!({}));
}

#[test]
fn structure_protection_and_virtual_shields_handle_both_calibres() {
    for heavy in [false, true] {
        let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
        m.tick = 4000;
        let o = m.outposts.get_mut("blue").unwrap();
        o.rotation_stopped = true;
        o.rotation_stop_at = 100.0;
        o.rotation_stop_angle = 0.0;
        o.rotation = 0.0;
        hit_structure(&mut m, true, heavy);
        assert_eq!(
            m.outposts["blue"].hp,
            1500.0 - if heavy { 200.0 } else { 20.0 }
        );
        hit_structure(&mut m, false, heavy);
        assert_eq!(
            m.bases["blue"].hp, 5000.0,
            "a live outpost protects its base"
        );
        m.outposts.get_mut("blue").unwrap().hp = 0.0;
        hit_structure(&mut m, false, heavy);
        assert_eq!(
            m.bases["blue"].hp,
            if heavy { 4800.0 } else { 5000.0 },
            "closed base armor"
        );
    }
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    m.tick = 4000;
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    let b = m.bases.get_mut("blue").unwrap();
    b.armor_open = true;
    b.armor_open_progress = 1.0;
    b.shield_hp = 200.0;
    hit_structure(&mut m, false, false);
    assert_eq!(m.bases["blue"].hp, 5000.0);
    assert_eq!(m.bases["blue"].shield_hp, 180.0);
    for _ in 0..3 {
        m.step(&json!({}));
    }
    hit_structure(&mut m, false, true);
    assert_eq!(m.bases["blue"].shield_hp, 0.0);
    assert_eq!(m.bases["blue"].hp, 4980.0);
}
