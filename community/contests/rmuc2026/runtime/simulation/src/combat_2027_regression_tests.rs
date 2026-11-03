//! Regression coverage for 2027 table 5-14 XP attribution and revival timing.
use super::*;

fn match_() -> Engine {
    Engine::new(42, 420.0, "rmuc2027").unwrap()
}
fn robot(m: &Engine, id: &str) -> usize {
    m.robots.iter().position(|r| r.id == id).unwrap()
}
fn before_idle(m: &mut Engine) {
    let actions = (0..m.robots.len())
        .map(|i| m.normalize(&json!({}), i))
        .collect::<Vec<_>>();
    m.before_step_2027(&actions);
}
fn hit_robot(m: &mut Engine, source: &str, role: &str, victim: &str, damage: f64) {
    let i = robot(m, victim);
    m.robots[i].defense_buff = 0.0;
    m.robots[i].vulnerability = 0.0;
    let r = &m.robots[i];
    let point = [
        r.x + r.chassis_size_m[0] / 2.0,
        r.y,
        r.z + r.armor_center_z_m,
    ];
    let normal = physics::armor_normal(r, physics::face_at_point(r, point));
    let p = Projectile {
        id: 1,
        source: source.into(),
        team: "red".into(),
        role: role.into(),
        x: point[0],
        y: point[1],
        z: point[2],
        vx: -25.0 * normal.x,
        vy: -25.0 * normal.y,
        vz: -25.0 * normal.z,
        remaining: 1.0,
        damage,
        radius: 0.0084,
        deployed: false,
    };
    m.robot_hit_2027(i, &p, point[0], point[1], point[2]);
}
fn red_xp(m: &Engine) -> (f64, f64) {
    (
        m.robots[robot(m, "red-3")].experience,
        m.robots[robot(m, "red-4")].experience,
    )
}

/// A 42 mm round aimed at a world point has to leave the muzzle on the solved flat arc and come down
/// where the crosshair sat: the drop is what the cockpit asks for, not a high lob.
#[test]
fn lobbed_42mm_follows_the_solved_flat_arc() {
    let mut m = match_();
    let i = robot(&m, "red-1");
    assert_eq!(m.robots[i].role, "heavy");
    // This test is about the ballistics; the rules' 42 mm unlock is assumed done.
    m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2, 3];
    {
        let r = &mut m.robots[i];
        r.launcher = "installed".into();
        r.alive = true;
        r.ammo = 20;
        r.cooldown = 0.0;
        r.heat = 0.0;
        r.x = 14.0;
        r.y = 7.5;
        r.z = height_at(&m.arena, 14.0, 7.5) + 0.02;
        r.heading = 0.0;
        r.aim = 0.0;
        r.pitch = 0.0;
        r.vx = 0.0;
        r.vy = 0.0;
        r.vz = 0.0;
    }
    // The cockpit solves this for a point twelve metres out: 0.21 rad is the flat-arc elevation.
    let pitch = 0.21;
    let idle = json!({"red":{"1":{"fire":false,"aim":0.0,"pitch":pitch,"move":[0.0,0.0]}}});
    for _ in 0..120 {
        m.advance(&idle);
    }
    let (_, barrel) = physics::muzzle(&m.robots[i]);
    let elevation = barrel[2].atan2(barrel[0].hypot(barrel[1])).to_degrees();
    println!(
        "settled local {:.2} deg, barrel {:.2} deg, z {:.2}, v ({:.2},{:.2},{:.2})",
        m.robots[i].pitch.to_degrees(),
        elevation,
        m.robots[i].z,
        m.robots[i].vx,
        m.robots[i].vy,
        m.robots[i].vz
    );
    let commands = json!({"red":{"1":{"fire":true,"aim":0.0,"pitch":pitch,"move":[0.0,0.0]}}});
    let mut shot = None;
    for _ in 0..200 {
        m.advance(&commands);
        if let Some(p) = m.projectiles.iter().find(|p| p.source == "red-1") {
            shot = Some((p.x, p.y, p.z, p.vx, p.vy, p.vz));
            break;
        }
    }
    let (x0, y0, z0, vx0, vy0, vz0) = shot.expect("the heavy launcher fired");
    println!(
        "muzzle v ({:.2},{:.2},{:.2}) from ({:.2},{:.2},{:.2})",
        vx0, vy0, vz0, x0, y0, z0
    );
    let mut last = (x0, y0, z0);
    let mut apex = z0;
    for _ in 0..400 {
        m.advance(&commands);
        if let Some(p) = m.projectiles.iter().find(|p| p.source == "red-1") {
            last = (p.x, p.y, p.z);
            apex = apex.max(p.z);
        } else {
            break;
        }
    }
    println!(
        "apex {:.2} m, landing ({:.2},{:.2},{:.2}), range {:.2} m",
        apex,
        last.0,
        last.1,
        last.2,
        (last.0 - 14.0).hypot(last.1 - 7.5)
    );
    assert!(
        (elevation - 12.03).abs() < 1.0,
        "the barrel has to sit on the solved flat arc, not somewhere else: {elevation}"
    );
    assert!(apex > z0 && apex < 3.0, "a flat arc only rises a little: {apex}");
    assert!(
        ((last.0 - 14.0).hypot(last.1 - 7.5) - 12.0).abs() < 1.5,
        "the shell has to come down on the point it was solved for"
    );
}

#[test]
fn sentry_damage_and_kill_share_xp_with_living_infantry() {
    let mut m = match_();
    let victim = robot(&m, "blue-3");
    m.robots[victim].hp = 20.0;
    hit_robot(&mut m, "red-7", "sentry", "blue-3", 20.0);
    assert!(!m.robots[victim].alive);
    assert_eq!(red_xp(&m), (65.0, 65.0)); // (20*4 + 50)/2
}

#[test]
fn unknown_and_dead_sources_share_projectile_damage_xp() {
    for source in ["unidentified", "red-3"] {
        let mut m = match_();
        let a = robot(&m, "red-3");
        if source == "red-3" {
            m.robots[a].alive = false;
        }
        hit_robot(&mut m, source, "infantry", "blue-3", 20.0);
        assert_eq!(
            red_xp(&m),
            if source == "red-3" {
                (0.0, 80.0)
            } else {
                (40.0, 40.0)
            }
        );
    }
}

#[test]
fn capped_source_and_dead_recipients_do_not_swallow_team_xp() {
    let mut m = match_();
    let a = robot(&m, "red-3");
    m.robots[a].level = m.teams["red"].level_cap;
    m.robots[a].experience = 2200.0;
    hit_robot(&mut m, "red-3", "infantry", "blue-3", 20.0);
    assert_eq!(red_xp(&m), (2200.0, 80.0));
    let mut m = match_();
    let a = robot(&m, "red-3");
    m.robots[a].alive = false;
    hit_robot(&mut m, "red-7", "sentry", "blue-3", 20.0);
    assert_eq!(red_xp(&m), (0.0, 80.0));
}

#[test]
fn forty_two_mm_xp_is_not_shared_with_seventeen_mm_infantry() {
    let mut m = match_();
    let h = robot(&m, "red-1");
    m.robots[h].last_shot_time = m.time();
    hit_robot(&mut m, "red-1", "heavy", "blue-3", 20.0);
    assert_eq!(red_xp(&m), (0.0, 0.0));
}

#[test]
fn fallback_kill_level_comes_from_average_experience() {
    let mut m = match_();
    m.teams.get_mut("red").unwrap().level_cap = 10;
    let a = robot(&m, "red-3");
    let b = robot(&m, "red-4");
    m.robots[a].experience = 1250.0;
    m.robots[a].level = 3;
    m.robots[b].experience = 4000.0;
    m.robots[b].level = 8;
    m.award_kill_xp_2027(None, "red", 9, Some("17mm"));
    // Mean XP 2625 -> level 5; 50*9*(1+(9-5)*0.2)/2 = 405.
    assert_eq!(red_xp(&m), (1655.0, 4405.0));
}

#[test]
fn base_odd_damage_experience_rounds_up_before_attribution() {
    let mut m = match_();
    m.positions.get_mut("blue_outpost").unwrap().controller = "red".into();
    m.teams.get_mut("blue").unwrap().tech_defense = 0.25;
    let plate = Engine::structure_plates_2027(&m.bases["blue"], false)[0].clone();
    let p = Projectile {
        id: 1,
        source: "red-3".into(),
        team: "red".into(),
        role: "infantry".into(),
        x: plate.center[0],
        y: plate.center[1],
        z: plate.center[2],
        vx: -25.0 * plate.normal[0],
        vy: -25.0 * plate.normal[1],
        vz: -25.0 * plate.normal[2],
        remaining: 1.0,
        damage: 20.0,
        radius: 0.0084,
        deployed: false,
    };
    let before = m.bases["blue"].hp;
    m.structure_hit_2027(
        "blue",
        false,
        &p,
        plate.center[0],
        plate.center[1],
        plate.center[2],
    );
    assert_eq!(before - m.bases["blue"].hp, 15.0);
    assert_eq!(red_xp(&m), (8.0, 0.0));
}

#[test]
fn drone_impact_experience_is_shared_with_living_infantry() {
    let mut m = match_();
    let i = robot(&m, "red-6");
    let plate = Engine::structure_plates_2027(&m.outposts["blue"], true)[0].clone();
    let d = &mut m.robots[i].drones[0];
    d.state = "flying".into();
    d.x = plate.center[0] + 0.03 * plate.normal[0];
    d.y = plate.center[1] + 0.03 * plate.normal[1];
    d.z = plate.center[2] + 0.03 * plate.normal[2];
    d.vx = -3.0 * plate.normal[0];
    d.vy = -3.0 * plate.normal[1];
    d.vz = -3.0 * plate.normal[2];
    let before = m.outposts["blue"].hp;
    m.drone_strikes();
    assert_eq!(before - m.outposts["blue"].hp, 200.0);
    assert_eq!(red_xp(&m), (200.0, 200.0));
}

#[test]
fn referee_damage_and_death_share_xp_only_in_2027() {
    for (map, gets_xp) in [("rmuc2027", true), ("rmuc2026", false)] {
        let mut m = Engine::new(42, 420.0, map).unwrap();
        let i = robot(&m, "blue-3");
        let hp = m.robots[i].hp;
        m.process_referee_commands(Some(&json!([{"kind":"red", "robot":"blue-3"}])));
        assert!(!m.robots[i].alive);
        let expected = if gets_xp { hp * 4.0 / 2.0 + 25.0 } else { 0.0 };
        assert_eq!(red_xp(&m), (expected, expected));
    }
}

#[test]
fn instant_revive_weakness_expires_without_a_card() {
    let mut m = match_();
    m.arena.zones.clear();
    let i = robot(&m, "red-3");
    m.respawn_2027(i, true);
    m.tick = 59;
    before_idle(&mut m);
    assert!(m.robots[i].weak);
    m.tick = 60;
    before_idle(&mut m);
    assert!(!m.robots[i].weak);
    assert_eq!(m.robots[i].weak_until, None);
}

#[test]
fn normal_revive_still_requires_a_card_to_clear_weakness() {
    let mut m = match_();
    m.arena.zones.clear();
    let i = robot(&m, "red-3");
    m.respawn_2027(i, false);
    m.tick = 620;
    before_idle(&mut m);
    assert!(m.robots[i].weak);
    assert_eq!(m.robots[i].weak_until, None);
}

#[test]
fn supply_healing_and_fast_revive_use_delayed_card_contact() {
    let mut m = match_();
    let i = robot(&m, "red-3");
    m.robots[i].x = 20.0;
    m.robots[i].y = 7.5;
    m.arena
        .zones
        .iter_mut()
        .filter(|z| z.kind == "supply" && z.owner.as_deref() == Some("red"))
        .for_each(|z| {
            m.robots[i].zone_contacts.insert(z.id.clone(), 0.0);
        });
    m.robots[i].hp = 1.0;
    m.tick = 20;
    before_idle(&mut m);
    assert_eq!(m.robots[i].hp, 1.0 + (m.robots[i].max_hp * 0.1).round());
    let remaining_hp = m.robots[i].hp;
    hit_robot(&mut m, "unidentified", "infantry", "red-3", remaining_hp);
    assert!(!m.robots[i].alive);
    m.robots[i].respawn_required = 1000.0;
    m.robots[i].respawn_progress = 0.0;
    before_idle(&mut m);
    assert!((m.robots[i].respawn_progress - 0.2).abs() < 1e-8);
    m.tick = 41;
    m.robots[i].respawn_progress = 0.0;
    before_idle(&mut m);
    assert!((m.robots[i].respawn_progress - 0.05).abs() < 1e-8);
}
