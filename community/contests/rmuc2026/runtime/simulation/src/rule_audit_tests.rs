use super::*;

fn match_() -> Engine {
    Engine::new(42, 420.0, "rmuc2026").unwrap()
}
fn zone(m: &mut Engine, i: usize, id: &str) {
    let z = m.arena.zones.iter().find(|z| z.id == id).unwrap();
    m.robots[i].x = z.x;
    m.robots[i].y = z.y;
}

#[test]
fn fortress_reserve_capacity_tracks_current_base_hp_without_refilling_on_reentry() {
    let mut m = match_();
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.bases.get_mut("red").unwrap().hp = 2000.0;
    zone(&mut m, 3, "red_fortress");
    m.update_rulebook_buffs();
    assert_eq!(m.teams["red"].fortress_ammo, 500);
    m.teams.get_mut("red").unwrap().fortress_ammo = 300;
    m.bases.get_mut("red").unwrap().hp = 5000.0;
    m.update_rulebook_buffs();
    assert_eq!(m.teams["red"].fortress_capacity, 100);
    assert_eq!(m.teams["red"].fortress_ammo, 100);
    m.teams.get_mut("red").unwrap().fortress_ammo = 20;
    m.robots[3].x = 5.0;
    m.robots[3].y = 5.0;
    m.tick += 50;
    m.update_rulebook_buffs();
    zone(&mut m, 3, "red_fortress");
    m.update_rulebook_buffs();
    assert_eq!(m.teams["red"].fortress_ammo, 20);
}

#[test]
fn permanent_heat_lock_survives_natural_and_paid_revival() {
    for paid in [false, true] {
        let mut m = match_();
        m.robots[0].heat_permanent_lock = true;
        m.robots[0].alive = false;
        m.robots[0].hp = 0.0;
        m.respawn(0, paid);
        assert!(m.robots[0].heat_permanent_lock);
        m.robots[0].ammo = 100;
        let mut a = vec![Action::default(); 12];
        a[0].fire = true;
        m.rulebook_fire(&a);
        assert!(m.projectiles.is_empty());
    }
}

#[test]
fn interrupted_engineer_can_finish_during_the_fifteen_second_buffer() {
    let mut m = match_();
    m.tick = 4000;
    zone(&mut m, 1, "red_assembly");
    m.robots[1].carried_units = 1;
    let s = m.assemblies.get_mut("red").unwrap();
    s.difficulty = 1;
    s.stage = "buffered".into();
    s.ready_at = 200.0;
    s.inserted = 0.1;
    zone(&mut m, 7, "blue_assembly");
    m.robots[7].carried_units = 2;
    m.teams.get_mut("blue").unwrap().completed_difficulties = vec![1, 2, 3];
    m.assemblies.get_mut("red").unwrap().stage = "inserting".into();
    m.assembly_rpc(7, &json!({"assembly":4}));
    assert_eq!(m.assemblies["red"].stage, "buffered");
    for _ in 0..4 {
        super::mechanism_tests::align_engineer(&mut m, 1);
        m.assembly_rpc(1, &json!({"assembly_confirm":true}));
        m.tick += 2;
    }
    assert_eq!(m.assemblies["red"].stage, "complete");
    assert_eq!(m.teams["red"].tech_income, 50.0);
}

#[test]
fn actual_overspeed_is_detected_by_the_launcher_sensor() {
    let mut m = match_();
    m.specs.get_mut("infantry").unwrap().projectile_speed = 30.0;
    m.robots[3].ammo = 100;
    let mut actions = vec![Action::default(); 12];
    actions[3].fire = true;
    m.rulebook_fire(&actions);
    assert_eq!(m.snapshot()["robots"][3]["overspeed_locked"], true);
    assert!(m.events.iter().any(|e| e["type"] == "overspeed"));
}

#[test]
fn yellow_card_deducts_direct_hp_and_credits_damage_without_experience() {
    let mut m = match_();
    let hp = m.robots[3].max_hp;
    m.robots[3].invulnerable_until = 100.0;
    m.step(&json!({"referee":[{"kind":"yellow","robot":"red-4","reason":"test"}]}));
    assert_eq!(m.snapshot()["robots"][3]["yellow_cards"], 1);
    assert_eq!(m.robots[3].hp, hp - (hp * 0.15).round());
    assert!(m.teams["blue"].attack_damage > 0.0);
    assert!(m
        .robots
        .iter()
        .filter(|r| r.team == "blue")
        .all(|r| r.experience == 0.0));
}

#[test]
fn enemy_fortress_requires_three_minutes_and_destroyed_enemy_outpost_and_twenty_seconds() {
    for (team, i, enemy) in [("red", 3, "blue"), ("blue", 9, "red")] {
        let mut m = match_();
        m.tick = 3599;
        zone(&mut m, i, &format!("{enemy}_fortress"));
        m.outposts.get_mut(enemy).unwrap().hp = 0.0;
        m.outposts.get_mut(enemy).unwrap().ever_destroyed = true;
        m.update_rulebook_buffs();
        assert_eq!(m.robots[i].fortress_hold, 0.0);
        for _ in 0..400 {
            m.tick += 1;
            m.update_rulebook_buffs();
        }
        assert!(!m.bases[enemy].armor_open);
        assert_eq!(m.robots[i].vulnerability, 1.0);
        m.tick += 1;
        m.update_rulebook_buffs();
        assert!(m.bases[enemy].armor_open, "{team} occupation");
        assert_eq!(
            m.events
                .iter()
                .filter(|e| e["type"] == "base_armor_opened")
                .count(),
            1
        );
        m.outposts.get_mut(enemy).unwrap().hp = 750.0;
        m.bases.get_mut(enemy).unwrap().hp = 5000.0;
        m.tick += 1;
        m.update_rulebook_buffs();
        assert!(m.bases[enemy].armor_open);
        assert_eq!(m.robots[i].vulnerability, 0.0);
    }
}

#[test]
fn fortress_does_not_open_the_base_before_enemy_outpost_destruction() {
    let mut m = match_();
    m.tick = 4000;
    zone(&mut m, 3, "blue_fortress");
    for _ in 0..450 {
        m.tick += 1;
        m.update_rulebook_buffs();
    }
    assert!(!m.bases["blue"].armor_open);
    assert_eq!(m.robots[3].fortress_hold, 0.0);
}

#[test]
fn fortress_is_single_occupant_for_defenders_but_allows_multiple_attackers() {
    let mut m = match_();
    m.tick = 4000;
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    m.outposts.get_mut("blue").unwrap().ever_destroyed = true;
    for i in [3, 5, 9, 11] {
        zone(&mut m, i, "blue_fortress");
    }
    m.update_rulebook_buffs();
    let occupants = &m.zones["blue_fortress"].occupants;
    assert!(occupants.contains(&"blue-4".into()));
    assert!(!occupants.contains(&"blue-7".into()));
    assert!(occupants.contains(&"red-4".into()) && occupants.contains(&"red-7".into()));
    assert_eq!(m.robots[3].vulnerability, 1.0);
    assert_eq!(m.robots[9].defense_buff, 0.5);
}

#[test]
fn fortress_pause_survives_three_seconds_but_not_reentry_on_the_next_frame() {
    for pause in [3.0, 3.05] {
        let mut m = match_();
        m.tick = 4000;
        m.outposts.get_mut("blue").unwrap().hp = 0.0;
        m.outposts.get_mut("blue").unwrap().ever_destroyed = true;
        zone(&mut m, 3, "blue_fortress");
        m.update_rulebook_buffs();
        m.robots[3].fortress_hold = 5.0;
        m.robots[3].alive = false;
        m.robots[3].hp = 0.0;
        let start = m.tick;
        for _ in 0..60 {
            m.tick += 1;
            m.update_rulebook_buffs();
        }
        m.tick = start + (pause / DT).round() as u64;
        m.robots[3].alive = true;
        m.robots[3].hp = m.robots[3].max_hp;
        m.update_rulebook_buffs();
        let expected = if pause == 3.0 { 5.0 } else { 0.0 };
        assert!(
            (m.robots[3].fortress_hold - expected).abs() < 1e-8,
            "pause={pause}"
        );
    }
}

#[test]
fn own_fortress_works_after_reconstruction_and_uses_reserve_before_personal_ammo() {
    let mut m = match_();
    zone(&mut m, 3, "red_fortress");
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.outposts.get_mut("red").unwrap().hp = 750.0;
    m.bases.get_mut("red").unwrap().hp = 3000.0;
    m.update_rulebook_buffs();
    assert_eq!(m.robots[3].cooling_bonus, 50.0);
    m.robots[3].ammo = 10;
    let reserve = m.teams["red"].fortress_ammo;
    let mut a = vec![Action::default(); 12];
    a[3].fire = true;
    m.rulebook_fire(&a);
    assert_eq!(m.robots[3].ammo, 10);
    assert_eq!(m.teams["red"].fortress_ammo, reserve - 1);
}

#[test]
fn overspeed_table_boundaries_and_undefined_42mm_band_are_explicit() {
    let m = match_();
    for (speed, lock) in [
        (25.0, None),
        (25.000001, Some(15.0)),
        (29.999999, Some(15.0)),
        (30.0, Some(20.0)),
        (34.999999, Some(20.0)),
        (35.0, Some(f64::INFINITY)),
    ] {
        assert_eq!(m.overspeed_penalty(false, speed, 25.0), Ok(lock));
    }
    for (speed, lock) in [
        (12.0, None),
        (12.01, Some(15.0)),
        (13.2, Some(15.0)),
        (13.200001, Some(20.0)),
        (14.4, Some(20.0)),
        (18.000001, Some(f64::INFINITY)),
    ] {
        assert_eq!(m.overspeed_penalty(true, speed, 12.0), Ok(lock));
    }
    for speed in [14.400001, 17.0, 18.0] {
        assert!(m.overspeed_penalty(true, speed, 12.0).is_err());
    }
    assert_eq!(m.overspeed_penalty(true, 18.0, 16.5), Ok(Some(15.0)));
    assert_eq!(
        m.overspeed_penalty(true, 18.000001, 16.5),
        Ok(Some(f64::INFINITY))
    );
}

#[test]
fn overspeed_and_heat_locks_are_independent_and_do_not_disappear_on_revive() {
    let mut m = match_();
    m.specs.get_mut("infantry").unwrap().projectile_speed = 30.0;
    m.robots[3].ammo = 20;
    let mut a = vec![Action::default(); 12];
    a[3].fire = true;
    m.rulebook_fire(&a);
    assert_eq!(m.robots[3].overspeed_lock_until, 20.0);
    m.robots[3].alive = false;
    m.respawn(3, true);
    m.robots[3].cooldown = 0.0;
    m.robots[3].overheated = false;
    m.rulebook_fire(&a);
    assert_eq!(m.projectiles.len(), 1);
    m.specs.get_mut("infantry").unwrap().projectile_speed = 25.0;
    m.tick = 400;
    m.robots[3].overheated = true;
    m.update_referee_locks();
    assert!(!m.robots[3].overspeed_locked);
    m.rulebook_fire(&a);
    assert_eq!(m.projectiles.len(), 1);
    m.robots[3].overheated = false;
    m.rulebook_fire(&a);
    assert_eq!(m.projectiles.len(), 2);
    m.record_overspeed(3, 35.0, f64::INFINITY);
    m.respawn(3, true);
    assert!(m.robots[3].overspeed_permanent_lock);
}

#[test]
fn inherited_world_velocity_does_not_trigger_overspeed_and_invalid_hardware_fails_closed() {
    let mut m = match_();
    m.robots[3].ammo = 5;
    m.robots[3].vx = 20.0;
    let mut a = vec![Action::default(); 12];
    a[3].fire = true;
    m.rulebook_fire(&a);
    assert!(!m.robots[3].overspeed_locked);
    assert!(!m.events.iter().any(|e| e["type"] == "overspeed"));
    let mut m = match_();
    m.robots[0].ammo = 5;
    m.specs.get_mut("hero").unwrap().projectile_speed = 17.0;
    let mut a = vec![Action::default(); 12];
    a[0].fire = true;
    m.rulebook_fire(&a);
    assert!(m.projectiles.is_empty());
    assert_eq!(m.robots[0].ammo, 5);
    assert!(m.robots[0].launcher_fault);
}

fn card(m: &mut Engine, kind: &str, id: Option<&str>) {
    let mut c = json!({"kind":kind});
    if let Some(id) = id {
        c["robot"] = json!(id);
    }
    m.process_referee_commands(Some(&json!([c])));
}

#[test]
fn yellow_repetition_uses_the_thirty_second_boundary_and_escalates_to_red() {
    for second in [29.95, 30.0] {
        let mut m = match_();
        card(&mut m, "yellow", Some("red-4"));
        m.tick = (second / DT).round() as u64;
        card(&mut m, "yellow", Some("red-4"));
        assert_eq!(
            m.robots[3].yellow_damage_ratio,
            if second < 30.0 { 0.30 } else { 0.15 }
        );
        assert!(!m.robots[3].red_card);
        card(&mut m, "yellow", Some("red-4"));
        assert!(m.robots[3].red_card);
        assert_eq!(m.robots[3].hp, 0.0);
        assert!(!m.robots[3].alive);
        let mut m = match_();
        card(&mut m, "yellow", Some("red-2"));
        card(&mut m, "yellow", Some("red-2"));
        assert!(m.robots[1].red_card);
    }
}

#[test]
fn yellow_damage_keeps_one_hp_and_does_not_resurrect_dead_teammates() {
    let mut m = match_();
    m.robots[3].hp = 2.0;
    m.robots[3].shield = 100.0;
    m.robots[0].alive = false;
    m.robots[0].hp = 0.0;
    let kills = m.teams["blue"].kills;
    card(&mut m, "yellow", Some("red-4"));
    assert_eq!(m.robots[3].hp, 1.0);
    assert!(m.robots[3].alive);
    assert_eq!(m.robots[0].hp, 0.0);
    assert!(!m.robots[0].alive);
    assert_eq!(m.teams["blue"].kills, kills);
}

#[test]
fn simultaneous_yellow_does_not_count_as_individual_cards() {
    let mut m = match_();
    let old = m.robots.iter().map(|r| r.hp).collect::<Vec<_>>();
    card(&mut m, "yellow_both", None);
    for (i, r) in m.robots.iter().enumerate() {
        assert_eq!(r.yellow_cards, 0);
        assert_eq!(r.fpv_blocked_until, 2.0);
        if r.role != "aerial" {
            assert_eq!(r.hp, old[i] - (r.max_hp * 0.05).round());
        }
    }
}

#[test]
fn red_card_prevents_revival_support_and_future_gameplay() {
    for (i, id) in [(1, "red-2"), (4, "red-6"), (5, "red-7")] {
        let mut m = match_();
        m.teams.get_mut("red").unwrap().resources = 10000.0;
        card(&mut m, "red", Some(id));
        m.tick = 400;
        m.robots[i].respawn_progress = 1000.0;
        m.rulebook_rpc(i, &json!({"revive":true,"support":true,"pick_unit":true}));
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert!(m.robots[i].red_card);
        assert!(!m.robots[i].alive);
        assert!(!m.robots[i].support_active);
        assert_eq!(m.teams["red"].resources, 10000.0);
    }
}

#[test]
fn referee_batches_are_atomic_and_forfeit_finishes_the_match() {
    let mut m = match_();
    m.process_referee_commands(Some(
        &json!([{"kind":"yellow","robot":"red-4"},{"kind":"red","robot":"red-5"}]),
    ));
    assert_eq!(m.robots[3].yellow_cards, 0);
    assert!(m.events.iter().any(|e| e["type"] == "referee_error"));
    let result = m.step(&json!({"referee":[{"kind":"forfeit","team":"blue"}]}));
    assert_eq!(result["status"], "finished");
    assert_eq!(result["reason"], "forfeit");
    assert_eq!(m.bases["blue"].hp, 0.0);
    assert_eq!(m.winner.as_deref(), Some("red"));
}

#[test]
fn yellow_camera_blackout_is_individual_and_teammate_times_stack() {
    let mut m = match_();
    card(&mut m, "yellow", Some("red-4"));
    assert_eq!(m.robots[3].fpv_blocked_until, 5.0);
    assert_eq!(m.robots[2].fpv_blocked_until, 2.0);
    let blocked = m.observation("red", Some(&[4]));
    assert!(blocked["vision"].as_object().unwrap().is_empty());
    assert!(blocked["runes"]["red"]["targets"]
        .as_array()
        .unwrap()
        .is_empty());
    m.tick = 40;
    assert!(!m.observation("red", Some(&[3]))["vision"]
        .as_object()
        .unwrap()
        .is_empty());
    assert!(m.observation("red", Some(&[4]))["vision"]
        .as_object()
        .unwrap()
        .is_empty());
    card(&mut m, "yellow", Some("red-4"));
    assert_eq!(m.robots[3].fpv_blocked_until, 10.0);
    assert_eq!(m.robots[2].fpv_blocked_until, 4.0);
}

#[test]
fn both_helipads_are_forbidden_to_ground_robots_but_not_aircraft() {
    let m = match_();
    for team in ["red", "blue"] {
        let pad = m.arena.pads[team];
        for r in &m.robots {
            assert_eq!(m.blocked_for(r, pad[0], pad[1]), r.role != "aerial");
        }
    }
}

#[test]
fn base_hp_threshold_opens_armor_and_tech_healing_never_closes_it() {
    let mut m = match_();
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    m.bases.get_mut("blue").unwrap().hp = 2100.0;
    m.robots[0].last_shot_time = 0.0;
    m.robots[0].ammo = 10;
    let b = m.bases["blue"].clone();
    let point = structures::plates(&b,false)[0].center;
    let p = Projectile {
        id: 1,
        source: "red-1".into(),
        team: "red".into(),
        role: "hero".into(),
        x: b.x - 1.0,
        y: b.y,
        z: b.z + 1.05,
        // Fired from the field side: the blue base's armour faces inward.
        vx: 12.0,
        vy: 0.0,
        vz: 0.0,
        remaining: 10.0,
        damage: 200.0,
        radius: 0.02125,
        deployed: false,
    };
    m.rulebook_structure_hit("blue", false, &p, 0.0, point[0], point[1]+0.02, point[2]);
    assert!(m.bases["blue"].hp <= 2000.0);
    assert!(m.bases["blue"].armor_open);
    m.complete_assembly("blue", 4);
    assert!(m.bases["blue"].hp > 2000.0);
    assert!(m.bases["blue"].armor_open);
}

#[test]
fn outpost_buff_boundaries_and_own_outpost_requirement_match_the_rules() {
    let mut m = match_();
    zone(&mut m, 3, "blue_outpost");
    m.tick = 5999;
    m.outposts.get_mut("blue").unwrap().hp = 0.0;
    m.update_rulebook_buffs();
    assert!(m.outpost_buff_allowed(3));
    m.tick = 6000;
    m.update_rulebook_buffs();
    assert!(!m.outpost_buff_allowed(3));
    m.tick = 5000;
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.update_rulebook_buffs();
    assert!(!m.outpost_buff_allowed(3));
}
