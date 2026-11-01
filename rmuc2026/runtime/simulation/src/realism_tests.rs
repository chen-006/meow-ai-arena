use super::*;
use rapier3d::na::Vector3;

fn engineer_at_station(m: &mut Engine, team: &str) -> usize {
    let i = m
        .robots
        .iter()
        .position(|r| r.team == team && r.role == "engineer")
        .unwrap();
    let z = m
        .arena
        .zones
        .iter()
        .find(|z| z.id == format!("{team}_resource"))
        .unwrap();
    let yaw = if team == "red" {
        std::f64::consts::FRAC_PI_2
    } else {
        -std::f64::consts::FRAC_PI_2
    };
    let r = &mut m.robots[i];
    r.x = z.x;
    r.y = z.y;
    r.z = height_at(&m.arena, z.x, z.y);
    r.heading = yaw;
    r.quaternion = [0.0, 0.0, (yaw / 2.0).sin(), (yaw / 2.0).cos()];
    i
}

#[test]
fn pickup_needs_real_contact_and_all_four_phases_on_both_teams() {
    for team in ["red", "blue"] {
        let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
        let i = engineer_at_station(&mut m, team);
        let yaw = m.robots[i].heading;
        let command = json!({team:{"2":{"pick_unit":true,"heading":yaw}}});
        let mut stages = std::collections::BTreeSet::new();
        let mut pickups = 0;
        // Two grabs at half-rate arm speed: the old 240-tick window bought both units at the old
        // rate, so the window doubles with the arm.
        for _ in 0..600 {
            m.step(&command);
            if let Some(s) = &m.robots[i].unit_pickup {
                stages.insert(s.stage.clone());
            }
            pickups += m
                .events
                .iter()
                .filter(|e| e["type"] == "unit_pickup")
                .count();
            if m.robots[i].carried_units == 2 {
                break;
            }
        }
        assert_eq!(
            m.robots[i].carried_units,
            2,
            "{team}: tip {:?}, pickup {}",
            m.robots[i].arm_tip,
            json!(m.robots[i].unit_pickup)
        );
        assert_eq!(pickups, 2);
        assert_eq!(stages.len(), 4);
        assert!(m.time() >= 3.0, "inventory must not fill in two RPCs");
        for _ in 0..10 {
            m.step(&command);
        }
        assert_eq!(m.robots[i].carried_units, 2);
        assert!(m.robots[i].unit_pickup.is_none());
    }
}

#[test]
fn pickup_outside_arm_reach_cancellation_and_weakness_never_grant_stock() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let i = engineer_at_station(&mut m, "red");
    m.robots[i].y -= 0.5;
    let command = json!({"red":{"2":{"pick_unit":true,"heading":std::f64::consts::FRAC_PI_2}}});
    for _ in 0..100 {
        m.step(&command);
    }
    assert_eq!(m.robots[i].carried_units, 0);
    m.step(&json!({}));
    assert!(m.robots[i].unit_pickup.is_none());
    engineer_at_station(&mut m, "red");
    for _ in 0..50 {
        m.step(&command);
        if m.robots[i].unit_pickup.as_ref().is_some_and(|s| s.held) {
            break;
        }
    }
    assert!(m.robots[i].unit_pickup.as_ref().is_some_and(|s| s.held));
    m.robots[i].alive = false;
    m.robots[i].hp = 0.0;
    m.step(&command);
    assert_eq!(m.robots[i].carried_units, 0);
    assert!(m.robots[i].unit_pickup.is_none());
    m.robots[i].alive = true;
    m.robots[i].hp = 100.0;
    m.robots[i].weak = true;
    m.step(&command);
    assert!(m.robots[i].unit_pickup.is_none());
}

#[test]
fn finite_structure_panels_have_no_cylinder_damage_and_open_lower_targets_remain_reachable() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    let b = m.bases.get_mut("blue").unwrap();
    b.armor_open = true;
    b.armor_open_progress = 1.0;
    let plate = structures::plates(b, false)
        .into_iter()
        .find(|p| p.face == 2)
        .unwrap();
    let normal = Vector3::from(plate.normal);
    let tangent = Vector3::new(-normal.y, normal.x, 0.0).normalize();
    let center = Vector3::from(plate.center) + tangent * 0.03;
    for heavy in [false, true] {
        for _ in 0..4 {
            m.step(&json!({}));
        }
        let origin = center + normal * 0.12;
        let speed = if heavy { 12.0 } else { 25.0 };
        let v = -normal * speed;
        m.robots[0].last_shot_time = m.time();
        let before = m.bases["blue"].hp;
        m.projectiles.push(Projectile {
            id: 99,
            source: if heavy { "red-1" } else { "red-4" }.into(),
            team: "red".into(),
            role: if heavy { "hero" } else { "infantry" }.into(),
            x: origin.x,
            y: origin.y,
            z: origin.z,
            vx: v.x,
            vy: v.y,
            vz: v.z,
            remaining: 1.0,
            damage: if heavy { 200.0 } else { 20.0 },
            radius: if heavy { 0.02125 } else { 0.0084 },
            deployed: false,
        });
        m.step(&json!({}));
        assert_eq!(
            m.bases["blue"].hp,
            before - if heavy { 200.0 } else { 20.0 }
        );
    }
    let origin = center + tangent * 0.2 + normal * 0.12;
    assert!(structures::intersection(
        &plate,
        [origin.x, origin.y, origin.z],
        [-normal.x * 0.3, -normal.y * 0.3, -normal.z * 0.3]
    )
    .is_none());
    let point = center - normal * 0.01;
    let mut world = m.physics.take().unwrap();
    world.update_base_covers(&m.bases);
    let start = center + normal * 1.5;
    let delta = point - start;
    assert!(
        world
            .solid_hit(0, [start.x, start.y, start.z], [delta.x, delta.y, delta.z])
            .is_none(),
        "opened doors cannot hide the lower panel"
    );
    m.bases.get_mut("blue").unwrap().armor_open_progress = 0.0;
    world.update_base_covers(&m.bases);
    assert!(
        world
            .solid_hit(0, [start.x, start.y, start.z], [delta.x, delta.y, delta.z])
            .is_some(),
        "closed door protects lower panel"
    );
}

#[test]
fn armor_geometry_is_public_and_top_modules_only_register_heavy_ammunition() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    assert_eq!(
        m.snapshot()["bases"]["blue"]["armor_plates"]
            .as_array()
            .unwrap()
            .len(),
        3
    );
    assert_eq!(
        m.observation("red", None)["outposts"]["blue"]["armor_plates"]
            .as_array()
            .unwrap()
            .len(),
        6
    );
    assert!(structures::plates(&m.outposts["blue"], true)
        .iter()
        .filter(|p| p.face >= 3)
        .all(|p| p.heavy_only && !p.weakspot));
}

#[test]
fn temporary_heat_visibility_stays_reduced_until_zero_without_hiding_public_health() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let normal = m.effective_vision(&m.robots[3]);
    m.robots[3].overheated = true;
    m.robots[3].heat = 1.0;
    assert!(m.effective_vision(&m.robots[3]) < normal / 2.0);
    assert_eq!(
        m.observation("red", Some(&[4]))["vision"]["4"]["fpv_dimmed"],
        true
    );
    assert_eq!(
        m.observation("red", Some(&[4]))["referee"]["robots"]
            .as_array()
            .unwrap()
            .len(),
        12
    );
    m.robots[3].heat = 0.0;
    m.robots[3].heat_permanent_lock = true;
    assert_eq!(m.effective_vision(&m.robots[3]), normal);
}

#[test]
fn central_cover_stops_low_shots_but_preserves_the_dimensioned_access_ramps() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let wall = m
        .arena
        .obstacles
        .iter()
        .find(|w| w.id == "central_kerb_5")
        .unwrap();
    let z = wall.bottom + 0.075;
    let world = m.physics.as_ref().unwrap();
    assert!(world
        .solid_hit(0, [9.8, 10.5, z], [0.8, 0.0, 0.0])
        .is_some());
    assert!(world.solid_hit(0, [9.8, 7.5, z], [0.8, 0.0, 0.0]).is_none());
}

#[test]
fn multiple_engineers_cannot_claim_the_same_dispenser_unit() {
    let lineups=Some([("red".into(),vec![Deployment{slot:2,model:2},Deployment{slot:8,model:2}]),
        ("blue".into(),vec![Deployment{slot:2,model:2}])].into());
    let mut m=Engine::new_with_lineups(42,420.0,"rmuc2026",lineups).unwrap();
    for i in [0,1] {let z=m.arena.zones.iter().find(|z|z.id=="red_resource").unwrap();m.robots[i].x=z.x;m.robots[i].y=z.y;}
    m.request_pickup(0,&json!({"pick_unit":true}));
    m.request_pickup(1,&json!({"pick_unit":true}));
    assert!(m.robots[0].unit_pickup.is_some());assert!(m.robots[1].unit_pickup.is_none());
    m.request_pickup(0,&json!({}));
    m.request_pickup(1,&json!({"pick_unit":true}));assert!(m.robots[1].unit_pickup.is_some());
}

/// Visual fixture; synthetic starting poses are deliberately outside ranked play.
#[test]
#[ignore]
fn realism_qa_replay() {
    let path = std::env::var("QA_REPLAY").unwrap();
    let mut m = Engine::new(42, 12.0, "rmuc2026").unwrap();
    engineer_at_station(&mut m, "red");
    engineer_at_station(&mut m, "blue");
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    let b = m.bases["blue"].clone();
    let r = &mut m.robots[3];
    r.x = b.x - 1.8;
    r.y = b.y - 0.75;
    r.z = height_at(&m.arena, r.x, r.y);
    r.ammo = 200;
    let mut frames = vec![m.snapshot()];
    for _ in 0..240 {
        if m.time() >= 3.0 {
            m.bases.get_mut("blue").unwrap().armor_open = true;
        }
        let r = &m.robots[3];
        let mut target = m.bases["blue"].clone();
        target.armor_open = true;
        let plate = structures::plates(&target, false)
            .into_iter()
            .find(|p| p.face == 2)
            .unwrap();
        let distance = dist(r.x, r.y, plate.center[0], plate.center[1]);
        let aim = (plate.center[1] - r.y).atan2(plate.center[0] - r.x);
        let pitch = (plate.center[2] - r.z - r.muzzle_height_m
            + 0.5 * 9.81 * (distance / 25.0).powi(2))
        .atan2((distance - r.muzzle_reach_m).max(0.1));
        m.robots[3].heat = 0.0;
        m.robots[3].overheated = false;
        frames.push(m.step(&json!({"red":{"2":{"pick_unit":true,"heading":std::f64::consts::FRAC_PI_2},"4":{"aim":aim,"pitch":pitch,"fire":m.time()>5.0}},"blue":{"2":{"pick_unit":true,"heading":-std::f64::consts::FRAC_PI_2}}})));
    }
    assert_eq!(m.robots[1].carried_units, 2);
    assert!(m.bases["blue"].hp < 5000.0);
    let mut fixed = serde_json::Map::new();
    for key in ["arena", "rulebook_rules", "simulation_assumptions"] {
        fixed.insert(key.into(), frames[0][key].clone());
    }
    for f in &mut frames {
        for key in fixed.keys() {
            f.as_object_mut().unwrap().remove(key);
        }
    }
    let replay = json!({"format":"rmuc-replay-v4","metadata":{"seed":42,"duration":12,"map":"rmuc2026","dt":DT,"test_match":true,"teams":{"red":{"name":"realism-qa","version":"video-audit","mode":"team"},"blue":{"name":"realism-qa","version":"video-audit","mode":"team"}}},"static":fixed,"frames":frames,"actions":[],"diagnostics":[],"result":{"winner":null,"reason":"timeout","time":12.0,"digest":"","policy_errors":0}});
    std::fs::write(path, serde_json::to_vec(&replay).unwrap()).unwrap();
}

#[test]
fn both_bases_face_the_field_so_blue_is_turned_half_a_turn() {
    // The armour modules sit on the three faces of the pedestal, and those faces have to look into
    // the field on both halves. Red already does; blue sits on the far side, so its whole structure
    // (plates and swinging shells alike) is turned 180 deg about its own axis.
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let (mid_x, mid_y) = (m.arena.width / 2.0, m.arena.height / 2.0);
    for team in ["red", "blue"] {
        let b = &m.bases[team];
        let inward = if team == "red" { 1.0 } else { -1.0 };
        let plates = structures::plates(b, false);
        // Face 0 (the first 42 mm module) looks straight down the centre line.
        assert!((plates[0].normal[0] - inward).abs() < 1e-9, "{team}: {:?}", plates[0].normal);
        assert!(plates[0].normal[1].abs() < 1e-9, "{team}: {:?}", plates[0].normal);
        // The other two faces lean away from the centre line by 60 deg each.
        for plate in &plates[1..] {
            assert!((plate.normal[0] + inward / 2.0).abs() < 1e-9, "{team}: {:?}", plate.normal);
            assert!((plate.normal[1].abs() - 0.866_025_403_784_438_6).abs() < 1e-9, "{team}: {:?}", plate.normal);
        }
    }
    // Blue is red turned half a turn about the field centre: every plate and every shell maps onto
    // its counterpart, which is exactly what "the faces were reversed" used to fail.
    let (red, blue) = (structures::plates(&m.bases["red"], false), structures::plates(&m.bases["blue"], false));
    for (a, b) in red.iter().zip(&blue) {
        assert!((b.center[0] - (2.0 * mid_x - a.center[0])).abs() < 1e-9, "plate {:?}", a.center);
        assert!((b.center[1] - (2.0 * mid_y - a.center[1])).abs() < 1e-9, "plate {:?}", a.center);
        assert!((b.normal[0] + a.normal[0]).abs() < 1e-9, "plate normal {:?}", a.normal);
        assert!((b.normal[1] + a.normal[1]).abs() < 1e-9, "plate normal {:?}", a.normal);
    }
    let (red_shells, blue_shells) = (structures::covers(&m.bases["red"]), structures::covers(&m.bases["blue"]));
    assert_eq!(blue_shells.len(), red_shells.len());
    for (a, b) in red_shells.iter().zip(&blue_shells) {
        let (ax, ay) = (a.x + a.w / 2.0, a.y + a.h / 2.0);
        let (bx, by) = (b.x + b.w / 2.0, b.y + b.h / 2.0);
        assert!((bx - (2.0 * mid_x - ax)).abs() < 1e-9, "shell {} {:?}", a.id, (ax, ay));
        assert!((by - (2.0 * mid_y - ay)).abs() < 1e-9, "shell {} {:?}", a.id, (ax, ay));
        let turn = (b.rotation - a.rotation - std::f64::consts::PI).rem_euclid(std::f64::consts::TAU);
        assert!(turn.min(std::f64::consts::TAU - turn) < 1e-9, "shell {} turn {turn}", a.id);
    }
}
