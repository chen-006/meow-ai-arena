//! Physical hardware: SI units, Z-up, six fixed 120 Hz substeps per game tick.
use super::*;
use rapier3d::na as nalgebra;
use rapier3d::na::{Quaternion, UnitQuaternion};
use rapier3d::prelude::*;

pub const PHYSICS_DT: f64 = 1.0 / 120.0;
/// Step climb: how long the hull is carried onto the tread, and the pause before the next step of
/// a flight. The carry is longer than the leg stroke because a chassis pressed against the face
/// needs the whole body translated over the lip, not just unweighted.
const CLIMB_CARRY_S: f64 = 1.30;
/// A finished climb must settle before the next one may start: retracting the legs onto the tread
/// briefly drops the hull, and re-probing during that drop made the chassis twitch.
const CLIMB_COOLDOWN_S: f64 = 0.35;
/// The carry is a velocity servo on the hull: it rises just past the tread, then moves forward at a
/// walking pace. The gains stay low enough that the lift cannot launch the chassis — a hard lift
/// over the lip rolled it over its nose.
const CLIMB_RISE_MPS: f64 = 0.6;
const CLIMB_FORWARD_MPS: f64 = 1.1;
const CLIMB_SERVO_GAIN: f64 = 0.6;
const CLIMB_SERVO_LIMIT: f64 = 5.0;
/// Upright assist while the hull is carried over a lip: N·m per kg per radian of tilt and per
/// rad/s of pitch/roll rate. The carry pulls on the centre of mass while the wheels still touch
/// the step face, which would otherwise pitch the chassis over its nose.
const CLIMB_UPRIGHT_GAIN: f64 = 10.0;
const CLIMB_UPRIGHT_DAMP: f64 = 1.0;
/// Fraction of the supercapacitor's stored energy that never reaches the motors (DC-DC and
/// wiring losses). The capacitor must therefore drain 1/(1-LOSS) of the useful boost.
const SUPERCAP_LOSS: f64 = 0.40;
fn n(v: &Value, key: &str) -> f64 {
    v[key].as_f64().unwrap_or(0.0)
}
fn p3(a: &Value) -> Vector<f64> {
    vector![
        a[0].as_f64().unwrap(),
        a[1].as_f64().unwrap(),
        a[2].as_f64().unwrap()
    ]
}
fn q(r: &Robot) -> UnitQuaternion<f64> {
    UnitQuaternion::new_normalize(Quaternion::new(
        r.quaternion[3],
        r.quaternion[0],
        r.quaternion[1],
        r.quaternion[2],
    ))
}

pub(super) fn arm_direction(r: &Robot) -> [f64; 3] {
    arm_direction_for(r, false)
}

pub(super) fn arm_direction_for(r: &Robot, secondary: bool) -> [f64; 3] {
    let joints = if secondary { &r.arm2_joints } else { &r.arm_joints };
    let yaw = joints[0] + joints[3];
    let pitch = joints[4];
    let v = q(r) * vector![yaw.cos() * pitch.cos(), yaw.sin() * pitch.cos(), -pitch.sin()];
    [v.x, v.y, v.z]
}

pub struct PhysicsWorld {
    pipeline: PhysicsPipeline,
    parameters: IntegrationParameters,
    islands: IslandManager,
    broad: BroadPhaseMultiSap,
    narrow: NarrowPhase,
    pub bodies: RigidBodySet,
    pub colliders: ColliderSet,
    joints: ImpulseJointSet,
    multibody: MultibodyJointSet,
    ccd: CCDSolver,
    queries: QueryPipeline,
    base_covers: Vec<(String, ColliderHandle, usize)>,
    handles: Vec<RigidBodyHandle>,
    hulls: Vec<ColliderHandle>,
    carriers: Vec<Option<RigidBodyHandle>>,
    designs: Vec<Value>,
    last: Vec<[f64; 3]>,
    /// Footprints (x0, y0, x1, y1) of humped road sections, where tyres need their radius.
    hump_areas: Vec<[f64; 4]>,
    last_alive: Vec<bool>,
    impacts: BTreeMap<usize, Value>,
    grasps: Vec<Option<(usize, Point<f64>)>>,
    /// Robot each engineer has gripped since it was last righted.
    rescuing: Vec<Option<usize>>,
    arm_events: Vec<Value>,
    /// Wheel-legged leg command and jump timing.
    leg_cmd: Vec<f64>,
    jump_until: Vec<f64>,
    jump_ready: Vec<f64>,
    jump_strength: Vec<f64>,
    tuck_until: Vec<f64>,
    /// Step climb: until when the hull is carried onto the tread, when the next step may be
    /// attempted, and the rise the current climb is mounting.
    climb_push_until: Vec<f64>,
    climb_ready: Vec<f64>,
    climb_rise: Vec<f64>,
    /// Body height the current climb has to reach before it carries the hull forward.
    climb_target_z: Vec<f64>,
    /// Serial-leg hip angle (see `Robot::hip_angle`) and self-righting state: when the robot
    /// was first seen upset, whether it is righting, since when, and the hip angle it ends
    /// on (decided once the floor lies in the legs' plane).
    hip_angle: Vec<f64>,
    upset_since: Vec<f64>,
    righting: Vec<bool>,
    righting_since: Vec<f64>,
    right_goal: Vec<Option<f64>>,
    /// +1 rolls the body the short way back into the legs' plane; a failed attempt (body
    /// braced against a step, say) flips it so the next one rolls over the other side.
    roll_sign: Vec<f64>,
    /// Rules season of the field (2027 drops chassis energy and adds aerial systems).
    season: u32,
    /// 2027 aerial robots, per robot index (empty unless that robot is a hangar).
    drone_bodies: Vec<Vec<RigidBodyHandle>>,
}

/// What an engineer's arm is doing this tick; the engine derives it from rules state.
#[derive(Clone, Default)]
pub struct ArmGoal {
    pub mode: String,
    pub point: Option<[f64; 3]>,
    pub dir: Option<[f64; 3]>,
    pub roll: f64,
    pub grip: f64,
    pub target: Option<usize>,
    /// Direct joint targets (manual mode), clamped to the hardware limits.
    pub joints: Option<[f64; 7]>,
    /// Point and direction are in the robot frame instead of the world frame.
    pub local: bool,
    pub secondary: Option<Box<ArmGoal>>,
}

pub(crate) fn limit_arm_workspace(arm: &Value, mut joints: [f64;7]) -> [f64;7] {
    if let Some(half)=arm["workspace_half_m"].as_f64() {
        let base=p3(&arm["base_m"]);let yaw=joints[0]+joints[3];
        let tool=vector![yaw.cos()*joints[4].cos(),yaw.sin()*joints[4].cos(),0.0]*n(arm,"tool_length_m");
        let direction=[joints[0].cos(),joints[0].sin()];let mut maximum=arm_limit(arm,2).1;
        for k in 0..2 {let component=direction[k];if component.abs()>1e-8 {
            maximum=maximum.min(((if component>0.0 {half} else {-half})-base[k]-tool[k])/component);
        }}
        joints[2]=joints[2].min(maximum.max(arm_limit(arm,2).0));
    }
    joints
}

// Mining a unit and feeding a core are deliberate, slow arm jobs: the tool has to line up on a
// dispenser mouth or a socket, so the joints run at half the manual rate and the sequence takes
// twice as long as it did at its original speed. Manual control and a rescue grip keep full rate —
// those are the pilot's own hands.
const INTERACTION_ARM_RATE_SCALE: f64 = 0.375;

fn arm_rate_scale(mode: &str) -> f64 {
    if matches!(mode, "pick" | "assembly") {
        INTERACTION_ARM_RATE_SCALE
    } else {
        1.0
    }
}

#[cfg(test)]
mod arm_rate_tests {
    use super::arm_rate_scale;

    #[test]
    fn pickup_and_assembly_run_at_half_the_manual_joint_rate() {
        assert_eq!(arm_rate_scale("pick"), 0.375);
        assert_eq!(arm_rate_scale("assembly"), 0.375);
        assert_eq!(arm_rate_scale("manual"), 1.0);
        assert_eq!(arm_rate_scale("rescue"), 1.0);
    }
}

pub(crate) fn manual_arm_goal(cmd:&Value,r:&Robot,arm:&Value)->Option<ArmGoal>{
    let vector=|key:&str,len:usize|cmd[key].as_array().filter(|v|v.len()==len).and_then(|v|v.iter().map(|x|x.as_f64().filter(|x|x.is_finite())).collect::<Option<Vec<f64>>>());
    let mut g=ArmGoal::default();g.mode="manual".into();
    if cmd["stow"]==true {g.joints=Some(arm_stowed(arm));}
    else if let Some(q)=vector("joints",7) {g.joints=Some(std::array::from_fn(|k|q[k]));}
    else if let Some(p)=vector("tip",3) {
        g.local=cmd["frame"]=="robot";g.point=Some([p[0],p[1],p[2]]);
        let dir=vector("dir",3).unwrap_or_else(||if g.local {vec![1.0,0.0,0.0]} else {vec![r.heading.cos(),r.heading.sin(),0.0]});
        g.dir=Some([dir[0],dir[1],dir[2]]);g.roll=cmd["roll"].as_f64().unwrap_or(0.0);g.grip=cmd["grip"].as_f64().unwrap_or(0.0).clamp(0.0,1.0);
    } else {return None;} Some(g)
}

fn arm_vec(arm: &Value, key: &str) -> Vec<f64> {
    arm[key]
        .as_array()
        .map(|v| v.iter().map(|x| x.as_f64().unwrap_or(0.0)).collect())
        .unwrap_or_default()
}
pub fn arm_stowed(arm: &Value) -> [f64; 7] {
    let v = arm_vec(arm, "stowed");
    std::array::from_fn(|k| v.get(k).copied().unwrap_or(0.0))
}
fn arm_limit(arm: &Value, k: usize) -> (f64, f64) {
    let l = &arm["limits"][k];
    (l[0].as_f64().unwrap_or(-1e9), l[1].as_f64().unwrap_or(1e9))
}
/// Boom tail, shoulder, wrist and tool tip of the engineer arm in robot coordinates.
pub fn arm_points(arm: &Value, q: &[f64; 7]) -> [Point<f64>; 4] {
    let base = p3(&arm["base_m"]);
    let (c, s) = (q[0].cos(), q[0].sin());
    let shoulder = point![base.x, base.y, n(arm, "shoulder_rest_z_m") + q[1]];
    let wrist = shoulder + vector![c * q[2], s * q[2], 0.0];
    let tail = shoulder - vector![c, s, 0.0] * n(arm, "boom_tail_m");
    let yaw = q[0] + q[3];
    let tool = vector![yaw.cos() * q[4].cos(), yaw.sin() * q[4].cos(), -q[4].sin()];
    [
        tail,
        shoulder,
        wrist,
        wrist + tool * n(arm, "tool_length_m"),
    ]
}
/// Joint targets that put the tool tip at `tip` pointing along `dir` (robot frame).
pub fn arm_ik(arm: &Value, tip: Point<f64>, dir: Vector<f64>, roll: f64, grip: f64) -> [f64; 7] {
    let d = if dir.norm() > 1e-9 {
        dir.normalize()
    } else {
        vector![1.0, 0.0, 0.0]
    };
    let base = p3(&arm["base_m"]);
    let wrist = tip - d * n(arm, "tool_length_m");
    let (x, y) = (wrist.x - base.x, wrist.y - base.y);
    let slew = if x.hypot(y) > 1e-6 { y.atan2(x) } else { 0.0 };
    let horizontal = d.x.hypot(d.y);
    let heading = if horizontal > 1e-3 {
        d.y.atan2(d.x)
    } else {
        slew
    };
    let wrap = |a: f64| {
        (a + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI
    };
    let q = [
        slew,
        wrist.z - n(arm, "shoulder_rest_z_m"),
        x.hypot(y),
        wrap(heading - slew),
        (-d.z).atan2(horizontal),
        wrap(roll),
        grip,
    ];
    std::array::from_fn(|k| {
        let (lo, hi) = arm_limit(arm, k);
        q[k].clamp(lo, hi)
    })
}

impl PhysicsWorld {
    pub fn new(arena: &Arena, robots: &[Robot], designs: &BTreeMap<String, Value>) -> Self {
        let mut world = Self {
            pipeline: PhysicsPipeline::new(),
            parameters: IntegrationParameters {
                dt: PHYSICS_DT,
                // Rapier's own default. Every extra CCD substep re-runs a shape cast against the
                // whole field for each fast body, and even at the 5 m/s infantry cap a robot covers
                // 4.2 cm per substep — far inside its own collision shape — so the extra passes
                // bought nothing but spikes: four substeps put the slowest tick at 139 ms
                // (p95 82 ms) against 63 ms (p95 17 ms) at one, with the whole engine suite green
                // either way.
                max_ccd_substeps: 1,
                ..Default::default()
            },
            islands: IslandManager::new(),
            broad: BroadPhaseMultiSap::new(),
            narrow: NarrowPhase::new(),
            bodies: RigidBodySet::new(),
            colliders: ColliderSet::new(),
            joints: ImpulseJointSet::new(),
            multibody: MultibodyJointSet::new(),
            ccd: CCDSolver::new(),
            queries: QueryPipeline::new(),
            base_covers: Vec::new(),
            handles: Vec::new(),
            hulls: Vec::new(),
            carriers: Vec::new(),
            designs: Vec::new(),
            last: Vec::new(),
            hump_areas: Vec::new(),
            last_alive: Vec::new(),
            impacts: BTreeMap::new(),
            grasps: Vec::new(),
            rescuing: Vec::new(),
            arm_events: Vec::new(),
            leg_cmd: Vec::new(),
            jump_until: Vec::new(),
            jump_ready: Vec::new(),
            jump_strength: Vec::new(),
            tuck_until: Vec::new(),
            climb_push_until: Vec::new(),
            climb_ready: Vec::new(),
            climb_rise: Vec::new(),
            climb_target_z: Vec::new(),
            hip_angle: Vec::new(),
            upset_since: Vec::new(),
            righting: Vec::new(),
            righting_since: Vec::new(),
            right_goal: Vec::new(),
            roll_sign: Vec::new(),
            season: arena.season.parse().unwrap_or(2026),
            drone_bodies: Vec::new(),
        };
        for (a, b) in [(0.0, 7.5), (7.5, 15.0)] {
            world.prism(
                &[[0.0, a], [28.0, a], [28.0, b], [0.0, b]],
                &[0.0; 4],
                -0.1,
                arena,
            );
        }
        for t in &arena.terrain {
            let points = if t.polygon.is_empty() {
                vec![
                    [t.x, t.y],
                    [t.x + t.w, t.y],
                    [t.x + t.w, t.y + t.h],
                    [t.x, t.y + t.h],
                ]
            } else {
                t.polygon.clone()
            };
            if t.kind == "bumps" {
                world.humps(t, arena);
                world.hump_areas.push([t.x, t.y, t.x + t.w, t.y + t.h]);
            } else if t.edge_ramp_m > 0.0 {
                let cx = points.iter().map(|p| p[0]).sum::<f64>() / points.len() as f64;
                let cy = points.iter().map(|p| p[1]).sum::<f64>() / points.len() as f64;
                let apothem = (0..points.len())
                    .map(|i| {
                        let a = points[i];
                        let b = points[(i + 1) % points.len()];
                        ((cx - a[0]) * (b[1] - a[1]) - (cy - a[1]) * (b[0] - a[0])).abs()
                            / ((b[0] - a[0]).hypot(b[1] - a[1]))
                    })
                    .fold(f64::INFINITY, f64::min);
                let scale = (1.0 - t.edge_ramp_m / apothem).max(0.02);
                let inner: Vec<[f64; 2]> = points
                    .iter()
                    .map(|p| [cx + (p[0] - cx) * scale, cy + (p[1] - cy) * scale])
                    .collect();
                world.prism(
                    &inner,
                    &vec![t.elevation; inner.len()],
                    t.base_elevation,
                    arena,
                );
                for i in 0..points.len() {
                    let next = (i + 1) % points.len();
                    world.prism(
                        &[points[i], points[next], inner[next], inner[i]],
                        &[t.base_elevation, t.base_elevation, t.elevation, t.elevation],
                        t.base_elevation - 0.01,
                        arena,
                    );
                }
            } else {
                let z: Vec<f64> = points
                    .iter()
                    .map(|p| terrain_elevation(t, p[0], p[1]))
                    .collect();
                world.prism(&points, &z, -0.02, arena);
            }
        }
        for w in &arena.obstacles {
            let collider = ColliderBuilder::cuboid(w.w / 2.0, w.h / 2.0, w.height / 2.0)
                .translation(vector![
                    w.x + w.w / 2.0,
                    w.y + w.h / 2.0,
                    w.bottom + w.height / 2.0
                ])
                .rotation(vector![0.0, 0.0, w.rotation])
                .friction(0.75)
                .build();
            world.colliders.insert(collider);
        }
        for (x, y, w, h) in [
            (-0.04, 7.5, 0.08, 15.0),
            (28.04, 7.5, 0.08, 15.0),
            (14.0, -0.04, 28.0, 0.08),
            (14.0, 15.04, 28.0, 0.08),
        ] {
            world.colliders.insert(
                ColliderBuilder::cuboid(w / 2.0, h / 2.0, 1.25)
                    .translation(vector![x, y, 1.15])
                    .friction(0.5)
                    .build(),
            );
        }
        for (map, height, radius, datum) in [(&arena.bases, 0.913, 0.16, 0.0), (&arena.outposts, 1.23, 0.09, arena.outpost_plinth_m)] {
            for p in map.values() {
                let z = height_at(arena, p[0], p[1]) - datum;
                world.colliders.insert(
                    ColliderBuilder::cylinder(height / 2.0, radius)
                        .rotation(vector![std::f64::consts::FRAC_PI_2, 0.0, 0.0])
                        .translation(vector![p[0], p[1], z + height / 2.0])
                        .build(),
                );
            }
        }
        for (team, p) in &arena.bases {
            let z = height_at(arena, p[0], p[1]);
            for (index, w) in structures::cover_geometry(p[0],p[1],z,0.0,structures::base_yaw(team)).iter().enumerate() {
                let handle = world.colliders.insert(ColliderBuilder::cuboid(w.w/2.0,w.h/2.0,w.height/2.0)
                    .translation(vector![w.x+w.w/2.0,w.y+w.h/2.0,w.bottom+w.height/2.0])
                    .rotation(vector![0.0,0.0,w.rotation]).build());
                world.base_covers.push((team.clone(),handle,index));
            }
        }
        // Robot and joint insertion order is stable and identical on every replay.
        for r in robots {
            let d = designs[&r.design_slot().to_string()].clone();
            let mass = n(&d, "mass_kg");
            let com = p3(&d["com_m"]);
            let inertia = p3(&d["inertia_kgm2"]);
            if d["chassis_type"] == "hangar" {
                // A 2027 hangar sits fixed on its platform; its aerial robots fly as their own bodies.
                let handle = world.bodies.insert(RigidBodyBuilder::fixed().translation(vector![r.x, r.y, r.z]).rotation(vector![0.0, 0.0, r.heading]).build());
                let size = p3(&d["chassis_size_m"]);
                let hull = world.colliders.insert_with_parent(
                    ColliderBuilder::cuboid(size.x / 2.0, size.y / 2.0, size.z / 2.0).translation(vector![0.0, 0.0, size.z / 2.0]).friction(0.6).build(),
                    handle, &mut world.bodies);
                let drone = &d["drone"];
                let bodies = r.drones.iter().map(|dr| {
                    let body = world.bodies.insert(RigidBodyBuilder::kinematic_position_based()
                        .translation(vector![dr.x, dr.y, dr.z]).lock_rotations().ccd_enabled(true).can_sleep(false).linear_damping(0.05)
                        .additional_mass_properties(MassProperties::new(point![0.0, 0.0, 0.0], n(drone, "mass_kg"), vector![1e-4, 1e-4, 1e-4]))
                        .build());
                    world.colliders.insert_with_parent(ColliderBuilder::ball(n(drone, "radius_m")).density(0.0).friction(0.4).restitution(0.35).build(), body, &mut world.bodies);
                    body
                }).collect();
                world.handles.push(handle);
                world.hulls.push(hull);
                world.carriers.push(None);
                world.designs.push(d);
                world.last.push([r.x, r.y, r.z]);
                world.last_alive.push(r.alive);
                world.grasps.push(None);
                world.rescuing.push(None);
                world.leg_cmd.push(0.0);
                world.jump_until.push(-1.0);
                world.jump_ready.push(0.0);
                world.jump_strength.push(0.0);
                world.tuck_until.push(-1.0);
                world.climb_push_until.push(-1.0);
                world.climb_ready.push(0.0);
                world.climb_rise.push(0.0);
                world.climb_target_z.push(0.0);
                world.hip_angle.push(0.0);
                world.upset_since.push(-1.0);
                world.righting.push(false);
                world.righting_since.push(0.0);
                world.right_goal.push(None);
                world.roll_sign.push(1.0);
                world.drone_bodies.push(bodies);
                continue;
            }
            let body = RigidBodyBuilder::dynamic()
                .translation(vector![r.x, r.y, r.z])
                .rotation(vector![0.0, 0.0, r.heading])
                .additional_mass_properties(MassProperties::new(
                    point![com.x, com.y, com.z],
                    mass,
                    inertia,
                ))
                .ccd_enabled(true)
                .can_sleep(false)
                .angular_damping(0.8)
                .build();
            let handle = world.bodies.insert(body);
            let stowed = d["arm"].is_object().then(|| arm_stowed(&d["arm"]));
            let legs = (d["chassis_type"] == "wheel_legged").then(|| [n(&d, "leg_nominal_m"); 2]);
            let hull = world.colliders.insert_with_parent(
                Self::hull(&d, false, stowed, true, legs)
                    .density(0.0)
                    .friction(0.55)
                    .restitution(n(&d, "contact_restitution"))
                    .build(),
                handle,
                &mut world.bodies,
            );
            let carrier = if r.role == "aerial" {
                let lane = &arena.flight_lanes[&r.team];
                let rail = world.bodies.insert(
                    RigidBodyBuilder::kinematic_position_based()
                        .translation(vector![r.x, n(lane, "center_y"), 3.9])
                        .build(),
                );
                let joint = RopeJointBuilder::new(2.4)
                    .local_anchor2(point![0.0, 0.0, n(&d, "mast_top_z_m")])
                    .contacts_enabled(false);
                world.joints.insert(rail, handle, joint, true);
                Some(rail)
            } else {
                None
            };
            world.handles.push(handle);
            world.hulls.push(hull);
            world.carriers.push(carrier);
            world.designs.push(d);
            world.last.push([r.x, r.y, r.z]);
            world.last_alive.push(r.alive);
            world.grasps.push(None);
            world.rescuing.push(None);
            world
                .leg_cmd
                .push(n(&world.designs[world.designs.len() - 1], "leg_nominal_m"));
            world.jump_until.push(-1.0);
            world.jump_ready.push(0.0);
            world.jump_strength.push(0.0);
            world.tuck_until.push(-1.0);
            world.climb_push_until.push(-1.0);
            world.climb_ready.push(0.0);
            world.climb_rise.push(0.0);
            world.climb_target_z.push(0.0);
            world.hip_angle.push(0.0);
            world.upset_since.push(-1.0);
            world.righting.push(false);
            world.righting_since.push(0.0);
            world.right_goal.push(None);
            world.roll_sign.push(1.0);
            world.drone_bodies.push(Vec::new());
        }
        world.queries.update(&world.colliders);
        world
    }

    fn hull(
        d: &Value,
        crouched: bool,
        arm_q: Option<[f64; 7]>,
        tool: bool,
        legs: Option<[f64; 2]>,
    ) -> ColliderBuilder {
        Self::hull_with_arms(d,crouched,arm_q,None,tool,legs)
    }
    /// Wheel-legged hull: balancing body at the hip axle, gimbal on top, wheels at the leg
    /// ends, the legs turned `swing` about the hip axle (serial legs only).
    fn leg_hull(d: &Value, legs: [f64; 2], swing: f64) -> ColliderBuilder {
        let size = p3(&d["chassis_size_m"]);
        let hip = n(d, "hip_height_m");
        let mut shapes = vec![
            (
                Isometry::translation(0.0, 0.0, hip),
                // Keep the coarse trunk behind the upper edge of its exposed armor.
                SharedShape::round_cuboid(size.x * 0.45, size.y * 0.42, size.z / 2.0, 0.012),
            ),
            (
                Isometry::translation(0.0, 0.0, n(d, "muzzle_z_m") - 0.07),
                SharedShape::cuboid(0.09, 0.10, 0.06),
            ),
        ];
        // Wheel spheres sit inside the tyre so the leg rays, not the spheres, carry it.
        for (side, leg) in [(-1.0, legs[0]), (1.0, legs[1])] {
            let y = side * n(d, "track_m") / 2.0;
            shapes.push((
                if swing == 0.0 {
                    Isometry::translation(0.0, y, hip - leg)
                } else {
                    Isometry::translation(-leg * swing.sin(), y, hip - leg * swing.cos())
                },
                SharedShape::ball(n(d, "wheel_radius_m") * 0.6),
            ));
        }
        ColliderBuilder::compound(shapes)
    }
    fn hull_with_arms(d: &Value,crouched:bool,arm_q:Option<[f64;7]>,arm2_q:Option<[f64;7]>,tool:bool,legs:Option<[f64;2]>) -> ColliderBuilder {
        if let Some(legs) = legs {
            return Self::leg_hull(d, legs, 0.0);
        }
        let size = p3(&d["chassis_size_m"]);
        let mut shapes = Vec::new();
        // The chassis box's underside sits the design's ground clearance above the ground at
        // static ride; the body origin rides the static sag below it.
        let clearance = n(d, "ground_clearance_m") + n(d, "suspension_static_compression_m") + 0.012;
        shapes.push((
            Isometry::translation(0.0, 0.0, clearance + size.z / 2.0),
            SharedShape::round_cuboid(size.x * 0.42, size.y * 0.42, size.z / 2.0, 0.012),
        ));
        if d["role"] == "aerial" {
            let count=d["rotor_count"].as_u64().unwrap_or(4);
            let centers=if count==6 {(0..6).map(|k| {let a=k as f64*std::f64::consts::TAU/6.0;[0.46*a.cos(),0.46*a.sin()]}).collect::<Vec<_>>()} else {vec![[-0.33,-0.33],[-0.33,0.33],[0.33,-0.33],[0.33,0.33]]};
            for [x,y] in centers {shapes.push((Isometry::translation(x,y,0.30),SharedShape::cylinder(0.08,n(d,"guard_radius_m"))));}
            // Cylinders in Rapier are Y-up; rotate the closed guard envelope to Z-up.
            for item in shapes.iter_mut().skip(1) {
                item.0.rotation =
                    UnitQuaternion::from_axis_angle(&Vector::x_axis(), std::f64::consts::FRAC_PI_2);
            }
            for y in [-0.11, 0.11] {
                shapes.push((
                    Isometry::translation(0.0, y, 0.018),
                    SharedShape::cuboid(0.14, 0.012, 0.018),
                ));
            }
        } else {
            let h = if d["transform_allowed"]==true {n(d,"muzzle_z_m")} else if crouched {
                0.205
            } else {
                n(d, "muzzle_z_m").max(0.40)
            };
            if d["role"] != "engineer" {
                shapes.push((
                    Isometry::translation(0.0, 0.0, if d["transform_allowed"]==true {h+0.005} else {h-0.07}),
                    SharedShape::cuboid(0.09, 0.10, 0.06),
                ));
            }
            for x in [-size.x / 2.0, size.x / 2.0] {
                for y in [-size.y / 2.0, size.y / 2.0] {
                    shapes.push((Isometry::translation(x, y, clearance + 0.03), SharedShape::ball(0.018)));
                }
            }
        }
        let arms=[("arm",arm_q),("arm2",if d["arm_count"]==2 {Some(arm2_q.unwrap_or_else(||arm_stowed(&d["arm2"])))} else {None})];
        for (name,state) in arms {if let Some(q) = state {
            // Lift mast, telescoping boom and wrist/tool follow the joint state, so the
            // arm collides where it is drawn. The tool is omitted while it grips a robot.
            let arm = &d[name];
            let base = p3(&arm["base_m"]);
            let mast = p3(&arm["mast_half_size_m"]);
            shapes.push((
                Isometry::translation(base.x, base.y, base.z + mast.z),
                SharedShape::cuboid(mast.x, mast.y, mast.z),
            ));
            let [tail, shoulder, wrist, tip] = arm_points(arm, &q);
            let _ = shoulder;
            shapes.push((
                Isometry::identity(),
                SharedShape::capsule(tail, wrist, 0.022),
            ));
            if tool {
                shapes.push((
                    Isometry::identity(),
                    SharedShape::capsule(wrist, tip, 0.018),
                ));
            }
        }
        }
        ColliderBuilder::compound(shapes)
    }

    fn prism(&mut self, points: &[[f64; 2]], heights: &[f64], bottom: f64, arena: &Arena) {
        let count = points.len();
        let floor = |y: f64| y.min(arena.height - y) * arena.floor_slope_deg.to_radians().tan();
        let mut vertices: Vec<Point<f64>> = points
            .iter()
            .zip(heights)
            .map(|(p, z)| point![p[0], p[1], floor(p[1]) + z])
            .collect();
        vertices.extend(
            points
                .iter()
                .map(|p| point![p[0], p[1], floor(p[1]) + bottom]),
        );
        let triangles = triangulate(points);
        let mut indices = triangles.clone();
        indices.extend(triangles.iter().map(|t| {
            [
                t[2] + count as u32,
                t[1] + count as u32,
                t[0] + count as u32,
            ]
        }));
        for i in 0..count {
            let j = (i + 1) % count;
            indices.push([i as u32, (i + count) as u32, j as u32]);
            indices.push([j as u32, (i + count) as u32, (j + count) as u32]);
        }
        self.colliders.insert(
            ColliderBuilder::trimesh(vertices, indices)
                .expect("valid field mesh")
                .friction(0.8)
                .restitution(0.02)
                .build(),
        );
    }

    /// One continuous mesh for a humped road section (top and both long sides down to its
    /// base), so neither wheels nor bodies catch on seams between strips.
    fn humps(&mut self, t: &Terrain, arena: &Arena) {
        let floor = |y: f64| y.min(arena.height - y) * arena.floor_slope_deg.to_radians().tan();
        let count = (t.w / t.bump_pitch * 16.0).round().max(1.0) as usize;
        let (y0, y1) = (t.y, t.y + t.h);
        let mut vertices = Vec::new();
        for i in 0..=count {
            let x = t.x + t.w * i as f64 / count as f64;
            for y in [y0, y1] {
                vertices.push(point![x, y, floor(y) + terrain_elevation(t, x, y)]);
            }
            for y in [y0, y1] {
                vertices.push(point![x, y, floor(y) + t.base_elevation]);
            }
        }
        let mut indices = Vec::new();
        for i in 0..count as u32 {
            let (a, b) = (4 * i, 4 * i + 4);
            // Top (x0 y0, x1 y0, x1 y1, x0 y1), then the y0 and y1 sides.
            indices.push([a, b, b + 1]);
            indices.push([a, b + 1, a + 1]);
            indices.push([a + 2, b + 2, b]);
            indices.push([a + 2, b, a]);
            indices.push([a + 1, b + 1, b + 3]);
            indices.push([a + 1, b + 3, a + 3]);
        }
        self.colliders.insert(
            ColliderBuilder::trimesh_with_flags(vertices, indices, TriMeshFlags::FIX_INTERNAL_EDGES)
                .expect("valid hump mesh")
                .friction(0.8)
                .restitution(0.02)
                .build(),
        );
    }

    /// Ground contact of one wheel, as (suspension length, contact point, ground normal).
    /// A probe below the hub suffices on steps and slopes, which are larger than any tyre.
    /// Over road bumps, smaller than the tyre, it is a circle in its rolling plane probed by
    /// rays fanned across its lower arc, so it rolls over the humps instead of dropping into
    /// the hollows like a point. The first contact sets the length.
    fn wheel_contact(
        &self,
        anchor: Point<f64>,
        axle: UnitQuaternion<f64>,
        radius: f64,
        travel: f64,
    ) -> Option<(f64, Point<f64>, Vector<f64>)> {
        let down = axle * vector![0.0, 0.0, -1.0];
        let roll = axle * vector![1.0, 0.0, 0.0];
        let mut best: Option<(f64, Point<f64>, Vector<f64>)> = None;
        let reach = radius + 0.05;
        let humped = self.hump_areas.iter().any(|a| {
            anchor.x > a[0] - reach && anchor.x < a[2] + reach && anchor.y > a[1] - reach && anchor.y < a[3] + reach
        });
        let fan = if humped { 4 } else { 0 };
        for k in -fan..=fan {
            let angle = k as f64 * 16f64.to_radians();
            let (offset, depth) = (radius * angle.sin(), radius * angle.cos());
            let ray = Ray::new(anchor + roll * offset, down);
            if let Some((_, hit)) = self.queries.cast_ray_and_get_normal(
                &self.bodies,
                &self.colliders,
                &ray,
                travel + depth,
                true,
                QueryFilter::only_fixed(),
            ) {
                let length = (hit.time_of_impact - depth).max(0.0);
                if best.is_none_or(|b| length < b.0) {
                    best = Some((length, ray.point_at(hit.time_of_impact), hit.normal));
                }
            }
        }
        best
    }

    pub fn step(
        &mut self,
        arena: &Arena,
        robots: &mut [Robot],
        actions: &[Action],
        goals: &[ArmGoal],
        now: f64,
    ) {
        // Engineer index, rescued robot, grasp point and gripper tip (world frame).
        let mut rescue_forces: Vec<(usize, usize, Point<f64>, Point<f64>)> = Vec::new();
        let ids: Vec<String> = robots.iter().map(|r| r.id.clone()).collect();
        for i in 0..robots.len() {
            if self.designs[i]["chassis_type"] == "hangar" {
                self.drive_drones(i, arena, &mut robots[i], &actions[i], now);
                continue;
            }
            let r = &mut robots[i];
            let d = &self.designs[i];
            let h = self.handles[i];
            // Rule-driven respawns and developer fixtures are explicit teleports.
            if (vector![
                r.x - self.last[i][0],
                r.y - self.last[i][1],
                r.z - self.last[i][2]
            ])
            .norm()
                > 1e-7
                || (!self.last_alive[i] && r.alive)
            {
                self.bodies[h].set_position(
                    Isometry::from_parts(Translation::new(r.x, r.y, r.z), q(r)),
                    true,
                );
                self.bodies[h].set_linvel(vector![r.vx, r.vy, r.vz], true);
                self.bodies[h].set_angvel(Vector::zeros(), true);
                self.hip_angle[i] = 0.0;
                self.upset_since[i] = -1.0;
                self.righting[i] = false;
                self.roll_sign[i] = 1.0;
            }
            self.last_alive[i] = r.alive;
            self.bodies[h].reset_forces(true);
            self.bodies[h].reset_torques(true);
            let pose = *self.bodies[h].position();
            let velocity = *self.bodies[h].linvel();
            let angular = *self.bodies[h].angvel();
            let a = &actions[i];
            let world_yaw = a.aim.unwrap_or(r.aim);
            let desired = vector![
                world_yaw.cos() * a.pitch.cos(),
                world_yaw.sin() * a.pitch.cos(),
                a.pitch.sin()
            ];
            let local = pose.rotation.inverse() * desired;
            let local_yaw = local.y.atan2(local.x);
            // The mount's own elevation stops: the 2027 heavy lifts its launcher to a steep lob angle,
            // so the clamp has to come from the design instead of a fixed 45°.
            let (pitch_lo, pitch_hi) = {
                let stops = &d["pitch_limits_rad"];
                (
                    stops[0].as_f64().unwrap_or(-0.261799),
                    stops[1].as_f64().unwrap_or(0.785398),
                )
            };
            let local_pitch = local
                .z
                .atan2(local.x.hypot(local.y))
                .clamp(pitch_lo, pitch_hi);
            r.aim = r.heading
                + turn(
                    r.aim - r.heading,
                    local_yaw,
                    n(d, "turret_yaw_rate_rad_s") * PHYSICS_DT,
                );
            r.pitch = approach(
                r.pitch,
                local_pitch,
                n(d, "turret_pitch_rate_rad_s") * PHYSICS_DT,
            );
            r.cooldown = (r.cooldown - PHYSICS_DT).max(0.0);
            if r.arm_count > 0 && d["arm"].is_object() {
                let arm = &d["arm"];
                let goal = &goals[i];
                if self.rescuing[i] != goal.target {
                    self.rescuing[i] = None;
                }
                let mut target = arm_stowed(arm);
                let mut contact = false;
                let mut grasp_world = None;
                match goal.mode.as_str() {
                    "pick" | "assembly" | "manual" if r.alive => {
                        if let Some(q) = goal.joints {
                            target = std::array::from_fn(|k| {
                                let (lo, hi) = arm_limit(arm, k);
                                q[k].clamp(lo, hi)
                            });
                        } else if let (Some(p), Some(dir)) = (goal.point, goal.dir) {
                            let (p, dir) =
                                (point![p[0], p[1], p[2]], vector![dir[0], dir[1], dir[2]]);
                            target = if goal.local {
                                arm_ik(arm, p, dir, goal.roll, goal.grip)
                            } else {
                                arm_ik(
                                    arm,
                                    pose.inverse_transform_point(&p),
                                    pose.rotation.inverse() * dir,
                                    goal.roll,
                                    goal.grip,
                                )
                            };
                        }
                        self.grasps[i] = None;
                    }
                    "rescue" if r.alive => {
                        let j = goal.target.unwrap();
                        let tpose = *self.bodies[self.handles[j]].position();
                        let upright = (tpose.rotation * Vector::z()).z;
                        // A grip holds its point; otherwise re-plan, e.g. after the robot rolled away.
                        let kept = self.grasps[i].filter(|(k, _)| *k == j && r.rescue_contact);
                        let grasp = if upright >= n(arm, "rescue_upright_cos") && kept.is_none() {
                            None
                        } else {
                            kept.map(|(_, g)| g)
                                .or_else(|| self.choose_grasp(i, j, &pose))
                        };
                        self.grasps[i] = grasp.map(|g| (j, g));
                        if let Some(g) = grasp {
                            let world = tpose * g;
                            let local = pose.inverse_transform_point(&world);
                            // Approach horizontally from the mast, tilted toward the grasp height.
                            let base = p3(&arm["base_m"]);
                            let reach = vector![local.x - base.x, local.y - base.y, 0.0];
                            let height = local.z - n(arm, "shoulder_rest_z_m") - 0.25;
                            let dir = vector![reach.x, reach.y, height.min(0.0) * 0.8];
                            let tip_now = pose * arm_points(arm, &r.arm_joints)[3];
                            let error = (tip_now - world).norm();
                            // Closing needs the jaws on the edge; a closed grip then rides out
                            // tracking lag until the edge leaves the workspace.
                            contact = r.rescue_contact && error < 5.0 * n(arm, "grasp_tolerance_m")
                                || error < n(arm, "grasp_tolerance_m");
                            target = arm_ik(arm, local, dir, 0.0, if contact { 0.0 } else { 1.0 });
                            grasp_world = Some((j, world, tip_now));
                        }
                    }
                    _ => self.grasps[i] = None,
                }
                if contact && !r.rescue_contact {
                    self.rescuing[i] = goal.target;
                    self.arm_events.push(json!({"type":"rescue_grip","robot":r.id,"target":ids[goal.target.unwrap()]}));
                }
                r.rescue_contact = contact;
                target=limit_arm_workspace(arm,target);
                let rate_scale = arm_rate_scale(&goal.mode);
                let rates = arm_vec(arm, "rates");
                for (k, q) in r.arm_joints.iter_mut().enumerate() {
                    *q = approach(*q, target[k], rates[k] * rate_scale * PHYSICS_DT);
                }
                if let (true, Some((j, world, tip))) =
                    (contact && r.arm_joints[6] < 0.35, grasp_world)
                {
                    rescue_forces.push((i, j, world, tip));
                }
                let points = arm_points(arm, &r.arm_joints);
                let tip = pose * points[3];
                r.arm_tip = [tip.x, tip.y, tip.z];
                r.arm_mode = if r.alive {
                    goal.mode.clone()
                } else {
                    "stow".into()
                };
                if r.arm_mode.is_empty() {
                    r.arm_mode = "stow".into();
                }
                let (lo, hi) = arm_limit(arm, 2);
                r.arm_extension = ((r.arm_joints[2] - lo) / (hi - lo)).clamp(0.0, 1.0);
                // About 2.4 kg of boom and wrist hang off the mast; carried units sit aft.
                let held_unit = r.unit_pickup.as_ref().is_some_and(|s| s.held);
                // A fitted modular 42 mm launcher adds its own mass to the heavy robot.
                let module = if ["fitted", "installed"].contains(&r.launcher.as_str()) { n(d, "launcher_module_kg") } else { 0.0 };
                let payload = 0.45 * (r.carried_units as f64 + if held_unit {1.0} else {0.0}) + module;
                let mass = n(d, "mass_kg") + payload;
                let mut com = p3(&d["com_m"]);
                let lever = (points[1].coords + points[2].coords) * 0.5;
                com.x = (0.45 * r.carried_units as f64 * -0.17 + if held_unit {0.45 * points[3].x} else {0.0} + 2.4 * lever.x) / mass;
                com.y = (if held_unit {0.45 * points[3].y} else {0.0} + 2.4 * lever.y) / mass;
                self.bodies[h].set_additional_mass_properties(
                    MassProperties::new(point![com.x, com.y, com.z], mass, p3(&d["inertia_kgm2"])),
                    true,
                );
                r.mass_kg = mass;
                self.colliders[self.hulls[i]].set_shape(
                    Self::hull(d, false, Some(r.arm_joints), goal.mode != "rescue", None)
                        .build()
                        .shared_shape()
                        .clone(),
                );
            }
            if r.arm_count==2 && d["arm2"].is_object() {
                let arm=&d["arm2"];let goal=goals[i].secondary.as_deref();
                let mut target=arm_stowed(arm);
                if r.alive {if let Some(goal)=goal {
                    if let Some(values)=goal.joints {target=std::array::from_fn(|k| {let(lo,hi)=arm_limit(arm,k);values[k].clamp(lo,hi)});}
                    else if let (Some(p),Some(dir))=(goal.point,goal.dir) {
                        let p=point![p[0],p[1],p[2]];let dir=vector![dir[0],dir[1],dir[2]];
                        target=arm_ik(arm,if goal.local {p} else {pose.inverse_transform_point(&p)},if goal.local {dir} else {pose.rotation.inverse()*dir},goal.roll,goal.grip);
                    }
                }}
                target=limit_arm_workspace(arm,target);
                let rate_scale=goal.map_or(1.0,|goal|arm_rate_scale(&goal.mode));
                let rates=arm_vec(arm,"rates");for(k,q)in r.arm2_joints.iter_mut().enumerate(){*q=approach(*q,target[k],rates[k]*rate_scale*PHYSICS_DT);}
                let second=arm_points(arm,&r.arm2_joints);let first=arm_points(&d["arm"],&r.arm_joints);
                let tip=pose*second[3];r.arm2_tip=[tip.x,tip.y,tip.z];r.arm2_mode=if r.alive {goal.map(|g|g.mode.clone()).unwrap_or_else(||"stow".into())} else {"stow".into()};
                let held=r.unit_pickup.as_ref().filter(|s|s.held);
                let module=if ["fitted","installed"].contains(&r.launcher.as_str()) {n(d,"launcher_module_kg")} else {0.0};
                let payload=0.45*(r.carried_units as f64+if held.is_some(){1.0}else{0.0})+module;let mass=n(d,"mass_kg")+payload;
                let held_tip=if held.is_some_and(|s|s.arm==1){second[3]}else{first[3]};
                let lever=(first[1].coords+first[2].coords+second[1].coords+second[2].coords)*0.5*1.8;
                let mut com=p3(&d["com_m"]);com.x=(0.45*r.carried_units as f64*-0.17+if held.is_some(){0.45*held_tip.x}else{0.0}+lever.x)/mass;
                com.y=(if held.is_some(){0.45*held_tip.y}else{0.0}+lever.y)/mass;
                self.bodies[h].set_additional_mass_properties(MassProperties::new(point![com.x,com.y,com.z],mass,p3(&d["inertia_kgm2"])),true);r.mass_kg=mass;
                self.colliders[self.hulls[i]].set_shape(Self::hull_with_arms(d,false,Some(r.arm_joints),Some(r.arm2_joints),goals[i].mode!="rescue",None).build().shared_shape().clone());
            }
            if r.role == "aerial" {
                let a = &actions[i];
                let pad = arena.pads[&r.team];
                let lane = &arena.flight_lanes[&r.team];
                let on_pad = dist(r.x, r.y, pad[0], pad[1]) < 0.7;
                let request = a.altitude.unwrap_or(if r.support_active {
                    1.85
                } else {
                    height_at(arena, pad[0], pad[1])
                });
                let minimum = 1.5 + field_height(arena, r.x, r.y, true);
                let target_z = if on_pad && request < 1.15 {
                    height_at(arena, pad[0], pad[1])
                } else {
                    request.clamp(minimum.min(2.08), 2.08)
                };
                let mut desired = vector![
                    a.mv[0] * n(d, "speed_reference_mps"),
                    a.mv[1] * n(d, "speed_reference_mps"),
                    0.0
                ];
                if !r.support_active {
                    desired.x = (pad[0] - r.x).clamp(-1.2, 1.2);
                    desired.y = (pad[1] - r.y).clamp(-1.2, 1.2);
                }
                if r.x <= n(lane, "min_x") + 0.1 && desired.x < 0.0
                    || r.x >= n(lane, "max_x") - 0.1 && desired.x > 0.0
                {
                    desired.x = 0.0;
                }
                let mut force = vector![
                    (desired.x - velocity.x) * r.mass_kg * 2.0,
                    (desired.y - velocity.y) * r.mass_kg * 2.0,
                    r.mass_kg * (9.81 + 5.0 * (target_z - r.z) - 4.0 * velocity.z)
                ];
                let tilt = (if field_height(arena, r.x, r.y, true) > 0.3 {
                    0.12
                } else {
                    n(d, "flight_tilt_limit_rad")
                })
                .tan();
                let horizontal = force.x.hypot(force.y);
                let maximum = force.z.max(0.0) * tilt;
                if horizontal > maximum {
                    force.x *= maximum / horizontal;
                    force.y *= maximum / horizontal;
                }
                let yaw = a.heading.unwrap_or(r.heading);
                force.z = force.z.max(0.1);
                let up = force.normalize();
                let forward_hint = vector![yaw.cos(), yaw.sin(), 0.0];
                let right = up.cross(&forward_hint).normalize();
                let forward = right.cross(&up).normalize();
                let desired_q = UnitQuaternion::from_rotation_matrix(
                    &nalgebra::Rotation3::from_matrix_unchecked(nalgebra::Matrix3::from_columns(
                        &[forward, right, up],
                    )),
                );
                let error = (desired_q * pose.rotation.inverse()).scaled_axis();
                let inertia = p3(&d["inertia_kgm2"]);
                let torque = (error * 18.0 - angular * 7.0).component_mul(&inertia);
                let thrust =
                    if !r.support_active && on_pad && r.z - height_at(arena, r.x, r.y) < 0.08 {
                        0.0
                    } else {
                        force.norm().min(n(d, "thrust_max_n")).max(0.0)
                    };
                let lift = pose.rotation * vector![0.0, 0.0, thrust];
                self.bodies[h].add_force(lift, true);
                self.bodies[h].add_torque(torque, true);
                r.motor_power_w = (thrust.powf(1.5)
                    / (2.0 * 1.225 * d["rotor_count"].as_f64().unwrap_or(4.0) * std::f64::consts::PI * n(d, "rotor_radius_m").powi(2))
                        .sqrt()
                    / 0.72
                    + 18.0)
                    .min(n(d, "flight_power_max_w"));
                r.power_w = r.motor_power_w;
                r.battery_remaining_wh =
                    (r.battery_remaining_wh - r.motor_power_w * PHYSICS_DT / 3600.0).max(0.0);
                r.grounded = on_pad && r.z - height_at(arena, r.x, r.y) < 0.08;
                let rail = self.carriers[i].unwrap();
                self.bodies[rail].set_next_kinematic_translation(vector![
                    r.x.clamp(n(lane, "min_x"), n(lane, "max_x")),
                    n(lane, "center_y"),
                    3.9
                ]);
                continue;
            }
            let a = &actions[i];
            let legged = d["chassis_type"] == "wheel_legged";
            // The referee cuts chassis power on destruction, over-power cut-off or a flat
            // battery: motors go limp, so the chassis coasts on its own momentum.
            let powered = r.alive && now >= r.power_cutoff_until && r.battery_remaining_wh > 0.0;
            r.powered = powered;
            let disabled = !powered || r.deployed;
            let wheel_r = n(d, "wheel_radius_m");
            let (leg_min, leg_max) = (n(d, "leg_min_m"), n(d, "leg_max_m"));
            let mut leg_thrust = 0.0;
            // Serial legs have full-turn hip joints. A robot that comes to rest on its side,
            // nose or roof swings its legs round to the floor and turns itself back upright.
            let serial = legged && d["leg_type"] == "serial";
            let wrap_pi = |a: f64| (a + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI;
            let down = pose.rotation.inverse() * vector![0.0, 0.0, -1.0];
            // Hip angle that points the legs at the floor, once the floor lies in their plane.
            let mut floor_angle = None;
            if serial {
                if !powered {
                    self.righting[i] = false;
                    self.upset_since[i] = -1.0;
                } else if self.righting[i] {
                    if now - self.righting_since[i] > 4.0 {
                        // Wedged (against a wall or a step, say): settle and try the other way.
                        self.righting[i] = false;
                        self.upset_since[i] = now;
                        self.roll_sign[i] = -self.roll_sign[i];
                    }
                } else if -down.z < 0.6 && velocity.norm() < 0.5 && angular.norm() < 3.0 {
                    // Upset and resting on something (a robot cannot hang still in the air).
                    if self.upset_since[i] < 0.0 {
                        self.upset_since[i] = now;
                    }
                    if now - self.upset_since[i] >= n(d, "self_right_delay_s") {
                        self.righting[i] = true;
                        self.righting_since[i] = now;
                        self.right_goal[i] = None;
                        self.arm_events.push(json!({"type":"self_righting","robot":r.id}));
                    }
                } else {
                    self.upset_since[i] = -1.0;
                }
                let alpha = self.hip_angle[i];
                if down.x.hypot(down.z) > 0.35 {
                    floor_angle = Some(alpha + wrap_pi((-down.x).atan2(-down.z) - alpha));
                }
                let rest = (alpha / std::f64::consts::TAU).round() * std::f64::consts::TAU;
                let goal = if self.righting[i] {
                    if let (None, Some(target)) = (self.right_goal[i], floor_angle) {
                        // From the roof the body keeps turning the way the legs swung, so the
                        // hips finish a full revolution; otherwise it turns back the short way.
                        self.right_goal[i] = Some(if (target - rest).abs() > 2.4 {
                            rest + std::f64::consts::TAU * (target - rest).signum()
                        } else {
                            rest
                        });
                    }
                    floor_angle.unwrap_or(alpha)
                } else {
                    rest
                };
                if powered {
                    self.hip_angle[i] = approach(alpha, goal, n(d, "hip_speed_rad_s") * PHYSICS_DT);
                }
            }
            let righting = serial && self.righting[i];
            // Legs stay tucked until the body has rolled the floor close to their plane, so a leg
            // braced sideways cannot prop the body on its side.
            let planted_angle = down.y.abs() < 0.45
                && floor_angle.is_some_and(|f| (f - self.hip_angle[i]).abs() < 0.35);
            if legged {
                // Step climb: with a step the legs can reach in front, a short low push puts the
                // wheels on the tread and the drive carries the hull on, then the legs retract.
                // The push is a fraction of a jump, sized to the step it found.
                let step_limit = n(d, "step_climb_m");
                if powered
                    && a.climb > 0.0
                    && step_limit > 0.0
                    && r.grounded
                    && !righting
                    && now >= self.climb_ready[i]
                    && now >= self.jump_until[i]
                {
                    let heading = a.heading.unwrap_or(r.heading);
                    let here = height_at(arena, r.x, r.y);
                    // Probe from just past the nose outwards: a chassis pressed against a step
                    // face must still see the tread, while a deeper probe would look over a
                    // 0.3 m stair onto the next one and find a rise it cannot mount.
                    let edge = r.radius.max(0.22);
                    let rise = [edge + 0.02, edge + 0.16, edge + 0.32]
                        .iter()
                        .map(|reach| {
                            height_at(
                                arena,
                                r.x + heading.cos() * reach,
                                r.y + heading.sin() * reach,
                            ) - here
                        })
                        .find(|rise| *rise > 0.04 && *rise <= step_limit + 0.01)
                        .unwrap_or(0.0);
                    // Only a real ledge within the legs' reach starts a climb; a curb lower than
                    // the wheels, or a wall it cannot mount, leaves the drive in charge.
                    if rise > 0.04 {
                        // The legs extend to their full stroke and the carry below servos the hull
                        // onto the tread. A climb never fires the leg thrusters: the servo is the
                        // only thing lifting the hull, so the step face cannot lever it over.
                        let share = (rise / step_limit).clamp(0.0, 1.0);
                        self.jump_until[i] = now + n(d, "jump_duration_s");
                        self.jump_strength[i] = 0.05 + 0.12 * share;
                        self.jump_ready[i] = now + n(d, "jump_cooldown_s").min(0.55);
                        self.climb_push_until[i] = self.jump_until[i] + CLIMB_CARRY_S;
                        self.climb_ready[i] = self.climb_push_until[i] + CLIMB_COOLDOWN_S;
                        self.climb_rise[i] = rise;
                        // Just enough lift for the chassis skirt to clear the tread: any more and
                        // the hull sails over the next step instead of settling on this one.
                        self.climb_target_z[i] = r.z + rise + 0.08;
                        self.arm_events
                            .push(json!({"type":"step_climb","robot":r.id,"rise":rise}));
                    }
                }
                // Jump: legs push at a fixed thrust until they reach full length, then tuck.
                if powered && a.jump > 0.0 && r.grounded && !righting && now >= self.jump_ready[i] {
                    self.jump_until[i] = now + n(d, "jump_duration_s");
                    self.jump_strength[i] = a.jump;
                    self.jump_ready[i] = now + n(d, "jump_cooldown_s");
                    self.arm_events
                        .push(json!({"type":"jump","robot":r.id,"strength":a.jump}));
                }
                if powered && now < self.jump_until[i] {
                    // A step climb uses the same leg stroke as a jump but with a far smaller
                    // strength, and the carry below only starts driving once the wheels are level
                    // with the tread — that split is what stops the face from levering the hull
                    // over its nose.
                    let climbing = now < self.climb_push_until[i];
                    leg_thrust = r.mass_kg
                        * 9.81
                        * 0.5
                        * (1.0 + n(d, "jump_thrust_gain") * self.jump_strength[i])
                        * if climbing { 0.55 } else { 1.0 };
                    self.leg_cmd[i] = leg_max;
                    // A step climb keeps the legs long through the carry: tucking them at the end
                    // of the stroke dropped the hull onto the step face, where it balanced out of
                    // reach of the drive and no further climb could start.
                    if self.jump_until[i] - now <= PHYSICS_DT + 1e-9
                        && now >= self.climb_push_until[i]
                    {
                        self.tuck_until[i] = now + n(d, "jump_tuck_s");
                    }
                } else {
                    let wanted = if !powered {
                        leg_min
                    } else if righting {
                        // Tucked while swinging, then pushing the body off the floor.
                        if planted_angle { n(d, "leg_nominal_m") } else { leg_min }
                    } else if now < self.climb_push_until[i] {
                        // Climb carry: hold the stroke so the chassis clears the tread and the
                        // wheels can reach it, instead of dropping back into the step face.
                        leg_max
                    } else if now < self.tuck_until[i] {
                        leg_min + 0.01
                    } else {
                        a.leg_height
                            .unwrap_or(n(d, "leg_nominal_m"))
                            .clamp(leg_min, leg_max)
                    };
                    let rate = n(d, "leg_speed_mps") * PHYSICS_DT * if powered { 1.0 } else { 3.0 };
                    self.leg_cmd[i] = approach(self.leg_cmd[i], wanted, rate);
                }
                // Carry: the wheels have no grip while the hull is over the lip, so the body is
                // servoed onto the tread: rise just above it, then move along the heading at a
                // walking pace until the wheels find the step.
                if powered && now < self.climb_push_until[i] {
                    let heading = a.heading.unwrap_or(r.heading);
                    let body_z = self.bodies[h].position().translation.z;
                    let velocity = *self.bodies[h].linvel();
                    // Rise first, drive second. Pushing forward while the skirt is still below the
                    // lip turns the step face into a pivot and rolls the hull over its nose, which
                    // is what made a finished climb look like a twitch: climb, tip, fall, repeat.
                    // Two thirds of the step is enough for the wheels to clear the lip; asking for
                    // the full lift stalls the servo against the leg stroke.
                    let start_z = self.climb_target_z[i] - self.climb_rise[i] - 0.08;
                    let crossing = body_z >= start_z + self.climb_rise[i] * 0.9;
                    // Keep lifting until the full clearance is reached — stopping at the crossing
                    // point left the front wheel a centimetre under the lip, and the drive then
                    // pressed into the face forever.
                    let desired_z = if body_z >= self.climb_target_z[i] - 0.01 {
                        0.0
                    } else {
                        CLIMB_RISE_MPS
                    };
                    let forward_target = if crossing { CLIMB_FORWARD_MPS } else { 0.0 };
                    let forward = velocity.x * heading.cos() + velocity.y * heading.sin();
                    let mut force = vector![
                        heading.cos() * (forward_target - forward),
                        heading.sin() * (forward_target - forward),
                        desired_z - velocity.z
                    ] * (r.mass_kg * CLIMB_SERVO_GAIN / PHYSICS_DT);
                    let limit = r.mass_kg * 9.81 * CLIMB_SERVO_LIMIT;
                    if force.norm() > limit {
                        force *= limit / force.norm();
                    }
                    self.bodies[h].add_force(force, true);
                    // The carry pulls on the centre of mass while the wheels are still against the
                    // step face, so pitch would keep growing until the hull went over its nose.
                    // Hold it upright and bleed the pitch/roll rate; the yaw of a spinning chassis
                    // is left alone.
                    let up = self.bodies[h].rotation() * vector![0.0, 0.0, 1.0];
                    let tilt = up.cross(&vector![0.0, 0.0, 1.0]);
                    let spin = *self.bodies[h].angvel();
                    let torque = tilt * (r.mass_kg * CLIMB_UPRIGHT_GAIN)
                        - vector![spin.x, spin.y, 0.0] * (r.mass_kg * CLIMB_UPRIGHT_DAMP);
                    self.bodies[h].add_torque(torque, true);
                }
            }
            let wheels: &[(f64, f64)] = if legged {
                &[(0.0, -1.0), (0.0, 1.0)]
            } else {
                &[(-1.0, -1.0), (-1.0, 1.0), (1.0, -1.0), (1.0, 1.0)]
            };
            let hip = if legged {
                n(d, "hip_height_m")
            } else {
                wheel_r + n(d, "suspension_rest_m")
            };
            let reach = if legged {
                leg_max + wheel_r + 0.03
            } else {
                wheel_r + n(d, "suspension_rest_m") + n(d, "suspension_travel_m")
            };
            let mut normal_total = 0.0;
            let mut contacts = Vec::new();
            let mut leg_power = 0.0;
            for (wheel, (x, y)) in wheels.iter().enumerate() {
                let local = point![
                    x * n(d, "wheelbase_m") / 2.0,
                    y * n(d, "track_m") / 2.0,
                    hip
                ];
                let anchor = pose * local;
                // The tyre turns with its axle: steered on a swerve module, 45 degrees on omni.
                let yaw = if d["chassis_type"] == "swerve" {
                    r.steering_angles[wheel]
                } else if d["chassis_type"] == "omni" {
                    -x * y * std::f64::consts::FRAC_PI_4
                } else {
                    0.0
                };
                let axle = if serial {
                    pose.rotation * UnitQuaternion::from_axis_angle(&Vector::y_axis(), self.hip_angle[i])
                } else {
                    pose.rotation * UnitQuaternion::from_axis_angle(&Vector::z_axis(), yaw)
                };
                // While righting, a serial leg pushes only on floor-like surfaces: a hip below the
                // floor or inside a wall would otherwise find the underside or no normal at all.
                let hit = self.wheel_contact(anchor, axle, wheel_r, reach - wheel_r)
                    .filter(|(_, _, normal)| !righting || normal.z > 0.3);
                if let Some((length, point, normal)) = hit {
                    let point_velocity = self.bodies[h].velocity_at_point(&anchor);
                    let support = if legged {
                        // Virtual leg spring toward the commanded length; limp legs only
                        // keep a weak passive stiffness down to their mechanical stop.
                        let ratio = if powered {
                            1.0
                        } else {
                            n(d, "leg_passive_ratio")
                        };
                        let spring = n(d, "leg_stiffness_npm") * ratio * (self.leg_cmd[i] - length)
                            - point_velocity.dot(&normal)
                                * n(d, "leg_damping_nspm")
                                * ratio.max(0.4);
                        let stop = (leg_min - 0.01 - length).max(0.0) * 40000.0;
                        let force = if righting {
                            // A leg swung onto the floor presses only as hard as its joint
                            // motors allow; there is no rigid stop behind it.
                            spring.clamp(0.0, r.mass_kg * 9.81 * 0.8)
                        } else {
                            spring.max(leg_thrust).max(0.0) + stop
                        };
                        leg_power += (point_velocity.dot(&normal) * force).abs() * 0.5;
                        force.min(r.mass_kg * 9.81 * 3.0)
                    } else {
                        // The spring's free length may exceed the hub's rest position (a soft
                        // spring that still rides at the design height).
                        let free = n(d, "suspension_free_length_m").max(n(d, "suspension_rest_m"));
                        let compression = (free - length).max(0.0);
                        (compression * n(d, "suspension_stiffness_npm")
                            - point_velocity.dot(&normal) * n(d, "suspension_damping_nspm"))
                        .clamp(0.0, r.mass_kg * 9.81 * 2.0)
                    };
                    // A leg is a strut to the tyre patch: its load acts at the ground, which
                    // makes the balancing chassis an inverted pendulum.
                    let at = if legged { point } else { anchor };
                    self.bodies[h].add_force_at_point(normal * support, at, true);
                    normal_total += support;
                    contacts.push((point, normal, support));
                    r.suspension[wheel] = length;
                    r.wheel_loads_n[wheel] = support;
                    if legged {
                        // A leg reaches at most its commanded length; the ground may be farther.
                        r.legs[wheel] = length.min(self.leg_cmd[i]).max(leg_min - 0.01);
                    }
                } else {
                    r.suspension[wheel] = reach - wheel_r;
                    r.wheel_loads_n[wheel] = 0.0;
                    if legged {
                        r.legs[wheel] = self.leg_cmd[i];
                    }
                }
            }
            if legged {
                r.suspension[2] = r.suspension[0];
                r.suspension[3] = r.suspension[1];
                r.wheel_loads_n[2] = 0.0;
                r.wheel_loads_n[3] = 0.0;
            }
            // A chassis straddling a step can hang both wheels over the tread edges: the casts find
            // nothing, the drive loses its grip budget and the robot sits on its belly forever.
            // Being within a step of the highest ground under the chassis while upright is still
            // standing on it. A hull parked on a 0.31 m high-point tread with its wheels over the edge
            // leans about 32°, so the bound sits a little past that.
            let upright = (pose.rotation * vector![0.0, 0.0, 1.0]).z > 0.80;
            let side = r.heading + std::f64::consts::FRAC_PI_2;
            let (fore, starboard) = (r.heading.cos(), r.heading.sin());
            let centre = height_at(arena, r.x, r.y);
            // A tread *above* the hull is the next step, not the ground holding the chassis up.
            // Counting it made a robot parked against a stair read as airborne, which then blocked
            // the step climb (`grounded` is its gate) and froze it on the edge of the tread.
            let underneath = [
                height_at(arena, r.x + fore * 0.3, r.y + starboard * 0.3),
                height_at(arena, r.x - fore * 0.3, r.y - starboard * 0.3),
                height_at(arena, r.x + side.cos() * 0.2, r.y + side.sin() * 0.2),
                height_at(arena, r.x - side.cos() * 0.2, r.y - side.sin() * 0.2),
            ]
            .into_iter()
            .filter(|h| *h <= r.z + 0.05)
            .fold(centre, f64::max);
            let straddling = contacts.is_empty()
                && upright
                && (r.z - underneath).abs() < 0.15
                && r.z - underneath > -0.3;
            if straddling {
                normal_total = normal_total.max(r.mass_kg * 9.81);
            }
            r.grounded = !contacts.is_empty() || straddling;
            if legged {
                self.colliders[self.hulls[i]].set_shape(
                    Self::leg_hull(d, r.legs, if serial { self.hip_angle[i] } else { 0.0 })
                        .build()
                        .shared_shape()
                        .clone(),
                );
            }
            let mut limit = r.power_limit;
            // 2026 chassis energy; 2027 robots carry their full effective limit (engine-set).
            if self.season == 2026 && (r.role == "hero" || r.role == "infantry") {
                if r.energy <= 0.0 {
                    limit = 35.0;
                } else if r.energy >= 25000.0 {
                    limit = (limit * 1.25).min(200.0);
                }
            }
            if self.season == 2026 && now < r.power_boost_until {
                limit = (limit * 2.0).min(200.0);
            }
            // Without an explicit budget the chassis keeps to its limit, except that the
            // supercap firmware backs the drive while the robot is on road bumps.
            let on_humps = self.hump_areas.iter().any(|h| {
                pose.translation.x > h[0] && pose.translation.x < h[2] && pose.translation.y > h[1] && pose.translation.y < h[3]
            });
            // The supercapacitor's DC-DC stage does not deliver all of its stored energy to the
            // motors: 40 % is lost as heat, so the usable boost is only 60 % of the capacitor's
            // rated output and the store drains 1/0.6 of the power the chassis actually gains.
            let supercap_useful = n(d, "voltage_v")
                * n(d, "supercap_max_current_a")
                * (1.0 - SUPERCAP_LOSS);
            let default_budget = if on_humps && r.supercap_energy_j > 100.0 {
                limit + supercap_useful
            } else {
                limit
            };
            let budget = if disabled {
                0.0
            } else {
                a.power.unwrap_or(default_budget).clamp(0.0, 200.0)
            };
            let motor_budget = budget.min(
                limit
                    + if r.supercap_energy_j > 0.0 {
                        supercap_useful
                    } else {
                        0.0
                    },
            );
            let speed = n(d, "speed_reference_mps") * if r.crouched { 0.5 } else { 1.0 };
            // A climbing chassis drives itself onto the ledge along its own heading, whether or
            // not the driver also holds a move command.
            let climbing = powered && now < self.climb_push_until[i];
            let mut desired = if disabled || righting {
                Vector::zeros()
            } else {
                vector![a.mv[0] * speed, a.mv[1] * speed, 0.0]
            };
            if climbing && desired.norm() < speed * 0.5 {
                desired = vector![
                    r.heading.cos() * speed * 0.8,
                    r.heading.sin() * speed * 0.8,
                    0.0
                ];
            }
            let wrap = |a: f64| {
                (a + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI
            };
            let body_x = pose.rotation * Vector::x();
            let forward = {
                let f = vector![body_x.x, body_x.y, 0.0];
                if f.norm() > 1e-6 {
                    f.normalize()
                } else {
                    vector![r.heading.cos(), r.heading.sin(), 0.0]
                }
            };
            let side = vector![-forward.y, forward.x, 0.0];
            let planar = vector![velocity.x, velocity.y, 0.0];
            let commanded = !righting && (climbing || a.mv[0].hypot(a.mv[1]) >= 0.01);
            // Yaw goal: explicit heading; otherwise mecanum robots face their motion and a
            // two-wheeled chassis turns its wheels (forward or reverse) onto the motion.
            let mut yaw_goal = if righting { None } else { a.heading };
            if yaw_goal.is_none() && desired.norm() > 0.02 {
                let course = desired.y.atan2(desired.x);
                yaw_goal = Some(
                    if legged
                        && wrap(course + std::f64::consts::PI - r.heading).abs()
                            < wrap(course - r.heading).abs()
                    {
                        course + std::f64::consts::PI
                    } else {
                        course
                    },
                );
            }
            if legged {
                desired = forward * desired.dot(&forward);
            }
            let mut swerve_yaw_factor=1.0;
            if d["chassis_type"]=="swerve" {
                let vx=desired.dot(&forward);let vy=desired.dot(&side);
                let yaw=a.yaw_rate.unwrap_or_else(||yaw_goal.map(|goal|(wrap(goal-r.heading)*3.0).clamp(-3.0,3.0)).unwrap_or(0.0));
                let mut projected=Vector::zeros();let mut align=0.0;
                for (k,(sx,sy)) in [(-1.0,-1.0),(-1.0,1.0),(1.0,-1.0),(1.0,1.0)].iter().enumerate() {
                    let wheel=vector![vx-yaw*sy*n(d,"track_m")/2.0,vy+yaw*sx*n(d,"wheelbase_m")/2.0,0.0];
                    if wheel.norm()>0.01 {let mut target=wheel.y.atan2(wheel.x);if wrap(target-r.steering_angles[k]).abs()>std::f64::consts::FRAC_PI_2 {target+=std::f64::consts::PI;}
                        r.steering_angles[k]+=wrap(target-r.steering_angles[k]).clamp(-n(d,"steer_rate_rad_s")*PHYSICS_DT,n(d,"steer_rate_rad_s")*PHYSICS_DT);
                    }
                    let axis=vector![r.steering_angles[k].cos(),r.steering_angles[k].sin(),0.0];projected+=axis*wheel.dot(&axis);
                    align+=if wheel.norm()>0.01 {wheel.normalize().dot(&axis).abs()} else {1.0};
                }
                projected/=4.0;desired=forward*projected.x+side*projected.y;swerve_yaw_factor=align/4.0;
            }
            let mut drive = if legged {
                forward
                    * ((desired - planar).dot(&forward) * r.mass_kg * n(d, "velocity_gain_per_s"))
            } else {
                (desired - planar) * r.mass_kg * n(d, "velocity_gain_per_s")
            };
            let magnitude = drive.norm();
            let local_force = pose.rotation.inverse() * drive;
            let lateral = local_force.y.abs() / magnitude.max(1e-9);
            let grip = normal_total
                * n(d, "tire_friction")
                * (1.0 - lateral * (1.0 - n(d, "lateral_friction_ratio")));
            let available = ((motor_budget - 3.0).max(0.0) * n(d, "drive_efficiency")
                / planar.norm().max(0.55))
            .min(n(d, "motor_force_max_n"))
            .min(grip);
            if magnitude > available && magnitude > 0.0 {
                drive *= available / magnitude;
            }
            // Turning costs forward speed: the wheels that generate the yaw couple also have to carry
            // the chassis, so a chassis that pivots while driving sheds speed in proportion to its
            // yaw rate. A design without the parameter is unaffected, which keeps every other model
            // exactly as it was; the heavy robot pays the most (its yaw torque is spent against a
            // long, heavy hull, so a turn genuinely bleeds a lot of ground speed).
            let turn_cost = n(d, "turn_speed_loss_per_rad_s");
            if turn_cost > 0.0 && planar.norm() > 0.05 {
                let yaw_rate = angular.z.abs().min(12.0);
                drive *= 1.0 / (1.0 + turn_cost * yaw_rate);
            }
            let mut roll_drag = planar;
            let v = roll_drag.norm();
            if v > 0.01 {
                roll_drag *= -(n(d, "rolling_resistance") * normal_total
                    + 0.5 * 1.225 * n(d, "drag_area_m2") * v * v)
                    / v;
            } else {
                roll_drag = Vector::zeros();
            }
            let mut yaw_brake = 0.0;
            let inertia_z = p3(&d["inertia_kgm2"]).z;
            if !powered {
                // Unpowered: back-driven gearboxes and rollers give only Coulomb drag, so
                // the robot slides on; tyres still grip sideways on a two-wheeled chassis.
                let along = if legged {
                    forward * planar.dot(&forward)
                } else {
                    planar
                };
                let mut coast = Vector::zeros();
                if along.norm() > 1e-4 {
                    let cap = r.mass_kg * along.norm() / PHYSICS_DT;
                    coast = -along.normalize() * (n(d, "coast_friction") * normal_total).min(cap);
                }
                if legged {
                    let slip = planar.dot(&side);
                    coast -= side
                        * (slip * r.mass_kg * 8.0).clamp(
                            -normal_total * n(d, "tire_friction"),
                            normal_total * n(d, "tire_friction"),
                        );
                }
                drive = coast;
                if normal_total > 1.0 {
                    let scrub = n(d, "coast_friction") * normal_total * n(d, "track_m") * 0.5;
                    yaw_brake = -scrub * (angular.z / 0.2).tanh();
                }
            } else if r.deployed || (!commanded && !a.coast) {
                // Active brake: powered motors hold position and heading.
                if normal_total > 1.0 && a.heading.is_none() && a.yaw_rate.is_none() {
                    let reach = 0.5 * n(d, "wheelbase_m").hypot(n(d, "track_m"));
                    let limit = n(d, "tire_friction")
                        * n(d, "lateral_friction_ratio")
                        * normal_total
                        * reach;
                    yaw_brake = (-angular.z * inertia_z * 12.0).clamp(-limit, limit);
                }
                let mut brake = -planar * r.mass_kg * 9.0;
                let norm = brake.norm();
                // Grip is the real limit: the tyre force the contact patch can carry caps the stop
                // before the brake motors do, so a heavier chassis still sheds speed quickly.
                let max = n(d, "brake_force_max_n").min(normal_total * n(d, "tire_friction"));
                if norm > max {
                    brake *= max / norm;
                }
                let normal = contacts
                    .iter()
                    .map(|(_, normal, support)| normal * (*support / normal_total.max(1e-9)))
                    .sum::<Vector<f64>>();
                // A ray that starts inside a wall reports no normal; it gives no slope to hold.
                if normal_total > 1.0 && normal.norm() > 1e-9 {
                    let normal = normal.normalize();
                    let gravity = vector![0.0, 0.0, -9.81];
                    brake -= (gravity - normal * gravity.dot(&normal)) * r.mass_kg;
                }
                drive = brake;
            } else if !commanded {
                // Powered coast on request: free wheels, rolling resistance only.
                drive = Vector::zeros();
            }
            if legged && powered {
                // Tyres resist sideways slip; a two-wheeled chassis cannot strafe.
                let slip = planar.dot(&side);
                drive -= side
                    * (slip * r.mass_kg * 8.0).clamp(
                        -normal_total * n(d, "tire_friction"),
                        normal_total * n(d, "tire_friction"),
                    );
            }
            for (point, normal, support) in &contacts {
                let share = if normal_total > 0.0 {
                    support / normal_total
                } else {
                    0.0
                };
                let force = (drive + roll_drag) * share;
                let tangent = force - normal * force.dot(normal);
                self.bodies[h].add_force_at_point(tangent, *point, true);
            }
            // The supercapacitor feeds the drive and the yaw motors out of the same power budget
            // (manual p.49), so a chassis asking for more than its own limit can turn harder while
            // the capacitor is dumping. `yaw_boost` adds the pilot's own burst authority on top:
            // without either, the rated yaw torque caps the spin far below what the wheels allow.
            // Only a pilot's burst channel (`yaw_boost`) opens the extra yaw authority, so a
            // strategy that simply asks for a higher power budget keeps the rated yaw torque.
            let budget_boost = if a.yaw_boost > 0.0
                && motor_budget > limit + 1e-9
                && r.supercap_energy_j > 0.0
            {
                1.0 + ((motor_budget - limit) / limit.max(1.0)).min(1.0) * 1.4
            } else {
                1.0
            };
            let max_yaw = if legged {
                n(d, "yaw_torque_max_nm")
                    .min(n(d, "tire_friction") * normal_total * n(d, "track_m") * 0.5)
            } else {
                n(d, "yaw_torque_max_nm")
            } * budget_boost
                * (1.0 + 1.6 * a.yaw_boost);
            let torque = if disabled || righting || normal_total < 1.0 {
                0.0
            } else if let Some(rate) = a.yaw_rate {
                (inertia_z * 8.0 * (rate - angular.z)).clamp(-max_yaw, max_yaw)
            } else if let Some(goal) = yaw_goal {
                (wrap(goal - r.heading) * 18.0 - angular.z * 6.0).clamp(-max_yaw, max_yaw)
            } else if commanded || a.coast {
                0.0
            } else {
                (-angular.z * 6.0).clamp(-max_yaw, max_yaw)
            };
            self.bodies[h].add_torque(vector![0.0, 0.0, torque*swerve_yaw_factor + yaw_brake], true);
            let mut balance = 0.0;
            if legged && r.alive {
                let local_w = pose.rotation.inverse() * angular;
                let nose_up = body_x.z.clamp(-1.0, 1.0).asin();
                let body_y = pose.rotation * Vector::y();
                let roll = body_y.z.clamp(-1.0, 1.0).asin();
                let lying = nose_up.abs() > 0.8 && roll.abs() < 0.8;
                let target = -a.lean;
                if righting {
                    // Roll the floor into the legs' plane (the lower leg pushes off beside the
                    // body); with the legs planted the hip motors turn the body over the axle.
                    let cap = n(d, "self_right_roll_torque_max_nm");
                    let error = down.y.clamp(-1.0, 1.0).asin();
                    let error = if self.roll_sign[i] > 0.0 { error } else { error - std::f64::consts::PI.copysign(error) };
                    let rate = (3.0 * error).clamp(-3.0, 3.0);
                    let roll_torque = (25.0 * (rate - local_w.x)).clamp(-cap, cap);
                    let planted = planted_angle && !contacts.is_empty();
                    if let (Some(goal), Some(angle), true) = (self.right_goal[i], floor_angle, planted) {
                        let cap = n(d, "self_right_torque_max_nm");
                        let rate = (3.0 * (angle - goal)).clamp(-3.5, 3.5);
                        balance = (25.0 * (rate - local_w.y)).clamp(-cap, cap);
                    }
                    self.bodies[h].add_torque(pose.rotation * vector![roll_torque, balance, 0.0], true);
                    leg_power += (roll_torque * local_w.x).abs() / 0.75;
                    // On both wheels and calm: legs off their stops (or the body at rest, say
                    // with one wheel up on a kerb), so balancing takes over without a kick.
                    let settled = contacts.len() == 2
                        && (angular.norm() < 1.5 && r.legs.iter().all(|l| *l > leg_min) || angular.norm() < 0.3);
                    if let Some(goal) = self.right_goal[i].filter(|g| {
                        -down.z > 0.9 && planted && settled && (self.hip_angle[i] - g).abs() < 0.3
                    }) {
                        // Back on its wheels; whole hip turns leave the legs where they were.
                        self.righting[i] = false;
                        self.upset_since[i] = -1.0;
                        self.roll_sign[i] = 1.0;
                        self.hip_angle[i] -= goal;
                        self.arm_events.push(json!({"type":"self_righted","robot":r.id}));
                    }
                } else if powered && r.grounded && !lying {
                    // Balance: wheel torque keeps the inverted pendulum at the lean target,
                    // limited by tyre friction at the hip height.
                    let cap = n(d, "balance_torque_max_nm")
                        .min(n(d, "tire_friction") * normal_total * n(d, "hip_height_m"));
                    balance = (n(d, "balance_kp_nm_per_rad") * (nose_up - target)
                        - n(d, "balance_kd_nms_per_rad") * local_w.y)
                        .clamp(-cap, cap);
                    let level = (-60.0 * roll - 6.0 * local_w.x).clamp(-25.0, 25.0);
                    self.bodies[h].add_torque(pose.rotation * vector![level, balance, 0.0], true);
                } else if powered && lying && r.z - field_height(arena, r.x, r.y, false) < 0.4 {
                    // Legs and wheels push off the floor to stand back up. This also catches a
                    // body pitched past ~46 degrees by a hard launch before it falls over.
                    let cap = n(d, "stand_torque_max_nm");
                    balance = (n(d, "balance_kp_nm_per_rad") * nose_up - 12.0 * local_w.y)
                        .clamp(-cap, cap);
                    self.bodies[h].add_torque(pose.rotation * vector![0.0, balance, 0.0], true);
                } else if powered && !r.grounded {
                    // Airborne: only wheel spin-up reactions trim the attitude (the wheels
                    // accelerated together give pitch, against each other give yaw).
                    let cap = n(d, "air_torque_max_nm");
                    let inertia = p3(&d["inertia_kgm2"]);
                    balance =
                        (40.0 * inertia.y * (nose_up - target) - 6.0 * local_w.y).clamp(-cap, cap);
                    let yaw = yaw_goal
                        .map(|goal| (wrap(goal - r.heading) * 12.0 - angular.z * 3.0) * inertia.z)
                        .unwrap_or(-angular.z * 3.0 * inertia.z)
                        .clamp(-cap, cap);
                    let level = (-30.0 * inertia.x * roll - 4.0 * local_w.x).clamp(-cap, cap);
                    self.bodies[h].add_torque(
                        pose.rotation * vector![level, balance, 0.0] + vector![0.0, 0.0, yaw],
                        true,
                    );
                }
                leg_power += n(d, "leg_holding_power_w") * if powered { 1.0 } else { 0.0 };
            }
            r.motor_power_w = if disabled {
                0.0
            } else {
                3.0 + drive.dot(&planar).max(0.0) / n(d, "drive_efficiency")
                    + (torque * angular.z).abs() / 0.75
                    + (balance * angular.y).abs() / 0.75
            };
            // Energy drawn from the store to cover the excess: the chassis only receives
            // (1-LOSS) of it, so the drain is scaled up by the same factor.
            let useful = (r.motor_power_w - limit).max(0.0);
            let cap_out = (useful / (1.0 - SUPERCAP_LOSS))
                .min(r.supercap_energy_j / PHYSICS_DT);
            r.supercap_energy_j = (r.supercap_energy_j - cap_out * PHYSICS_DT).max(0.0);
            let useful_supercap = cap_out * (1.0 - SUPERCAP_LOSS);
            let recharge = (limit - r.motor_power_w)
                .max(0.0)
                .min(35.0)
                .min((n(d, "supercap_nominal_j") - r.supercap_energy_j) / PHYSICS_DT)
                .max(0.0);
            if !disabled {
                r.supercap_energy_j += recharge * PHYSICS_DT * 0.9;
            }
            r.power_w = if disabled {
                0.0
            } else {
                // Reported chassis power is what the battery supplies; the capacitor's losses are
                // not added to it.
                r.motor_power_w - useful_supercap + recharge
            };
            // Leg joint motors are not chassis power (construction manual p.49) but drain the battery.
            r.battery_remaining_wh = (r.battery_remaining_wh
                - (r.power_w + if legged { leg_power } else { 0.0 }) * PHYSICS_DT / 3600.0)
                .max(0.0);
            r.charging_power_w = self.charge_power(arena, r, d, &pose);
            if r.charging_power_w > 0.0 {
                r.supercap_energy_j = (r.supercap_energy_j + r.charging_power_w * PHYSICS_DT * 0.9)
                    .min(n(d, "supercap_nominal_j"));
            }
            if r.design_slot() == 7 && r.crouched != a.crouch {
                let proposed = Self::hull(d, a.crouch, None, true, None).build();
                let clear = a.crouch
                    || self
                        .queries
                        .intersection_with_shape(
                            &self.bodies,
                            &self.colliders,
                            &pose,
                            proposed.shape(),
                            QueryFilter::default().exclude_rigid_body(h),
                        )
                        .is_none();
                if clear {
                    r.crouched = a.crouch;
                    self.colliders[self.hulls[i]].set_shape(proposed.shared_shape().clone());
                    let mut com = p3(&d["com_m"]);
                    if r.crouched {
                        com.z -= 0.035;
                    }
                    self.bodies[h].set_additional_mass_properties(
                        MassProperties::new(
                            point![com.x, com.y, com.z],
                            r.mass_kg,
                            p3(&d["inertia_kgm2"]),
                        ),
                        true,
                    );
                }
            }
            if r.transform_allowed {
                let target=if a.transform.unwrap_or(a.crouch) {1.0} else {0.0};
                let progress=approach(r.transform_progress,target,PHYSICS_DT/n(d,"transform_time_s").max(0.1));
                let muzzle=n(d,"muzzle_z_m")+(n(d,"folded_muzzle_z_m")-n(d,"muzzle_z_m"))*progress;
                let mut folded=d.clone();folded["muzzle_z_m"]=json!(muzzle);
                let proposed=Self::hull(&folded,false,None,true,None).build();
                let clear=progress>=r.transform_progress || self.queries.intersection_with_shape(&self.bodies,&self.colliders,&pose,proposed.shape(),QueryFilter::default().exclude_rigid_body(h)).is_none();
                if clear {r.transform_progress=progress;r.muzzle_height_m=muzzle;r.crouched=progress>0.999;self.colliders[self.hulls[i]].set_shape(proposed.shared_shape().clone());}
                r.pitch=r.pitch.clamp(-0.261799,0.785398+(0.139626-0.785398)*r.transform_progress);
            } else {
                r.muzzle_height_m=if r.crouched {0.185} else {n(d,"muzzle_z_m")};
            }
            let local_velocity = pose.rotation.inverse() * velocity;
            let local_angular = pose.rotation.inverse() * angular;
            for (wheel, (sx, sy)) in [(-1.0, -1.0), (-1.0, 1.0), (1.0, -1.0), (1.0, 1.0)]
                .iter()
                .enumerate()
            {
                let point = vector![
                    sx * n(d, "wheelbase_m") / 2.0,
                    sy * n(d, "track_m") / 2.0,
                    0.0
                ];
                let v = local_velocity + local_angular.cross(&point);
                r.wheel_angular_velocity[wheel] = if d["chassis_type"]=="swerve" {
                    (v.x*r.steering_angles[wheel].cos()+v.y*r.steering_angles[wheel].sin())/n(d,"wheel_radius_m")
                } else if d["chassis_type"]=="omni" {
                    let angle=-sx*sy*std::f64::consts::FRAC_PI_4;(v.x*angle.cos()+v.y*angle.sin())/n(d,"wheel_radius_m")
                } else {(v.x-sx*sy*v.y)/n(d,"wheel_radius_m")};
            }
        }
        for (i, j, grasp, tip) in rescue_forces {
            // Compliant wrist: the transmitted force fades as the tip lags the edge.
            let lag = ((tip - grasp).norm() - 0.08).max(0.0) / 0.17;
            let force = self.righting_force(i, j, grasp, robots[j].mass_kg) * (1.0 - lag).max(0.0);
            self.bodies[self.handles[j]].add_force_at_point(force, grasp, true);
            self.bodies[self.handles[i]].add_force_at_point(-force, tip, true);
        }
        self.pipeline.step(
            &vector![0.0, 0.0, -9.81],
            &self.parameters,
            &mut self.islands,
            &mut self.broad,
            &mut self.narrow,
            &mut self.bodies,
            &mut self.colliders,
            &mut self.joints,
            &mut self.multibody,
            &mut self.ccd,
            Some(&mut self.queries),
            &(),
            &(),
        );
        for pair in self.narrow.contact_pairs() {
            let impulse = pair.total_impulse();
            if impulse.x.hypot(impulse.y) < 3.0 {
                continue;
            }
            let point = pair
                .manifolds
                .iter()
                .flat_map(|m| m.data.solver_contacts.iter())
                .next()
                .map(|c| c.point);
            if let Some(point) = point {
                for collider in [pair.collider1, pair.collider2] {
                    if let Some(body) = self.colliders[collider].parent() {
                        if let Some(i) = self.handles.iter().position(|h| *h == body) {
                            self.impacts.insert(i,json!({"robot":robots[i].id,"x":point.x,"y":point.y,"z":point.z,"impulse_n_s":impulse.norm()}));
                        }
                    }
                }
            }
        }
        for (i, r) in robots.iter_mut().enumerate() {
            let h = self.handles[i];
            let pose = *self.bodies[h].position();
            let p = pose.translation.vector;
            // R58: the highway no-go area behind a fly slope admits a robot that ran the
            // 17 degree slope from its low end to the crest; it may stay there or drive out.
            match fly_slope_progress(arena, p.x, p.y) {
                Some(s) if s < 0.25 && r.fly_pass == 0 => r.fly_pass = 1,
                Some(s) if s > 0.85 && r.fly_pass == 1 => r.fly_pass = 2,
                Some(_) => {}
                None => {
                    if !arena.forbidden.iter().any(|z| {
                        z["kind"] == "highway" && in_zone_rect(z, p.x, p.y, 0.0)
                    }) {
                        r.fly_pass = 0;
                    }
                }
            }
            let illegal = r.role != "aerial"
                && arena.forbidden.iter().any(|z| {
                    forbidden_for(z, &r.team, r.fly_pass) && in_zone_rect(z, p.x, p.y, 0.0)
                });
            if illegal {
                self.bodies[h]
                    .set_translation(vector![self.last[i][0], self.last[i][1], p.z], true);
                self.bodies[h].set_linvel(Vector::zeros(), true);
            }
            let body = &self.bodies[h];
            let p = body.translation();
            let v = body.linvel();
            let rot = body.rotation().quaternion();
            let (_, _, heading) = body.rotation().euler_angles();
            r.x = p.x;
            r.y = p.y;
            r.z = p.z;
            r.vx = v.x;
            r.vy = v.y;
            r.vz = v.z;
            r.heading = heading;
            r.quaternion = [rot.i, rot.j, rot.k, rot.w];
            r.angular_velocity = [body.angvel().x, body.angvel().y, body.angvel().z];
            r.upright = (body.rotation() * Vector::z()).z;
            r.tipped = r.role != "aerial" && r.upright < 0.5;
            if let Some(hip) = r.hip_angle.as_mut() {
                *hip = self.hip_angle[i];
            }
            r.self_righting = self.righting[i];
            if r.arm_count > 0 && self.designs[i]["arm"].is_object() {
                let tip = body.position() * arm_points(&self.designs[i]["arm"], &r.arm_joints)[3];
                r.arm_tip = [tip.x, tip.y, tip.z];
                if r.arm_count==2 {let tip=body.position()*arm_points(&self.designs[i]["arm2"],&r.arm2_joints)[3];r.arm2_tip=[tip.x,tip.y,tip.z];}
            }
            self.last[i] = [r.x, r.y, r.z];
        }
        // 2027 aerial robots: read their bodies back.
        for (i, r) in robots.iter_mut().enumerate() {
            for (k, handle) in self.drone_bodies[i].iter().enumerate() {
                let body = &self.bodies[*handle];
                let (p, v) = (body.translation(), body.linvel());
                let d = &mut r.drones[k];
                d.x = p.x;
                d.y = p.y;
                d.z = p.z;
                d.vx = v.x;
                d.vy = v.y;
                d.vz = v.z;
            }
        }
        for i in 0..robots.len() {
            let Some(j) = self.rescuing[i] else { continue };
            let body = &self.bodies[self.handles[j]];
            let level = (body.rotation() * Vector::z()).z;
            if level >= n(&self.designs[i]["arm"], "rescue_upright_cos")
                && body.angvel().norm() < 0.8
            {
                self.grasps[i] = None;
                self.rescuing[i] = None;
                robots[i].rescue_contact = false;
                self.arm_events
                    .push(json!({"type":"rescue_complete","robot":ids[i],"target":ids[j]}));
            }
        }
    }

    /// Force the gripper applies at the grasp point: it rolls the robot about the
    /// horizontal axis that brings its up vector back to vertical, at a limited rate.
    fn righting_force(&self, i: usize, j: usize, grasp: Point<f64>, mass: f64) -> Vector<f64> {
        let body = &self.bodies[self.handles[j]];
        let rotation = *body.rotation();
        let up = rotation * Vector::z();
        let mut axis = up.cross(&Vector::z());
        if axis.norm() < 0.05 {
            // Upside down: roll about the chassis length axis first.
            axis = rotation * Vector::x();
            axis.z = 0.0;
        }
        if axis.norm() < 1e-6 {
            return Vector::zeros();
        }
        let axis = axis.normalize();
        let tangent = axis.cross(&(grasp - body.center_of_mass()));
        let lever = tangent.norm();
        if lever < 0.04 {
            return Vector::zeros();
        }
        let d = &self.designs[j];
        let tilt = up.z.clamp(-1.0, 1.0).acos();
        let local = rotation.inverse() * axis;
        let inertia = local.component_mul(&local).dot(&p3(&d["inertia_kgm2"])) + mass * 0.06;
        let spin = body.angvel().dot(&axis);
        // Gravity holds the robot on its side until the centre of mass passes the wheel line.
        let gravity = mass
            * 9.81
            * (p3(&d["com_m"]).z * tilt.sin() - n(d, "track_m") * 0.5 * tilt.cos()).max(0.0);
        let torque = inertia * 9.0 * ((1.8 * tilt).min(1.0) - spin) + gravity;
        let limit = n(&self.designs[i]["arm"], "grip_force_max_n");
        tangent / lever * (torque / lever).clamp(-limit, limit)
    }

    pub fn design(&self, i: usize) -> &Value {
        &self.designs[i]
    }
    /// Developer fixtures and tests: put a flying aerial robot at a point with a velocity.
    pub fn place_drone(&mut self, robot: usize, drone: usize, at: [f64; 3], v: [f64; 3]) {
        let handle = self.drone_bodies[robot][drone];
        let body = &mut self.bodies[handle];
        body.set_body_type(RigidBodyType::Dynamic, true);
        body.set_translation(vector![at[0], at[1], at[2]], true);
        body.set_linvel(vector![v[0], v[1], v[2]], true);
    }
    pub fn set_drone_velocity(&mut self, robot: usize, drone: usize, v: [f64; 3]) {
        if let Some(handle) = self.drone_bodies.get(robot).and_then(|b| b.get(drone)) {
            self.bodies[*handle].set_linvel(vector![v[0], v[1], v[2]], true);
        }
    }
    /// Flight control of a hangar's aerial robots for one physics substep (S75-S80 hardware,
    /// rules 3.2.1). Commands come from the aerial system's action: `drones` maps the robot's
    /// index (1..4) to {"launch":true} / {"move":[x,y,z]} / {"goto":[x,y,z]} / {"dock":true} /
    /// {"land":true}. Thrust is limited by the motors and a tilt limit, power drains the 4 Wh
    /// battery, and a robot without battery or guard integrity falls and stays down.
    fn drive_drones(&mut self, i: usize, arena: &Arena, r: &mut Robot, action: &Action, now: f64) {
        let d = self.designs[i]["drone"].clone();
        let (mass, radius, speed, climb) = (n(&d, "mass_kg"), n(&d, "radius_m"), n(&d, "speed_reference_mps"), n(&d, "climb_mps"));
        let (thrust_max, tilt) = (n(&d, "thrust_max_n"), n(&d, "tilt_limit_rad"));
        let disc = n(&d, "rotor_count") * std::f64::consts::PI * n(&d, "rotor_radius_m").powi(2);
        for k in 0..r.drones.len() {
            let handle = self.drone_bodies[i][k];
            let dr = r.drones[k].clone();
            let cmd = &action.rpc["drones"][(k + 1).to_string()];
            let body = &mut self.bodies[handle];
            body.reset_forces(true);
            let p = *body.translation();
            let v = *body.linvel();
            let usable = dr.battery_wh > 0.0 && dr.integrity > 1e-9 && r.alive && !r.red_card;
            match dr.state.as_str() {
                "docked" => {
                    body.set_body_type(RigidBodyType::KinematicPositionBased, true);
                    body.set_next_kinematic_translation(vector![dr.dock[0], dr.dock[1], dr.dock[2]]);
                    body.set_linvel(Vector::zeros(), true);
                    if usable && (cmd["launch"] == true || cmd.get("move").is_some() || cmd.get("goto").is_some()) {
                        body.set_body_type(RigidBodyType::Dynamic, true);
                        r.drones[k].state = "flying".into();
                        r.drones[k].launch_until = now + 0.6;
                    }
                    r.drones[k].power_w = 0.0;
                    continue;
                }
                "disabled" => {
                    body.set_body_type(RigidBodyType::Dynamic, true);
                    r.drones[k].power_w = 0.0;
                    continue;
                }
                "down" => {
                    body.set_body_type(RigidBodyType::Dynamic, true);
                    r.drones[k].power_w = 0.0;
                    if !usable || !(cmd["launch"] == true || cmd.get("move").is_some() || cmd.get("goto").is_some()) {
                        continue;
                    }
                    r.drones[k].state = "flying".into();
                    r.drones[k].launch_until = now + 0.4;
                }
                _ => {
                    if body.body_type() != RigidBodyType::Dynamic {
                        body.set_body_type(RigidBodyType::Dynamic, true);
                    }
                }
            }
            if !usable {
                r.drones[k].state = if dr.integrity <= 1e-9 { "disabled".into() } else { "down".into() };
                r.drones[k].power_w = 0.0;
                continue;
            }
            let ground = field_height(arena, p.x, p.y, true);
            let ceiling = 2.35;
            let dock = vector![dr.dock[0], dr.dock[1], dr.dock[2]];
            let mut wanted = Vector::zeros();
            if now < dr.launch_until {
                wanted.z = climb;
            } else if cmd["land"] == true {
                wanted = vector![0.0, 0.0, -0.6];
                if p.z - ground < radius + 0.04 && v.norm() < 0.3 {
                    r.drones[k].state = "down".into();
                    continue;
                }
            } else if cmd["dock"] == true {
                let above = dock + vector![0.0, 0.0, 0.3];
                let flat = vector![dock.x - p.x, dock.y - p.y, 0.0];
                if flat.norm() > 0.08 {
                    wanted = (above - p) * 2.0;
                } else {
                    wanted = (dock - p) * 1.5;
                    if (dock - p).norm() < 0.05 && v.norm() < 0.4 {
                        r.drones[k].state = "docked".into();
                        body.set_body_type(RigidBodyType::KinematicPositionBased, true);
                        body.set_next_kinematic_translation(dock);
                        continue;
                    }
                }
            } else if let Some(g) = cmd["goto"].as_array().filter(|g| g.len() == 3) {
                let target = vector![g[0].as_f64().unwrap_or(p.x), g[1].as_f64().unwrap_or(p.y), g[2].as_f64().unwrap_or(p.z)];
                wanted = (target - p) * 2.2;
            } else if let Some(m) = cmd["move"].as_array().filter(|m| m.len() == 3) {
                let c = |j: usize| m[j].as_f64().filter(|x| x.is_finite()).unwrap_or(0.0).clamp(-1.0, 1.0);
                wanted = vector![c(0) * speed, c(1) * speed, c(2) * climb];
            }
            // Speed limits, the field ceiling (2.4 m fence) and ground clearance.
            let flat = vector![wanted.x, wanted.y, 0.0];
            if flat.norm() > speed {
                let scale = speed / flat.norm();
                wanted.x *= scale;
                wanted.y *= scale;
            }
            wanted.z = wanted.z.clamp(-climb, climb);
            if p.z > ceiling - 0.1 {
                wanted.z = wanted.z.min(-(p.z - ceiling + 0.1) * 3.0);
            }
            if p.z < ground + radius + 0.12 {
                wanted.z = wanted.z.max((ground + radius + 0.12 - p.z) * 4.0);
            }
            let recovering = now < dr.recover_until;
            let gain = if recovering { 1.2 } else { 4.0 };
            let mut force = (wanted - v) * mass * gain + vector![0.0, 0.0, mass * 9.81];
            // Body drag of the guarded frame.
            let area = (2.0 * radius).powi(2) * 0.6;
            force += v * (0.5 * 1.225 * 1.1 * area * v.norm());
            force.z = force.z.max(0.0);
            let horizontal = force.x.hypot(force.y);
            let maximum = force.z * tilt.tan();
            if horizontal > maximum && horizontal > 0.0 {
                force.x *= maximum / horizontal;
                force.y *= maximum / horizontal;
            }
            let total = force.norm();
            if total > thrust_max {
                force *= thrust_max / total;
            }
            let thrust = force.norm();
            body.add_force(force - vector![0.0, 0.0, 0.0], true);
            // Drag acts on the body itself.
            body.add_force(-v * (0.5 * 1.225 * 1.1 * area * v.norm()), true);
            let power = thrust.powf(1.5) / (2.0 * 1.225 * disc.max(1e-6)).sqrt() / n(&d, "hover_efficiency").max(0.1) + n(&d, "avionics_w");
            let drone = &mut r.drones[k];
            drone.power_w = power;
            drone.battery_wh = (drone.battery_wh - power * PHYSICS_DT / 3600.0).max(0.0);
            // Attitude for display: the frame tilts with its thrust vector.
            let up = if thrust > 1e-6 { force / thrust } else { Vector::z() };
            let axis = Vector::z().cross(&up);
            let q = if axis.norm() > 1e-9 {
                UnitQuaternion::from_axis_angle(&nalgebra::Unit::new_normalize(axis), Vector::z().dot(&up).clamp(-1.0, 1.0).acos())
            } else {
                UnitQuaternion::identity()
            };
            drone.quaternion = [q.i, q.j, q.k, q.w];
        }
        r.motor_power_w = r.drones.iter().map(|d| d.power_w).sum();
        r.power_w = 0.0;
    }
    pub fn take_arm_events(&mut self) -> Vec<Value> {
        std::mem::take(&mut self.arm_events)
    }

    /// Grasp point on the rescued robot's hull (its own frame): the reachable chassis
    /// edge that gives the most lifting leverage about the righting axis.
    fn choose_grasp(&self, i: usize, j: usize, engineer: &Isometry<f64>) -> Option<Point<f64>> {
        let arm = &self.designs[i]["arm"];
        let d = &self.designs[j];
        let body = &self.bodies[self.handles[j]];
        let pose = *body.position();
        let up = pose.rotation * Vector::z();
        let mut axis = up.cross(&Vector::z());
        if axis.norm() < 0.05 {
            axis = pose.rotation * Vector::x();
            axis.z = 0.0;
        }
        if axis.norm() < 1e-6 {
            return None;
        }
        let axis = axis.normalize();
        let size = p3(&d["chassis_size_m"]);
        // Grippable body box: low mecanum frame, or the wheel-legged body at hip height.
        let (hx, hy, low, high) = if d["chassis_type"] == "wheel_legged" {
            let hip = n(d, "hip_height_m");
            (
                size.x * 0.46 + 0.012,
                size.y * 0.42 + 0.012,
                hip - size.z / 2.0,
                hip + size.z / 2.0,
            )
        } else {
            (
                size.x * 0.42 + 0.012,
                size.y * 0.42 + 0.012,
                0.065,
                0.065 + size.z + 0.012,
            )
        };
        let base = p3(&arm["base_m"]);
        let (min_reach, max_reach) = arm_limit(arm, 2);
        let tool = n(arm, "tool_length_m");
        let mut best: Option<(f64, Point<f64>)> = None;
        let mast = engineer * point![base.x, base.y, base.z];
        let toward = vector![
            mast.x - body.center_of_mass().x,
            mast.y - body.center_of_mass().y,
            0.0
        ];
        let mut candidates = Vec::new();
        for z in [low, high] {
            for (x, y) in [
                (1.0, 0.0),
                (-1.0, 0.0),
                (0.0, 1.0),
                (0.0, -1.0),
                (1.0, 1.0),
                (1.0, -1.0),
                (-1.0, 1.0),
                (-1.0, -1.0),
            ] {
                candidates.push(point![x * hx, y * hy, z]);
            }
        }
        for c in candidates {
            let world = pose * c;
            // Only edges facing the engineer: the boom must not reach across the robot.
            let outward = vector![
                world.x - body.center_of_mass().x,
                world.y - body.center_of_mass().y,
                0.0
            ];
            if outward.dot(&toward) <= 0.02 * toward.norm() {
                continue;
            }
            let tangent = axis.cross(&(world - body.center_of_mass()));
            let lever = tangent.norm();
            if lever < 0.05 {
                continue;
            }
            let local = engineer.inverse_transform_point(&world);
            let reach = (local.x - base.x).hypot(local.y - base.y);
            if reach < min_reach + tool
                || reach > max_reach + tool * 0.75
                || !(0.1..=0.95).contains(&local.z)
            {
                continue;
            }
            // A reachable edge behind a wall is not a possible grip.
            let shoulder = engineer * arm_points(arm, &arm_stowed(arm))[1];
            if !self.visible(
                i,
                j,
                [shoulder.x, shoulder.y, shoulder.z],
                [world.x, world.y, world.z],
            ) {
                continue;
            }
            let lift = (tangent.z / lever).max(0.0);
            // Prefer edges that rise as the robot rolls back: the gripper lifts, not shoves.
            let score = lever * (0.15 + lift) - 0.3 * reach;
            if best.map(|(s, _)| score > s).unwrap_or(true) {
                best = Some((score, c));
            }
        }
        best.map(|(_, c)| c)
    }

    fn charge_power(&self, arena: &Arena, r: &Robot, d: &Value, pose: &Isometry<f64>) -> f64 {
        if d["charge_receiver"] != true {
            return 0.0;
        }
        let dock = &arena.charging_docks[&r.team];
        if dock.is_null() {
            return 0.0;
        }
        let tx = &dock["transmitter"];
        let target = point![
            tx[0].as_f64().unwrap(),
            tx[1].as_f64().unwrap(),
            tx[2].as_f64().unwrap()
                + height_at(arena, tx[0].as_f64().unwrap(), tx[1].as_f64().unwrap())
        ];
        let size = p3(&d["chassis_size_m"]);
        let rx = pose * point![size.x / 2.0, 0.0, 0.16];
        let normal = pose.rotation * vector![1.0, 0.0, 0.0];
        let dock_normal = p3(&dock["normal"]);
        let target = target + dock_normal * 0.24;
        if (rx - target).norm() <= 0.03
            && normal.dot(&dock_normal) < -0.98
            && r.vx.hypot(r.vy) < 0.08
        {
            100.0
        } else {
            0.0
        }
    }

    pub fn recoil(&mut self, index: usize, impulse: Vector<f64>, point: Point<f64>) {
        self.bodies[self.handles[index]].apply_impulse_at_point(impulse, point, true);
    }
    pub fn velocity_at_point(&self, index: usize, point: [f64; 3]) -> Vector<f64> {
        self.bodies[self.handles[index]].velocity_at_point(&point![point[0], point[1], point[2]])
    }
    pub fn take_impacts(&mut self) -> Vec<Value> {
        std::mem::take(&mut self.impacts).into_values().collect()
    }
    pub fn refresh_velocities(&self, robots: &mut [Robot]) {
        for (i, r) in robots.iter_mut().enumerate() {
            let b = &self.bodies[self.handles[i]];
            r.vx = b.linvel().x;
            r.vy = b.linvel().y;
            r.vz = b.linvel().z;
            r.angular_velocity = [b.angvel().x, b.angvel().y, b.angvel().z];
        }
    }
    pub fn visible(&self, observer: usize, target: usize, origin: [f64; 3], end: [f64; 3]) -> bool {
        let ray = Ray::new(
            point![origin[0], origin[1], origin[2]],
            vector![end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]],
        );
        match self.queries.cast_ray(
            &self.bodies,
            &self.colliders,
            &ray,
            1.0,
            true,
            QueryFilter::default().exclude_rigid_body(self.handles[observer]),
        ) {
            Some((h, _)) => self.colliders[h].parent() == Some(self.handles[target]),
            None => true,
        }
    }
    pub fn visible_point(&self, observer: usize, origin: [f64; 3], end: [f64; 3]) -> bool {
        let ray = Ray::new(
            point![origin[0], origin[1], origin[2]],
            vector![end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]],
        );
        self.queries
            .cast_ray(
                &self.bodies,
                &self.colliders,
                &ray,
                1.0,
                true,
                QueryFilter::default().exclude_rigid_body(self.handles[observer]),
            )
            .map(|(_, toi)| toi >= 1.0 - 1e-7)
            .unwrap_or(true)
    }
    pub fn update_base_covers(&mut self, bases: &BTreeMap<String, Base>) {
        for (team, handle, index) in &self.base_covers {
            let w = structures::covers(&bases[team])[*index].clone();
            self.colliders[*handle].set_position(Isometry::new(
                vector![w.x+w.w/2.0,w.y+w.h/2.0,w.bottom+w.height/2.0], vector![0.0,0.0,w.rotation]));
        }
        self.queries.update(&self.colliders);
    }
    pub fn solid_hit(&self, source: usize, origin: [f64; 3], delta: [f64; 3]) -> Option<f64> {
        // Aerial robots are resolved by the engine (they are pushed, not walls).
        let drones: Vec<RigidBodyHandle> = self.drone_bodies.iter().flatten().copied().collect();
        let predicate = |_: ColliderHandle, c: &Collider| c.parent().map(|p| !drones.contains(&p)).unwrap_or(true);
        let mut filter = QueryFilter::default().predicate(&predicate);
        if let Some(handle) = self.handles.get(source) {
            filter = filter.exclude_rigid_body(*handle);
        }
        self.queries
            .cast_ray(
                &self.bodies,
                &self.colliders,
                &Ray::new(
                    point![origin[0], origin[1], origin[2]],
                    vector![delta[0], delta[1], delta[2]],
                ),
                1.0,
                true,
                filter,
            )
            .map(|(_, t)| t)
    }
}

fn triangulate(points: &[[f64; 2]]) -> Vec<[u32; 3]> {
    let area = (0..points.len())
        .map(|i| {
            let a = points[i];
            let b = points[(i + 1) % points.len()];
            a[0] * b[1] - b[0] * a[1]
        })
        .sum::<f64>();
    let mut ids: Vec<usize> = (0..points.len()).collect();
    if area < 0.0 {
        ids.reverse();
    }
    let mut triangles = Vec::new();
    let cross = |a: [f64; 2], b: [f64; 2], c: [f64; 2]| {
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    };
    while ids.len() > 3 {
        let mut ear = None;
        for i in 0..ids.len() {
            let a = ids[(i + ids.len() - 1) % ids.len()];
            let b = ids[i];
            let c = ids[(i + 1) % ids.len()];
            if cross(points[a], points[b], points[c]) <= 1e-10 {
                continue;
            }
            let inside = ids.iter().any(|&j| {
                j != a
                    && j != b
                    && j != c
                    && cross(points[a], points[b], points[j]) >= -1e-9
                    && cross(points[b], points[c], points[j]) >= -1e-9
                    && cross(points[c], points[a], points[j]) >= -1e-9
            });
            if !inside {
                ear = Some((i, [a as u32, b as u32, c as u32]));
                break;
            }
        }
        let (i, t) = ear.expect("simple non-self-intersecting field polygon");
        triangles.push(t);
        ids.remove(i);
    }
    if ids.len() == 3 {
        triangles.push([ids[0] as u32, ids[1] as u32, ids[2] as u32]);
    }
    triangles
}

pub fn armor_hit(r: &Robot, origin: [f64; 3], delta: [f64; 3]) -> Option<(f64, usize)> {
    if r.role == "aerial" {
        return None;
    }
    let rotation = q(r);
    let inverse = rotation.inverse();
    let p = inverse * vector![origin[0] - r.x, origin[1] - r.y, origin[2] - r.z];
    let v = inverse * vector![delta[0], delta[1], delta[2]];
    let mut best = None;
    let tilt = 15.0_f64.to_radians();
    let width = if r.role == "hero" { 0.23 } else { 0.135 };
    for face in 0..4 {
        let a = face as f64 * std::f64::consts::FRAC_PI_2;
        let (nx, ny) = (a.cos(), a.sin());
        let normal = vector![nx * tilt.cos(), ny * tilt.cos(), tilt.sin()];
        let distance = r.chassis_size_m[face % 2] / 2.0 - r.armor_inset_m + r.armor_mount_offset_m;
        let z = if face % 2 == 1 {
            r.armor_side_center_z_m.unwrap_or(r.armor_center_z_m)
        } else {
            r.armor_center_z_m
        };
        let center = vector![nx * distance, ny * distance, z] + normal * r.armor_hit_face_offset_m;
        let approach = v.dot(&normal);
        if approach >= -1e-10 {
            continue;
        }
        let t = (center - p).dot(&normal) / approach;
        if !(0.0..=1.0).contains(&t) {
            continue;
        }
        let offset = p + v * t - center;
        let horizontal = offset.dot(&vector![-ny, nx, 0.0]);
        let vertical = offset.dot(&vector![-nx * tilt.sin(), -ny * tilt.sin(), tilt.cos()]);
        if horizontal.abs() <= width / 2.0
            && vertical.abs() <= 0.0625
            && best.map(|(time, _)| t < time).unwrap_or(true)
        {
            best = Some((t, face));
        }
    }
    best
}

pub fn armor_normal(r: &Robot, face: usize) -> Vector<f64> {
    let a = face as f64 * std::f64::consts::FRAC_PI_2;
    let tilt = 15.0_f64.to_radians();
    q(r) * vector![a.cos() * tilt.cos(), a.sin() * tilt.cos(), tilt.sin()]
}
pub fn face_at_point(r: &Robot, point: [f64; 3]) -> usize {
    let local = q(r).inverse() * vector![point[0] - r.x, point[1] - r.y, point[2] - r.z];
    (local.y.atan2(local.x).rem_euclid(std::f64::consts::TAU) / std::f64::consts::FRAC_PI_2).round()
        as usize
        % 4
}

pub fn muzzle(r: &Robot) -> ([f64; 3], [f64; 3]) {
    let yaw = r.aim - r.heading;
    let local = vector![
        yaw.cos() * r.pitch.cos(),
        yaw.sin() * r.pitch.cos(),
        r.pitch.sin()
    ];
    let direction = q(r) * local;
    let pivot = q(r) * vector![0.0, 0.0, r.muzzle_height_m];
    let origin = vector![r.x, r.y, r.z] + pivot + direction * r.muzzle_reach_m;
    (
        [origin.x, origin.y, origin.z],
        [direction.x, direction.y, direction.z],
    )
}
