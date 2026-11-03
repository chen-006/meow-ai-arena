use super::*;

fn match_() -> Engine {
    Engine::new(42, 420.0, "rmuc2026").unwrap()
}
fn in_zone(m: &mut Engine, i: usize, kind: &str) {
    let zone = m
        .arena
        .zones
        .iter()
        .find(|z| z.kind == kind && z.owner.as_deref() == Some(m.robots[i].team.as_str()))
        .unwrap();
    m.robots[i].x = zone.x;
    m.robots[i].y = zone.y;
    assert!(m.raw_zone(i, kind, Some(&m.robots[i].team)));
}
fn operations() -> Value {
    json!({"assembly_insert":true,"assembly_translate":true,"assembly_rotate":90,"assembly_confirm":true})
}
pub(super) fn align_engineer(m: &mut Engine, i: usize) {
    let r = m.robots[i].clone();
    let s = &m.assemblies[&r.team];
    let (entry, normal, slide) = m.core_entry(&r.team).unwrap();
    let expected = std::array::from_fn(|k| entry[k] - normal[k] * s.inserted + slide[k] * s.translated);
    let q = rapier3d::na::UnitQuaternion::new_normalize(rapier3d::na::Quaternion::new(
        r.quaternion[3],r.quaternion[0],r.quaternion[1],r.quaternion[2]));
    let dir = q.inverse() * rapier3d::na::Vector3::from(normal.map(|v| -v));
    let roll = (s.current_pose[5] + s.rotation).to_radians();
    let r = &mut m.robots[i];
    r.arm_tip = expected;
    r.arm_joints[0] = 0.0;
    r.arm_joints[3] = dir.y.atan2(dir.x);
    r.arm_joints[4] = (-dir.z).atan2(dir.x.hypot(dir.y));
    r.arm_joints[5] = roll;
}
fn operate(m: &mut Engine) {
    align_engineer(m, 1);
    m.assembly_rpc(1, &operations());
    m.tick += 2;
    m.rulebook_after_step();
}

#[test]
fn confirmation_needs_physical_alignment_and_a_stationary_upright_engineer() {
    for reason in ["reach", "direction", "roll", "moving", "turning", "tipped"] {
        let mut m = match_();in_zone(&mut m, 1, "assembly");m.robots[1].carried_units = 1;
        let s = m.assemblies.get_mut("red").unwrap();s.stage = "inserting".into();s.difficulty = 1;s.inserted = 0.1;s.confirm_progress_s = 0.3;
        align_engineer(&mut m, 1);
        match reason {
            "reach" => m.robots[1].arm_tip[0] += 0.2,
            "direction" => m.robots[1].arm_joints[3] += 0.4,
            "roll" => m.robots[1].arm_joints[5] += 0.4,
            "moving" => m.robots[1].vx = 0.3,
            "turning" => m.robots[1].angular_velocity[2] = 0.6,
            _ => m.robots[1].tipped = true,
        }
        m.assembly_rpc(1, &operations());
        assert_eq!(m.assemblies["red"].confirm_progress_s, 0.0, "{reason}");
        assert_eq!(m.robots[1].carried_units, 1, "{reason}");
        assert_eq!(m.teams["red"].tech_income, 0.0, "{reason}");
        assert!(m.assembly_snapshots()["red"]["blocked_reason"].is_string(), "{reason}");
    }
}

#[test]
fn repeat_reset_blocks_farming_but_does_not_delay_a_new_difficulty() {
    let mut m = match_();m.tick = 1200;in_zone(&mut m,1,"assembly");m.robots[1].carried_units = 2;
    m.complete_assembly("red",1);
    let ready = m.assemblies["red"].repeat_ready_at;
    assert_eq!(ready,80.0);
    m.assembly_rpc(1,&json!({"assembly":1}));assert_eq!(m.assemblies["red"].stage,"complete");
    m.assembly_rpc(1,&json!({"assembly":2}));assert_eq!(m.assemblies["red"].stage,"moving");
    m.assemblies.get_mut("red").unwrap().stage="complete".into();m.tick=1600;
    m.assembly_rpc(1,&json!({"assembly":1}));assert_eq!(m.assemblies["red"].stage,"moving");
}

#[test]
fn basic_and_precise_physical_assembly_can_complete_on_both_sides() {
    for seed in [1,42,71] {
        for team in ["red", "blue"] {
            for difficulty in 1..=2 {
                let mut m = Engine::new(seed,420.0,"rmuc2026").unwrap();m.tick = 3600;
                let i = if team == "red" {1} else {7};in_zone(&mut m,i,"assembly");
                let sign=if team=="red" {1.0} else {-1.0};m.robots[i].x+=sign*0.14;m.robots[i].y+=sign*0.14;
                let r=&mut m.robots[i];r.z=height_at(&m.arena,r.x,r.y);
                r.heading=if team=="red" {std::f64::consts::FRAC_PI_4} else {-3.0*std::f64::consts::FRAC_PI_4};
                r.quaternion=[0.0,0.0,(r.heading/2.0).sin(),(r.heading/2.0).cos()];r.carried_units=2;
                m.teams.get_mut(team).unwrap().completed_difficulties=(1..difficulty).collect();
                m.physics=Some(physics::PhysicsWorld::new(&m.arena,&m.robots,&config().robot_designs));
                for _ in 0..400 {
                    let rotate=m.assemblies[team].target_rotation;
                    m.step(&json!({team:{"2":{"assembly":difficulty,"assembly_insert":true,"assembly_translate":true,"assembly_rotate":rotate,"assembly_confirm":true,"brake":true}}}));
                    if ["complete","failed"].contains(&m.assemblies[team].stage.as_str()) {break;}
                }
                assert_eq!(m.assemblies[team].stage,"complete","seed {seed} team {team} difficulty {difficulty}: {} robot {}",m.assembly_snapshots()[team],serde_json::to_value(&m.robots[i]).unwrap());
            }
        }
    }
}

#[test]
fn shot_cone_is_seeded_bounded_and_widens_with_range() {
    let mut a = match_();
    let mut b = match_();
    let mut different = Engine::new(43, 420.0, "rmuc2026").unwrap();
    for (role, limit) in [("hero", 1.3_f64), ("infantry", 1.8), ("aerial", 1.8)] {
        for direction in [[1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.6, 0.8, 0.0]] {
            let axis = rapier3d::na::Vector3::from(direction);
            let mut near_sq = 0.0;
            let mut far_sq = 0.0;
            let mut sum = rapier3d::na::Vector3::zeros();
            for _ in 0..1000 {
                let d = a.dispersed_shot_direction(direction, role);
                assert_eq!(d, b.dispersed_shot_direction(direction, role));
                assert_ne!(d, different.dispersed_shot_direction(direction, role));
                let ray = rapier3d::na::Vector3::from(d);
                assert!((ray.norm() - 1.0).abs() < 1e-12);
                let cosine = ray.dot(&axis);
                assert!(cosine >= limit.to_radians().cos() - 1e-12);
                let offset = ray / cosine - axis;
                near_sq += (offset * 2.0).norm_squared();
                far_sq += (offset * 20.0).norm_squared();
                sum += offset;
            }
            assert!(near_sq > 0.01);
            assert!((far_sq / near_sq - 100.0).abs() < 1e-9);
            assert!(sum.norm() / 1000.0 < 0.003);
        }
    }
}

#[test]
fn firing_applies_spread_to_the_real_velocity_without_changing_muzzle_speed() {
    let mut m = match_();
    m.robots[2].ammo = 1;
    m.robots[2].vx = 1.0;
    m.robots[2].vy = -0.5;
    m.robots[2].vz = 0.25;
    let (_, axis) = physics::muzzle(&m.robots[2]);
    let mut actions = vec![Action::default(); 12];
    actions[2].fire = true;
    m.rulebook_fire(&actions);
    let p = &m.projectiles[0];
    let velocity = rapier3d::na::Vector3::new(p.vx - 1.0, p.vy + 0.5, p.vz - 0.25);
    assert!((velocity.norm() - 25.0).abs() < 1e-10);
    assert!(velocity.normalize().dot(&rapier3d::na::Vector3::from(axis)) < 1.0 - 1e-10);
    assert_eq!(m.robots[2].ammo, 0);
}

#[test]
fn finite_plate_geometric_accuracy_falls_with_distance_for_both_calibres() {
    // The rulebook's dispersion is a 0.3 degree half-angle cone, so a shot stays inside a plate's
    // outline for much longer than it did with the old 1.8/1.3 degree cone: accuracy is flat until
    // the cone grows wider than the plate, then falls. The test pins both halves of that.
    let mut report=Vec::new();
    for (role,width) in [("hero",0.23),("infantry",0.135)] {
        let mut rates=Vec::new();
        for range in [2.0,5.0,10.0,15.0] {
            let mut m=match_();let mut hits=0;
            for _ in 0..5000 {
                let ray=m.dispersed_shot_direction([1.0,0.0,0.0],role);
                if (range*ray[1]/ray[0]).abs()<=width/2.0 && (range*ray[2]/ray[0]).abs()<=0.0625 {hits+=1;}
            }
            let rate=hits as f64/5000.0;rates.push(rate);
            report.push(json!({"role":role,"range_m":range,"plate_width_m":width,"plate_height_m":0.125,"shots":5000,"geometric_hit_rate":rate}));
        }
        // Never better than the previous range: the cone only ever spreads further out.
        assert!(rates.windows(2).all(|r|r[1]<=r[0]+1e-9),"{role}: {rates:?}");
        // Inside 10 m the 0.3 degree cone still fits the plate, so nearly every centred shot lands.
        assert!(rates[0]>0.99 && rates[2]>0.99,"{role}: {rates:?}");
        // At 15 m the cone is wider than the 125 mm plate, so the far shot is measurably worse.
        assert!(rates[3]<0.99,"{role}: {rates:?}");
    }
    let value=json!({"note":"Seeded stationary centered ray/finite-plate coverage; gravity, drag, normal-speed threshold, motion and occlusion can further reduce actual damage. Not a measurement of real match accuracy.","cases":report});
    if let Ok(path)=std::env::var("CALIBRATION_REPORT") {std::fs::write(path,serde_json::to_vec_pretty(&value).unwrap()).unwrap();}
    println!("{value}");
}

#[test]
fn assembly_has_timed_sequential_phases_and_continuous_confirmation() {
    let mut m = match_();
    in_zone(&mut m, 1, "assembly");
    m.robots[1].carried_units = 2;
    m.assembly_rpc(
        1,
        &json!({"assembly":1,"assembly_insert":true,"assembly_confirm":true}),
    );
    assert_eq!(m.assembly_snapshots()["red"]["operation"], "positioning");
    assert_eq!(m.assemblies["red"].inserted, 0.0);
    m.tick = 40;
    m.rulebook_after_step();
    // The insert phase is paced by `arm_insert_speed_mps`, so drive it until it reports verifying
    // instead of assuming a fixed number of settlements.
    for _ in 0..200 {
        operate(&mut m);
        if m.assembly_snapshots()["red"]["operation"] == "verifying" {
            break;
        }
    }
    assert_eq!(m.assemblies["red"].translated, 0.0);
    assert_eq!(m.assemblies["red"].rotation, 0.0);
    assert_eq!(m.teams["red"].tech_income, 0.0);
    assert_eq!(m.assembly_snapshots()["red"]["operation"], "verifying");
    for _ in 0..3 {
        operate(&mut m);
    }
    assert_eq!(m.robots[1].carried_units, 2);
    m.assembly_rpc(1, &json!({"assembly_confirm":false}));
    assert_eq!(m.assemblies["red"].confirm_progress_s, 0.0);
    operate(&mut m);
    m.robots[1].x = 1.0;
    m.rulebook_after_step();
    assert_eq!(m.assemblies["red"].confirm_progress_s, 0.0);
    in_zone(&mut m, 1, "assembly");
    for _ in 0..4 {
        operate(&mut m);
    }
    assert_eq!(m.assemblies["red"].stage, "complete");
    assert_eq!(m.teams["red"].tech_income, 50.0);
    assert_eq!(m.robots[1].carried_units, 1);
    assert_eq!(m.assembly_snapshots()["red"]["progress"], 1.0);
}

#[test]
fn level_four_accepts_perfect_low_rotation_operations_inside_the_actuator_budget() {
    // The configured window is a floor: the second core has to fit the insert, slide, rotate and
    // confirm phases at the rulebook's own actuator speeds, whatever those are set to.
    let mut m = match_();
    m.tick = 3600;
    in_zone(&mut m, 1, "assembly");
    m.robots[1].carried_units = 2;
    m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2, 3];
    m.assembly_rpc(1, &json!({"assembly":4}));
    m.assemblies.get_mut("red").unwrap().target_rotation=0.0;
    m.tick += 40;
    m.rulebook_after_step();
    for _ in 0..400 {
        operate(&mut m);
        if m.assemblies["red"].inserted_units == 1 { break; }
    }
    let first = m.assemblies["red"].first_unit_at;
    assert_eq!(m.assemblies["red"].inserted_units, 1);
    assert_eq!(m.robots[1].carried_units, 1);
    assert_eq!(m.teams["red"].tech_income, 0.0);
    let progress=m.assembly_snapshots()["red"]["progress"].as_f64().unwrap();
    assert!(progress>=0.5 && progress<1.0);
    let window = m.secondary_core_window_s(0.0);
    for _ in 0..600 {
        operate(&mut m);
        if ["complete", "failed"].contains(&m.assemblies["red"].stage.as_str()) { break; }
    }
    assert_eq!(m.assemblies["red"].stage, "complete", "{}", m.assembly_snapshots()["red"]);
    assert!(m.time() - first <= window + 1e-8, "{} > {window}", m.time() - first);
    assert_eq!(m.robots[1].carried_units, 0);
    assert!(m.teams["red"].completed_difficulties.contains(&4));
}

#[test]
fn interrupted_assembly_waits_for_positioning_and_dead_or_weak_engineers_cannot_confirm() {
    let mut m = match_();
    in_zone(&mut m, 1, "assembly");
    m.robots[1].carried_units = 1;
    let s = m.assemblies.get_mut("red").unwrap();
    s.difficulty = 1;
    s.stage = "buffered".into();
    s.ready_at = 2.0;
    s.buffer_until = 15.0;
    m.assembly_rpc(1, &operations());
    assert_eq!(m.assemblies["red"].inserted, 0.0);
    let s = m.assemblies.get_mut("red").unwrap();
    s.stage = "inserting".into();
    s.inserted = 0.1;
    s.confirm_progress_s = 0.3;
    m.robots[1].weak = true;
    m.assembly_rpc(1, &operations());
    assert_eq!(m.assemblies["red"].confirm_progress_s, 0.0);
    m.robots[1].weak = false;
    m.robots[1].alive = false;
    m.assembly_rpc(1, &operations());
    assert_eq!(m.teams["red"].tech_income, 0.0);
    assert_eq!(m.robots[1].carried_units, 1);
}

#[test]
fn rune_speed_varies_in_both_modes_and_is_shared_continuous_and_seeded() {
    let mut m = match_();
    let mut other = match_();
    for start in [0.0, 180.0] {
        let speeds: Vec<f64> = (0..400)
            .map(|i| m.rune_motor_rates(start + i as f64 * DT).0)
            .collect();
        assert!(speeds.iter().all(|v| *v > 0.0 && *v <= 2.10));
        let range = speeds.iter().copied().fold(f64::NEG_INFINITY, f64::max)
            - speeds.iter().copied().fold(f64::INFINITY, f64::min);
        assert!(range > if start == 0.0 { 0.35 } else { 1.0 });
        for t in [start, start + 0.7, start + 4.0] {
            let (speed, acceleration) = m.rune_motor_rates(t);
            assert_eq!((speed, acceleration), other.rune_motor_rates(t));
            let numeric = (m.rune_motor_rates(t + 1e-5).0 - m.rune_motor_rates(t - 1e-5).0) / 2e-5;
            assert!((numeric - acceleration).abs() < 1e-5);
        }
    }
    assert!((m.rune_motor_rates(180.0 - 1e-6).0 - m.rune_motor_rates(180.0 + 1e-6).0).abs() < 1e-5);
    m.start_rune("red");
    for _ in 0..20 {
        m.update_rune_motor();
        other.update_rune_motor();
        assert_eq!(m.rune_angle, other.rune_angle);
        let public = m.rune_snapshots();
        assert_eq!(
            public["red"]["angular_velocity"],
            public["blue"]["angular_velocity"]
        );
        assert_eq!(public["red"]["angle"], public["blue"]["angle"]);
        m.tick += 1;
        other.tick += 1;
    }
}

#[test]
fn rebuild_reports_the_fastest_robot_and_resets_when_eligibility_changes() {
    let mut m = match_();
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.teams.get_mut("red").unwrap().rebuild_credits = 2;
    in_zone(&mut m, 1, "outpost");
    in_zone(&mut m, 2, "outpost");
    let holds = &mut m.assemblies.get_mut("red").unwrap().rebuild_hold;
    holds.insert("red-2".into(), 1.0);
    holds.insert("red-3".into(), 3.0);
    assert_eq!(m.outpost_snapshots()["red"]["rebuild"]["robot"], "red-3");
    assert_eq!(m.outpost_snapshots()["red"]["rebuild"]["progress"], 0.3);
    m.robots[2].x = 1.0;
    m.rulebook_after_step();
    assert_eq!(m.outpost_snapshots()["red"]["rebuild"]["robot"], "red-2");
    assert!(!m.assemblies["red"].rebuild_hold.contains_key("red-3"));
    m.robots[1].weak = true;
    m.rulebook_after_step();
    assert!(m.outpost_snapshots()["red"]["rebuild"].is_null());
    m.robots[1].weak = false;
    for _ in 0..100 {
        m.rulebook_after_step();
        m.tick += 1;
    }
    assert_eq!(m.outposts["red"].hp, 750.0);
    assert_eq!(m.teams["red"].rebuild_credits, 1);
    assert!(m.outposts["red"].ever_destroyed);
    assert!(m.outpost_snapshots()["red"]["rebuild"].is_null());
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.rulebook_after_step();
    assert!(m.outpost_snapshots()["red"]["rebuild"].is_object());
    m.tick = 6000;
    m.rulebook_after_step();
    assert!(m.assemblies["red"].rebuild_hold.is_empty());
    assert!(m.outpost_snapshots()["red"]["rebuild"].is_null());
    assert_eq!(m.outposts["red"].hp, 0.0);
}

/// Visual QA fixture: real engine steps, explicitly unranked initial conditions.
/// QA_REPLAY=/tmp/mechanisms.json cargo test mechanism_qa_replay -- --ignored
#[test]
#[ignore]
fn mechanism_qa_replay() {
    let mut m = Engine::new(42, 14.0, "rmuc2026").unwrap();
    let place = |m: &mut Engine, i: usize, x: f64, y: f64, heading: f64| {
        let r = &mut m.robots[i];
        r.x = x;
        r.y = y;
        r.z = height_at(&m.arena, x, y);
        r.heading = heading;
        r.aim = heading;
        r.quaternion = [0.0, 0.0, (heading / 2.0).sin(), (heading / 2.0).cos()];
    };
    place(&mut m, 1, 13.3353, 6.8353, std::f64::consts::FRAC_PI_4);
    place(&mut m, 5, 10.40, 11.357, 0.0);
    place(&mut m, 3, 5.5, 6.5, 0.0);
    place(&mut m, 9, 22.5, 8.5, std::f64::consts::PI);
    m.robots[1].carried_units = 2;
    m.robots[3].ammo = 100;
    m.robots[9].ammo = 100;
    m.outposts.get_mut("red").unwrap().hp = 0.0;
    m.outposts.get_mut("red").unwrap().ever_destroyed = true;
    m.teams.get_mut("red").unwrap().rebuild_credits = 1;
    m.physics = Some(physics::PhysicsWorld::new(
        &m.arena,
        &m.robots,
        &config().robot_designs,
    ));
    let mut frames = vec![m.snapshot()];
    for tick in 0..280 {
        let shot = (50..65).contains(&tick) && tick % 4 == 2;
        frames.push(m.step(&json!({"red":{
            "2":{"assembly":1,"assembly_insert":true,"assembly_confirm":true,"brake":true},
            "4":{"fire":shot,"aim":0.0,"pitch":0.12},
            "7":{"brake":true}},
            "blue":{"4":{"fire":shot,"aim":std::f64::consts::PI,"pitch":0.12}}})));
    }
    assert!(frames
        .iter()
        .any(|f| f["assemblies"]["red"]["operation"] == "inserting"));
    assert!(frames
        .iter()
        .any(|f| f["outposts"]["red"]["rebuild"]["progress"]
            .as_f64()
            .unwrap_or(0.0)
            > 0.5));
    assert_eq!(m.outposts["red"].hp, 750.0);
    assert!(frames.iter().any(|f| f["projectiles"]
        .as_array()
        .unwrap()
        .iter()
        .any(|p| p["team"] == "red")));
    assert!(frames.iter().any(|f| f["projectiles"]
        .as_array()
        .unwrap()
        .iter()
        .any(|p| p["team"] == "blue")));
    let mut static_data = serde_json::Map::new();
    for key in ["arena", "rulebook_rules", "simulation_assumptions"] {
        static_data.insert(key.into(), frames[0][key].clone());
        for frame in &mut frames {
            frame.as_object_mut().unwrap().remove(key);
        }
    }
    let replay = json!({"format":"rmuc-replay-v4","metadata":{"seed":42,"duration":14,"map":"rmuc2026","dt":DT,"test_match":true,
        "teams":{"red":{"name":"mechanism-qa","version":"v3","mode":"team"},"blue":{"name":"mechanism-qa","version":"v3","mode":"team"}}},
        "static":static_data,"frames":frames,"actions":[],"diagnostics":[],
        "result":{"winner":m.winner,"reason":"timeout","time":14,"digest":"","policy_errors":0}});
    std::fs::write(
        std::env::var("QA_REPLAY").expect("QA_REPLAY path"),
        serde_json::to_vec(&replay).unwrap(),
    )
    .unwrap();
}

#[test]
fn dead_infantry_cannot_gain_health_from_initial_profile_selection() {
    let mut m=match_();m.robots[2].alive=false;m.robots[2].hp=0.0;
    m.select_profile(2,&json!({"profile":{"chassis":"hp","weapon":"burst"}}));
    assert_eq!(m.robots[2].hp,0.0);assert!(!m.robots[2].alive);
}

#[test]
fn infantry_natural_and_paid_revives_have_distinct_hp_and_paid_events() {
    let mut m=match_();m.tick=1000;m.robots[2].alive=false;m.robots[2].hp=0.0;
    m.robots[2].respawn_required=15.0;m.robots[2].respawn_progress=0.0;m.teams.get_mut("red").unwrap().resources=1000.0;
    m.rulebook_before_step(&vec![Action::default();12]);assert!(!m.robots[2].alive);assert_eq!(m.robots[2].hp,0.0);
    m.respawn(2,false);assert_eq!(m.robots[2].hp,(m.robots[2].max_hp*0.1).round());assert!(m.robots[2].weak);
    m.robots[2].alive=false;m.robots[2].hp=0.0;m.robots[2].respawn_progress=0.0;
    let before=m.teams["red"].resources;let cost=80.0+m.robots[2].level as f64*20.0;
    m.rulebook_rpc(2,&json!({"revive":true}));
    assert_eq!(m.robots[2].hp,m.robots[2].max_hp);assert!(m.robots[2].alive);assert!(!m.robots[2].weak);
    assert_eq!(m.teams["red"].resources,before-cost);
    let e=m.events.iter().rev().find(|e|e["type"]=="respawn").unwrap();assert_eq!(e["instant"],true);assert_eq!(e["cost"],cost);assert_eq!(e["team"],"red");
    m.rulebook_rpc(2,&json!({"revive":true}));assert_eq!(m.teams["red"].resources,before-cost);
    m.robots[2].alive=false;m.robots[2].hp=0.0;m.teams.get_mut("red").unwrap().resources=cost-1.0;
    m.rulebook_rpc(2,&json!({"revive":true}));assert!(!m.robots[2].alive);assert_eq!(m.robots[2].hp,0.0);
}

#[test]
fn assembly_profiles_strictly_increase_precision_and_stability_requirements() {
    let m=match_();
    for d in 1..4 {
        let a=m.assembly_difficulty(d);let b=m.assembly_difficulty(d+1);
        for key in ["tip_tolerance_m","tool_tolerance_deg","chassis_speed_mps","chassis_angular_speed_rad_s","rotation_tolerance_deg"] {
            assert!(n(a,key)>n(b,key), "{d}: {key}");
        }
        assert!(n(a,"confirm_hold_s")<n(b,"confirm_hold_s"));
        assert!(n(a,"min_upright")<n(b,"min_upright"));
    }
}

#[test]
fn identical_mechanical_error_is_easy_at_level_one_and_rejected_at_level_four() {
    for (difficulty,accepted) in [(1,true),(2,true),(3,false),(4,false)] {
        let mut m=match_();in_zone(&mut m,1,"assembly");
        m.assemblies.get_mut("red").unwrap().difficulty=difficulty;
        align_engineer(&mut m,1);
        m.robots[1].arm_tip[0]+=0.02;
        assert_eq!(m.assembly_alignment_error(1).is_none(),accepted);
    }
    for (difficulty,accepted) in [(1,true),(2,false),(3,false),(4,false)] {
        let mut m=match_();in_zone(&mut m,1,"assembly");
        m.assemblies.get_mut("red").unwrap().difficulty=difficulty;
        align_engineer(&mut m,1);
        m.robots[1].arm_joints[5]+=8.0_f64.to_radians();
        assert_eq!(m.assembly_alignment_error(1).is_none(),accepted);
    }
    for (difficulty,accepted) in [(1,true),(2,true),(3,true),(4,false)] {
        let mut m=match_();in_zone(&mut m,1,"assembly");
        m.assemblies.get_mut("red").unwrap().difficulty=difficulty;
        align_engineer(&mut m,1);
        m.robots[1].arm_tip[0]+=0.007;
        assert_eq!(m.assembly_alignment_error(1).is_none(),accepted);
    }
    for (difficulty,accepted) in [(1,true),(2,true),(3,false),(4,false)] {
        let mut m=match_();in_zone(&mut m,1,"assembly");
        m.assemblies.get_mut("red").unwrap().difficulty=difficulty;
        m.robots[1].vx=0.07;
        assert_eq!(!m.assembly_motion_blocked(1),accepted);
    }
}

#[test]
fn timed_out_lower_grade_can_be_retried_without_level_four_lock() {
    for difficulty in 1..=3 {
        let mut m=match_();m.tick=3600;in_zone(&mut m,1,"assembly");m.robots[1].carried_units=2;
        m.teams.get_mut("red").unwrap().completed_difficulties=(1..difficulty).collect();
        m.assembly_rpc(1,&json!({"assembly":difficulty}));
        let started=m.assemblies["red"].started;
        let deadline=n(m.assembly_difficulty(difficulty),"attempt_timeout_s");
        m.tick=((started+deadline)/DT).ceil() as u64;
        m.rulebook_after_step();m.rulebook_after_step();
        assert_eq!(m.assemblies["red"].stage,"failed");
        assert!(!m.assemblies["red"].permanent_lock);
        m.assembly_rpc(1,&json!({"assembly":difficulty}));
        assert_eq!(m.assemblies["red"].stage,"moving");
    }
}

#[test]
fn failing_a_lower_grade_does_not_clear_an_existing_level_four_retry_lock() {
    let mut m=match_();m.tick=3600;in_zone(&mut m,1,"assembly");m.robots[1].carried_units=2;
    m.teams.get_mut("red").unwrap().completed_difficulties=vec![1,2,3];
    m.assembly_rpc(1,&json!({"assembly":4}));m.fail_assembly("red","timeout");
    let retry_at=m.assemblies["red"].retry_at;
    m.assembly_rpc(1,&json!({"assembly":1}));m.fail_assembly("red","timeout");
    assert_eq!(m.assemblies["red"].retry_at,retry_at);
    m.assembly_rpc(1,&json!({"assembly":4}));
    assert_eq!(m.assemblies["red"].stage,"failed");
    m.tick=(retry_at/DT).ceil() as u64;
    m.assembly_rpc(1,&json!({"assembly":4}));
    assert_eq!(m.assemblies["red"].stage,"moving");
}

fn physical_grade_attempt(seed: i64, team: &str, d: u32, dock: f64) -> Engine {
    physical_grade_attempt_with_model(seed,team,d,dock,2)
}

fn physical_grade_attempt_with_model(seed: i64, team: &str, d: u32, dock: f64, model: u32) -> Engine {
    let lineups=Some([("red".into(),vec![Deployment{slot:2,model}]),("blue".into(),vec![Deployment{slot:2,model}])].into());
    let mut m=Engine::new_with_lineups(seed,420.0,"rmuc2026",lineups).unwrap();m.tick=3600;
    let i=m.robots.iter().position(|r|r.team==team).unwrap();
    in_zone(&mut m,i,"assembly");let sign=if team=="red" {1.0}else{-1.0};
    let r=&mut m.robots[i];r.x+=sign*dock;r.y+=sign*dock;r.z=height_at(&m.arena,r.x,r.y);r.heading=if team=="red"{std::f64::consts::FRAC_PI_4}else{-3.0*std::f64::consts::FRAC_PI_4};r.quaternion=[0.0,0.0,(r.heading/2.0).sin(),(r.heading/2.0).cos()];r.carried_units=2;
    m.teams.get_mut(team).unwrap().completed_difficulties=(1..d).collect();
    m.physics=Some(physics::PhysicsWorld::new(&m.arena,&m.robots,&config().robot_designs));
    let duration=n(m.assembly_difficulty(d),"attempt_timeout_s")+3.0;
    for _ in 0..(duration/DT).ceil() as usize {
        let rotate=m.assemblies[team].target_rotation;
        m.advance(&json!({team:{"2":{"assembly":d,"assembly_insert":true,"assembly_translate":true,"assembly_rotate":rotate,"assembly_confirm":true,"brake":true}}}));
        if ["complete","failed"].contains(&m.assemblies[team].stage.as_str()) {break;}
    }
    m
}

#[test]
fn extreme_grade_is_physically_possible_for_favourable_poses_on_both_sides() {
    for team in ["red","blue"] {
        let m=physical_grade_attempt(101,team,4,0.25);
        assert_eq!(m.assemblies[team].stage,"complete","{team}: {}",m.assembly_snapshots()[team]);
        assert!(m.teams[team].completed_difficulties.contains(&4));
    }
}

#[test]
fn level_four_secondary_window_covers_the_actuator_budget_for_any_pose() {
    // The second core's phases are paced by the rulebook actuators, so the window has to be larger
    // than their sum for every pose angle instead of only for near-zero tool rotation.
    let m=match_();let rules=&m.rules["assembly"];
    let fixed=n(rules,"insert_depth_m")/n(&m.sim,"arm_insert_speed_mps")
        +n(rules,"translation_m")/n(&m.sim,"arm_translate_speed_mps")
        +n(&m.assembly_difficulty(4),"confirm_hold_s");
    let mut previous=0.0;
    for rotation in [0.0,9.33,45.0,90.0] {
        let budget=fixed+rotation/n(&m.sim,"arm_rotate_speed_deg_s");
        let window=m.secondary_core_window_s(rotation);
        assert!(window>budget,"{rotation}: {window} <= {budget}");
        assert!(window>=previous,"{rotation}: window shrinks");previous=window;
    }
    assert!(m.secondary_core_window_s(90.0)>n(rules,"difficulty_4_secondary_insert_window_s"));
}

#[test]
fn seeded_difficulty_poses_are_symmetric_and_respect_rulebook_envelopes() {
    let mut m=match_();m.tick=3600;
    for d in 1..=4 {
        for i in [1,7] {in_zone(&mut m,i,"assembly");m.robots[i].carried_units=2;}
        for team in ["red","blue"] {m.teams.get_mut(team).unwrap().completed_difficulties=(1..d).collect();m.assemblies.get_mut(team).unwrap().stage="idle".into();}
        m.assembly_rpc(1,&json!({"assembly":d}));m.assembly_rpc(7,&json!({"assembly":d}));
        // Level four mutual exclusion is normative; compare its generated pose in
        // separate engines rather than bypassing that lock.
        if d<4 {assert_eq!(m.assemblies["red"].pose,m.assemblies["blue"].pose);}
        let limits=&m.rules["assembly"]["pose_ranges"][if d==1 {"1"}else{"2-4"}];
        for (k,key) in ["x","y","z","theta","phi","alpha"].iter().enumerate() {
            let value=m.assemblies["red"].pose[k];
            assert!(value>=limits[key][0].as_f64().unwrap()-1e-8&&value<=limits[key][1].as_f64().unwrap()+1e-8);
        }
    }
    let red=physical_grade_attempt(101,"red",4,0.25);let blue=physical_grade_attempt(101,"blue",4,0.25);
    assert_eq!(red.assemblies["red"].pose,blue.assemblies["blue"].pose);
    assert_eq!(red.assemblies["red"].target_rotation,blue.assemblies["blue"].target_rotation);
}

// Calibration uses an actual engineer arm (E35 by default), automatic tracking, and a
// well-aligned fixed docking point. Every tick goes through normal 120 Hz physics.
// It does not set the arm tip, grant successes, or roll a random success gate.
#[test]
#[ignore = "Empirical grade calibration; set RMUC_CALIBRATION_SEEDS for sample count"]
fn assembly_difficulty_calibration() {
    let samples=std::env::var("RMUC_CALIBRATION_SEEDS").ok().and_then(|s|s.parse::<i64>().ok()).unwrap_or(64);
    let grades=std::env::var("RMUC_CALIBRATION_GRADES").unwrap_or("1234".into());
    let dock=std::env::var("RMUC_CALIBRATION_DOCK").ok().and_then(|s|s.parse::<f64>().ok()).unwrap_or(0.25);
    let model=std::env::var("RMUC_CALIBRATION_MODEL").ok().and_then(|s|s.parse::<u32>().ok()).unwrap_or(2);
    for d in 1..=4 {
        if !grades.contains(char::from_digit(d,10).unwrap()) {continue;}
        let mut successes=Vec::new();let mut reasons=std::collections::BTreeMap::<String,u32>::new();
        for seed in 1..=samples {
            let m=physical_grade_attempt_with_model(seed,"red",d,dock,model);
            if m.assemblies["red"].stage=="complete" {successes.push(seed);}
            else {
                let reason=if m.assemblies["red"].inserted_units==1 {"second_unit_timeout".into()}
                    else if m.assembly_motion_blocked(0) {"chassis_unstable".into()}
                    else {m.assembly_alignment_error(0).unwrap_or("timeout").into()};
                *reasons.entry(reason).or_default()+=1;
            }
            if std::env::var("RMUC_CALIBRATION_DIAGNOSTICS").is_ok() && seed<=3 {
                let r=&m.robots[0];println!("DIAG {}",json!({"seed":seed,"dock":dock,"stage":m.assemblies["red"].stage,"inserted":m.assemblies["red"].inserted,"translate":m.assemblies["red"].translated,"rotate":m.assemblies["red"].rotation,"target_rotation":m.assemblies["red"].target_rotation,"held":m.assemblies["red"].confirm_progress_s,"upright":r.upright,"speed":(r.vx*r.vx+r.vy*r.vy+r.vz*r.vz).sqrt(),"angular_speed":r.angular_velocity.iter().map(|v|v*v).sum::<f64>().sqrt(),"alignment":m.assembly_alignment_metrics(0)}));
            }
        }
        println!("CALIBRATION {}",json!({"grade":d,"model":model,"samples":samples,"dock":dock,"successes":successes.len(),"seeds":successes,"failures":reasons}));
    }
}

#[test]
fn runtime_settings_change_actual_shot_spread_and_preserve_other_defaults() {
    let settings = json!({"projectile_spread_half_angle_deg.17mm":0.0});
    let mut m = Engine::new_with_settings(42, 5.0, "rmuc2026", None, Some(settings)).unwrap();
    let axis = [1.0,0.0,0.0];
    for _ in 0..20 { assert_eq!(m.dispersed_shot_direction(axis,"infantry"),axis); }
    assert_eq!(m.snapshot()["simulation_assumptions"]["projectile_spread_half_angle_deg"]["42mm"],json!(0.3));
    assert!(Engine::new_with_settings(42, 5.0, "rmuc2026", None,
        Some(json!({"drone_impact.recovery_s":1.0}))).is_err());
    for value in [json!(-1),json!(100),json!(true),json!("0")] {
        assert!(Engine::new_with_settings(42,5.0,"rmuc2027",None,
            Some(json!({"projectile_spread_half_angle_deg.17mm":value}))).is_err());
    }
}

#[test]
fn the_2026_unit_turns_about_q_in_either_direction() {
    // Rule 89: facing the core the unit may turn 90 degrees about Q clockwise or anticlockwise, so
    // the sampled target angle takes both signs instead of only turning counter-clockwise.
    let (mut clockwise, mut anticlockwise) = (false, false);
    for seed in 1..=24 {
        let mut m = Engine::new(seed, 420.0, "rmuc2026").unwrap();
        m.tick = (180.0 / DT) as u64;
        in_zone(&mut m, 1, "assembly");
        m.robots[1].carried_units = 2;
        m.teams.get_mut("red").unwrap().completed_difficulties = vec![1, 2];
        m.assembly_rpc(1, &json!({"assembly": 3}));
        let angle = m.assemblies["red"].target_rotation;
        assert!(angle.abs() <= 90.0 + 1e-9, "{seed}: {angle}");
        clockwise |= angle < 0.0;
        anticlockwise |= angle > 0.0;
    }
    assert!(clockwise && anticlockwise, "both directions: {clockwise} {anticlockwise}");
}
