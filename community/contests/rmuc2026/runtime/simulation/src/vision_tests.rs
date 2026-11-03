use super::*;
use rapier3d::na::{UnitQuaternion, Vector3};

fn place(m:&mut Engine,i:usize,x:f64,y:f64,aim:f64){
    let r=&mut m.robots[i];r.x=x;r.y=y;r.z=height_at(&m.arena,x,y);r.heading=0.0;r.aim=aim;r.pitch=0.0;r.quaternion=[0.0,0.0,0.0,1.0];
}
fn field(enemy_x:f64,enemy_y:f64)->Engine{
    let mut m=Engine::new(42,30.0,"rmuc2026").unwrap();
    place(&mut m,3,6.0,9.8,0.0);place(&mut m,9,enemy_x,enemy_y,std::f64::consts::PI);
    for _ in 0..8 {m.step(&json!({"red":{"4":{"aim":0,"pitch":0}}}));}
    m
}
fn has(obs:&Value,id:&str)->bool{obs["enemies"].as_array().unwrap().iter().any(|e|e["id"]==id)}

#[test]
fn ground_camera_has_horizontal_and_vertical_edges_and_no_rear_coverage(){
    let m=Engine::new(42,30.0,"rmuc2026").unwrap();let mut r=m.robots[3].clone();r.heading=0.0;r.aim=0.0;r.pitch=0.0;r.quaternion=[0.0,0.0,0.0,1.0];
    let eye=Engine::eye(&r);let point=|h:f64,v:f64|[eye[0]+2.0,eye[1]+2.0*h.to_radians().tan(),eye[2]+2.0*v.to_radians().tan()];
    assert!(m.point_in_camera(&r,point(49.9,34.9)));assert!(!m.point_in_camera(&r,point(50.1,0.0)));assert!(!m.point_in_camera(&r,point(0.0,35.1)));
    assert!(!m.point_in_camera(&r,[eye[0]-2.0,eye[1],eye[2]]));assert!(!m.point_in_camera(&r,[eye[0],eye[1]+2.0,eye[2]]));
    r.role="aerial".into();assert!(m.point_in_camera(&r,[eye[0]-2.0,eye[1],eye[2]]));assert_eq!(m.camera_metadata(&r)["horizontal_fov_deg"],360.0);
}

#[test]
fn aperture_follows_actual_muzzle_yaw_pitch_and_chassis_pose(){
    let m=Engine::new(42,30.0,"rmuc2026").unwrap();let mut r=m.robots[3].clone();r.heading=std::f64::consts::PI;r.aim=0.0;r.pitch=0.0;r.quaternion=[0.0,0.0,1.0,0.0];
    let eye=Engine::eye(&r);assert!(m.point_in_camera(&r,[eye[0]+3.0,eye[1],eye[2]]));assert!(!m.point_in_camera(&r,[eye[0]-3.0,eye[1],eye[2]]));
    r.heading=0.0;r.aim=0.7;r.pitch=0.4;let q=UnitQuaternion::from_euler_angles(0.25,0.3,0.0);r.quaternion=[q.i,q.j,q.k,q.w];
    let eye=Vector3::from(Engine::eye(&r));let axis=Vector3::from(physics::muzzle(&r).1);
    assert!(m.point_in_camera(&r,(eye+axis*3.0).into()));assert!(!m.point_in_camera(&r,(eye-axis*3.0).into()));
    assert_eq!(m.camera_metadata(&r)["direction"],json!(physics::muzzle(&r).1));
}

#[test]
fn rotating_the_gun_reveals_a_rear_enemy_without_changing_public_telemetry(){
    let mut m=field(4.4,9.8);let hidden=m.observation("red",Some(&[4]));assert!(!has(&hidden,"blue-4"));
    m.robots[3].aim=std::f64::consts::PI;let seen=m.observation("red",Some(&[4]));assert!(has(&seen,"blue-4"));
    for key in ["referee","bases","outposts","teams","zones","runes","assemblies","roster"] {assert_eq!(hidden[key],seen[key],"{key} must stay independent of the camera cone");}
    assert_eq!(hidden["robots"].as_array().unwrap().len(),6);
    for own in hidden["robots"].as_array().unwrap(){assert!(own.get("x").is_some()&&own.get("y").is_some()&&own.get("z").is_some());}
    assert_eq!(seen["vision"]["4"]["camera"]["horizontal_fov_deg"],100.0);
}

#[test]
fn team_observation_merges_real_cameras_but_independent_control_does_not(){
    let mut m=field(4.4,10.7);place(&mut m,2,6.2,10.7,std::f64::consts::PI);
    for _ in 0..8 {m.step(&json!({"red":{"3":{"aim":std::f64::consts::PI,"pitch":0},"4":{"aim":0,"pitch":0}}}));}
    let single=m.observation("red",Some(&[4]));let team=m.observation("red",None);
    assert!(!has(&single,"blue-4"));let target=team["enemies"].as_array().unwrap().iter().find(|e|e["id"]=="blue-4").expect("teammate sees the enemy");
    assert!(target["seen_by"].as_array().unwrap().contains(&json!(3)));assert!(!target["seen_by"].as_array().unwrap().contains(&json!(4)));
    assert_eq!(single["referee"],team["referee"]);assert_eq!(single["robots"],team["robots"]);
}

#[test]
fn enemy_bullets_need_the_camera_cone_but_own_bullets_remain_known(){
    let mut m=field(8.5,9.8);let eye=Engine::eye(&m.robots[3]);
    for (id,team,x) in [(101,"blue",7.5),(102,"blue",4.5),(103,"red",4.5)] {
        m.projectiles.push(Projectile{id,source:format!("{team}-4"),team:team.into(),role:"infantry".into(),x,y:eye[1],z:eye[2],vx:1.0,vy:0.0,vz:0.0,remaining:10.0,damage:20.0,radius:0.0085,deployed:false});
    }
    let obs=m.observation("red",Some(&[4]));let ids:Vec<_>=obs["projectiles"].as_array().unwrap().iter().map(|p|p["id"].as_u64().unwrap()).collect();
    assert!(ids.contains(&101));assert!(!ids.contains(&102));assert!(ids.contains(&103));
}

#[test]
fn an_enemy_body_part_inside_the_edge_is_visible_even_if_the_center_is_outside(){
    let mut m=Engine::new(42,30.0,"rmuc2026").unwrap();m.physics=None;m.arena.obstacles.clear();place(&mut m,3,6.0,9.8,0.0);
    let eye=Engine::eye(&m.robots[3]);let angle=50.5_f64.to_radians();let r=&mut m.robots[9];r.x=eye[0]+3.0*angle.cos();r.y=eye[1]+3.0*angle.sin();r.z=eye[2]-r.armor_center_z_m;r.quaternion=[0.0,0.0,0.0,1.0];
    let center=[r.x,r.y,eye[2]];
    assert!(!m.point_in_camera(&m.robots[3],center));assert!(m.visible(&m.robots[3],&m.robots[9]));
}
