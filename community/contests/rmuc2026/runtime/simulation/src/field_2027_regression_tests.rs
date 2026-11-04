//! Rule regressions for the 2027 field, RFID contacts and position control.
use super::*;

fn arena() -> Engine {
    Engine::new(42, 420.0, "rmuc2027").unwrap()
}

fn robot(m: &Engine, id: &str) -> usize {
    m.robots.iter().position(|r| r.id == id).unwrap()
}

fn card(m: &Engine, id: &str) -> Zone {
    m.arena.zones.iter().find(|z| z.id == id).unwrap().clone()
}

fn place_on_card(m: &mut Engine, i: usize, id: &str) {
    let z = card(m, id);
    let point = if z.polygon.is_empty() || point_in_polygon(z.x, z.y, &z.polygon) {
        [z.x, z.y]
    } else {
        let count = z.polygon.len() as f64;
        [z.polygon.iter().map(|p| p[0]).sum::<f64>() / count,
         z.polygon.iter().map(|p| p[1]).sum::<f64>() / count]
    };
    let surface = z.card_z_m.unwrap_or_else(|| height_at(&m.arena, point[0], point[1]));
    let r = &mut m.robots[i];
    r.x = point[0];
    r.y = point[1];
    r.z = surface;
    r.quaternion = [0.0, 0.0, 0.0, 1.0];
}

#[test]
fn a_heavy_standing_in_its_assembly_bay_is_invulnerable() {
    let mut m = arena();
    let heavy = robot(&m, "red-1");
    assert_eq!(m.robots[heavy].role, "heavy");
    place_on_card(&mut m, heavy, "red_assembly");
    m.update_buffs_2027();
    assert!(m.robot_invulnerable_2027(heavy), "the bay itself protects the heavy");
    // Protection is positional, not a budget: a heavy that already burned the old protection time
    // stays safe, and an assembly does not have to be running.
    m.robots[heavy].assembly_invulnerable_spent = 999.0;
    m.update_buffs_2027();
    assert!(m.robot_invulnerable_2027(heavy));
    // Driving out of the bay ends it, and the enemy bay never protects.
    place_on_card(&mut m, heavy, "blue_assembly");
    m.robots[heavy].zone_contacts.clear();
    m.tick += 400;
    m.update_buffs_2027();
    assert!(!m.robot_invulnerable_2027(heavy));
}

#[test]
fn temporary_position_progress_pauses_when_both_teams_activate() {
    let mut m = arena();
    let blue = robot(&m, "blue-3");
    let red = robot(&m, "red-3");
    place_on_card(&mut m, blue, "red_outpost");
    place_on_card(&mut m, red, "red_central");
    let p = m.positions.get_mut("red_outpost").unwrap();
    p.control = "temporary".into();
    p.controller = "blue".into();
    p.progress = 2.0;
    m.update_buffs_2027();
    assert_eq!(m.positions["red_outpost"].active.len(), 2);
    assert_eq!(m.positions["red_outpost"].progress, 2.0);
}

#[test]
fn rebuilt_outpost_inherits_its_current_owners_buffs() {
    let mut m = arena();
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.set_position("red_outpost", "full", "blue");
    assert_eq!(m.outposts["red"].owner.as_deref(), Some("blue"));
    let rune = m.runes.get_mut("blue").unwrap();
    rune.status = "active".into();
    rune.defense = 0.5;
    assert_eq!(m.structure_defense("red", true), 0.5);
    m.runes.get_mut("blue").unwrap().status = "idle".into();
    m.teams.get_mut("blue").unwrap().tech_defense = 0.25;
    m.teams.get_mut("red").unwrap().tech_defense = 0.5;
    assert_eq!(m.structure_defense("red", true), 0.25);
}

#[test]
fn tunnel_time_window_covers_the_whole_crossing() {
    let mut m = arena();
    let i = robot(&m, "red-3");
    for (tick, id) in [(0, "red_road_tunnel_0"), (50, "red_road_tunnel_1"), (100, "red_road_tunnel_2")] {
        m.tick = tick;
        place_on_card(&mut m, i, id);
        m.update_buffs_2027();
    }
    assert!(!m.robots[i].crossing_buffs.contains_key("tunnel_through"));
    assert!(!m.robots[i].crossing_buffs.contains_key("tunnel_over"));
}

#[test]
fn another_crossing_card_interrupts_the_road_on_the_default_map() {
    let mut m = arena();
    let i = robot(&m, "red-3");
    for (tick, id) in [(0, "red_road_0"), (20, "red_road_tunnel_0"), (40, "red_road_1")] {
        m.tick = tick;
        place_on_card(&mut m, i, id);
        m.update_buffs_2027();
    }
    assert!(!m.robots[i].crossing_buffs.contains_key("road"));
}

#[test]
fn tunnel_floor_contact_and_roof_crossing_give_different_buffs() {
    for through in [true, false] {
        let mut m = arena();
        let i = robot(&m, "red-3");
        for (tick, id) in [(0, "red_road_tunnel_0"), (20, "red_road_tunnel_1"), (40, "red_road_tunnel_2")] {
            m.tick = tick;
            place_on_card(&mut m, i, id);
            if id.ends_with("_1") && !through {
                let roof = m.arena.obstacles.iter().find(|w| w.id == "red_road_tunnel_roof").unwrap();
                m.robots[i].z = roof.bottom + roof.height;
                assert!(!m.raw_zone_2027(i, "tunnel", None));
            }
            m.update_buffs_2027();
        }
        assert_eq!(m.robots[i].crossing_buffs.contains_key("tunnel_through"), through);
        assert_eq!(m.robots[i].crossing_buffs.contains_key("tunnel_over"), !through);
    }
}

#[test]
fn rfid_detection_uses_the_construction_manual_range() {
    let mut m = arena();
    let i = robot(&m, "red-4");
    // The far tunnel's inner card has an explicit floor plane under its metal plate (the
    // road tunnel's inner card lies on the ramp and follows it).
    place_on_card(&mut m, i, "red_far_tunnel_1");
    let z = card(&m, "red_far_tunnel_1");
    let mount = n(&m.robot_models[&m.robots[i].design_slot().to_string()], "ground_clearance_m");
    m.robots[i].z = z.card_z_m.unwrap() - mount + 0.105;
    assert!(m.card_contact_2027(i, &z));
    m.robots[i].z += 0.0001;
    assert!(!m.card_contact_2027(i, &z));
}

#[test]
fn supply_invulnerability_keeps_the_two_second_release_delay() {
    let mut m = arena();
    let i = robot(&m, "red-1");
    place_on_card(&mut m, i, "red_supply");
    m.update_buffs_2027();
    assert!(m.robot_invulnerable_2027(i));
    m.robots[i].x = 5.0;
    m.robots[i].y = 7.5;
    m.robots[i].z = height_at(&m.arena, 5.0, 7.5);
    m.tick = 40;
    m.update_buffs_2027();
    assert!(m.contacted_zone_2027(i, "supply", Some("red")));
    assert!(m.robot_invulnerable_2027(i));
    m.tick = 41;
    m.update_buffs_2027();
    assert!(!m.contacted_zone_2027(i, "supply", Some("red")));
    assert!(!m.robot_invulnerable_2027(i));
}

#[test]
fn destroyed_robots_keep_supply_contact_without_capturing_a_position() {
    let mut m = arena();
    let i = robot(&m, "red-3");
    place_on_card(&mut m, i, "red_supply");
    m.robots[i].alive = false;
    m.tick = 100;
    m.update_buffs_2027();
    assert!(m.contacted_zone_2027(i, "supply", Some("red")));
    assert!(!m.zones["red_supply"].occupants.contains(&m.robots[i].id));
}

#[test]
fn a_card_cannot_end_the_three_second_instant_respawn_weakness_early() {
    let mut m = arena();
    let i = robot(&m, "red-3");
    place_on_card(&mut m, i, "red_base");
    m.robots[i].weak = true;
    m.robots[i].weak_until = Some(3.0);
    m.tick = 20;
    m.update_buffs_2027();
    assert!(m.robots[i].weak);
}
