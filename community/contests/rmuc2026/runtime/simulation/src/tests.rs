use super::*;

#[test]
fn referee_health_is_public_without_enemy_coordinates_or_private_economy() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    m.event(
        "destroy",
        json!({"robot":"blue-3","killer":"red-4","x":18.0,"y":5.0}),
    );
    m.event(
        "ammo_purchase",
        json!({"robot":"blue-3","team":"blue","amount":50}),
    );
    m.event("income", json!({"team":"red","amount":400}));
    m.step(&json!({}));
    let obs = m.observation("red", Some(&[1]));
    assert!(obs["enemies"].as_array().unwrap().is_empty());
    let referee = &obs["referee"];
    assert_eq!(referee["robots"].as_array().unwrap().len(), 12);
    for r in referee["robots"].as_array().unwrap() {
        assert!(r.get("x").is_none() && r.get("vx").is_none() && r.get("ammo").is_none());
    }
    let events = referee["events"].as_array().unwrap();
    assert!(events.iter().any(|e| e["type"] == "destroy"));
    assert!(!events.iter().any(|e| e["type"] == "ammo_purchase"));
    assert!(events
        .iter()
        .all(|e| e.get("x").is_none() && e.get("y").is_none()));
    assert!(referee["economy"].get("resources").is_some());
    assert!(referee["economy"].get("blue").is_none());
}

#[test]
fn enemy_projectiles_behind_a_base_are_not_visible() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    m.projectiles.push(Projectile {
        id: 999,
        source: "blue-3".into(),
        team: "blue".into(),
        role: "infantry".into(),
        x: 1.3,
        y: 7.5,
        z: 0.315,
        vx: 20.0,
        vy: 0.0,
        vz: 0.0,
        remaining: 40.0,
        damage: 20.0,
        radius: 0.0084,
        deployed: false,
    });
    assert!(m.observation("red", Some(&[4]))["projectiles"]
        .as_array()
        .unwrap()
        .is_empty());
}

#[test]
fn source_defaults_roster_and_structures() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    assert_eq!(m.robots.len(), 12);
    assert_eq!(m.arena.width, 28.0);
    assert_eq!(m.arena.height, 15.0);
    assert_eq!(
        m.robots
            .iter()
            .filter(|r| r.team == "red")
            .map(|r| r.slot)
            .collect::<Vec<_>>(),
        vec![1, 2, 3, 4, 6, 7]
    );
    assert_eq!(
        (
            m.robots[0].hp,
            m.robots[0].power_limit,
            m.robots[0].max_heat,
            m.robots[0].cooling_rate
        ),
        (150.0, 50.0, 100.0, 20.0)
    );
    assert_eq!((m.robots[1].hp, m.robots[1].power_limit), (250.0, 120.0));
    for slot in [3, 4, 7] {
        let r = m
            .robots
            .iter()
            .find(|r| r.team == "red" && r.slot == slot)
            .unwrap();
        assert_eq!(r.role, "infantry");
        let expected = match slot {
            3 => (150.0, 60.0, 40.0, 12.0, 0),
            4 => (150.0, 60.0, 170.0, 5.0, 0),
            _ => (200.0, 45.0, 40.0, 12.0, 0),
        };
        assert_eq!(
            (r.hp, r.power_limit, r.max_heat, r.cooling_rate, r.ammo),
            expected
        );
    }
    assert_eq!(
        m.robots
            .iter()
            .find(|r| r.team == "red" && r.slot == 6)
            .unwrap()
            .ammo,
        750
    );
    assert_eq!(m.bases["red"].hp, 5000.0);
    assert_eq!(m.outposts["red"].hp, 1500.0);
    assert_eq!(m.teams["red"].level_cap, 5);
    assert_eq!(m.teams["red"].resources, 0.0);
}
#[test]
fn full_performance_tables_and_profile_lock() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    m.step(&json!({"red":{"1":{"profile":"melee"},"3":{"profile":{"chassis":"power","weapon":"burst"}}}}));
    assert_eq!(
        (
            m.robots[0].hp,
            m.robots[0].power_limit,
            m.robots[0].max_heat,
            m.robots[0].cooling_rate
        ),
        (200.0, 70.0, 140.0, 12.0)
    );
    let i = m
        .robots
        .iter()
        .position(|r| r.team == "red" && r.slot == 3)
        .unwrap();
    assert_eq!((m.robots[i].hp, m.robots[i].max_heat), (150.0, 170.0));
    m.step(&json!({"red":{"1":{"profile":"ranged"}}}));
    assert_eq!(m.robots[0].profile, "melee");
    m.robots[0].level = 10;
    m.apply_performance(0, true);
    assert_eq!(
        (
            m.robots[0].max_hp,
            m.robots[0].power_limit,
            m.robots[0].max_heat,
            m.robots[0].cooling_rate
        ),
        (450.0, 120.0, 240.0, 30.0)
    );
    m.robots[i].level = 9;
    m.robots[i].weapon_profile = "cooling".into();
    m.apply_performance(i, true);
    assert_eq!(m.robots[i].max_heat, 114.0);
}
#[test]
fn same_trace_is_deterministic() {
    let mut a = Engine::new(42, 2.0, "rmuc2026").unwrap();
    let mut b = Engine::new(42, 2.0, "rmuc2026").unwrap();
    for _ in 0..40 {
        let action = json!({"red":{"3":{"move":[1,0],"fire":true}},"blue":{"3":{"move":[-1,0],"fire":true}}});
        assert_eq!(a.step(&action), b.step(&action));
    }
    assert_eq!(a.digest(), b.digest());
}
#[test]
fn protected_spawn_geometry_and_sparse_observations() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    for r in &m.robots {
        if r.role != "aerial" {
            assert!(!m.blocked_for(r, r.x, r.y), "bad spawn {}", r.id);
        }
    }
    let obs = m.observation("red", Some(&[7]));
    assert_eq!(obs["controlled_ids"], json!(["7"]));
    assert_eq!(obs["robots"].as_array().unwrap().len(), 6);
    assert!(obs["teams"]["blue"].get("resources").is_none());
    assert!(obs["runes"].get("red").is_some());
}
#[test]
fn whole_team_dead_is_not_a_win() {
    let mut m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    for r in &mut m.robots {
        if r.team == "red" && r.role != "aerial" {
            r.alive = false;
            r.hp = 0.0;
            r.respawn_required = 1000.0;
        }
    }
    m.step(&json!({}));
    assert_eq!(m.status, "running");
}
#[test]
fn rulebook_timeout_tiebreaker_preserves_outpost_destruction_history() {
    let mut m = Engine::new(42, 1.0, "rmuc2026").unwrap();
    m.tick = 20;
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.outposts.get_mut("red").unwrap().hp = 750.0;
    m.teams.get_mut("red").unwrap().attack_damage = 9999.0;
    m.check_rulebook_winner();
    assert_eq!(m.winner.as_deref(), Some("blue"));
    let mut m = Engine::new(42, 1.0, "rmuc2026").unwrap();
    m.tick = 20;
    for o in m.outposts.values_mut() {
        o.ever_destroyed = true;
    }
    m.teams.get_mut("red").unwrap().attack_damage = 1.0;
    m.check_rulebook_winner();
    assert_eq!(m.winner.as_deref(), Some("red"));
}

#[test]
fn chapter4_positions_and_module_dimensions_follow_the_manual() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    assert_eq!(m.arena.bases["red"], [2.407, 7.5]);
    assert_eq!(m.arena.outposts["blue"], [17.008, 3.643]);
    assert_eq!(m.arena.outposts["red"], [10.992, 11.357]);
    let fort = m
        .arena
        .terrain
        .iter()
        .find(|t| t.id == "red_fortress")
        .unwrap();
    assert!((fort.w - 2.24).abs() < 1e-9);
    assert!((fort.h - 1.9398969).abs() < 1e-6);
    assert_eq!(fort.elevation, 0.15);
    let central = m
        .arena
        .terrain
        .iter()
        .find(|t| t.id == "central_highland")
        .unwrap();
    assert!((central.w - 7.7).abs() < 1e-9);
    assert!((central.h - 10.82).abs() < 1e-9);
    assert_eq!(m.arena.layout_revision, "chapter4-2026-v3");
}

#[test]
fn polygon_surfaces_do_not_fill_their_empty_bounding_corners() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let floor = |y: f64| y.min(15.0 - y) * 1.5_f64.to_radians().tan();
    assert!((height_at(&m.arena, 10.2, 2.2) - floor(2.2)).abs() < 1e-9);
    assert!((height_at(&m.arena, 14.0, 3.5) - floor(3.5) - 0.4).abs() < 1e-9);
    // Figure 4-27 [5]: 10.5 degree slopes join the deck to the 200 mm highway at the ends.
    let ramp = m
        .arena
        .terrain
        .iter()
        .find(|t| t.id == "central_red_end_slope")
        .unwrap();
    let a = ramp.ramp_start.unwrap();
    let b = ramp.ramp_end.unwrap();
    let run = (b[0] - a[0]).hypot(b[1] - a[1]);
    assert!((run * 10.5_f64.to_radians().tan() - 0.2).abs() < 1e-9);
    let (mx, my) = ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0);
    assert!((height_at(&m.arena, mx, my) - floor(my) - 0.3).abs() < 1e-9);
    // The fortress-facing side is a 200 mm block in front of the 400 mm deck, not a ramp.
    for (x, h) in [(8.3, 0.0), (9.0, 0.2), (10.0, 0.2), (10.3, 0.4)] {
        assert!((height_at(&m.arena, x, 7.5) - floor(7.5) - h).abs() < 1e-9, "x {x}");
    }
}

#[test]
fn tunnel_roofs_leave_a_ground_passage_and_pad_is_raised() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    // Figure 4-35: tunnel A sits in the road's north strip east of the road-crossing step.
    assert!(!m.blocked(9.39, 12.5, 0.28));
    assert!(m.blocked(9.39 - 0.375, 12.5, 0.28));
    let floor = |y: f64| y.min(15.0 - y) * 1.5_f64.to_radians().tan();
    assert!((height_at(&m.arena, 9.39, 12.5) - floor(12.5) - 0.2).abs() < 1e-9, "tunnel A passage at road level");
    assert!((height_at(&m.arena, 9.39, 12.0) - floor(12.0)).abs() < 1e-9, "floor north of tunnel A");
    assert!(!m.blocked(8.235, 13.3, 0.28), "the step's high card sits on open highway");
    let drone = m.robots.iter().find(|r| r.id == "red-6").unwrap();
    assert!((drone.z - height_at(&m.arena, drone.x, drone.y)).abs() < 1e-9);
    assert!(drone.z > 0.9);
    let rotated = m
        .arena
        .obstacles
        .iter()
        .find(|w| w.id == "red_resource_side_0.075")
        .unwrap();
    assert!(segment_box(-1.0, 14.2, 2.0, 0.0, rotated).is_some());
    let floor = 0.6 * 1.5_f64.to_radians().tan();
    assert!(
        (height_at(&m.arena, 14.02, 14.4) - floor).abs() < 1e-9,
        "the 650 mm fly gap must stay open floor"
    );
}

#[test]
fn highway_access_follows_figure_4_35() {
    // [1]: the notch is floor and its south edge the 200 mm road-crossing step; west of it a
    // 10 degree slope climbs onto the highway's north strip; [4]: past the fly ramp an
    // 11 degree ramp leads down to tunnel B, a beam 250 mm above the floor.
    let floor = |y: f64| y.min(15.0 - y) * 1.5_f64.to_radians().tan();
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    let h = |m: &Engine, x: f64, y: f64| height_at(&m.arena, x, y) - floor(y);
    assert!(h(&m, 8.235, 12.85).abs() < 1e-9 && (h(&m, 8.235, 12.97) - 0.2).abs() < 1e-9);
    assert!((h(&m, 5.8, 12.0) - 0.2).abs() < 1e-9 && h(&m, 7.3, 12.0).abs() < 1e-9);
    assert!((h(&m, 13.1, 13.37) - 0.2).abs() < 1e-9 && h(&m, 14.3, 13.37).abs() < 1e-9);
    let beam = m.arena.obstacles.iter().find(|w| w.id == "red_tunnel_b_roof").unwrap();
    assert!((beam.bottom - floor(13.37) - 0.25).abs() < 1e-3);
    // An ordinary mecanum infantry is stopped by the step but drives up the slope.
    let drive = |m: &mut Engine, x: f64, y: f64, heading: f64, mv: [f64; 2]| {
        place(m, 3, x, y, heading);
        for _ in 0..20 {
            m.step(&json!({}));
        }
        for _ in 0..80 {
            m.step(&json!({"red":{"4":{"move":mv,"heading":heading,"power":200.0}}}));
        }
        let r = &m.robots[3];
        (r.x, r.y, r.z - floor(r.y))
    };
    let (_, y, z) = drive(&mut m, 8.235, 12.3, std::f64::consts::FRAC_PI_2, [0.0, 0.4]);
    assert!(y < 12.91 && z < 0.12, "climbed the road step to y {y} z {z}");
    let (x, _, z) = drive(&mut m, 7.6, 12.0, std::f64::consts::PI, [-0.4, 0.0]);
    assert!(x < 5.9 && z > 0.15, "stopped on the slope at x {x} z {z}");
}

#[test]
fn highway_no_go_area_admits_only_robots_that_flew_the_slope() {
    // R58: driving into the area behind the fly slope is refused; flying in is not.
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    place(&mut m, 3, 17.4, 14.4, std::f64::consts::PI);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    for _ in 0..60 {
        let r = &m.robots[3];
        m.step(&json!({"red":{"4":{"move":[-0.5, (14.4 - r.y) * 1.5],"power":200.0}}}));
    }
    let r = &m.robots[3];
    assert!(r.x > 16.79 && (r.y - 14.4).abs() < 0.3, "drove into the no-go area to x {}", r.x);
    let m = fly_slope_run(2, 10.0, 1.0, 1.0, f64::INFINITY, 0.0, 40);
    let r = &m.robots[2];
    assert!(r.x > 14.6 && r.x < 16.79 && r.fly_pass == 2, "flew to x {} pass {}", r.x, r.fly_pass);
}

#[test]
fn fly_slope_continues_the_highway_without_a_kerb() {
    // Figures 4-35/4-36: the highway runs straight onto the ramp's 203 mm low end, the
    // 17 degree slope climbs toward the centre line, and the landing sits across the gap.
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    let ramp = m.arena.terrain.iter().find(|t| t.id == "red_fly_slope").unwrap();
    let (lip, crest) = (ramp.ramp_start.unwrap(), ramp.ramp_end.unwrap());
    assert!(crest[0] > lip[0] && lip[0] < 14.0, "red flies toward the blue half");
    let step = height_at(&m.arena, lip[0] + 0.005, lip[1]) - height_at(&m.arena, lip[0] - 0.005, lip[1]);
    assert!(step.abs() < 0.01, "kerb of {step} m in front of the fly slope");
    let floor = 0.6 * 1.5_f64.to_radians().tan();
    assert!((height_at(&m.arena, crest[0] + 0.325, 14.4) - floor).abs() < 1e-9);
    assert!((height_at(&m.arena, crest[0] + 0.7, 14.4) - floor - 0.2).abs() < 1e-9);
    // Figure 4-36: 151 mm rails flank the 860 mm lane.
    let tilt = 0.5 * 1.5_f64.to_radians().tan();
    let rail = height_at(&m.arena, 13.0, 13.9) - height_at(&m.arena, 13.0, 14.4) - tilt;
    assert!((rail - 0.151 / 17_f64.to_radians().cos()).abs() < 1e-9);
    // An ordinary mecanum infantry rolls slowly from the highway up onto the slope.
    place(&mut m, 3, 11.5, 14.4, 0.0);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    for _ in 0..80 {
        let r = &m.robots[3];
        m.step(&json!({"red":{"4":{"move":[0.3, (14.4 - r.y) * 1.5],"power":200.0}}}));
    }
    let r = &m.robots[3];
    assert!(
        r.x > lip[0] + 0.6 && r.z > 0.35 && r.upright > 0.9,
        "stopped at x {} z {}",
        r.x,
        r.z
    );
}

#[test]
fn hardware_variants_are_identical_between_teams_and_inside_construction_limits() {
    let m = Engine::new(42, 420.0, "rmuc2026").unwrap();
    let cfg = config();
    for slot in SLOT_IDS {
        let red = m
            .robots
            .iter()
            .find(|r| r.team == "red" && r.slot == slot)
            .unwrap();
        let blue = m
            .robots
            .iter()
            .find(|r| r.team == "blue" && r.slot == slot)
            .unwrap();
        assert_eq!(red.mass_kg, blue.mass_kg);
        assert_eq!(red.chassis_size_m, blue.chassis_size_m);
        assert_eq!(red.model_asset, blue.model_asset);
        let d = &cfg.robot_designs[&slot.to_string()];
        let limits = &cfg.rulebook["construction"]["envelope"][&red.role];
        assert!(
            d["bare_mass_kg"].as_f64().unwrap()
                <= limits["mass_excluding_referee_kg"].as_f64().unwrap()
        );
        assert!(d["battery_wh"].as_f64().unwrap() <= limits["battery_wh"].as_f64().unwrap());
        assert!(d["voltage_v"].as_f64().unwrap() <= limits["voltage_v"].as_f64().unwrap());
    }
    assert!(m.robots[2].speed_reference_mps > m.robots[3].speed_reference_mps);
    assert!(m.robots[3].max_heat > m.robots[5].max_heat);
    assert!(m.robots[5].hp > m.robots[2].hp);
}

#[test]
fn inertia_and_brakes_replace_instant_velocity_commands() {
    let mut m = Engine::new(42, 6.0, "rmuc2026").unwrap();
    let i = 2;
    m.robots[i].x = 6.8;
    m.robots[i].y = 9.8;
    m.robots[i].z = height_at(&m.arena, 6.8, 9.8);
    let start = m.robots[i].x;
    m.step(&json!({"red":{"3":{"move":[1,0]}}}));
    assert!(m.robots[i].vx > 0.0 && m.robots[i].vx < 1.0);
    assert!(m.robots[i].x - start < 0.05);
    for _ in 0..25 {
        m.step(&json!({"red":{"3":{"move":[1,0]}}}));
    }
    let fast = m.robots[i].vx;
    m.step(&json!({}));
    assert!(m.robots[i].vx > 0.0 && m.robots[i].vx < fast);
    for _ in 0..25 {
        m.step(&json!({}));
    }
    assert!(m.robots[i].vx.abs() < 0.12);
}

#[test]
fn suspension_supports_weight_and_parking_holds_a_sloped_floor() {
    let mut m = Engine::new(42, 4.0, "rmuc2026").unwrap();
    let start = m.robots[0].y;
    for _ in 0..60 {
        m.step(&json!({}));
    }
    let r = &m.robots[0];
    assert!((r.y - start).abs() < 0.05);
    assert!(
        (r.wheel_loads_n.iter().sum::<f64>() - r.mass_kg * 9.81).abs() < r.mass_kg * 9.81 * 0.1
    );
    assert!(r.grounded);
    assert!(r.quaternion[0].abs() > 0.005);
}

#[test]
fn aerial_remains_in_sustained_flight_and_obeys_the_tether() {
    let mut m = Engine::new(42, 15.0, "rmuc2026").unwrap();
    for _ in 0..240 {
        m.step(&json!({"red":{"6":{"support":true,"altitude":1.9,"move":[1,0]}}}));
    }
    let r = m.robots.iter().find(|r| r.id == "red-6").unwrap();
    assert!(r.z > 1.5 && r.support_active);
    assert!(r.battery_remaining_wh > 0.0);
    assert!(r.power_w > 500.0);
    let mast = rapier3d::na::UnitQuaternion::new_normalize(rapier3d::na::Quaternion::new(
        r.quaternion[3],
        r.quaternion[0],
        r.quaternion[1],
        r.quaternion[2],
    )) * rapier3d::na::Vector3::new(0.0, 0.0, 0.65);
    let distance = (r.y + mast.y - 1.85).hypot(r.z + mast.z - 3.9);
    assert!(distance < 2.43);
    assert!(r.x <= 14.1);
}

#[test]
fn only_the_four_exposed_armor_planes_register_hits() {
    let m = Engine::new(42, 2.0, "rmuc2026").unwrap();
    let r = &m.robots[2];
    let z = r.z + r.armor_center_z_m;
    assert!(physics::armor_hit(r, [r.x + 1.0, r.y, z], [-2.0, 0.0, 0.0]).is_some());
    assert!(physics::armor_hit(r, [r.x + 1.0, r.y, z + 0.20], [-2.0, 0.0, 0.0]).is_none());
    assert!(physics::armor_hit(r, [r.x + 1.0, r.y + 0.15, z], [-2.0, 0.0, 0.0]).is_none());
    let normal = physics::armor_normal(r, 0);
    assert!((normal.z.acos().to_degrees() - 75.0).abs() < 1e-8);
}

#[test]
fn aerial_world_control_is_symmetric_with_opposite_initial_yaw() {
    let mut m = Engine::new(42, 6.0, "rmuc2026").unwrap();
    for _ in 0..100 {
        m.step(&json!({"red":{"6":{"support":true,"altitude":1.9,"move":[1,0]}},"blue":{"6":{"support":true,"altitude":1.9,"move":[-1,0]}}}));
    }
    let red = m.robots.iter().find(|r| r.id == "red-6").unwrap();
    let blue = m.robots.iter().find(|r| r.id == "blue-6").unwrap();
    assert!(
        red.x > 3.4 && blue.x < 24.6,
        "both aircraft must move in their commanded world direction"
    );
    assert!((red.x + blue.x - 28.0).abs() < 0.1);
    assert!((red.z - blue.z).abs() < 0.03);
}

#[test]
fn docked_robots_recharge_chassis_energy_inside_the_supply_area() {
    let mut m = Engine::new(42, 6.0, "rmuc2026").unwrap();
    for (index, team) in [(2, "red"), (8, "blue")] {
        let dock = m.arena.charging_docks[team].clone();
        let (tx, ty, nx) = (
            dock["transmitter"][0].as_f64().unwrap(),
            dock["transmitter"][1].as_f64().unwrap(),
            dock["normal"][0].as_f64().unwrap(),
        );
        let r = &mut m.robots[index];
        // Receiver on the chassis front, facing the transmitter's coupling face.
        r.x = tx + nx * (0.24 + r.chassis_size_m[0] / 2.0);
        r.y = ty;
        r.heading = if nx > 0.0 { std::f64::consts::PI } else { 0.0 };
        r.quaternion = if nx > 0.0 {
            [0.0, 0.0, 1.0, 0.0]
        } else {
            [0.0, 0.0, 0.0, 1.0]
        };
        let (x, y) = (r.x, r.y);
        m.robots[index].z = height_at(&m.arena, x, y);
    }
    let before = [m.robots[2].energy, m.robots[8].energy];
    for _ in 0..40 {
        m.step(&json!({}));
    }
    for (k, index) in [2, 8].into_iter().enumerate() {
        let r = &m.robots[index];
        assert_eq!(r.charging_power_w, 100.0, "{} must be docked", r.id);
        assert!(
            r.energy > before[k] + 1000.0,
            "{} energy {} -> {}",
            r.id,
            before[k],
            r.energy
        );
    }
}

#[test]
fn a_deployed_hero_holds_its_position_on_the_sloped_floor() {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    let (x, y) = (10.4, 3.6);
    m.robots[0].x = x;
    m.robots[0].y = y;
    m.robots[0].z = height_at(&m.arena, x, y);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    for _ in 0..300 {
        m.step(&json!({"red":{"1":{"deploy":true}}}));
    }
    let r = &m.robots[0];
    assert!(r.deployed);
    assert!(
        (r.x - x).hypot(r.y - y) < 0.05,
        "drifted to ({}, {})",
        r.x,
        r.y
    );
}

fn rescue_trial(orientation: [f64; 4], gap: f64, steps: usize) -> (Engine, Vec<Value>) {
    rescue_robot(3, orientation, gap, steps, true)
}

fn rescue_robot(
    index: usize,
    orientation: [f64; 4],
    gap: f64,
    steps: usize,
    chase: bool,
) -> (Engine, Vec<Value>) {
    let mut m = Engine::new(42, 40.0, "rmuc2026").unwrap();
    let (x, y) = (6.8, 9.8);
    let e = &mut m.robots[1];
    e.x = x - gap;
    e.y = y;
    e.z = height_at(&m.arena, x - gap, y);
    let slot = m.robots[index].slot;
    let t = &mut m.robots[index];
    t.x = x;
    t.y = y;
    t.z = height_at(&m.arena, x, y) + 0.45;
    t.quaternion = orientation;
    for _ in 0..30 {
        m.step(&json!({}));
    }
    let mut events = Vec::new();
    for _ in 0..steps {
        // Like a policy: close back in if the robot rolled out of reach while released.
        let (e, t) = (&m.robots[1], &m.robots[index]);
        let (dx, dy) = (t.x - e.x, t.y - e.y);
        let gap = dx.hypot(dy);
        let mv = if chase && !e.rescue_contact && gap > 0.78 {
            [dx / gap * 0.25, dy / gap * 0.25]
        } else {
            [0.0, 0.0]
        };
        m.step(&json!({"red":{"2":{"rescue":slot,"move":mv}}}));
        events.extend(m.events.iter().cloned());
        if events.iter().any(|e| e["type"] == "rescue_complete") {
            break;
        }
    }
    for _ in 0..30 {
        m.step(&json!({}));
    }
    (m, events)
}

#[test]
fn engineer_arm_rights_tipped_robots() {
    let h = std::f64::consts::FRAC_1_SQRT_2;
    for (label, q) in [
        ("side", [h, 0.0, 0.0, h]),
        ("other side", [-h, 0.0, 0.0, h]),
        ("roof", [1.0, 0.0, 0.0, 0.0]),
    ] {
        let (m, events) = rescue_trial(q, 0.72, 240);
        let (e, t) = (&m.robots[1], &m.robots[3]);
        assert!(
            events.iter().any(|e| e["type"] == "rescue_grip"),
            "{label}: never gripped"
        );
        assert!(
            events.iter().any(|e| e["type"] == "rescue_complete"),
            "{label}: not righted, upright {}",
            t.upright
        );
        assert!(
            !t.tipped && t.upright > 0.93 && t.grounded,
            "{label}: upright {}",
            t.upright
        );
        assert!(
            !e.tipped && e.upright > 0.9,
            "{label}: engineer tipped itself {}",
            e.upright
        );
        assert!(
            t.wheel_loads_n.iter().all(|f| *f > 1.0),
            "{label}: not back on its wheels"
        );
    }
    // The 34 kg hero on its side is within the 230 N gripper's reach as well.
    let (m, events) = rescue_robot(0, [h, 0.0, 0.0, h], 0.75, 240, true);
    assert!(
        events.iter().any(|e| e["type"] == "rescue_complete"),
        "hero upright {}",
        m.robots[0].upright
    );
}

#[test]
fn rescue_needs_reach_and_a_friendly_tipped_robot() {
    let h = std::f64::consts::FRAC_1_SQRT_2;
    let (m, events) = rescue_robot(3, [h, 0.0, 0.0, h], 1.8, 60, false);
    assert!(events.iter().all(|e| e["type"] != "rescue_grip"));
    assert!(m.robots[3].tipped);
    // Upright robots are left alone and the arm returns to its stowed pose.
    let mut m = Engine::new(42, 10.0, "rmuc2026").unwrap();
    for _ in 0..40 {
        m.step(&json!({"red":{"2":{"rescue":3}}}));
    }
    assert!(m.robots[1].arm_mode == "rescue" && !m.robots[1].rescue_contact);
    let stowed = physics::arm_stowed(&config().robot_designs["2"]["arm"]);
    assert!(m.robots[1]
        .arm_joints
        .iter()
        .zip(stowed)
        .all(|(a, b)| (a - b).abs() < 1e-6));
    m.step(&json!({"red":{"2":{"rescue":9}},"blue":{"2":{"rescue":"red-3"}}}));
    assert!(m.robots[1].rescue_target.is_none() && m.robots[7].rescue_target.is_none());
}

#[test]
fn engineer_cannot_grip_a_teammate_through_a_wall() {
    let mut m = Engine::new(42, 20.0, "rmuc2026").unwrap();
    let mut wall = m.arena.obstacles[0].clone();
    wall.id = "rescue_test_wall".into();
    wall.x = 6.42;
    wall.y = 9.0;
    wall.w = 0.08;
    wall.h = 1.6;
    wall.height = 1.2;
    wall.bottom = 0.0;
    wall.rotation = 0.0;
    m.arena.obstacles.push(wall);
    m.physics = Some(physics::PhysicsWorld::new(
        &m.arena,
        &m.robots,
        &config().robot_designs,
    ));
    place(&mut m, 1, 6.08, 9.8, 0.0);
    place(&mut m, 3, 6.8, 9.8, 0.0);
    let h = std::f64::consts::FRAC_1_SQRT_2;
    m.robots[3].quaternion = [h, 0.0, 0.0, h];
    m.robots[3].z += 0.45;
    for _ in 0..30 {
        m.step(&json!({}));
    }
    assert!(m.robots[3].tipped);
    for _ in 0..80 {
        m.step(&json!({"red":{"2":{"rescue":4}}}));
        assert!(!m.robots[1].rescue_contact);
        assert!(!m.events.iter().any(|e| e["type"] == "rescue_grip"));
    }
    assert!(m.robots[3].tipped);
}

#[test]
fn rune_faces_are_coaxial_on_both_sides_of_the_gantry() {
    let m = Engine::new(42, 10.0, "rmuc2026").unwrap();
    let runes = m.rune_snapshots();
    let rune = &m.arena.rune;
    let (cx, cy, yaw) = (
        rune["x"].as_f64().unwrap(),
        rune["y"].as_f64().unwrap(),
        rune["axis_yaw"].as_f64().unwrap(),
    );
    let along = |t: &Value| {
        (t["x"].as_f64().unwrap() - cx) * yaw.cos() + (t["y"].as_f64().unwrap() - cy) * yaw.sin()
    };
    for (team, sign) in [("red", -1.0), ("blue", 1.0)] {
        for t in runes[team]["targets"].as_array().unwrap() {
            assert!(
                (along(t) - sign * 0.35).abs() < 1e-9,
                "{team} face offset {}",
                along(t)
            );
        }
    }
    // Same shaft angle: each blue target mirrors its red target through the hub plane.
    for k in 0..5 {
        let (r, b) = (&runes["red"]["targets"][k], &runes["blue"]["targets"][k]);
        assert!((r["z"].as_f64().unwrap() - b["z"].as_f64().unwrap()).abs() < 1e-9);
    }
    // The pedestal and gantry posts are solid and split the plinth.
    for id in ["rune_pedestal", "rune_post"] {
        assert!(m.arena.obstacles.iter().any(|w| w.id == id));
    }
    assert!(
        m.arena.assembly_frames["red"]["x_axis"][0]
            .as_f64()
            .unwrap()
            < 0.0
    );
}

fn place(m: &mut Engine, i: usize, x: f64, y: f64, heading: f64) {
    let r = &mut m.robots[i];
    r.x = x;
    r.y = y;
    r.z = height_at(&m.arena, x, y);
    r.heading = heading;
    r.quaternion = [0.0, 0.0, (heading / 2.0).sin(), (heading / 2.0).cos()];
}

#[test]
fn wheel_legged_infantry_balances_drives_and_cannot_strafe() {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    place(&mut m, 2, 6.8, 9.8, 0.0);
    for _ in 0..60 {
        m.step(&json!({}));
    }
    let r = &m.robots[2];
    assert_eq!(r.legs.len(), 2);
    assert!(
        r.upright > 0.97 && r.grounded && !r.tipped,
        "standing {}",
        r.upright
    );
    assert!((r.legs[0] - 0.20).abs() < 0.05, "legs {:?}", r.legs);
    // Commanded sideways: the chassis turns onto the course rather than strafing.
    for _ in 0..24 {
        m.step(&json!({"red":{"3":{"move":[0.0, 1.0]}}}));
    }
    let r = &m.robots[2];
    assert!(
        r.vy > 1.5 && r.upright > 0.9,
        "vy {} upright {}",
        r.vy,
        r.upright
    );
    let course = r.heading.rem_euclid(std::f64::consts::PI);
    assert!(
        (course - std::f64::consts::FRAC_PI_2).abs() < 0.3,
        "heading {}",
        r.heading
    );
    // A lower stance lowers the body; leg commands are clamped to the hardware range.
    for _ in 0..30 {
        m.step(&json!({"red":{"3":{"leg_height":0.05}}}));
    }
    assert!(m.robots[2].legs[0] < 0.15 && m.robots[2].upright > 0.95);
    m.step(&json!({"red":{"4":{"jump":1.0}}}));
    assert!(m.events.iter().any(|e| e["type"] == "invalid_action"));
}

#[test]
fn wheel_legged_infantry_jumps_a_step_and_lands_upright() {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    // Isolate the 400 mm step capability. The real deck now has a 150 mm
    // protective kerb here; jumping that 550 mm obstacle is a different test.
    m.arena.obstacles.retain(|w| w.id != "central_kerb_5");
    m.physics = Some(physics::PhysicsWorld::new(&m.arena,&m.robots,&config().robot_designs));
    // Central highland west face: a 400 mm vertical step at x = 10.15 beside the ramp.
    place(&mut m, 2, 8.6, 10.5, 0.0);
    for _ in 0..30 {
        m.step(&json!({}));
    }
    let floor = m.robots[2].z;
    let mut peak: f64 = 0.0;
    let mut jumped = false;
    for k in 0..90 {
        let near = m.robots[2].x > 9.55 && !jumped;
        let cmd = if k < 60 {
            json!({"red":{"3":{"move":[0.45, 0.0],"heading":0.0,"jump": if near {1.0} else {0.0}}}})
        } else {
            json!({"red":{"3":{"heading":0.0}}})
        };
        m.step(&cmd);
        jumped |= m.events.iter().any(|e| e["type"] == "jump");
        peak = peak.max(m.robots[2].z - floor);
    }
    let r = &m.robots[2];
    assert!(peak > 0.35, "jump height {peak}");
    assert!(
        r.x > 10.35 && r.z > floor + 0.33,
        "landed at x {} dz {}",
        r.x,
        r.z - floor
    );
    assert!(r.upright > 0.9 && r.grounded, "upright {}", r.upright);
    assert!(peak < 0.8, "unrealistic jump {peak}");
}

#[test]
fn a_step_climbing_infantry_walks_up_the_high_point_stairs() {
    let mut m = Engine::new(42, 30.0, "rmuc2027").unwrap();
    // The red high point rises in three 200 mm steps off the trapezoid (0.2 -> 0.4 -> 0.6 -> 0.8 m).
    // The wheel-legged infantry (slot 3) holds F, so the legs mount each step without jumping.
    place(&mut m, 1, 3.55, 2.9, std::f64::consts::PI);
    for _ in 0..30 {
        m.step(&json!({}));
    }
    let floor = m.robots[1].z;
    let mut climbed = false;
    let mut jumped = false;
    // Height of the hull above the ground it stands on: a stride keeps it under a step height,
    // while a jump would throw it much higher.
    let mut clearance: f64 = 0.0;
    for _ in 0..600 {
        m.step(&json!({"red":{"3":{
            "move":[-0.6,0.0],"heading":std::f64::consts::PI,"climb":1.0}}}));
        climbed |= m.events.iter().any(|e| e["type"] == "step_climb");
        jumped |= m.events.iter().any(|e| e["type"] == "jump");
        let r = &m.robots[1];
        clearance = clearance.max(r.z - height_at(&m.arena, r.x, r.y));
        if r.x < 2.4 {
            break;
        }
    }
    // Let the last step settle before reading the pose, as a pilot would.
    for _ in 0..40 {
        m.step(&json!({"red":{"3":{"heading":std::f64::consts::PI}}}));
    }
    let r = &m.robots[1];
    assert!(climbed, "no step_climb event at x {}", r.x);
    assert!(!jumped, "the climb fired a jump instead of stepping");
    assert!(
        r.z > floor + 0.33 && r.x < 3.0,
        "did not mount the stairs: x {} dz {}",
        r.x,
        r.z - floor
    );
    // A step is a low hop, not a jump: a full jump throws the hull ~0.9 m off its own ground.
    // The first riser of this flight is 0.4 m, so mounting it while the centre is still over the
    // lower tread reads higher on this metric than a 0.2 m stair; keep it well under a jump.
    assert!(clearance < 0.78, "the climb became a jump {clearance}");
    assert!(r.upright > 0.85 && r.grounded, "upright {} grounded {}", r.upright, r.grounded);
}

#[test]
fn a_step_ahead_never_reads_as_the_ground_under_a_parked_chassis() {
    let mut m = Engine::new(42, 30.0, "rmuc2027").unwrap();
    // This approach pose used to stall: the carry parked the hull on the first tread with the
    // second step 0.3 m ahead, the step in front counted as "underneath", the chassis read as
    // airborne, and the next climb — gated on `grounded` — could never start. The robot sat on the
    // edge of the tread with no wheel contact for the rest of the match.
    place(&mut m, 1, 3.505, 2.9, std::f64::consts::PI);
    for _ in 0..30 {
        m.step(&json!({}));
    }
    let floor = m.robots[1].z;
    let mut climbs = 0;
    for _ in 0..600 {
        m.step(&json!({"red":{"3":{
            "move":[-0.6,0.0],"heading":std::f64::consts::PI,"climb":1.0}}}));
        climbs += m.events.iter().filter(|e| e["type"] == "step_climb").count();
        if m.robots[1].x < 2.4 {
            break;
        }
    }
    for _ in 0..40 {
        m.step(&json!({"red":{"3":{"heading":std::f64::consts::PI}}}));
    }
    let r = &m.robots[1];
    assert!(climbs >= 3, "stalled on the tread after {climbs} climbs at x {}", r.x);
    assert!(
        r.grounded && r.upright > 0.9,
        "left hanging on the edge: grounded {} upright {}",
        r.grounded,
        r.upright
    );
    assert!(r.z > floor + 0.55, "did not reach the top tread: dz {}", r.z - floor);
}

#[test]
fn a_step_climb_starts_even_when_the_nose_already_touches_the_face() {
    let mut m = Engine::new(42, 30.0, "rmuc2027").unwrap();
    // Nose against the first 200 mm step of the red high point: a pilot who drives up to the
    // kerb and then holds F must still get the tread into view of the probe.
    place(&mut m, 1, 3.43, 2.9, std::f64::consts::PI);
    for _ in 0..30 {
        m.step(&json!({}));
    }
    let floor = m.robots[1].z;
    let mut climbed = false;
    for _ in 0..120 {
        m.step(&json!({"red":{"3":{"move":[-0.4,0.0],"heading":std::f64::consts::PI,"climb":1.0}}}));
        climbed |= m.events.iter().any(|e| e["type"] == "step_climb");
        if m.robots[1].z > floor + 0.12 {
            break;
        }
    }
    let r = &m.robots[1];
    assert!(climbed, "a pressed chassis did not start a climb at x {}", r.x);
    assert!(r.z > floor + 0.12, "did not mount the step: dz {}", r.z - floor);
    assert!(r.upright > 0.7, "upright {}", r.upright);
}

#[test]
fn only_step_climbing_chassis_accept_the_climb_command() {
    let mut m = Engine::new(42, 30.0, "rmuc2027").unwrap();
    // Slot 4 is the folding mecanum infantry: no legs, so `climb` is rejected outright.
    m.step(&json!({"red":{"4":{"climb":1.0}}}));
    assert!(m
        .events
        .iter()
        .any(|e| e["type"] == "invalid_action" && e["reason"].as_str().unwrap_or("").contains("wheel-legged")));
}

#[test]
fn central_steps_stop_ordinary_wheels_but_the_end_slope_climbs() {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    let floor = |y: f64| y.min(15.0 - y) * 1.5_f64.to_radians().tan();
    // Mecanum infantry (slot 4) drives at the fortress-side step block and stops.
    place(&mut m, 3, 7.9, 7.5, 0.0);
    for _ in 0..80 {
        m.step(&json!({"red":{"4":{"move":[1.0, 0.0],"heading":0.0}}}));
    }
    let r = &m.robots[3];
    assert!(
        r.x < 10.15 && r.z - floor(r.y) < 0.3,
        "climbed the step: x {} dz {}",
        r.x,
        r.z - floor(r.y)
    );
    // From the highway it drives up the 10.5 degree end slope onto the deck
    // (body z rides about 0.33 m above the floor datum on the 0.4 m deck).
    place(&mut m, 3, 11.9, 13.4, -std::f64::consts::FRAC_PI_2);
    for _ in 0..50 {
        m.step(&json!({"red":{"4":{"move":[0.0, -1.0],"heading":-std::f64::consts::FRAC_PI_2}}}));
    }
    let r = &m.robots[3];
    assert!(
        r.y < 11.6 && r.z - floor(r.y) > 0.3,
        "end slope not climbed: y {} dz {}",
        r.y,
        r.z - floor(r.y)
    );
}

#[test]
fn destroyed_robots_lose_chassis_power_and_coast() {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    place(&mut m, 3, 6.0, 9.6, 0.0);
    place(&mut m, 2, 6.0, 8.6, 0.0);
    for _ in 0..30 {
        m.step(&json!({"red":{"4":{"move":[1.0, 0.0]},"3":{"move":[1.0, 0.0]}}}));
    }
    let (v4, v3, x4, x3) = (m.robots[3].vx, m.robots[2].vx, m.robots[3].x, m.robots[2].x);
    assert!(v4 > 1.5 && v3 > 1.5, "speeds {v4} {v3}");
    for i in [2, 3] {
        m.robots[i].alive = false;
        m.robots[i].hp = 0.0;
        m.robots[i].respawn_required = 1e9;
    }
    let mut reach3: f64 = 0.0;
    for _ in 0..80 {
        m.step(&json!({"red":{"4":{"move":[1.0, 0.0]},"3":{"move":[1.0, 0.0]}}}));
        reach3 = reach3.max(m.robots[2].x - x3);
    }
    let (a, b) = (&m.robots[3], &m.robots[2]);
    assert!(!a.powered && !b.powered);
    // Momentum carries both on; friction, not a parking brake, stops them.
    let slide4 = a.x - x4;
    assert!(slide4 > 0.6 && slide4 < 4.0, "mecanum slide {slide4}");
    assert!(reach3 > 0.25, "wheel-legged slide {reach3}");
    assert!(a.vx.hypot(a.vy) < 0.05, "mecanum still moving {}", a.vx);
    // Limp legs fold and the unbalanced two-wheeler tips onto its body.
    assert!(
        b.upright < 0.95,
        "wheel-legged still balancing {}",
        b.upright
    );
    assert!(b.legs.iter().all(|l| *l < 0.15), "legs {:?}", b.legs);
}

#[test]
fn wheel_legged_robot_stands_up_from_its_front_but_needs_help_from_its_side() {
    let h = std::f64::consts::FRAC_1_SQRT_2;
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    place(&mut m, 2, 6.8, 9.8, 0.0);
    m.robots[2].quaternion = [0.0, h, 0.0, h];
    m.robots[2].z += 0.3;
    for _ in 0..80 {
        m.step(&json!({}));
    }
    assert!(
        m.robots[2].upright > 0.93 && m.robots[2].grounded,
        "front: {}",
        m.robots[2].upright
    );
    let (m, events) = rescue_robot(2, [h, 0.0, 0.0, h], 0.72, 240, true);
    assert!(
        events.iter().any(|e| e["type"] == "rescue_complete"),
        "side: {}",
        m.robots[2].upright
    );
    assert!(m.robots[2].upright > 0.93 && m.robots[2].grounded);
}

#[test]
fn engineer_arm_can_be_driven_directly_and_blocks_sight() {
    let mut m = Engine::new(42, 20.0, "rmuc2026").unwrap();
    place(&mut m, 1, 7.5, 9.8, 0.0);
    place(&mut m, 3, 5.6, 9.2, 0.0);
    for _ in 0..10 {
        m.step(&json!({}));
    }
    let (from, to) = ([7.3, 10.25, 0.86], [7.75, 10.25, 0.86]);
    assert!(
        m.physics.as_ref().unwrap().visible_point(3, from, to),
        "path must start clear"
    );
    // Raise the boom to the left of the chassis: tool tip 0.45 m to the side at 0.86 m.
    let tip = [7.5, 10.25, 0.86];
    for _ in 0..40 {
        m.step(&json!({"red":{"2":{"arm":{"tip":tip,"dir":[0.0, 1.0, 0.0]}}}}));
    }
    let e = &m.robots[1];
    assert_eq!(e.arm_mode, "manual");
    let error = (e.arm_tip[0] - tip[0])
        .hypot(e.arm_tip[1] - tip[1])
        .hypot(e.arm_tip[2] - tip[2]);
    assert!(error < 0.03, "arm tip {:?}", e.arm_tip);
    assert!(
        !m.physics.as_ref().unwrap().visible_point(3, from, to),
        "raised arm must block the line"
    );
    // Joint-space commands are clamped to the hardware limits.
    for _ in 0..40 {
        m.step(&json!({"red":{"2":{"arm":{"joints":[0.0, 9.0, 0.1, 0.0, 0.0, 0.0, 0.0]}}}}));
    }
    let q = m.robots[1].arm_joints;
    assert!(
        (q[1] - 0.5).abs() < 1e-6 && (q[2] - 0.22).abs() < 1e-6,
        "{q:?}"
    );
}

#[test]
fn observations_report_every_visible_robot_with_its_exterior_state() {
    let mut m = Engine::new(42, 20.0, "rmuc2026").unwrap();
    place(&mut m, 3, 6.0, 9.8, 0.0);
    place(&mut m, 9, 8.5, 9.8, std::f64::consts::PI);
    place(&mut m, 8, 8.5, 10.6, std::f64::consts::PI);
    m.robots[8].alive = false;
    m.robots[8].hp = 0.0;
    m.robots[8].respawn_required = 1e9;
    for _ in 0..6 {
        m.step(&json!({}));
    }
    let obs = m.observation("red", None);
    let enemies = obs["enemies"].as_array().unwrap();
    let blue4 = enemies
        .iter()
        .find(|e| e["id"] == "blue-4")
        .expect("blue-4 in plain view");
    for key in [
        "aim",
        "pitch",
        "angular_velocity",
        "quaternion",
        "powered",
        "upright",
        "seen_by",
    ] {
        assert!(blue4.get(key).is_some(), "missing {key}");
    }
    assert!(blue4["seen_by"].as_array().unwrap().contains(&json!(4)));
    let wreck = enemies
        .iter()
        .find(|e| e["id"] == "blue-3")
        .expect("wrecks stay visible");
    assert_eq!(wreck["alive"], false);
    assert_eq!(wreck["targetable"], false);
    assert!(wreck.get("legs").is_some());
    let mine = &obs["vision"]["4"];
    assert!(mine["enemies"]
        .as_array()
        .unwrap()
        .contains(&json!("blue-4")));
}

/// Drives robot `idx` along the red fly-slope lane (y = 14.4) and jumps once at `jump_x`.
fn fly_slope_run(
    idx: usize,
    start_x: f64,
    dir: f64,
    speed: f64,
    jump_x: f64,
    strength: f64,
    ticks: usize,
) -> Engine {
    let mut m = Engine::new(42, 30.0, "rmuc2026").unwrap();
    place(
        &mut m,
        idx,
        start_x,
        14.4,
        if dir < 0.0 { std::f64::consts::PI } else { 0.0 },
    );
    m.robots[idx].z = height_at(&m.arena, start_x, 14.4);
    // A run starting in the no-go area behind the slope has flown in (R58).
    m.robots[idx].fly_pass = 2;
    for _ in 0..20 {
        m.step(&json!({}));
    }
    let slot = m.robots[idx].slot.to_string();
    let mut jumped = false;
    for _ in 0..ticks {
        let r = &m.robots[idx];
        let mut cmd = json!({"move":[dir * speed, (14.4 - r.y) * 1.5],"power":200.0});
        if !jumped && (r.x - jump_x) * dir > 0.0 {
            cmd["jump"] = json!(strength);
        }
        m.step(&json!({"red":{slot.clone(): cmd}}));
        jumped |= m.events.iter().any(|e| e["type"] == "jump");
    }
    m
}

#[test]
fn wheel_legged_infantry_flies_the_fly_slope_both_ways() {
    // Forward: drive off the highway up the 17 degree slope and clear the 650 mm gap.
    let m = fly_slope_run(2, 10.0, 1.0, 1.0, f64::INFINITY, 0.0, 45);
    let r = &m.robots[2];
    assert!(
        r.x > 14.6 && r.z > 0.12 && r.upright > 0.9 && !r.tipped,
        "landed x {} z {} up {}",
        r.x,
        r.z,
        r.upright
    );
    // Reverse: from the landing, jump the gap up onto the 553 mm crest and drive down.
    let m = fly_slope_run(2, 15.6, -1.0, 0.6, 14.7, 1.0, 90);
    let r = &m.robots[2];
    assert!(
        r.x < 12.55 && (r.y - 14.4).abs() < 0.6 && r.upright > 0.9 && !r.tipped,
        "reverse ended x {} y {} up {}",
        r.x,
        r.y,
        r.upright
    );
}

/// Scripted showcase for viewer QA: `QA_REPLAY=path cargo test -- --ignored qa_scenario_replay`.
/// Fly-slope both ways, a step jump, an arm rescue, an assembly, power-cut coasting
/// and both rune faces activating. Writes a v4 replay; not part of the ranked engine.
#[test]
#[ignore]
fn qa_scenario_replay() {
    let path = std::env::var("QA_REPLAY").expect("QA_REPLAY path");
    let h = std::f64::consts::FRAC_1_SQRT_2;
    let mut m = Engine::new(42, 40.0, "rmuc2026").unwrap();
    let env = |k: &str, d: f64| {
        std::env::var(k)
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(d)
    };
    let (start, gap_jump) = (env("QA_START", 10.0), env("QA_GAP_JUMP", 14.8));
    place(&mut m, 2, start, 14.4, 0.0);
    place(&mut m, 8, 19.4, 4.5, std::f64::consts::PI);
    place(&mut m, 1, 6.08, 9.8, 0.0);
    place(&mut m, 3, 6.8, 9.8, 0.0);
    m.robots[3].z += 0.45;
    m.robots[3].quaternion = [h, 0.0, 0.0, h];
    let blue_heading = (-0.70710678f64).atan2(-0.70710678);
    place(&mut m, 7, 14.665, 8.165, blue_heading);
    m.robots[7].carried_units = 2;
    place(&mut m, 9, 24.0, 5.2, std::f64::consts::PI);
    for _ in 0..20 {
        m.step(&json!({}));
    }
    let mut frames = Vec::new();
    let mut fly = 0;
    let mut jumped_back = false;
    for tick in 0..800 {
        let t = tick as f64 * DT;
        let r3 = m.robots[2].clone();
        let lane = (14.4 - r3.y) * 1.5;
        // Red 3: forward fly-slope, then turn and fly it in reverse.
        let red3 = match fly {
            0 if r3.x > 15.4 => {
                fly = 1;
                json!({})
            }
            0 => json!({"move":[1.0, lane],"power":200.0}),
            1 if r3.x > 15.8 => {
                fly = 2;
                json!({})
            }
            1 => json!({"move":[0.5, lane]}),
            2 => {
                if t > 9.0 {
                    fly = 3;
                    json!({})
                } else {
                    json!({"heading":std::f64::consts::PI})
                }
            }
            _ => {
                let jump = !jumped_back && r3.x < gap_jump;
                jumped_back |= jump;
                if r3.x < 12.3 {
                    json!({})
                } else {
                    json!({"move":[-0.6, lane],"power":200.0,"jump": if jump {1.0} else {0.0}})
                }
            }
        };
        let b3 = m.robots[8].clone();
        let blue3 = if t < 1.0 || b3.x < 16.6 {
            json!({})
        } else {
            json!({"move":[-0.45, 0.0],"heading":std::f64::consts::PI,"jump": if b3.x < 18.45 && b3.x > 18.0 {1.0} else {0.0}})
        };
        // Red engineer rights red 4, then raises its arm across a sight line.
        let red2 = if t < 12.0 {
            json!({"rescue":4})
        } else {
            json!({"arm":{"tip":[6.5, 9.2, 0.85 + 0.1 * (t - 12.0).sin()],"dir":[0.0, -1.0, 0.0]}})
        };
        let core = &m.assemblies["blue"];
        let blue2 = json!({"assembly": if t < 4.0 {1} else {0},"assembly_insert": t > 3.0,"assembly_translate": t > 4.0,"assembly_rotate":core.target_rotation,"assembly_confirm": t > 6.0});
        let blue4 = json!({"move":[-1.0, 0.0],"power":200.0});
        // Power cut while moving: the mecanum robot coasts, the two-wheeler collapses.
        let kill = if (t - 1.5).abs() < 1e-9 {
            Some(9)
        } else if (t - 5.0).abs() < 1e-9 {
            Some(8)
        } else {
            None
        };
        if let Some(i) = kill {
            for i in [i] {
                m.robots[i].alive = false;
                m.robots[i].hp = 0.0;
                m.robots[i].respawn_required = 8.0;
                m.robots[i].respawn_progress = 0.0;
            }
        }
        let commands = json!({
            "red":{"2":red2,"3":red3,"7":{"rune_start": t > 1.0 && t < 1.2}},
            "blue":{"2":blue2,"3":blue3,"4":blue4,"7":{"rune_start": t > 2.0 && t < 2.2}}
        });
        frames.push(m.step(&commands));
    }
    let mut stat = serde_json::Map::new();
    for key in ["arena", "rulebook_rules", "simulation_assumptions"] {
        stat.insert(key.into(), frames[0][key].clone());
    }
    let frames: Vec<Value> = frames
        .into_iter()
        .map(|mut f| {
            for key in ["arena", "rulebook_rules", "simulation_assumptions"] {
                f.as_object_mut().unwrap().remove(key);
            }
            f
        })
        .collect();
    let replay = json!({"format":"rmuc-replay-v4","metadata":{"seed":42,"duration":40,"map":"rmuc2026","dt":DT,"test_match":true,
        "teams":{"red":{"name":"qa-scenario","version":"v2","mode":"team"},"blue":{"name":"qa-scenario","version":"v2","mode":"team"}}},
        "static":stat,"frames":frames,"actions":[],"diagnostics":[],
        "result":{"winner":null,"reason":"timeout","time":40.0,"digest":"","policy_errors":0}});
    std::fs::write(path, serde_json::to_vec(&replay).unwrap()).unwrap();
}
