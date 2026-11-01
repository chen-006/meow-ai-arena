use super::*;

fn setup(difficulty: u32) -> (Engine, usize) {
    let mut m = Engine::new(42, 420.0, "rmuc2027").unwrap();
    let h = m.robots.iter().position(|r| r.id == "red-1").unwrap();
    let z = m.arena.zones.iter().find(|z| z.id == "red_assembly").unwrap();
    m.robots[h].x = z.x;
    m.robots[h].y = z.y;
    m.robots[h].z = height_at(&m.arena, z.x, z.y);
    m.robots[h].carried_units = 2;
    m.robots[h].upright = 1.0;
    m.robots[h].quaternion = [0.0, 0.0, 0.0, 1.0];
    m.tick = 3600;
    m.teams.get_mut("red").unwrap().completed_difficulties = (1..difficulty).collect();
    m.assembly_rpc_2027(h, &json!({"assembly":difficulty}));
    assert_eq!(m.assemblies["red"].stage, "moving");
    (m, h)
}

fn ready(m: &mut Engine) {
    let s = m.assemblies.get_mut("red").unwrap();
    s.stage = "inserting".into();
    s.ready_at = 0.0;
    s.current_pose = s.pose;
}

fn align(m: &mut Engine, h: usize, core: usize) {
    let (point, direction, roll) = m.core_goal_2027("red", core).unwrap();
    let r = &mut m.robots[h];
    let secondary = core == 1 && r.arm_count == 2;
    if secondary { r.arm2_tip = point; } else { r.arm_tip = point; }
    let joints = if secondary { &mut r.arm2_joints } else { &mut r.arm_joints };
    joints[0] = direction[1].atan2(direction[0]);
    joints[3] = 0.0;
    joints[4] = -direction[2].atan2(direction[0].hypot(direction[1]));
    joints[5] = roll;
}

#[test]
fn failed_retry_preserves_pose_and_rotation_until_success() {
    let (mut m, h) = setup(3);
    let s = m.assemblies["red"].clone();
    m.fail_assembly_2027("red", "test_failure");
    m.assembly_rpc_2027(h, &json!({"assembly":3}));
    assert_eq!(m.assemblies["red"].pose, s.pose);
    assert_eq!(m.assemblies["red"].target_rotation, s.target_rotation);
    m.complete_assembly_2027("red", h, 3);
    // A finished assembly leaves the core re-initialising, so the next order has to wait it out.
    m.assembly_rpc_2027(h, &json!({"assembly":3}));
    assert_eq!(m.assemblies["red"].stage, "complete");
    m.assemblies.get_mut("red").unwrap().repeat_ready_at = 0.0;
    m.assembly_rpc_2027(h, &json!({"assembly":3}));
    assert_eq!(m.assemblies["red"].stage, "moving");
    assert_ne!(m.assemblies["red"].pose, s.pose);
}

#[test]
fn insert_requires_actual_tool_contact() {
    let (mut m, h) = setup(1);
    ready(&mut m);
    m.robots[h].arm_tip = [-10.0; 3];
    for _ in 0..30 { m.assembly_rpc_2027(h, &json!({"assembly_insert":true})); }
    assert_eq!(m.assemblies["red"].inserted, 0.0);
    assert_eq!(m.robots[h].carried_units, 2);
    align(&mut m, h, 0);
    m.assembly_rpc_2027(h, &json!({"assembly_insert":true}));
    assert!(m.assemblies["red"].inserted > 0.0);
    assert_eq!(m.robots[h].carried_units, 1);
}

#[test]
fn wrist_roll_must_match_the_unit_during_confirmation() {
    let (mut m, h) = setup(3);
    ready(&mut m);
    let s = m.assemblies.get_mut("red").unwrap();
    s.inserted = 0.1; s.lifted = 0.1; s.flipped = 90.0; s.rotation = s.target_rotation;
    align(&mut m, h, 0);
    m.robots[h].arm_joints[5] += std::f64::consts::PI;
    for _ in 0..20 { m.assembly_rpc_2027(h, &json!({"assembly_confirm":true})); }
    assert!(!m.assemblies["red"].confirmed);
    align(&mut m, h, 0);
    m.tick += 21;
    for _ in 0..5 { m.assembly_rpc_2027(h, &json!({})); }
    m.assembly_rpc_2027(h, &json!({"assembly_confirm":true}));
    assert_eq!(m.assemblies["red"].stage, "complete");
}

#[test]
fn lower_difficulties_have_no_artificial_attempt_timeout() {
    for difficulty in 1..=3 {
        let (mut m, _) = setup(difficulty);
        ready(&mut m);
        m.assemblies.get_mut("red").unwrap().buffer_until = m.time();
        m.assembly_after_step_2027();
        assert_eq!(m.assemblies["red"].stage, "inserting");
    }
}

#[test]
fn level_four_pair_deadline_runs_even_without_step_commands() {
    let (mut m, _) = setup(4);
    ready(&mut m);
    let now = m.time();
    let s = m.assemblies.get_mut("red").unwrap();
    s.ready_at = now; s.buffer_until = now; s.step_times[0] = Some(now - 5.1);
    m.assembly_after_step_2027();
    assert_eq!(m.assemblies["red"].stage, "failed");
    assert!(m.events.iter().any(|e| e["reason"] == "pair_window"));
}

#[test]
fn default_infantry_performance_is_hp_and_cooling() {
    let m = Engine::new(42, 420.0, "rmuc2027").unwrap();
    for r in m.robots.iter().filter(|r| r.role == "infantry") {
        assert_eq!((r.chassis_profile.as_str(), r.weapon_profile.as_str()), ("hp", "cooling"));
        assert_eq!((r.max_hp, r.power_limit, r.max_heat, r.cooling_rate), (200.0, 45.0, 40.0, 12.0));
    }
}

#[test]
fn rune_has_rulebook_constant_and_reset_sine_motion() {
    let mut m = Engine::new(42, 420.0, "rmuc2027").unwrap();
    let constant = std::f64::consts::PI / 3.0;
    for t in [0.0, 40.0, 179.0, 200.0] {
        assert!((m.rune_motor_rates_2027(t).0 - constant).abs() < 1e-12);
        assert_eq!(m.rune_motor_rates_2027(t).1, 0.0);
    }
    m.tick = 3600;
    m.runes.get_mut("red").unwrap().large_chances = 2;
    m.start_rune_2027("red");
    let (a, w) = (m.rune_a, m.rune_omega);
    assert!((m.rune_motor_rates_2027(180.0).0 - (2.090 - a)).abs() < 1e-12);
    assert!((m.rune_motor_rates_2027(181.0).0 - (a*w.sin() + 2.090-a)).abs() < 1e-12);
    m.runes.get_mut("red").unwrap().status = "idle".into();
    assert!((m.rune_motor_rates_2027(181.0).0 - constant).abs() < 1e-12);
    m.tick = 3620;
    m.start_rune_2027("red");
    assert_ne!((m.rune_a,m.rune_omega), (a,w));
    assert!((m.rune_motor_rates_2027(181.0).0 - (2.090-m.rune_a)).abs() < 1e-12);
}

#[test]
fn a_tool_line_the_arm_cannot_hold_does_not_deadlock_the_flip() {
    // The heavy levers the unit up with the jaws; on a tilted core the wrist saturates and can no
    // longer match the required tool line. Grade 3 used to freeze there — the flip that would have
    // moved the goal waited for an alignment the arm could never reach. Only the tool's position is
    // gated now, so the flip still ripples through to completion.
    let (mut m, h) = setup(3);
    ready(&mut m);
    {
        let s = m.assemblies.get_mut("red").unwrap();
        s.inserted = 0.1;
        s.lifted = 0.1;
        s.flipped = 0.0;
    }
    align(&mut m, h, 0);
    let before = m.assemblies["red"].core(0).flipped;
    // Saturated wrist: the tip is still on the unit, the tool line is ~45° off the goal.
    m.robots[h].arm_joints[4] = -1.0;
    for _ in 0..6 {
        m.assembly_rpc_2027(h, &json!({"assembly_flip":true}));
    }
    let after = m.assemblies["red"].core(0).flipped;
    assert_eq!(before, 0.0);
    assert!(after > 0.0, "the flip stalled while the tool line was out of reach");
    // A tool that has left the core still blocks it.
    let (point, _, _) = m.core_goal_2027("red", 0).unwrap();
    m.robots[h].arm_tip = [point[0] + 0.5, point[1], point[2]];
    let held = m.assemblies["red"].core(0).flipped;
    m.assembly_rpc_2027(h, &json!({"assembly_flip":true}));
    assert_eq!(m.assemblies["red"].core(0).flipped, held, "the unit left the core");
}

#[test]
fn all_assembly_grades_accept_aligned_sequential_operations() {
    for difficulty in 1..=4 {
        let (mut m, h) = setup(difficulty);
        ready(&mut m);
        let now = m.time();
        m.assemblies.get_mut("red").unwrap().started = now;
        let mut next_core = 0;
        for _ in 0..300 {
            let core = if difficulty == 4 { next_core } else { 0 };
            align(&mut m, h, core);
            let rotate = m.assemblies["red"].core(core).target_rotation;
            m.assembly_rpc_2027(h, &json!({"assembly_core":core,"assembly_insert":true,
                "assembly_lift":true,"assembly_flip":true,"assembly_rotate":rotate,"assembly_confirm":true}));
            if m.assemblies["red"].stage == "complete" { break; }
            next_core = 1-next_core;
            m.tick += 2;
            m.assemblies.get_mut("red").unwrap().buffer_until = m.time();
            m.assembly_after_step_2027();
            assert_ne!(m.assemblies["red"].stage,"failed","grade {difficulty}");
        }
        assert_eq!(m.assemblies["red"].stage,"complete","grade {difficulty}");
        assert!(m.teams["red"].completed_difficulties.contains(&difficulty));
        assert_eq!(m.robots[h].carried_units, if difficulty == 4 {0} else {1});
    }
}

#[test]
fn dual_arms_use_independent_physical_poses_and_target_rotations() {
    let list = vec![Deployment {slot:1, model:2712}];
    let lineups = Some([("red".to_string(), list.clone()), ("blue".to_string(), list)].into());
    let mut m = Engine::new_with_lineups(42,420.0,"rmuc2027",lineups).unwrap();
    let h = m.robots.iter().position(|r| r.id == "red-1").unwrap();
    let z = m.arena.zones.iter().find(|z| z.id == "red_assembly").unwrap();
    let r = &mut m.robots[h];
    r.x=z.x; r.y=z.y; r.z=height_at(&m.arena,z.x,z.y);
    r.quaternion=[0.0,0.0,0.0,1.0];r.upright=1.0;r.carried_units=2;
    m.tick=3600;
    m.teams.get_mut("red").unwrap().completed_difficulties=vec![1,2,3];
    m.assembly_rpc_2027(h,&json!({"assembly":4}));
    ready(&mut m);
    let now=m.time();
    m.assemblies.get_mut("red").unwrap().started=now;
    assert_ne!(m.assemblies["red"].target_rotation,m.assemblies["red"].core(1).target_rotation);
    align(&mut m,h,0);align(&mut m,h,1);
    m.robots[h].arm2_joints[5]+=std::f64::consts::PI;
    m.assembly_rpc_2027(h,&json!({"assembly_core":2,"assembly_insert":true}));
    assert!(m.assemblies["red"].inserted>0.0);
    assert_eq!(m.assemblies["red"].core(1).inserted,0.0);
    for _ in 0..200 {
        align(&mut m,h,0);align(&mut m,h,1);
        let a=m.assemblies["red"].core(0).target_rotation;
        let b=m.assemblies["red"].core(1).target_rotation;
        m.assembly_rpc_2027(h,&json!({"assembly_core":2,"assembly_insert":true,"assembly_lift":true,
            "assembly_flip":true,"assembly_rotate":a,"assembly_rotate2":b,"assembly_confirm":true}));
        if m.assemblies["red"].stage=="complete" {break;}
        m.tick+=2;
        m.assemblies.get_mut("red").unwrap().buffer_until=m.time();
        m.assembly_after_step_2027();
        assert_ne!(m.assemblies["red"].stage,"failed");
    }
    assert_eq!(m.assemblies["red"].stage,"complete");
}

#[test]
fn flip_rotates_the_tool_ninety_degrees_for_tilted_and_vertical_cores() {
    let (mut m, _) = setup(3);
    ready(&mut m);
    for phi in [0.0,30.0,60.0,90.0] {
        let s=m.assemblies.get_mut("red").unwrap();
        s.current_pose[4]=phi;s.flipped=0.0;
        let (_,before,_)=m.core_goal_2027("red",0).unwrap();
        m.assemblies.get_mut("red").unwrap().flipped=90.0;
        let (_,after,_)=m.core_goal_2027("red",0).unwrap();
        let length=after.iter().map(|v|v*v).sum::<f64>().sqrt();
        let dot=before.iter().zip(after).map(|(a,b)|a*b).sum::<f64>();
        assert!((length-1.0).abs()<1e-9,"phi {phi}");
        assert!(dot.abs()<1e-9,"phi {phi}");
    }
}
