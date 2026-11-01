use super::*;
fn selected(slot:u32,model:u32)->Engine{
    let lineups=Some([("red".into(),SLOT_IDS.iter().map(|s|Deployment{slot:*s,model:if *s==slot{model}else{*s}}).collect()),
        ("blue".into(),SLOT_IDS.iter().map(|s|Deployment{slot:*s,model:if *s==slot{model}else{*s}}).collect())].into());
    Engine::new_with_lineups(42,30.0,"rmuc2026",lineups).unwrap()
}
fn place(m:&mut Engine,i:usize,x:f64,y:f64,yaw:f64){let r=&mut m.robots[i];r.x=x;r.y=y;r.z=height_at(&m.arena,x,y);r.heading=yaw;r.quaternion=[0.0,0.0,(yaw/2.0).sin(),(yaw/2.0).cos()];}

#[test]
fn catalog_models_preserve_role_and_mirrored_fixed_spawn_points(){
    let cfg=config();assert_eq!(cfg.robot_designs.values().filter(|d|d["seasons"]["2026"]["legal"]==true).count(),21);
    for (id,d) in cfg.robot_designs.into_iter().filter(|(_,d)|d["seasons"]["2026"]["legal"]==true) {
        let model=id.parse().unwrap();let slot=match d["role"].as_str().unwrap(){"hero"=>1,"engineer"=>2,"aerial"=>6,_=>3};
        let m=selected(slot,model);let red=m.robots.iter().find(|r|r.team=="red"&&r.slot==slot).unwrap();
        let blue=m.robots.iter().find(|r|r.team=="blue"&&r.slot==slot).unwrap();
        assert_eq!(red.role,d["role"]);assert_eq!(red.model_asset,d["asset"]);assert_eq!(red.design_slot(),model);
        assert_eq!(red.x+blue.x,28.0);assert_eq!(red.y+blue.y,15.0);
        assert_eq!([red.x,red.y],m.arena.spawn_points["red"][&slot.to_string()]);
    }
}

#[test]
fn wheel_legged_hero_can_jump_but_cannot_deploy_even_with_ranged_profile(){
    let mut m=selected(1,103);m.step(&json!({"red":{"1":{"profile":"ranged"}}}));place(&mut m,0,8.0,7.5,0.0);
    for _ in 0..30 {m.step(&json!({}));}
    let mut jumped=false;for _ in 0..65 {m.step(&json!({"red":{"1":{"profile":"ranged","deploy":true,"jump":true,"power":200}}}));jumped|=m.events.iter().any(|e|e["type"]=="jump");}
    assert!(jumped);assert!(!m.robots[0].deployed);assert!(m.robots[0].deploy_at.is_none());assert!(!m.robots[0].deploy_allowed);
}

#[test]
fn hardware_observations_do_not_reveal_an_unseen_enemy_model(){
    let lineups=Some([("red".into(),vec![Deployment{slot:1,model:103}]),("blue".into(),vec![Deployment{slot:1,model:301}])].into());
    let m=Engine::new_with_lineups(42,30.0,"rmuc2026",lineups).unwrap();
    let obs=m.observation("red",None);assert_eq!(obs["enemies"].as_array().unwrap().len(),0);
    assert!(obs["robot_models"].get("103").is_some());assert!(obs["robot_models"].get("301").is_none());
    assert!(m.snapshot()["robot_models"].get("301").is_some());
}

#[test]
fn wheel_legged_hero_crosses_the_fly_slope_both_directions(){
    // Forward runs straight off the highway and over the gap; reverse jumps the gap onto the crest.
    for (start,dir,speed,trigger,ticks) in [(10.0,1.0,1.0,None,90),(15.6,-1.0,0.6,Some(14.65),110)] {
        let mut m=selected(1,103);place(&mut m,0,start,14.4,if dir<0.0{std::f64::consts::PI}else{0.0});
        m.robots[0].fly_pass=2; // the reverse run starts where a forward flight landed (R58)
        for _ in 0..20 {m.step(&json!({}));}
        let mut jumped=false;
        for _ in 0..ticks {let r=&m.robots[0];let mut action=json!({"move":[dir*speed,(14.4-r.y)*1.5],"power":200});
            if trigger.is_some_and(|x|!jumped&&(r.x-x)*dir>0.0) {action["jump"]=json!(1.0);}
            m.step(&json!({"red":{"1":action}}));jumped|=m.events.iter().any(|e|e["type"]=="jump");
            let r=&m.robots[0];if (dir>0.0&&r.x>14.6&&r.z>0.12&&r.grounded)||(dir<0.0&&r.x<12.4&&r.grounded){break;}
        }
        let r=&m.robots[0];assert_eq!(jumped,trigger.is_some());assert!(r.upright>0.85&&!r.tipped,"hero up {} x {}",r.upright,r.x);
        assert!((r.y-14.4).abs()<0.3&&if dir>0.0 {r.x>14.6&&r.z>0.12}else{r.x<12.55},"hero fly direction {dir} x {} y {} z {}",r.x,r.y,r.z);
    }
}

#[test]
fn swerve_wheels_turn_at_a_finite_rate_then_drive_sideways(){
    let mut m=selected(4,302);place(&mut m,3,8.0,7.5,0.0);
    for _ in 0..30 {m.step(&json!({}));}
    m.step(&json!({"red":{"4":{"move":[0,1],"heading":0,"power":200}}}));
    assert!(m.robots[3].steering_angles.iter().any(|v|v.abs()>0.05));
    assert!(m.robots[3].steering_angles.iter().all(|v|v.abs()<0.3));
    for _ in 0..25 {m.step(&json!({"red":{"4":{"move":[0,1],"heading":0,"power":200}}}));}
    assert!(m.robots[3].y>8.0,"swerve y {}",m.robots[3].y);
}

#[test]
fn twin_arms_move_independently_and_secondary_gripper_can_pick_up(){
    let mut m=selected(2,201);let station=m.arena.field_geometry["fixtures"].as_array().unwrap().iter().find(|f|f["kind"]=="resource"&&f["team"]=="red").unwrap();
    let p=[station["pick_point"][0].as_f64().unwrap(),station["pick_point"][1].as_f64().unwrap()];
    place(&mut m,1,p[0],p[1]-0.48,std::f64::consts::FRAC_PI_2);
    let command=json!({"red":{"2":{"heading":std::f64::consts::FRAC_PI_2,"arm":{"tip":[0.25,-0.08,0.48],"dir":[1,0,0],"frame":"robot"},"pick_unit":true,"pickup_arm":1}}});
    // The mining arm runs at half rate now, so the grab needs about twice the ticks it used to.
    let mut held=false;for _ in 0..300 {m.step(&command);held|=m.robots[1].unit_pickup.as_ref().is_some_and(|s|s.arm==1&&s.held);if m.robots[1].carried_units>0{break;}}
    assert!(held,"aux pickup {:?} tip {:?}",m.robots[1].unit_pickup.as_ref().map(|s|&s.stage),m.robots[1].arm2_tip);
    assert_eq!(m.robots[1].arm_count,2);assert_eq!(m.robots[1].carried_units,1);
    assert_eq!(m.robots[1].arm_mode,"manual");
    let distance=m.robots[1].arm_tip.iter().zip(m.robots[1].arm2_tip).map(|(a,b)|(a-b).powi(2)).sum::<f64>().sqrt();assert!(distance>0.1);
}

#[test]
fn transforming_infantry_crosses_both_side_tunnels_and_cannot_unfold_through_the_roof(){
    for team in ["red","blue"] {
        let mut m=selected(3,301);let i=if team=="red" {2}else{8};
        let wall=m.arena.obstacles.iter().find(|w|w.id==format!("{team}_tunnel_a_roof")).unwrap().clone();
        // Tunnel A runs from the road (road level) to the floor beside the notch: enter from the road.
        let x=wall.x+wall.w/2.0;let y=wall.y+wall.h/2.0;let north=if team=="red" {-1.0}else{1.0};
        place(&mut m,i,x,y-north*0.75,if team=="red" {-std::f64::consts::FRAC_PI_2}else{std::f64::consts::FRAC_PI_2});
        for _ in 0..35 {m.step(&json!({team:{"3":{"transform":true}}}));}
        assert!(m.robots[i].transform_progress>0.99);
        let mut reached=false;for _ in 0..80 {m.step(&json!({team:{"3":{"move":[0,north*0.55],"transform":true,"power":120}}}));if (m.robots[i].y-y).abs()<0.1 {reached=true;break;}}
        assert!(reached,"{team} tunnel entry {:?}",[m.robots[i].x,m.robots[i].y,m.robots[i].z]);
        for _ in 0..30 {m.step(&json!({team:{"3":{"transform":false}}}));}
        assert!(m.robots[i].transform_progress>0.7,"must not unfold through roof: {}",m.robots[i].transform_progress);
        for _ in 0..100 {m.step(&json!({team:{"3":{"move":[0,north*0.55],"transform":true,"power":120}}}));}
        assert!((m.robots[i].y-y)*north>0.5,"{team} tunnel exit {:?}",[m.robots[i].x,m.robots[i].y,m.robots[i].z]);
    }
}

#[test]
fn every_ground_model_crosses_the_road_bumps(){
    // Figures 4-33/4-34: ten smooth 70 mm humps fill the road west of the step. Every chassis
    // design gets over them driving straight at full command; supercaps back the low-power
    // chassis on the humps.
    for model in [1u32,101,102,103,2,201,202,203,3,301,302,303,304,305,4,7] {
        let slot=match model {1|101|102|103=>1,2|201|202|203=>2,4=>4,7=>7,_=>3};
        let mut m=selected(slot,model);let i=m.robots.iter().position(|r|r.id==format!("red-{slot}")).unwrap();
        place(&mut m,i,4.6,13.8,0.0);
        for _ in 0..20 {m.step(&json!({}));}
        for _ in 0..300 {
            let r=&m.robots[i];let cmd=json!({"move":[1.0,(13.8-r.y)*1.5],"heading":0.0});
            m.step(&json!({"red":{slot.to_string():cmd}}));if m.robots[i].x>8.0 {break;}
        }
        let r=&m.robots[i];
        assert!(r.x>8.0&&r.upright>0.8&&!r.tipped,"model {model} stopped at x {} up {}",r.x,r.upright);
    }
}

/// Visible QA fixture: a hero, a mecanum infantry and a wheel-legged infantry drive over the
/// road bumps. `QA_BUMPS_REPLAY=path cargo test -- --ignored bumps_showcase_replay`.
#[test]
#[ignore]
fn bumps_showcase_replay(){
    let path=std::env::var("QA_BUMPS_REPLAY").expect("QA_BUMPS_REPLAY path");
    let mut m=Engine::new(42,12.0,"rmuc2026").unwrap();
    let lanes=[(0usize,"1",13.35),(3,"4",13.95),(2,"3",14.5)];
    for (i,_,y) in lanes {place(&mut m,i,4.2,y,0.0);}
    let mut frames=vec![m.snapshot()];
    for tick in 0..200 {
        let mut red=serde_json::Map::new();
        for (i,slot,y) in lanes {let r=&m.robots[i];red.insert(slot.into(),if tick<20||r.x>9.6 {json!({})}else{json!({"move":[1.0,(y-r.y)*1.5],"heading":0.0})});}
        m.step(&json!({"red":red}));frames.push(m.snapshot());
    }
    for (i,_,_) in lanes {assert!(m.robots[i].x>8.0,"robot {i} stopped at {}",m.robots[i].x);}
    let keys=["arena","rulebook_rules","simulation_assumptions","robot_models"];
    let stat:serde_json::Map<String,Value>=keys.iter().map(|k|(k.to_string(),frames[0][*k].clone())).collect();
    for frame in &mut frames {for k in keys {frame.as_object_mut().unwrap().remove(k);}}
    let replay=json!({"format":"rmuc-replay-v4","static":stat,"frames":frames,"actions":[],"diagnostics":[],"metadata":{"seed":42,"duration":12,"map":"rmuc2026","dt":DT,"test_match":true,"teams":{"red":{"name":"起伏路段","version":"bumps-v1","mode":"team"},"blue":{"name":"起伏路段","version":"bumps-v1","mode":"team"}}},"result":{"winner":null,"reason":"timeout","time":12,"digest":"visual-fixture","policy_errors":0}});
    std::fs::write(path,serde_json::to_vec(&replay).unwrap()).unwrap();
}

/// Visible QA fixture, separate from competitive/reproducible strategy matches.
#[test]
#[ignore]
fn model_showcase_replay(){
    let path=std::env::var("QA_MODEL_REPLAY").expect("QA_MODEL_REPLAY path");
    let lineup=[(1,103),(2,201),(3,301),(4,302),(6,602),(7,303)];
    let lineups=Some(["red","blue"].into_iter().map(|t|(t.into(),lineup.iter().map(|(slot,model)|Deployment{slot:*slot,model:*model}).collect())).collect());
    let mut m=Engine::new_with_lineups(42,20.0,"rmuc2026",lineups).unwrap();
    place(&mut m,0,10.0,14.4,0.0);
    let wall=m.arena.obstacles.iter().find(|w|w.id=="red_tunnel_a_roof").unwrap().clone();
    let center=[wall.x+wall.w/2.0,wall.y+wall.h/2.0];
    // Tunnel A leads from the road north to the floor beside the notch.
    place(&mut m,2,center[0],center[1]+0.75,-std::f64::consts::FRAC_PI_2);
    let station=m.arena.field_geometry["fixtures"].as_array().unwrap().iter().find(|f|f["kind"]=="resource"&&f["team"]=="red").unwrap().clone();
    place(&mut m,1,station["pick_point"][0].as_f64().unwrap(),station["pick_point"][1].as_f64().unwrap()-0.48,std::f64::consts::FRAC_PI_2);
    let mut frames=vec![m.snapshot()];let mut landed=false;
    for tick in 0..400 {
        let r=&m.robots[0];landed|=r.x>14.6&&r.z>0.12&&r.grounded;
        let hero=if tick<20||landed {json!({})}else{json!({"move":[1.0,(14.4-r.y)*1.5],"power":200})};
        let r=&m.robots[2];let moving=tick>=25&&r.y>center[1]-0.7;
        let infantry=json!({"transform":tick<180,"move":[0,if moving {-0.55}else{0.0}],"heading":-std::f64::consts::FRAC_PI_2,"power":120});
        let command=json!({"red":{"1":hero,"2":{"heading":std::f64::consts::FRAC_PI_2,"arm":{"tip":[0.25,-0.08,0.48],"dir":[1,0,0],"frame":"robot"},"pick_unit":tick<150,"pickup_arm":1},"3":infantry,"4":{"heading":0,"move":[0,if (40..65).contains(&tick) {0.4}else{0.0}],"power":120},"6":{"support":true,"altitude":1.85}}});
        m.step(&command);
        frames.push(m.snapshot());
    }
    assert!(landed);assert!(m.robots[1].carried_units>0);assert!(m.robots[2].y<center[1]-0.5);assert!(m.robots[2].transform_progress<0.01);
    let keys=["arena","rulebook_rules","simulation_assumptions","robot_models"];
    let stat:serde_json::Map<String,Value>=keys.iter().map(|k|(k.to_string(),frames[0][*k].clone())).collect();
    for frame in &mut frames {for k in keys {frame.as_object_mut().unwrap().remove(k);}}
    let replay=json!({"format":"rmuc-replay-v4","static":stat,"frames":frames,"actions":[],"diagnostics":[],"metadata":{"seed":42,"duration":20,"map":"rmuc2026","dt":DT,"test_match":true,"teams":{"red":{"name":"型号机构展示","version":"model-v1","mode":"team"},"blue":{"name":"同型号蓝方","version":"model-v1","mode":"team"}}},"result":{"winner":null,"reason":"timeout","time":20,"digest":"visual-fixture","policy_errors":0}});
    std::fs::write(path,serde_json::to_vec(&replay).unwrap()).unwrap();
}

#[test]
fn serial_leg_infantry_rights_itself_from_any_fall_but_five_bar_legs_cannot(){
    // Model 305's hip joints turn without end stops: dropped on its roof, side, nose or back it
    // swings its legs round to the floor and turns the body back over the axle. Five-bar legs
    // (W3) only stand up from the nose or tail; from the roof they need an engineer.
    let h=std::f64::consts::FRAC_1_SQRT_2;let (s,c)=(0.3f64.sin(),0.3f64.cos());
    let falls=[("roof",[1.0,0.0,0.0,0.0]),("roof yawed",[c,s,0.0,0.0]),("right side",[h,0.0,0.0,h]),("left side",[-h,0.0,0.0,h]),
        ("nose",[0.0,h,0.0,h]),("back",[0.0,-h,0.0,h]),("tumbled",[0.6,0.3,0.2,0.714])];
    for (label,q) in falls {
        let mut m=selected(3,305);let i=m.robots.iter().position(|r|r.id=="red-3").unwrap();
        place(&mut m,i,6.8,9.8,0.0);let n=(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3] as f64).sqrt();
        m.robots[i].quaternion=q.map(|v|v/n);m.robots[i].z+=0.5;
        let (mut starts,mut turn,mut done)=(0,0.0f64,None);
        for _ in 0..100 {
            m.step(&json!({}));
            starts+=m.events.iter().filter(|e|e["type"]=="self_righting").count();
            if done.is_none() && m.events.iter().any(|e|e["type"]=="self_righted") {done=Some(m.time());}
            turn=turn.max(m.robots[i].hip_angle.unwrap().abs());
        }
        let r=&m.robots[i];
        println!("{label}: righted at {:?} s after {starts} attempt(s), hip swing {:.2} rad",done,turn);
        // On its nose or back the shared stand-up torque may lift it before righting starts.
        let pitched=label=="nose"||label=="back";
        assert!(starts==1||pitched&&starts==0,"{label}: righting attempts {starts}");
        assert!(done.is_some_and(|t|t<3.5)||pitched&&starts==0,"{label}: righted at {done:?}");
        assert!(r.upright>0.97&&r.grounded&&!r.tipped&&!r.self_righting&&r.hip_angle==Some(0.0),"{label}: up {} hip {:?}",r.upright,r.hip_angle);
        if label.starts_with("roof") {assert!(turn>5.5,"{label}: the hips turn a full revolution, not {turn}");}
    }
    let mut m=selected(3,3);let i=m.robots.iter().position(|r|r.id=="red-3").unwrap();
    place(&mut m,i,6.8,9.8,0.0);m.robots[i].quaternion=[1.0,0.0,0.0,0.0];m.robots[i].z+=0.5;
    for _ in 0..100 {m.step(&json!({}));}
    assert!(m.robots[i].tipped&&m.robots[i].hip_angle.is_none());
}

#[test]
fn serial_leg_infantry_keeps_its_balance_when_launching_forward(){
    // Creeping backwards, then punching forward pitches a balancing chassis back past 45
    // degrees; like the five-bar W3, the 305 must catch it rather than fall over.
    for (back,go,power) in [(0.0,0.7,200.0),(0.1,1.0,120.0),(0.2,0.4,120.0),(0.3,1.0,200.0),(0.3,0.7,120.0)] {
        let mut m=selected(3,305);let i=m.robots.iter().position(|r|r.id=="red-3").unwrap();
        place(&mut m,i,6.8,9.8,0.0);
        for _ in 0..20 {m.step(&json!({}));}
        for _ in 0..20 {m.step(&json!({"red":{"3":{"move":[-back,0.0],"heading":0.0,"power":power}}}));}
        for _ in 0..30 {
            m.step(&json!({"red":{"3":{"move":[go,0.0],"heading":0.0,"power":power}}}));
            assert!(!m.robots[i].tipped,"back {back} go {go} power {power}: tipped at {}",m.time());
        }
    }
}

#[test]
fn serial_leg_infantry_wedged_against_the_boundary_wall_stays_finite_and_rights_itself(){
    // In a match a 305 jumped into the south boundary and came to rest leaning on it with a
    // hip inside the wall: that leg ray had no normal and turned the brake into NaN. Lying on
    // its side a low hip can also sit below the floor and see its underside. Both must stay
    // finite and leave the robot on its wheels (here against the kerb).
    for (lean,y,jump) in [(1.03,0.46,true),(1.03,0.40,false),(0.8,0.42,true),(1.3,0.5,false)] {
        let mut m=selected(4,305);let i=m.robots.iter().position(|r|r.id=="red-4").unwrap();
        place(&mut m,i,9.0,y,std::f64::consts::PI);
        let q=rapier3d::na::UnitQuaternion::from_euler_angles(lean,0.0,0.0)*rapier3d::na::UnitQuaternion::from_euler_angles(0.0,0.0,std::f64::consts::PI);
        m.robots[i].quaternion=[q.i,q.j,q.k,q.w];m.robots[i].z+=0.1;
        for tick in 0..240 {
            let cmd=if jump&&tick%20==8 {json!({"red":{"4":{"jump":0.9,"move":[-0.5,0.0]}}})} else {json!({})};
            m.step(&cmd);let r=&m.robots[i];
            assert!([r.x,r.y,r.z,r.upright].iter().all(|v|v.is_finite()),"lean {lean} y {y}: tick {tick} non-finite state");
        }
        let r=&m.robots[i];
        assert!(r.upright>0.9&&!r.tipped,"lean {lean} y {y}: upright {}",r.upright);
    }
}

/// Visible QA fixture: two serial-leg infantry dropped on their roof and side flip themselves
/// back and drive off; a five-bar W3 (red 7) on its roof stays down.
/// `QA_SERIAL_REPLAY=path cargo test -- --ignored serial_leg_showcase_replay`.
#[test]
#[ignore]
fn serial_leg_showcase_replay(){
    let path=std::env::var("QA_SERIAL_REPLAY").expect("QA_SERIAL_REPLAY path");
    let red=[(1,1),(2,2),(3,305),(4,305),(6,6),(7,3)];let blue=[(1,1),(2,2),(3,305),(4,305),(6,6),(7,301)];
    let lineups=Some([("red".into(),red.iter().map(|(s,m)|Deployment{slot:*s,model:*m}).collect()),("blue".into(),blue.iter().map(|(s,m)|Deployment{slot:*s,model:*m}).collect())].into());
    let mut m=Engine::new_with_lineups(42,20.0,"rmuc2026",lineups).unwrap();
    let index=|m:&Engine,id:&str|m.robots.iter().position(|r|r.id==id).unwrap();
    let h=std::f64::consts::FRAC_1_SQRT_2;
    let drop=|m:&mut Engine,i:usize,x:f64,y:f64,q:[f64;4]|{place(m,i,x,y,0.0);m.robots[i].quaternion=q;m.robots[i].z+=0.45;};
    let (r3,r4,w3)=(index(&m,"red-3"),index(&m,"red-4"),index(&m,"red-7"));
    drop(&mut m,r3,6.2,9.0,[1.0,0.0,0.0,0.0]);drop(&mut m,r4,6.2,10.3,[h,0.0,0.0,h]);drop(&mut m,w3,5.0,9.65,[1.0,0.0,0.0,0.0]);
    let mut frames=vec![m.snapshot()];
    for tick in 0..380 {
        // A second, tumbling fall for red 4 after both have driven off.
        if tick==170 {let i=index(&m,"red-4");drop(&mut m,i,6.4,10.6,[0.6,0.3,0.2,0.714]);}
        let t=tick as f64*DT;let mut cmd=json!({});
        if (3.5..4.8).contains(&t) {cmd=json!({"red":{"3":{"move":[0.4,0.0],"heading":0.0},"4":{"move":[0.4,0.0],"heading":0.0}}});}
        if (6.0..6.2).contains(&t) {cmd=json!({"red":{"3":{"jump":0.6}}});}
        m.step(&cmd);frames.push(m.snapshot());
    }
    let (r3,r4,w3)=(&m.robots[index(&m,"red-3")],&m.robots[index(&m,"red-4")],&m.robots[index(&m,"red-7")]);
    assert!(r3.upright>0.95&&r4.upright>0.95&&w3.tipped,"{} {} {}",r3.upright,r4.upright,w3.upright);
    let keys=["arena","rulebook_rules","simulation_assumptions","robot_models"];
    let stat:serde_json::Map<String,Value>=keys.iter().map(|k|(k.to_string(),frames[0][*k].clone())).collect();
    for frame in &mut frames {for k in keys {frame.as_object_mut().unwrap().remove(k);}}
    let replay=json!({"format":"rmuc-replay-v4","static":stat,"frames":frames,"actions":[],"diagnostics":[],"metadata":{"seed":42,"duration":20,"map":"rmuc2026","dt":DT,"test_match":true,"lineups":{"red":red.iter().map(|(s,m)|json!({"slot":s,"model":m})).collect::<Vec<_>>(),"blue":blue.iter().map(|(s,m)|json!({"slot":s,"model":m})).collect::<Vec<_>>()},"teams":{"red":{"name":"串联腿自翻身","version":"serial-v1","mode":"team"},"blue":{"name":"串联腿阵容","version":"serial-v1","mode":"team"}}},"result":{"winner":null,"reason":"timeout","time":20,"digest":"visual-fixture","policy_errors":0}});
    std::fs::write(path,serde_json::to_vec(&replay).unwrap()).unwrap();
}
