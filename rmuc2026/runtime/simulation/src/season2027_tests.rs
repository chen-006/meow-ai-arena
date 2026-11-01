//! RMUC 2027 rules: lineups, inspection, economy, control, structures, aerial system,
//! heavy launcher, sentry postures, crossings and the timeout ranking.
use super::*;

fn match_() -> Engine {
    Engine::new(42, 420.0, "rmuc2027").unwrap()
}
fn robot(m: &Engine, id: &str) -> usize {
    m.robots.iter().position(|r| r.id == id).unwrap()
}
fn place(m: &mut Engine, i: usize, x: f64, y: f64) {
    let r = &mut m.robots[i];
    r.x = x;
    r.y = y;
    r.z = height_at(&m.arena, x, y);
}
fn zone_center(m: &Engine, id: &str) -> [f64; 2] {
    let z = m.arena.zones.iter().find(|z| z.id == id).unwrap();
    if z.polygon.is_empty() {
        return [z.x, z.y];
    }
    let n = z.polygon.len() as f64;
    [z.polygon.iter().map(|p| p[0]).sum::<f64>() / n, z.polygon.iter().map(|p| p[1]).sum::<f64>() / n]
}
fn lineup(models: &[(u32, u32)]) -> Option<BTreeMap<String, Vec<Deployment>>> {
    let list: Vec<Deployment> = models.iter().map(|(slot, model)| Deployment { slot: *slot, model: *model }).collect();
    Some([("red".into(), list.clone()), ("blue".into(), list)].into())
}
/// One lethal 17 mm hit, so death-time rules can be checked without driving the chassis.
fn slain(m: &mut Engine, id: &str) {
    let i = robot(m, id);
    m.robots[i].hp = 5.0;
    let r = m.robots[i].clone();
    let face = [r.x + r.chassis_size_m[0] / 2.0, r.y, r.z + r.armor_center_z_m];
    let p = Projectile { id: 1, source: "blue-4".into(), team: "blue".into(), role: "infantry".into(),
        x: face[0] + 0.2, y: face[1], z: face[2], vx: -25.0, vy: 0.0, vz: 0.0, remaining: 1.0, damage: 20.0,
        radius: 0.0084, deployed: false };
    m.robot_hit_2027(i, &p, face[0], face[1], face[2]);
}

#[test]
fn an_infantry_chassis_never_drives_faster_than_the_rulebook_cap() {
    // 5 m/s is the rulebook ceiling for an infantry chassis. The default lineup fields the
    // wheel-legged balancing infantry, and the fastest design in the table (the light omni
    // infantry) sits exactly on the cap instead of over it.
    let rules = &config().rulebook_2027["speed_limits"]["infantry_mps"].as_f64().unwrap();
    assert_eq!(*rules, 5.0);
    for model in [3, 4, 7, 301, 302, 303, 304, 305, 2731, 2732] {
        let m = Engine::new_with_lineups(42, 420.0, "rmuc2027", lineup(&[(1, 2711), (3, model), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
        let r = &m.robots[robot(&m, "red-3")];
        assert!(r.speed_reference_mps <= 5.0, "{model}: {} m/s", r.speed_reference_mps);
    }
    let m = match_();
    assert_eq!(m.robots[robot(&m, "red-3")].speed_reference_mps, 4.3);
}

#[test]
fn default_lineup_fields_heavy_infantry_sentry_and_aerial_system() {
    let m = match_();
    assert_eq!(m.season, 2027);
    let slots: Vec<u32> = m.robots.iter().filter(|r| r.team == "red").map(|r| r.slot).collect();
    assert_eq!(slots, vec![1, 3, 4, 6, 7]);
    let heavy = &m.robots[robot(&m, "red-1")];
    assert_eq!((heavy.role.as_str(), heavy.design_slot(), heavy.hp, heavy.launcher.as_str()), ("heavy", 2711, 400.0, "locked"));
    assert_eq!(heavy.speed_reference_mps, 2.5);
    let sentry = &m.robots[robot(&m, "red-7")];
    assert_eq!((sentry.role.as_str(), sentry.hp, sentry.ammo, sentry.max_heat, sentry.posture.as_str()), ("sentry", 400.0, 300, 260.0, "move"));
    let aerial = &m.robots[robot(&m, "red-6")];
    assert_eq!((aerial.role.as_str(), aerial.drones.len()), ("aerial", 4));
    assert!(aerial.drones.iter().all(|d| d.state == "docked"));
    assert_eq!((m.bases["red"].max_hp, m.outposts["red"].max_hp), (10000.0, 1000.0));
    assert_eq!(m.teams["red"].energy_units_left, Some(6));
    for id in ["red_outpost", "red_base", "blue_outpost", "blue_base"] {
        let p = &m.positions[id];
        assert_eq!((p.control.as_str(), p.controller.as_str()), ("full", p.owner.as_str()));
    }
    assert_eq!(m.snapshot()["rules"], "rmuc-2027-v1.0.0-arena-v1");
}

#[test]
fn every_wheel_legged_infantry_model_supports_f_step_climbing() {
    let designs = config().robot_designs;
    let infantry: Vec<_> = designs.values()
        .filter(|design| design["role"] == "infantry" && design["chassis_type"] == "wheel_legged")
        .collect();
    assert_eq!(infantry.len(), 4);
    for design in infantry {
        assert_eq!(design["step_climb_m"], 0.2, "{}", design["slot"]);
        assert_eq!(design["capabilities"]["step_climb_m"], 0.2, "{}", design["slot"]);
    }
    for model in [3, 304, 305, 2731] {
        let mut m = Engine::new_with_lineups(
            42,
            30.0,
            "rmuc2027",
            lineup(&[(1, 2711), (3, model), (4, 2732), (6, 2761), (7, 2771)]),
        ).unwrap();
        assert_eq!(m.robots[robot(&m, "red-3")].step_climb_m, 0.2, "model {model}");
        m.step(&json!({"red":{"3":{"climb":1.0}}}));
        assert!(
            !m.events.iter().any(|event| event["type"] == "invalid_action"),
            "model {model} rejected the F step-climb command"
        );
    }
}

#[test]
fn heavy_translation_keeps_its_speed_while_turning_at_50w() {
    // Speed is sampled while the chassis still has clear ground ahead: the corner of the field is
    // only a few metres away, and a longer run measures the wall, not the turning cost.
    let speed = |yaw_rate: f64| {
        let mut m = match_();
        let i = robot(&m, "red-1");
        place(&mut m, i, 6.0, 1.5);
        for _ in 0..40 {
            m.step(&json!({"red":{"1":{"move":[1.0,0.0],"power":50.0,"yaw_rate":yaw_rate}}}));
        }
        m.robots[i].vx.hypot(m.robots[i].vy)
    };
    let straight = speed(0.0);
    let gentle = speed(0.8);
    let turning = speed(2.0);
    eprintln!("straight {straight:.3} / yaw 0.8 {gentle:.3} / yaw 2.0 {turning:.3} m/s");
    assert!(straight <= 2.5 + 0.02, "straight-line speed was {straight:.3} m/s");
    // A heavy chassis pays for its yaw rate: a hard turn must bleed ground speed, a gentle one less.
    assert!(turning < straight * 0.85, "turning speed {turning:.3} m/s barely lost anything off {straight:.3} m/s");
    assert!(turning > straight * 0.3, "turning speed {turning:.3} m/s collapsed against {straight:.3} m/s");
    assert!(gentle < straight * 0.98, "even a slow turn must cost something: {gentle:.3} vs {straight:.3}");
    assert!(gentle > turning, "a slow turn ({gentle:.3}) must cost less than a fast one ({turning:.3})");
}

#[test]
fn only_the_heavy_robot_pays_for_turning() {
    // The turning cost is a heavy-robot property. Every other design must be untouched by it, so
    // the parameter cannot silently change infantry or sentry handling.
    let designs = config().robot_designs;
    let paying: Vec<u32> = designs.values()
        .filter(|design| design.get("turn_speed_loss_per_rad_s").and_then(Value::as_f64).unwrap_or(0.0) > 0.0)
        .map(|design| design["slot"].as_u64().unwrap() as u32)
        .collect();
    assert_eq!(paying, vec![2711, 2712, 2713], "only the 2027 heavy designs pay for turning");
    for slot in paying {
        assert_eq!(designs[&slot.to_string()]["turn_speed_loss_per_rad_s"], 0.6, "{slot}");
    }
}

#[test]
fn each_season_admits_only_models_that_pass_its_construction_rules() {
    let hero = Engine::new_with_lineups(42, 10.0, "rmuc2027", lineup(&[(1, 1), (3, 3), (4, 4), (6, 2761), (7, 2771)]));
    assert!(hero.err().unwrap().contains("does not pass the 2027"));
    let heavy = Engine::new_with_lineups(42, 10.0, "rmuc2026", lineup(&[(1, 2711), (2, 2), (3, 3), (4, 4), (6, 6), (7, 7)]));
    assert!(heavy.err().unwrap().contains("does not pass the 2026"));
    let drone = Engine::new_with_lineups(42, 10.0, "rmuc2027", lineup(&[(1, 2711), (6, 6)]));
    assert!(drone.is_err());
    // A 2026 infantry passes the 2027 infantry limits and plays a 2027 slot.
    let mixed = Engine::new_with_lineups(42, 10.0, "rmuc2027", lineup(&[(1, 2712), (3, 301), (4, 304), (6, 2762), (7, 2772)])).unwrap();
    assert_eq!(mixed.robots[robot(&mixed, "red-3")].design_slot(), 301);
    let twice = Engine::new_with_lineups(42, 10.0, "rmuc2027", lineup(&[(1, 2761), (2, 2762)]));
    assert!(twice.err().unwrap().contains("one aerial system"));
}

#[test]
fn coins_follow_the_2027_schedule() {
    let mut m = match_();
    m.tick = 20;
    m.step(&json!({}));
    assert_eq!(m.teams["red"].resources, 800.0);
    m.tick = 1220;
    m.step(&json!({}));
    assert_eq!(m.teams["red"].resources, 900.0);
    m.tick = 7220;
    m.step(&json!({}));
    assert_eq!(m.teams["red"].resources, 1200.0);
}

#[test]
fn occupying_an_outpost_position_takes_temporary_control_and_strips_the_base_defense() {
    let mut m = match_();
    let blue = robot(&m, "blue-3");
    let card = zone_center(&m, "red_outpost");
    place(&mut m, blue, card[0], card[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    assert_eq!(m.structure_defense("red", false), 1.0);
    for _ in 0..140 {
        m.robots[blue].x = card[0];
        m.robots[blue].y = card[1];
        m.step(&json!({}));
        if m.positions["red_outpost"].control == "temporary" {
            break;
        }
    }
    let p = &m.positions["red_outpost"];
    assert_eq!((p.control.as_str(), p.controller.as_str()), ("temporary", "blue"));
    assert_eq!(m.structure_defense("red", false), 0.0);
    assert!(m.fortress_active("red"));
    // Absent for five seconds, the temporary holder hands it back.
    place(&mut m, blue, 20.0, 7.5);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    for _ in 0..200 {
        m.step(&json!({}));
    }
    let p = &m.positions["red_outpost"];
    assert_eq!((p.control.as_str(), p.controller.as_str()), ("full", "red"));
}

#[test]
fn a_destroyed_outpost_is_rebuilt_for_the_team_that_takes_full_control() {
    let mut m = match_();
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.outpost_destroyed("red");
    assert_eq!(m.positions["red_outpost"].control, "temporary");
    assert_eq!(m.positions["red_outpost"].controller, "red");
    for _ in 0..120 {
        m.step(&json!({}));
    }
    let p = &m.positions["red_outpost"];
    assert_eq!((p.control.as_str(), p.controller.as_str()), ("full", "blue"));
    let o = &m.outposts["red"];
    assert_eq!((o.hp, o.owner.as_deref()), (500.0, Some("blue")));
    // Holding both outpost positions builds control degree for blue.
    for _ in 0..40 {
        m.step(&json!({}));
    }
    assert!(m.teams["blue"].control.unwrap() >= 1.9);
    assert_eq!(m.teams["red"].control.unwrap(), 0.0);
}

#[test]
fn an_aerial_robot_strike_damages_enemy_outpost_armor_and_wears_its_guard() {
    let mut m = match_();
    m.tick = 3620; // outposts stop rotating after 3 minutes
    let a = robot(&m, "blue-6");
    m.step(&json!({"blue":{"6":{"drones":{"1":{"launch":true}}}}}));
    let plate = Engine::structure_plates_2027(&m.outposts["red"], true)[0];
    let n = plate.normal;
    let start = [plate.center[0] + n[0] * 0.5, plate.center[1] + n[1] * 0.5, plate.center[2]];
    let speed = 3.0;
    m.robots[a].drones[0].state = "flying".into();
    m.robots[a].drones[0].launch_until = 0.0;
    m.physics.as_mut().unwrap().place_drone(a, 0, start, [-n[0] * speed, -n[1] * speed, 0.0]);
    let before = m.outposts["red"].hp;
    let mut struck = false;
    for _ in 0..20 {
        m.step(&json!({"blue":{"6":{"drones":{"1":{"move":[-n[0],-n[1],0.0]}}}}}));
        struck |= m.events.iter().any(|e| e["type"] == "drone_strike" && e["valid"] == true);
        if struck {
            break;
        }
    }
    assert!(struck, "{:?}", m.robots[a].drones[0].z);
    assert_eq!(m.outposts["red"].hp, before - 200.0);
    assert_eq!(m.robots[a].drones[0].integrity, 2.0);
    assert_eq!(m.teams["blue"].attack_damage, 200.0);
}

#[test]
fn the_heavy_launcher_unlocks_fits_at_the_workbench_and_installs_with_its_first_shot() {
    let mut m = match_();
    let h = robot(&m, "red-1");
    m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2, 3];
    m.step(&json!({}));
    assert_eq!(m.robots[h].launcher, "unlocked");
    let dock = m.arena.zones.iter().find(|z| z.id == "red_workbench").map(|z| [z.x, z.y]).unwrap();
    place(&mut m, h, dock[0], dock[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    for _ in 0..160 {
        m.step(&json!({"red":{"1":{"install_launcher":true,"brake":true}}}));
        if m.robots[h].launcher == "fitted" {
            break;
        }
    }
    assert_eq!(m.robots[h].launcher, "fitted");
    m.robots[h].ammo = 3;
    let mut shots = 0;
    for _ in 0..10 {
        m.step(&json!({"red":{"1":{"fire":true,"aim":0.0,"pitch":0.1,"brake":true}}}));
        shots += m.events.iter().filter(|e| e["type"] == "shot").count();
        if m.robots[h].launcher == "installed" {
            break;
        }
    }
    assert_eq!(m.robots[h].launcher, "installed");
    assert!(shots >= 1);
    assert_eq!(m.robots[h].max_hp, 600.0);
    assert_eq!(m.robots[h].max_heat, 300.0);
    assert!(m.projectiles.iter().any(|p| p.source == "red-1" && p.damage == 0.0) || shots == 1);
}

#[test]
fn launcher_unlocks_earlier_for_the_team_that_falls_behind() {
    let mut m = match_();
    m.bases.get_mut("red").unwrap().hp = 7500.0; // blue leads by 2500 HP: 300 - 50 = 250 s
    m.step(&json!({}));
    assert_eq!(m.teams["red"].launcher_unlock_s, Some(250.0));
    assert_eq!(m.teams["blue"].launcher_unlock_s, Some(300.0));
    m.tick = 250 * 20;
    m.step(&json!({}));
    assert_eq!(m.robots[robot(&m, "red-1")].launcher, "unlocked");
    assert_eq!(m.robots[robot(&m, "blue-1")].launcher, "locked");
}

#[test]
fn a_locked_heavy_fits_early_but_cannot_fire_until_the_unlock() {
    let mut m = match_();
    let h = robot(&m, "red-1");
    m.step(&json!({}));
    assert_eq!(m.robots[h].launcher, "locked");
    let dock = m.arena.zones.iter().find(|z| z.id == "red_workbench").map(|z| [z.x, z.y]).unwrap();
    place(&mut m, h, dock[0], dock[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    // Installing is allowed at any time in the match, before the rules' unlock.
    for _ in 0..200 {
        m.step(&json!({"red":{"1":{"install_launcher":true,"brake":true}}}));
        if m.robots[h].launcher == "fitted" {
            break;
        }
    }
    assert_eq!(m.robots[h].launcher, "fitted");
    assert!(m.teams["red"].launcher_unlock_s.unwrap() > 200.0);
    // …but the trigger stays dead while the team has not unlocked it: no round leaves the magazine.
    m.robots[h].ammo = 5;
    for _ in 0..40 {
        m.step(&json!({"red":{"1":{"fire":true,"aim":0.0,"pitch":0.1,"brake":true}}}));
    }
    assert_eq!(m.robots[h].ammo, 5);
    assert!(!m.events.iter().any(|e| e["type"] == "shot" && e["robot"] == "red-1"));
    assert_eq!(m.robots[h].launcher, "fitted");
    // Finish difficulty 3 and the same trigger fires, installing the module on its first shot.
    m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2, 3];
    for _ in 0..20 {
        m.step(&json!({"red":{"1":{"fire":true,"aim":0.0,"pitch":0.1,"brake":true}}}));
        if m.robots[h].ammo < 5 {
            break;
        }
    }
    assert!(m.events.iter().any(|e| e["type"] == "shot" && e["robot"] == "red-1"));
    assert_eq!(m.robots[h].launcher, "installed");
}

#[test]
fn sentry_postures_trade_power_cooling_and_protection() {
    let mut m = match_();
    let s = robot(&m, "red-7");
    m.step(&json!({}));
    assert_eq!(m.robots[s].power_limit, 150.0); // move posture: 100 W x 1.5
    assert_eq!(m.robots[s].vulnerability, 0.25);
    m.tick = 200;
    m.step(&json!({"red":{"7":{"posture":"attack"}}}));
    assert_eq!(m.robots[s].posture, "attack");
    assert_eq!((m.robots[s].power_limit, m.robots[s].cooling_multiplier), (50.0, 3.0));
    m.step(&json!({"red":{"7":{"posture":"defense"}}}));
    assert_eq!(m.robots[s].posture, "attack", "5 s switch cooldown");
    m.tick = 320;
    m.step(&json!({"red":{"7":{"posture":"defense","posture_enhanced":true}}}));
    assert_eq!((m.robots[s].posture.as_str(), m.robots[s].posture_enhanced), ("defense", true));
    assert_eq!(m.robots[s].defense_buff, 0.99);
    for _ in 0..320 {
        m.step(&json!({}));
    }
    assert!(!m.robots[s].posture_enhanced, "15 s enhanced budget");
    assert_eq!(m.robots[s].defense_buff, 0.5);
}

#[test]
fn sentry_collects_one_hundred_rounds_per_minute_at_its_supply_area() {
    let mut m = match_();
    let s = robot(&m, "red-7");
    m.tick = 20 * 125;
    let supply = zone_center(&m, "red_supply");
    place(&mut m, s, supply[0], supply[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    m.step(&json!({}));
    assert_eq!(m.robots[s].ammo, 500);
    m.step(&json!({}));
    assert_eq!(m.robots[s].ammo, 500);
}

// Rulebook p.74: death clears the shot heat and hands the chassis a buffer full to its ceiling.
#[test]
fn a_destroyed_chassis_clears_its_heat_and_refills_the_buffer() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    let capacity = m.rules["power"]["buffer_capacity_j"].as_f64().unwrap();
    m.robots[i].heat = m.robots[i].max_heat;
    m.robots[i].buffer_energy_j = 0.0;
    slain(&mut m, "red-3");
    assert!(!m.robots[i].alive);
    assert_eq!(m.robots[i].heat, 0.0);
    assert_eq!(m.robots[i].buffer_energy_j, capacity);
}

// Rulebook p.74 worked example: 120 s left and two paid revives already taken -> 10 + 30 + 40 = 80.
#[test]
fn the_free_revive_bar_matches_the_published_formula() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    m.robots[i].instant_revives = 2;
    m.tick = 20 * 300;
    slain(&mut m, "red-3");
    assert_eq!(m.robots[i].respawn_required, 80.0);
    // Each paid revive taken earlier adds its own 20 s to the next bar.
    let j = robot(&m, "red-4");
    m.robots[j].instant_revives = 3;
    slain(&mut m, "red-4");
    assert_eq!(m.robots[j].respawn_required, 100.0);
}

// Rulebook p.74: 1 point per second, 4 while on the supply buff card or with the own base below 4000.
#[test]
fn the_revive_bar_runs_four_times_faster_at_supply_or_below_four_thousand_base_hp() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    slain(&mut m, "red-3");
    m.robots[i].respawn_required = 1000.0;
    m.robots[i].zone_contacts.clear();
    m.bases.get_mut("red").unwrap().hp = 4000.0;
    assert!(!m.contacted_zone_2027(i, "supply", Some("red")));
    m.step(&json!({}));
    assert!((m.robots[i].respawn_progress - 0.05).abs() < 1e-9, "4000 is not below the threshold");
    m.bases.get_mut("red").unwrap().hp = 3999.0;
    m.step(&json!({}));
    let fast = m.robots[i].respawn_progress;
    assert!((fast - 0.25).abs() < 1e-9, "below 4000 the bar runs at 4/s: {fast}");
    m.bases.get_mut("red").unwrap().hp = 10000.0;
    let supply = zone_center(&m, "red_supply");
    place(&mut m, i, supply[0], supply[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    m.step(&json!({}));
    assert!(m.contacted_zone_2027(i, "supply", Some("red")), "the supply card is detected");
    let before = m.robots[i].respawn_progress;
    m.step(&json!({}));
    let fast = m.robots[i].respawn_progress - before;
    assert!((fast - 0.2).abs() < 1e-9, "on the supply card the bar runs at 4/s: {fast}");
}

#[test]
fn lost_base_hp_slows_respawn_and_remote_healing() {
    let mut m = match_();
    assert_eq!(m.logistics("red"), 1.0);
    m.bases.get_mut("red").unwrap().hp = 6000.0;
    assert!((m.logistics("red") - 0.8).abs() < 1e-12);
    let i = robot(&m, "red-3");
    let p = Projectile { id: 1, source: "blue-4".into(), team: "blue".into(), role: "infantry".into(), x: 0.0, y: 0.0, z: 0.0,
        vx: -25.0, vy: 0.0, vz: 0.0, remaining: 1.0, damage: 20.0, radius: 0.0084, deployed: false };
    m.robots[i].hp = 5.0;
    m.tick = 2000; // 100 s: (10 + 10) / 0.8 = 25
    let r = m.robots[i].clone();
    let (origin, _) = physics::muzzle(&r);
    let face = [r.x + r.chassis_size_m[0] / 2.0, r.y, r.z + r.armor_center_z_m];
    let _ = origin;
    m.robot_hit_2027(i, &Projectile { x: face[0] + 0.2, y: face[1], z: face[2], ..p }, face[0], face[1], face[2]);
    assert!(!m.robots[i].alive);
    assert_eq!(m.robots[i].respawn_required, 25.0);
}

#[test]
fn lost_base_hp_scales_bandwidth_and_fpv_pixels_while_the_ui_stays_sharp() {
    let mut m = match_();
    assert_eq!(m.interaction_bandwidth_bytes("red"), 512);
    assert_eq!(m.fpv_video("red")["width"], 1920);
    assert!((m.fpv_video("red")["pixels_scale"].as_f64().unwrap() - 1.0).abs() < 1e-12);
    // λ = 1 − 4000/20000 = 0.8: 512 B → 409 B, and the picture keeps 0.64 × 1920×1080 pixels.
    m.bases.get_mut("red").unwrap().hp = 6000.0;
    assert_eq!(m.interaction_bandwidth_bytes("red"), 409);
    let video = m.fpv_video("red");
    assert_eq!((video["width"].as_u64(), video["height"].as_u64()), (Some(1536), Some(864)));
    assert!((video["pixels_scale"].as_f64().unwrap() - 0.64).abs() < 1e-12);
    let effects = &m.observation("red", Some(&[3]))["logistics_effects"];
    assert_eq!(effects["interaction_bandwidth_bytes"], 409);
    assert!((effects["respawn_time_scale"].as_f64().unwrap() - 1.25).abs() < 1e-12);
    assert!((effects["supply_heal_scale"].as_f64().unwrap() - 0.8).abs() < 1e-12);
    assert_eq!(effects["fpv_resolution"]["width"], 1536);
    // The other team's intact base is unaffected.
    assert_eq!(m.interaction_bandwidth_bytes("blue"), 512);
}

#[test]
fn a_fully_controlled_outpost_heals_at_half_the_supply_rate_and_clears_weakness() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    let spot = zone_center(&m, "red_outpost");
    place(&mut m, i, spot[0], spot[1]);
    m.robots[i].last_shot_time = -1000.0;
    m.robots[i].last_damage_time = -1000.0;
    for _ in 0..4 {
        m.step(&json!({}));
    }
    assert!(m.contacted_zone_2027(i, "outpost", Some("red")));
    let cap = m.robots[i].max_hp;
    m.tick = 20;
    m.robots[i].hp = (cap * 0.25).round();
    let low = m.robots[i].hp;
    m.step(&json!({}));
    let gained = m.robots[i].hp - low;
    let expected = cap * 0.05;
    assert!((gained - expected).abs() <= 2.0, "outpost healed {gained:+}, expected about {expected:+} (half of the supply rate)");
    // The buff card also lifts weakness and the protection that came with it.
    m.robots[i].weak = true;
    m.robots[i].respawned_at = m.time() - 20.0;
    m.robots[i].invulnerable_until = m.time() + 30.0;
    for _ in 0..4 {
        m.step(&json!({}));
    }
    assert!(!m.robots[i].weak);
    assert!(m.robots[i].invulnerable_until <= m.time() + 1e-6);
}

#[test]
fn supercap_drain_includes_the_forty_percent_conversion_loss() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    place(&mut m, i, 5.0, 7.5);
    m.robots[i].supercap_energy_j = 1500.0;
    let order = json!({"red": {"3": {"move": [1.0, 0.0], "heading": 0.0, "power": 200.0}}});
    for _ in 0..10 {
        m.step(&order);
    }
    let limit = m.robots[i].power_limit;
    let e0 = m.robots[i].supercap_energy_j;
    let before = m.robots[i].motor_power_w;
    m.step(&order);
    let drop = e0 - m.robots[i].supercap_energy_j;
    let motor = (before + m.robots[i].motor_power_w) / 2.0;
    let excess = motor - limit;
    assert!(excess > 10.0, "chassis drew only {motor} W against a {limit} W limit");
    let ideal = excess * DT;
    assert!(drop > ideal * 1.25, "supercap dropped {drop} J, near the loss-free {ideal} J");
    assert!(drop < ideal / 0.5, "supercap dropped {drop} J, beyond the 40% loss model of {ideal}");
}

#[test]
fn tunnel_cards_in_order_give_the_through_or_over_buffs() {
    for (cards, buff) in [(vec![0, 1, 2], "tunnel_through"), (vec![2, 0], "tunnel_over")] {
        let mut m = match_();
        let i = robot(&m, "red-4");
        for card in cards {
            let c = zone_center(&m, &format!("red_far_tunnel_{card}"));
            place(&mut m, i, c[0], c[1]);
            m.update_buffs_2027();
            m.tick += 4;
        }
        assert!(m.robots[i].crossing_buffs.contains_key(buff), "{buff}: {:?}", m.robots[i].crossing_buffs);
    }
}

#[test]
fn a_level_order_always_gets_an_answer_so_the_panel_never_looks_dead() {
    // Off the card: the order must be answered, not swallowed.
    let mut m = match_();
    let h = robot(&m, "red-1");
    place(&mut m, h, 3.0, 7.5);
    let command = json!({"red":{"1":{"assembly":1,"brake":true}}});
    let mut refused = Vec::new();
    for _ in 0..6 {
        m.step(&command);
        refused.extend(m.events.iter().filter(|e| e["type"] == "assembly_refused").map(|e| (e["reason"].as_str().unwrap_or("").to_string(), e["difficulty"].as_u64())));
    }
    assert!(refused.iter().any(|(reason, level)| reason == "outside_zone" && *level == Some(1)),
        "an order given off the card has to come back as outside_zone: {refused:?}");
    // Asking again repeats the order but not the explanation: one event per second, not per tick.
    let repeats = refused.iter().filter(|(reason, _)| reason == "outside_zone").count();
    assert!(repeats <= 3, "the refusal flooded the feed: {repeats} events in 6 ticks");

    // On the card with no unit the answer names what is missing, so a pilot knows what to fix.
    let mut m = match_();
    let h = robot(&m, "red-1");
    let frame = m.arena.assembly_frames["red"]["origin"].as_array().unwrap().clone();
    let (ax, ay) = (frame[0].as_f64().unwrap(), frame[1].as_f64().unwrap());
    let z0 = m.arena.assembly_frames["red"]["z0_m"].as_f64().unwrap();
    place(&mut m, h, ax, ay);
    m.robots[h].z = z0;
    m.robots[h].carried_units = 0;
    m.robots[h].heading = 0.0;
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    let mut reasons = Vec::new();
    for _ in 0..12 {
        m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
        reasons.extend(m.events.iter().filter(|e| e["type"] == "assembly_refused").map(|e| e["reason"].as_str().unwrap_or("").to_string()));
    }
    assert!(!reasons.is_empty(), "an order with no unit has to be answered, not ignored");
    assert!(reasons.iter().all(|r| ["no_unit", "outside_zone", "busy", "locked"].contains(&r.as_str())),
        "an unexplained refusal code: {reasons:?}");
}

#[test]
fn mining_one_unit_takes_the_deliberate_slow_arm_time() {
    // The arm works a grab at half rate: the sequence is a docking manoeuvre on a dispenser mouth,
    // not a fast flick. Pin the wall-clock cost so a future rate change has to update this test.
    let mut m = Engine::new_with_lineups(42, 420.0, "rmuc2027", lineup(&[(1, 2711), (3, 2731), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
    let h = robot(&m, "red-1");
    let station = m.arena.field_geometry["fixtures"].as_array().unwrap().iter()
        .find(|f| f["kind"] == "resource" && f["team"] == "red").unwrap().clone();
    let (point, dir) = (&station["pick_point"], &station["pick_dir"]);
    let (dx, dy) = (dir[0].as_f64().unwrap(), dir[1].as_f64().unwrap());
    place(&mut m, h, point[0].as_f64().unwrap() - dx * 0.60, point[1].as_f64().unwrap() - dy * 0.60);
    let r = &mut m.robots[h];
    r.heading = dy.atan2(dx);
    r.aim = r.heading;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    let command = json!({"red":{"1":{"pick_unit":true,"brake":true,"heading":dy.atan2(dx)}}});
    let mut ticks = 0;
    let mut carried = false;
    for _ in 0..600 {
        m.step(&command);
        ticks += 1;
        if m.robots[h].carried_units > 0 {
            carried = true;
            break;
        }
    }
    assert!(carried, "the grab never completed: {:?}", m.robots[h].unit_pickup.as_ref().map(|p| p.stage.clone()));
    let seconds = ticks as f64 * DT;
    // A full grab is four stages held for 0.4 / 0.7 / 0.6 / 0.8 s plus the joint travel. At half
    // rate it measures about 7 s; the previous rate finished the same grab in roughly half that,
    // so a value under 5 s means the arm is running too fast again.
    assert!(seconds > 5.0, "a grab took only {seconds:.2} s");
    assert!(seconds < 15.0, "a grab took {seconds:.2} s, far beyond the slow arm's budget");
}

#[test]
fn timeout_ranks_control_degree_before_damage() {
    let mut m = Engine::new(42, 2.0, "rmuc2027").unwrap();
    m.teams.get_mut("red").unwrap().control = Some(3.0);
    m.teams.get_mut("blue").unwrap().attack_damage = 900.0;
    for _ in 0..40 {
        m.step(&json!({}));
    }
    assert_eq!((m.winner.as_deref(), m.reason.as_deref()), (Some("red"), Some("timeout")));
}

#[test]
fn the_2027_heavy_completes_a_level_one_assembly_physically() {
    let mut m = match_();
    let h = robot(&m, "red-1");
    let zone = m.arena.zones.iter().find(|z| z.id == "red_assembly").unwrap().clone();
    place(&mut m, h, zone.x + 0.14, zone.y + 0.14);
    let r = &mut m.robots[h];
    r.heading = std::f64::consts::FRAC_PI_4;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    r.carried_units = 1;
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    for _ in 0..500 {
        m.step(&json!({"red":{"1":{"assembly":1,"assembly_insert":true,"assembly_confirm":true,"brake":true}}}));
        if ["complete", "failed"].contains(&m.assemblies["red"].stage.as_str()) {
            break;
        }
    }
    assert_eq!(m.assemblies["red"].stage, "complete", "{}", m.assembly_snapshots()["red"]);
    assert_eq!(m.robots[h].level, 5);
    assert_eq!(m.robots[h].max_hp, 450.0);
    assert_eq!(m.teams["red"].tech_income, 40.0);
}

#[test]
fn every_heavy_picks_two_energy_units_at_the_resource_hub() {
    // The arm masts stand at the front corners beside the central gimbal; from the SDK's hub
    // approach (resource_pickup_point) each heavy still reaches the 0.70 m units.
    for model in [2711, 2712, 2713] {
        let mut m = Engine::new_with_lineups(42, 420.0, "rmuc2027", lineup(&[(1, model), (3, 2731), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
        let h = robot(&m, "red-1");
        let station = m.arena.field_geometry["fixtures"].as_array().unwrap().iter()
            .find(|f| f["kind"] == "resource" && f["team"] == "red").unwrap().clone();
        let (point, dir) = (&station["pick_point"], &station["pick_dir"]);
        let (dx, dy) = (dir[0].as_f64().unwrap(), dir[1].as_f64().unwrap());
        let reach = if model == 2712 { 0.48 } else { 0.60 };
        place(&mut m, h, point[0].as_f64().unwrap() - dx * reach, point[1].as_f64().unwrap() - dy * reach);
        let r = &mut m.robots[h];
        r.heading = dy.atan2(dx);
        r.aim = r.heading;
        r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
        m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
        for _ in 0..600 {
            m.step(&json!({"red":{"1":{"pick_unit":true,"brake":true,"heading":dy.atan2(dx)}}}));
            if m.robots[h].carried_units == 2 {
                break;
            }
        }
        assert_eq!(m.robots[h].carried_units, 2, "{model}: {:?}", m.robots[h].unit_pickup.as_ref().map(|p| p.stage.clone()));
        assert_eq!(m.teams["red"].energy_units_left, Some(4), "{model}");
    }
}

#[test]
fn a_heavy_carries_a_third_unit_in_its_gripper() {
    // Two bays fill first; the third unit the rules allow rides clamped in the gripper until a core
    // takes it, so the heavy can shuttle three cores back to back.
    let model = 2711;
    let mut m = Engine::new_with_lineups(42, 420.0, "rmuc2027", lineup(&[(1, model), (3, 2731), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
    let h = robot(&m, "red-1");
    let station = m.arena.field_geometry["fixtures"].as_array().unwrap().iter()
        .find(|f| f["kind"] == "resource" && f["team"] == "red").unwrap().clone();
    let (point, dir) = (&station["pick_point"], &station["pick_dir"]);
    let (dx, dy) = (dir[0].as_f64().unwrap(), dir[1].as_f64().unwrap());
    let reach = if model == 2712 { 0.48 } else { 0.60 };
    place(&mut m, h, point[0].as_f64().unwrap() - dx * reach, point[1].as_f64().unwrap() - dy * reach);
    let r = &mut m.robots[h];
    r.heading = dy.atan2(dx);
    r.aim = r.heading;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    let command = json!({"red":{"1":{"pick_unit":true,"brake":true,"heading":dy.atan2(dx)}}});
    for _ in 0..900 {
        m.step(&command);
        if m.robots[h].units_on_board() == 3 {
            break;
        }
    }
    assert_eq!(m.robots[h].carried_units, 2, "the bays fill before the gripper: {:?}", m.robots[h].unit_pickup.as_ref().map(|p| p.stage.clone()));
    assert!(m.robots[h].gripper_unit(), "the third unit has to stay in the gripper");
    assert_eq!(m.robots[h].unit_pickup.as_ref().map(|p| p.stage.as_str()), Some("carrying"));
    assert!(m.robots[h].unit_pickup.as_ref().unwrap().held);
    assert_eq!(m.robots[h].units_on_board(), 3);
    assert_eq!(m.teams["red"].energy_units_left, Some(3), "the third unit comes out of the team's stock");
    // Carrying across the field must not cancel the third unit, and asking again must not restart it.
    m.robots[h].x += 3.0;
    for _ in 0..20 {
        m.step(&json!({"red":{"1":{"brake":true}}}));
    }
    assert_eq!(m.robots[h].units_on_board(), 3, "a carried unit survives leaving the resource zone");
    m.step(&command);
    assert_eq!(m.robots[h].units_on_board(), 3, "a full robot cannot grab a fourth unit");
}

#[test]
fn a_grab_out_of_the_arms_reach_is_cancelled_instead_of_hanging_at_zero() {
    // The resource zone is 0.55 m around the hub, but the wrist can only arrive from the park
    // point one arm-length out. Standing at the zone edge used to start a grab whose tip never
    // reached the unit: the stage stayed "approaching" at 0 % for as long as the key was held.
    let mut m = Engine::new_with_lineups(42, 420.0, "rmuc2027", lineup(&[(1, 2711), (3, 2731), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
    let h = robot(&m, "red-1");
    let station = m.arena.field_geometry["fixtures"].as_array().unwrap().iter()
        .find(|f| f["kind"] == "resource" && f["team"] == "red").unwrap().clone();
    let (point, dir) = (&station["pick_point"], &station["pick_dir"]);
    let (dx, dy) = (dir[0].as_f64().unwrap(), dir[1].as_f64().unwrap());
    place(&mut m, h, point[0].as_f64().unwrap() - dx * 1.10, point[1].as_f64().unwrap() - dy * 1.10);
    let r = &mut m.robots[h];
    r.heading = dy.atan2(dx);
    r.aim = r.heading;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    let command = json!({"red":{"1":{"pick_unit":true,"brake":true,"heading":dy.atan2(dx)}}});
    m.step(&command);
    assert!(m.robots[h].unit_pickup.is_some(), "the grab starts inside the zone");
    // The arm now works at half rate, so the unreachable-grab window is twice as long: sample past
    // it instead of stopping at the old four-second mark.
    let mut cancelled = false;
    for _ in 0..240 {
        m.step(&command);
        if m.events.iter().any(|e| e["type"] == "unit_pickup_cancelled" && e["reason"] == "unreachable") {
            cancelled = true;
            break;
        }
    }
    assert!(cancelled, "an unreachable grab has to fail, not sit at {:?}", m.robots[h].unit_pickup.as_ref().map(|p| p.stage.clone()));
    assert!(m.robots[h].unit_pickup.is_none());
    assert_eq!(m.robots[h].carried_units, 0);
    assert_eq!(m.teams["red"].energy_units_left, Some(6), "a failed grab must not consume stock");
}

#[test]
fn launcher_fitting_restarts_when_the_heavy_robot_stops_asking_or_leaves_the_workbench() {
    let mut m = match_();
    let h = robot(&m, "red-1");
    m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2, 3];
    m.step(&json!({}));
    let dock = m.arena.zones.iter().find(|z| z.id == "red_workbench").map(|z| [z.x, z.y]).unwrap();
    place(&mut m, h, dock[0], dock[1]);
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    for _ in 0..6 {
        m.step(&json!({"red":{"1":{"install_launcher":true,"brake":true}}}));
    }
    assert_eq!(m.robots[h].launcher, "installing");
    assert!(m.robots[h].launcher_progress > 0.0);
    for _ in 0..4 {
        m.step(&json!({"red":{"1":{"brake":true}}}));
    }
    assert_eq!((m.robots[h].launcher.as_str(), m.robots[h].launcher_progress), ("unlocked", 0.0));
}

#[test]
fn tunnel_passages_match_the_road_area_plan() {
    // Figure 4-35: the near-highland tunnel's 800 mm passage lies between its rail on the
    // central highland's edge and the far tunnel's north rail, which the tunnels share; the
    // road tunnel has only its portal posts, 800 mm apart, over the top of its ramp.
    let m = match_();
    let walls = |id: &str| m.arena.obstacles.iter().filter(|w| w.id == id).cloned().collect::<Vec<_>>();
    let (near, far) = (walls("red_near_tunnel_side"), walls("red_far_tunnel_side"));
    assert_eq!((near.len(), far.len()), (1, 2));
    assert!((far[0].y - (near[0].y + near[0].h) - 0.8).abs() < 0.005, "near passage {}", far[0].y - near[0].y - near[0].h);
    assert!((far[1].y - (far[0].y + far[0].h) - 0.8).abs() < 0.005);
    assert!(walls("red_road_tunnel_side").is_empty());
    let posts = walls("red_road_tunnel_post");
    assert_eq!(posts.len(), 2);
    // Posts run along y (rotated a quarter turn): their centres sit 0.8 + 0.1 m apart.
    let centre = |w: &Wall| w.x + w.w / 2.0;
    assert!(((centre(&posts[1]) - centre(&posts[0])).abs() - 0.9).abs() < 1e-6);
    let ramp = m.arena.terrain.iter().find(|t| t.id == "red_road_tunnel_ramp").unwrap();
    // Heights above the tilted floor (1.5 degrees toward the long sides).
    let above = |y: f64| height_at(&m.arena, 9.32, y) - (15.0 - y) * m.arena.floor_slope_deg.to_radians().tan();
    let (foot, top) = (above(ramp.y + 0.01), above(12.9));
    assert!(foot < 0.01 && (top - 0.2).abs() < 0.01, "ramp {foot} .. {top}");
}

#[test]
fn a_folded_infantry_drives_up_the_road_tunnel_ramp_under_its_portal() {
    let mut m = Engine::new_with_lineups(42, 60.0, "rmuc2027", lineup(&[(1, 2711), (3, 2731), (4, 2732), (6, 2761), (7, 2771)])).unwrap();
    let i = robot(&m, "red-4");
    let x = 9.32;
    place(&mut m, i, x, 11.7);
    let r = &mut m.robots[i];
    r.heading = std::f64::consts::FRAC_PI_2;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    for _ in 0..60 {
        m.step(&json!({"red":{"4":{"transform":true,"heading":std::f64::consts::FRAC_PI_2}}}));
    }
    for _ in 0..400 {
        let r = &m.robots[i];
        m.step(&json!({"red":{"4":{"move":[(x - r.x) * 1.5, 0.6],"heading":std::f64::consts::FRAC_PI_2,"transform":true,"power":120}}}));
        if m.robots[i].y > 13.5 {
            break;
        }
    }
    let r = &m.robots[i];
    assert!(r.y > 13.4 && r.z > 0.15 && r.upright > 0.8 && !r.tipped, "stopped at y {} z {} up {}", r.y, r.z, r.upright);
    assert!(r.crossing_buffs.contains_key("tunnel_through"), "{:?}", r.crossing_buffs);
}

#[test]
fn drones_pushed_into_a_spinning_outpost_stay_outside_its_armor_and_tower() {
    // A drone steered at the outpost's axis, slower than a strike at first and then at full
    // speed, never ends a tick inside an armor module, the collar or the tower.
    let mut m = match_();
    m.tick = 600;
    let a = robot(&m, "blue-6");
    m.step(&json!({"blue":{"6":{"drones":{"1":{"launch":true}}}}}));
    let radius = m.robot_models["2761"]["drone"]["radius_m"].as_f64().unwrap();
    let plate = Engine::structure_plates_2027(&m.outposts["red"], true)[1];
    let n = plate.normal;
    let start = [plate.center[0] + n[0] * 0.4, plate.center[1] + n[1] * 0.4, plate.center[2]];
    m.robots[a].drones[0].state = "flying".into();
    m.robots[a].drones[0].launch_until = 0.0;
    m.physics.as_mut().unwrap().place_drone(a, 0, start, [-n[0] * 0.8, -n[1] * 0.8, 0.0]);
    let mut contacts = 0;
    for tick in 0..150 {
        let (o, d) = (&m.outposts["red"], &m.robots[a].drones[0]);
        let to = [o.x - d.x, o.y - d.y, plate.center[2] - d.z];
        let length = (to[0] * to[0] + to[1] * to[1]).sqrt().max(1e-9);
        let gain = if tick < 60 { 0.2 } else { 1.0 };
        m.step(&json!({"blue":{"6":{"drones":{"1":{"move":[gain * to[0] / length, gain * to[1] / length, to[2]]}}}}}));
        let (o, d) = (&m.outposts["red"], &m.robots[a].drones[0]);
        let p = [d.x, d.y, d.z];
        assert!(structures::outpost_overlap(o, p, radius * 0.98, radius * 0.39).is_none(), "tick {tick}: drone inside the outpost at {p:?}");
        contacts += structures::outpost_overlap(o, p, radius * 1.3, radius * 0.6).is_some() as u32;
    }
    assert!(contacts > 20, "the drone should rest against the outpost, contacts {contacts}");
}

#[test]
fn enemy_fortress_hold_opens_the_base_after_ten_seconds() {
    let mut m = match_();
    // Past the three-minute activation window, red controls blue's outpost position and blue's
    // outpost itself lies destroyed.
    m.tick = 4000;
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    m.outposts.get_mut("blue").unwrap().ever_destroyed = true;
    {
        let p = m.positions.get_mut("blue_outpost").unwrap();
        p.control = "temporary".into();
        p.controller = "red".into();
    }
    let i = robot(&m, "red-3");
    let [x, y] = zone_center(&m, "blue_fortress");
    place(&mut m, i, x, y);
    // Activation needs control of the owner's outpost position; after that the count only needs the
    // pilot to stay on the card. Let one tick elapse so the count has started, then simulate the
    // position lapsing back to blue: the base armor must still open at ten seconds on the card.
    m.update_buffs_2027();
    m.tick += 1;
    m.update_buffs_2027();
    assert!(m.robots[i].fortress_hold > 0.0, "the count did not start");
    m.positions.get_mut("blue_outpost").unwrap().controller = "blue".into();
    for _ in 0..210 {
        m.tick += 1;
        m.update_buffs_2027();
    }
    assert!(m.bases["blue"].armor_open, "hold={}", m.robots[i].fortress_hold);
}

#[test]
fn enemy_fortress_cannot_activate_without_the_owner_outpost_position() {
    let mut m = match_();
    m.tick = 4000;
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    m.outposts.get_mut("blue").unwrap().ever_destroyed = true;
    let i = robot(&m, "red-3");
    let [x, y] = zone_center(&m, "blue_fortress");
    place(&mut m, i, x, y);
    for _ in 0..400 {
        m.tick += 1;
        m.update_buffs_2027();
    }
    assert!(!m.bases["blue"].armor_open);
    assert_eq!(m.robots[i].fortress_hold, 0.0);
}

/// A heavy parked on the plinth with `units` cores, ready for an assembly order.
fn docked_heavy(m: &mut Engine, units: u32) -> usize {
    let h = robot(m, "red-1");
    let zone = m.arena.zones.iter().find(|z| z.id == "red_assembly").unwrap().clone();
    place(m, h, zone.x + 0.14, zone.y + 0.14);
    let r = &mut m.robots[h];
    r.heading = std::f64::consts::FRAC_PI_4;
    r.quaternion = [0.0, 0.0, (r.heading / 2.0).sin(), (r.heading / 2.0).cos()];
    r.carried_units = units;
    m.physics = Some(physics::PhysicsWorld::new(&m.arena, &m.robots, &config().robot_designs));
    h
}
/// Steps until the first unit sits in the core (or the attempt ends).
fn push_first_unit(m: &mut Engine, h: usize) {
    for _ in 0..600 {
        m.step(&json!({"red":{"1":{"assembly_insert":true,"brake":true}}}));
        if m.assemblies["red"].core_units > 0 || m.assemblies["red"].stage == "failed" { break; }
    }
    assert_eq!(m.assemblies["red"].core_units, 1, "{}", m.assembly_snapshots()["red"]);
    assert_eq!(m.robots[h].carried_units, 1, "the unit leaves the bays for the core");
}

#[test]
fn an_inserted_unit_locks_the_2027_assembly_grade_and_pose() {
    // Rule 88: with a unit already in the core the operator may not change the grade, and the core
    // keeps the same pose for that grade until a unit is actually assembled.
    let mut m = match_();
    let h = docked_heavy(&mut m, 2);
    m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
    let (pose, rotation, attempt) = (m.assemblies["red"].pose, m.assemblies["red"].target_rotation, m.assemblies["red"].attempt);
    push_first_unit(&mut m, h);
    for grade in [1, 2] {
        for _ in 0..5 {
            m.step(&json!({"red":{"1":{"assembly":grade,"brake":true}}}));
        }
        assert_eq!(m.assemblies["red"].attempt, attempt, "grade {grade} restarted with a unit in the core");
        assert_eq!(m.assemblies["red"].pose, pose, "the core re-rolled its pose under grade {grade}");
        assert_eq!(m.assemblies["red"].target_rotation, rotation);
        assert_eq!(m.assembly_snapshots()["red"]["difficulty_locked"], true);
        assert_eq!(m.assemblies["red"].stage, "inserting");
    }
}

#[test]
fn pulling_the_unit_back_out_fails_the_attempt_and_returns_it_to_the_robot() {
    // Rule 89: pulling the unit out (a 10 N axial pull at most) makes the core report the loss and
    // the attempt fails on the spot; the unit goes back to the robot.
    let mut m = match_();
    let h = docked_heavy(&mut m, 2);
    m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
    push_first_unit(&mut m, h);
    let mut aligned = false;
    for _ in 0..300 {
        m.step(&json!({"red":{"1":{"assembly_insert":true,"brake":true}}}));
        if m.assembly_snapshots()["red"]["blocked_reason"].is_null() { aligned = true; break; }
    }
    assert!(aligned, "the arm has to reach the unit before it can pull it: {}", m.assembly_snapshots()["red"]["blocked_reason"]);
    m.step(&json!({"red":{"1":{"assembly_extract":true,"brake":true}}}));
    assert_eq!(m.assemblies["red"].stage, "failed");
    assert_eq!(m.assemblies["red"].fail_reason.as_deref(), Some("unit_extracted"));
    assert_eq!(m.assemblies["red"].core_units, 0, "the unit left the core with the robot");
    assert_eq!(m.robots[h].carried_units, 2, "the pulled unit goes back into the empty bay");
    assert!(m.events.iter().any(|e| e["type"] == "assembly_unit_extracted"), "the pull has to be reported");
}

#[test]
fn a_destroyed_heavy_fails_the_attempt_and_the_core_keeps_the_unit_for_fifteen_seconds() {
    // Rule 89: dying during an assembly fails it at once. The operator gets 15 s to clear the core
    // before it salvages the unit, and acknowledging the failure salvages immediately.
    let mut m = match_();
    m.tick = (60.0 / DT) as u64;
    let h = docked_heavy(&mut m, 2);
    let stock = m.teams["red"].energy_units_left.unwrap();
    // The two cores on board came out of the team's stock.
    *m.teams.get_mut("red").unwrap().energy_units_left.as_mut().unwrap() -= 2;
    m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
    push_first_unit(&mut m, h);
    // Own assembly bay protects the heavy robot, so death can only strike once it is off the card
    // (the 15 s grace the rules give it to clear the core).
    // The heavy is only invulnerable on its own supply or assembly card, so the shot lands on the
    // open field (spawn protection has expired by the 60 s mark).
    let mut spot = None;
    for gx in 1..8 {
        for gy in 1..8 {
            let x = m.arena.width * gx as f64 / 8.0 - m.arena.width * 0.5;
            let y = m.arena.height * gy as f64 / 8.0 - m.arena.height * 0.5;
            place(&mut m, h, x, y);
            let free = ["assembly", "supply", "base", "high_point", "central", "fortress"]
                .iter().all(|kind| !m.raw_zone(h, kind, Some("red")));
            if free { spot = Some([x, y]); break; }
        }
        if spot.is_some() { break; }
    }
    let spot = spot.expect("the field has open ground outside every zone");
    place(&mut m, h, spot[0], spot[1]);
    // The assembly bay keeps protecting the heavy for a moment after it is off the card, so drop the
    // stale zone contact, step once for the position-driven buffs, and square the chassis up so the
    // shot lands on its front plate instead of a corner.
    m.robots[h].zone_contacts.clear();
    m.step(&json!({"red":{"1":{"brake":true}}}));
    m.robots[h].heading = 0.0;
    m.robots[h].quaternion = [0.0, 0.0, 0.0, 1.0];
    slain(&mut m, "red-1");
    assert!(!m.robots[h].alive, "the heavy has to die off the assembly card");
    m.step(&json!({"red":{"1":{"brake":true}}}));
    assert_eq!(m.assemblies["red"].stage, "failed");
    assert_eq!(m.assemblies["red"].fail_reason.as_deref(), Some("heavy_destroyed"));
    assert_eq!(m.assemblies["red"].core_units, 1, "the core still holds the unit");
    let wait = m.assemblies["red"].recover_at.unwrap() - m.time();
    assert!((wait - 15.0).abs() < 0.5, "the operator's clearing window is 15 s, got {wait}");
    // The robot cannot come back in 15 s, so the core salvages the unit into the team's stock.
    let mut recovered = false;
    for _ in 0..(17.0 / DT) as usize {
        m.step(&json!({"red":{"1":{"brake":true}}}));
        recovered |= m.events.iter().any(|e| e["type"] == "assembly_units_recovered");
    }
    assert_eq!(m.assemblies["red"].core_units, 0, "a failed attempt never destroys the unit");
    assert_eq!(m.teams["red"].energy_units_left, Some(stock - 1), "the salvaged unit returns to the team's stock");
    assert!(recovered, "the core has to report the salvage");
}

#[test]
fn losing_the_interaction_card_for_fifteen_seconds_ends_the_attempt_and_salvages_at_once() {
    // Rule 88: 15 s without the assembly card ends the attempt and the core salvages the unit right
    // away, so the operator finds the unit back in the team's stock instead of lost in the core.
    let mut m = match_();
    let h = docked_heavy(&mut m, 2);
    let stock = m.teams["red"].energy_units_left.unwrap();
    *m.teams.get_mut("red").unwrap().energy_units_left.as_mut().unwrap() -= 2;
    m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
    push_first_unit(&mut m, h);
    let away = m.arena.zones.iter().find(|z| z.id == "red_resource").map(|z| [z.x, z.y]).unwrap();
    place(&mut m, h, away[0], away[1]);
    for _ in 0..(16.0 / DT) as usize {
        m.step(&json!({"red":{"1":{"brake":true}}}));
        if m.assemblies["red"].stage == "failed" { break; }
    }
    assert_eq!(m.assemblies["red"].stage, "failed");
    assert_eq!(m.assemblies["red"].fail_reason.as_deref(), Some("left_assembly_area"));
    for _ in 0..4 {
        m.step(&json!({"red":{"1":{"brake":true}}}));
    }
    assert_eq!(m.assemblies["red"].core_units, 0);
    assert_eq!(m.teams["red"].energy_units_left, Some(stock - 1));
}

#[test]
fn a_robot_parking_in_the_cores_path_stalls_it_under_an_overload_alarm() {
    // Rule 89: the core has no obstacle sensing. A robot on its path trips an overload alarm, the
    // motion holds, and it resumes once the way is clear.
    let mut m = match_();
    let h = docked_heavy(&mut m, 1);
    let zone = m.arena.zones.iter().find(|z| z.id == "red_assembly").map(|z| [z.x, z.y]).unwrap();
    let walker = robot(&m, "red-3");
    // Park the blocker on the core's own entry point, which is where the core would sweep through.
    let (entry, _, _) = m.core_entry("red").unwrap();
    place(&mut m, walker, entry[0], entry[1]);
    m.robots[walker].alive = true;
    m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
    let start = m.assemblies["red"].current_pose;
    let mut alarm = false;
    for _ in 0..40 {
        m.step(&json!({"red":{"1":{"brake":true}},"red-3":{"brake":true}}));
        alarm |= m.events.iter().any(|e| e["type"] == "core_overload");
    }
    assert!(alarm, "the blocked core has to alarm");
    assert_eq!(m.assemblies["red"].current_pose, start, "a blocked core holds its pose");
    place(&mut m, walker, zone[0] + 4.0, zone[1] + 4.0);
    for _ in 0..40 {
        m.step(&json!({"red":{"1":{"brake":true}}}));
    }
    assert_ne!(m.assemblies["red"].current_pose, start, "the core resumes once the path clears");
    let _ = h;
}

#[test]
fn a_finished_assembly_leaves_the_core_re_initialising_for_five_seconds() {
    // Rule 88: the core has to re-initialise after a successful assembly, so the next order is
    // refused until the countdown runs out and the frame reports how long is left.
    let mut m = match_();
    docked_heavy(&mut m, 2);
    m.step(&json!({"red":{"1":{"assembly":1,"assembly_insert":true,"assembly_confirm":true,"brake":true}}}));
    let mut done = false;
    for _ in 0..1200 {
        m.step(&json!({"red":{"1":{"assembly_insert":true,"assembly_confirm":true,"brake":true}}}));
        if m.assemblies["red"].stage == "complete" { done = true; break; }
    }
    assert!(done, "{}", m.assembly_snapshots()["red"]);
    let finished = m.assemblies["red"].clone();
    let cooldown = m.assembly_snapshots()["red"]["repeat_cooldown_s"].as_f64().unwrap();
    assert!((cooldown - 5.0).abs() < 0.5, "re-initialisation is 5 s, got {cooldown}");
    // Ordering again right away is refused, so the attempt does not restart.
    let mut refused = false;
    for _ in 0..8 {
        m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
        refused |= m.events.iter().any(|e| e["type"] == "assembly_refused");
    }
    assert!(refused, "the core has to answer the order while it re-initialises");
    assert_eq!((m.assemblies["red"].stage.clone(), m.assemblies["red"].attempt), ("complete".into(), finished.attempt));
    assert!(m.assemblies["red"].repeat_ready_at - m.time() > 3.0, "still re-initialising");
    // Once the countdown is spent the core takes the next unit again.
    let mut started = false;
    for _ in 0..(7.0 / DT) as usize {
        m.step(&json!({"red":{"1":{"assembly":1,"brake":true}}}));
        if m.assemblies["red"].stage != "complete" { started = true; break; }
    }
    assert!(started, "cooldown={} snap={}", m.assemblies["red"].repeat_ready_at - m.time(), m.assembly_snapshots()["red"]);
    assert_eq!(m.assemblies["red"].stage, "moving");
    assert!(m.assemblies["red"].attempt > finished.attempt);
    assert_eq!(m.assembly_snapshots()["red"]["repeat_cooldown_s"].as_f64().unwrap(), 0.0);
}
