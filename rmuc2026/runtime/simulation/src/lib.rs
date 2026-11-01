use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, HashMap};
mod rulebook;
mod penalties;
mod season2027;
mod physics;
mod pickup;
mod structures;
mod vision;
mod training_scenario;

pub const DT: f64 = 0.05;
pub const SLOT_IDS: [u32; 6] = [1, 2, 3, 4, 6, 7];
pub const SLOT_IDS_2027: [u32; 5] = [1, 3, 4, 6, 7];
const ROLES_2027: [&str; 5] = ["heavy", "infantry", "infantry", "aerial", "sentry"];
/// 42 mm launchers: the 2026 hero and the 2027 heavy robot's modular launcher.
pub fn is_42(role: &str) -> bool {
    role == "hero" || role == "heavy"
}
const ROLES: [&str; 6] = [
    "hero", "engineer", "infantry", "infantry", "aerial", "infantry",
];

#[derive(Clone, Serialize, Deserialize)]
pub struct Spec {
    pub hp: f64,
    pub speed: f64,
    pub damage: f64,
    pub cooldown: f64,
    pub ammo: u32,
    pub vision: f64,
    pub range: f64,
    pub acceleration: f64,
    pub armor: f64,
    pub heat_per_shot: f64,
    pub max_heat: f64,
    pub cooling: f64,
    pub projectile_speed: f64,
    pub asset: String,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Wall {
    pub x: f64,
    pub y: f64,
    pub w: f64,
    pub h: f64,
    pub height: f64,
    pub kind: String,
    #[serde(default)]
    pub rotation: f64,
    #[serde(default)]
    pub bottom: f64,
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub source_page: u32,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Terrain {
    pub x: f64,
    pub y: f64,
    pub w: f64,
    pub h: f64,
    pub kind: String,
    pub elevation: f64,
    pub speed_multiplier: f64,
    #[serde(default)]
    pub direction: i32,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub axis: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub polygon: Vec<[f64; 2]>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ramp_start: Option<[f64; 2]>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ramp_end: Option<[f64; 2]>,
    #[serde(default)]
    pub base_elevation: f64,
    #[serde(default)]
    pub edge_ramp_m: f64,
    #[serde(default)]
    pub bump_pitch: f64,
    #[serde(default)]
    pub bump_height: f64,
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub source_page: u32,
    #[serde(default)]
    pub slope_deg: f64,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Zone {
    pub id: String,
    pub x: f64,
    pub y: f64,
    pub radius: f64,
    pub kind: String,
    pub owner: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub group: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub order: Option<u32>,
    /// Crossing kind when it differs from `kind` (the 2027 central card is also the highland's high card).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub crossing: Option<String>,
    /// Absolute height of the RFID card surface (2027); absent on legacy fields.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub card_z_m: Option<f64>,
    pub source: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub polygon: Vec<[f64; 2]>,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Arena {
    pub name: String,
    pub width: f64,
    pub height: f64,
    pub floor_slope_deg: f64,
    pub obstacles: Vec<Wall>,
    pub terrain: Vec<Terrain>,
    pub zones: Vec<Zone>,
    pub asset: String,
    pub spawn_points: BTreeMap<String, BTreeMap<String, [f64; 2]>>,
    pub bases: BTreeMap<String, [f64; 2]>,
    pub outposts: BTreeMap<String, [f64; 2]>,
    pub pads: BTreeMap<String, [f64; 2]>,
    pub flight_lanes: Value,
    pub forbidden: Vec<Value>,
    pub rune: Value,
    #[serde(default)]
    pub layout_revision: String,
    #[serde(default)]
    pub field_geometry: Value,
    #[serde(default)]
    pub structure_assets: Value,
    #[serde(default)]
    pub navigation: Value,
    #[serde(default)]
    pub charging_docks: Value,
    #[serde(default)]
    pub assembly_frames: Value,
    /// Rules season of this field ("2026" or "2027").
    #[serde(default = "season_2026")]
    pub season: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub ruleset: String,
    #[serde(default, skip_serializing_if = "Value::is_null")]
    pub hangars: Value,
    #[serde(default, skip_serializing_if = "Value::is_null")]
    pub positions: Value,
    /// 2027: the outpost's base plate is part of its pedestal terrain, and the outpost's own
    /// dimensions (armor heights, top) start this far below the pedestal top (figure 4-34).
    #[serde(default, skip_serializing_if = "is_zero")]
    pub outpost_plinth_m: f64,
}
fn season_2026() -> String {
    "2026".into()
}
#[derive(Clone, Deserialize)]
pub struct Config {
    pub maps: BTreeMap<String, Arena>,
    pub specs: BTreeMap<String, Spec>,
    pub rulebook: Value,
    pub simulation_assumptions: Value,
    pub robot_designs: BTreeMap<String, Value>,
    #[serde(default)]
    pub rulebook_2027: Value,
    #[serde(default)]
    pub seasons: BTreeMap<String, Value>,
}
pub fn config() -> Config {
    serde_json::from_str(include_str!("../../config/rules.json")).expect("valid embedded config")
}

#[derive(Clone, Serialize, Deserialize)]
pub struct Deployment {
    pub slot: u32,
    pub model: u32,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct Robot {
    pub id: String,
    pub slot: u32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model_slot: Option<u32>,
    pub team: String,
    pub role: String,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    pub hp: f64,
    pub max_hp: f64,
    pub ammo: u32,
    pub aim: f64,
    pub pitch: f64,
    pub heading: f64,
    pub radius: f64,
    pub cooldown: f64,
    pub skill_cooldown: f64,
    pub shield: f64,
    pub energy: f64,
    pub heat: f64,
    pub overheated: bool,
    pub dash: f64,
    pub armor: f64,
    pub level: u32,
    pub kills: u32,
    pub damage: f64,
    pub alive: bool,
    pub upgrades: BTreeMap<String, u32>,
    pub experience: f64,
    pub profile: String,
    pub chassis_profile: String,
    pub weapon_profile: String,
    pub profile_locked: bool,
    pub power_limit: f64,
    pub power_w: f64,
    pub buffer_energy_j: f64,
    pub power_cutoff_until: f64,
    pub power_boost_until: f64,
    pub max_heat: f64,
    pub cooling_rate: f64,
    pub heat_permanent_lock: bool,
    #[serde(default)]
    pub overspeed_lock_until: f64,
    #[serde(default)]
    pub overspeed_permanent_lock: bool,
    #[serde(default)]
    pub overspeed_locked: bool,
    #[serde(default)]
    pub launcher_fault: bool,
    #[serde(default)]
    pub yellow_cards: u32,
    #[serde(default)]
    pub last_yellow_time: f64,
    #[serde(default)]
    pub yellow_damage_ratio: f64,
    #[serde(default)]
    pub fpv_blocked_until: f64,
    #[serde(default)]
    pub red_card: bool,
    pub last_shot_time: f64,
    pub last_damage_time: f64,
    pub death_time: f64,
    pub armor_hit_times: Vec<f64>,
    pub invulnerable_until: f64,
    pub invulnerable: bool,
    pub weak: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub weak_until: Option<f64>,
    pub respawn_progress: f64,
    pub respawn_required: f64,
    pub instant_revives: u32,
    pub pending_ammo: Vec<(f64, u32)>,
    pub pending_heal: Option<f64>,
    pub deployed: bool,
    pub deploy_at: Option<f64>,
    pub support_active: bool,
    pub support_time: f64,
    pub support_paid_fraction: f64,
    pub carried_units: u32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub unit_pickup: Option<pickup::PickupState>,
    pub attack_multiplier: f64,
    pub defense_buff: f64,
    pub vulnerability: f64,
    pub cooling_multiplier: f64,
    pub cooling_bonus: f64,
    pub assembly_invulnerable_spent: f64,
    pub zone_contacts: BTreeMap<String, f64>,
    pub last_raw_zones: Vec<String>,
    pub crossings: BTreeMap<String, f64>,
    pub design_name: String,
    pub design_focus: String,
    pub model_asset: String,
    pub mass_kg: f64,
    pub bare_mass_kg: f64,
    pub chassis_size_m: [f64; 3],
    pub quaternion: [f64; 4],
    pub angular_velocity: [f64; 3],
    pub suspension: [f64; 4],
    pub wheel_loads_n: [f64; 4],
    pub wheel_angular_velocity: [f64; 4],
    pub grounded: bool,
    pub motor_power_w: f64,
    pub battery_remaining_wh: f64,
    pub supercap_energy_j: f64,
    pub charging_power_w: f64,
    pub arm_extension: f64,
    pub crouched: bool,
    pub muzzle_height_m: f64,
    pub muzzle_reach_m: f64,
    pub armor_center_z_m: f64,
    /// Side (left/right) plate height when it differs from the front/rear plates.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub armor_side_center_z_m: Option<f64>,
    pub armor_inset_m: f64,
    pub armor_mount_offset_m: f64,
    pub armor_hit_face_offset_m: f64,
    pub speed_reference_mps: f64,
    pub crossing_firsts: Vec<String>,
    pub crossing_defense: f64,
    pub crossing_defense_until: f64,
    pub crossing_cooling_until: f64,
    pub road_cooldown_until: f64,
    /// R58 fly-slope run: 1 after the low end, 2 after reaching the crest from it (may
    /// then enter the highway no-go area); cleared off the ramp and outside that area.
    #[serde(skip)]
    pub fly_pass: u8,
    pub fortress_hold: f64,
    pub fortress_last_contact: f64,
    /// Engineer arm joints: slew, lift, reach, wrist yaw/pitch/roll, grip.
    #[serde(default, skip_serializing_if = "zeros")]
    pub arm_joints: [f64; 7],
    #[serde(default, skip_serializing_if = "zeros")]
    pub arm_tip: [f64; 3],
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub arm_mode: String,
    #[serde(default, skip_serializing_if = "zeros")]
    pub arm2_joints: [f64; 7],
    #[serde(default, skip_serializing_if = "zeros")]
    pub arm2_tip: [f64; 3],
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub arm2_mode: String,
    #[serde(default)]
    pub arm_count: u32,
    #[serde(default)]
    pub deploy_allowed: bool,
    #[serde(default)]
    pub transform_allowed: bool,
    #[serde(default)]
    pub transform_progress: f64,
    #[serde(default)]
    pub steering_angles: [f64; 4],
    #[serde(default)]
    pub chassis_type: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rescue_target: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub rescue_contact: bool,
    /// Body z axis against world up; a ground robot below 0.5 has tipped over.
    #[serde(default)]
    pub upright: f64,
    #[serde(default)]
    pub tipped: bool,
    /// False while the referee has cut chassis power (destroyed, over-power, flat battery).
    #[serde(default = "powered_default")]
    pub powered: bool,
    /// Wheel-legged leg lengths (left, right), hip axle to wheel axle, metres.
    #[serde(default, skip_serializing_if = "zeros")]
    pub legs: [f64; 2],
    /// Tallest step the legs can mount, metres (0: the chassis cannot step-climb).
    #[serde(default, skip_serializing_if = "is_zero")]
    pub step_climb_m: f64,
    /// Serial-leg hip angle about the body's lateral axis (0 = legs straight down, + swings
    /// the wheels rearward), unwrapped: flipping back from the roof turns it a full revolution.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hip_angle: Option<f64>,
    /// A serial-leg robot is swinging its legs round to flip itself back upright.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub self_righting: bool,
    /// 2027 heavy robot's modular 42 mm launcher: locked, unlocked, installing, fitted
    /// (on the robot, waiting for its first detected shot) or installed.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub launcher: String,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub launcher_progress: f64,
    /// 2027 sentry posture (attack, defense, move) and its cumulative timers.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub posture: String,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub posture_enhanced: bool,
    #[serde(default, skip_serializing_if = "zeros")]
    pub posture_time: [f64; 3],
    #[serde(default, skip_serializing_if = "zeros")]
    pub posture_enhanced_time: [f64; 3],
    #[serde(default, skip_serializing_if = "is_zero")]
    pub posture_ready_at: f64,
    #[serde(default, skip_serializing_if = "is_zero_u32")]
    pub supply_grants_taken: u32,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub respawned_at: f64,
    /// 2027 terrain-crossing buffs held, by kind, until the given time.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub crossing_buffs: BTreeMap<String, f64>,
    /// Aerial robots carried by a 2027 aerial system's hangar.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub drones: Vec<season2027::Drone>,
}
impl Robot {
    pub fn design_slot(&self) -> u32 {
        self.model_slot.unwrap_or(self.slot)
    }
    /// The third energy unit a heavy robot carries in its gripper once both storage bays are full.
    pub fn gripper_unit(&self) -> bool {
        self.unit_pickup
            .as_ref()
            .is_some_and(|s| s.stage == "carrying")
    }
    /// Units on board, stowed bays plus the one clamped in the gripper.
    pub fn units_on_board(&self) -> u32 {
        self.carried_units + u32::from(self.gripper_unit())
    }
    /// A heavy robot has two bays and a gripper; every other arm carrier only has the bays.
    pub fn unit_capacity(&self) -> u32 {
        if self.role == "heavy" {
            3
        } else {
            2
        }
    }
}

fn powered_default() -> bool {
    true
}
fn is_zero(v: &f64) -> bool {
    *v == 0.0
}
fn is_zero_u32(v: &u32) -> bool {
    *v == 0
}
fn zeros<const N: usize>(v: &[f64; N]) -> bool {
    v.iter().all(|x| *x == 0.0)
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Base {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub hp: f64,
    pub max_hp: f64,
    pub radius: f64,
    pub shield_hp: f64,
    pub armor_open: bool,
    #[serde(default)]
    pub armor_open_progress: f64,
    pub ever_destroyed: bool,
    pub rotation: f64,
    /// How far the structure itself is turned about its own axis. Blue's base is half a turn round so
    /// its armour faces the field instead of the outside wall.
    #[serde(default)]
    pub yaw: f64,
    pub rotation_stopped: bool,
    pub rotation_stop_at: f64,
    pub rotation_stop_angle: f64,
    pub armor_hit_times: Vec<f64>,
    /// 2027: current owner of an outpost (control can hand it to the other team).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub owner: Option<String>,
}
#[derive(Clone, Serialize, Deserialize, Default)]
pub struct Team {
    pub score: f64,
    pub resources: f64,
    pub kills: u32,
    pub captured_seconds: f64,
    pub attack_damage: f64,
    pub level_cap: u32,
    pub purchased_17: u32,
    pub purchased_42: u32,
    pub tech_income: f64,
    pub completed_difficulties: Vec<u32>,
    pub tech_defense: f64,
    pub rebuild_credits: u32,
    pub base_loss_total: f64,
    pub fortress_ammo: u32,
    pub fortress_capacity: u32,
    pub small_bonus_xp: f64,
    pub next_tech_income: f64,
    /// 2027 control degree (5.6.2) and the inputs of the heavy launcher's unlock time.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub control: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub base_lowest_hp: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_base_gap: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_control_gap: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub energy_units_left: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub launcher_unlock_s: Option<f64>,
}
#[derive(Clone, Serialize, Deserialize, Default)]
pub struct ZoneState {
    pub owner: Option<String>,
    pub progress: f64,
    pub contested: bool,
    pub occupants: Vec<String>,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Projectile {
    pub id: u64,
    pub source: String,
    pub team: String,
    pub role: String,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    pub remaining: f64,
    pub damage: f64,
    pub radius: f64,
    pub deployed: bool,
}
#[derive(Clone, Default)]
struct Action {
    mv: [f64; 2],
    aim: Option<f64>,
    pitch: f64,
    fire: bool,
    skill: bool,
    mv_z: f64,
    altitude: Option<f64>,
    power: Option<f64>,
    heading: Option<f64>,
    crouch: bool,
    transform: Option<bool>,
    /// Commanded chassis yaw rate (rad/s), e.g. a spinning top; overrides heading.
    yaw_rate: Option<f64>,
    /// Burst yaw authority (0..1): the yaw motors may use torque above the chassis rating, the
    /// supercapacitor firmware's spin-up mode. A pilot spins faster with it; strategies ignore it.
    yaw_boost: f64,
    /// Powered robots with no move command coast instead of braking.
    coast: bool,
    /// Wheel-legged leg length (m), jump strength (0..1) and body lean (rad, + = nose down).
    leg_height: Option<f64>,
    jump: f64,
    lean: f64,
    /// Step-climbing mode (0..1): with a climbable step in front, the legs lift the hull onto it
    /// and then retract, so a step-climbing chassis mounts kerbs and stairs without a full jump.
    climb: f64,
    rpc: Value,
}

pub struct Engine {
    pub seed: i64,
    pub duration: f64,
    pub tick: u64,
    pub status: String,
    pub winner: Option<String>,
    pub reason: Option<String>,
    pub arena: Arena,
    pub robots: Vec<Robot>,
    pub bases: BTreeMap<String, Base>,
    pub outposts: BTreeMap<String, Base>,
    pub teams: BTreeMap<String, Team>,
    pub zones: BTreeMap<String, ZoneState>,
    pub projectiles: Vec<Projectile>,
    /// Robot index by id. Every projectile resolves its shooter once per substep and the roster
    /// never changes during a match, so the lookup is built once instead of scanned per shot.
    source_index: HashMap<String, usize>,
    /// Drone collision radius per robot, straight from the design: the sweep used to re-read it
    /// out of the model JSON for every projectile that passed an aerial robot.
    drone_radius: Vec<f64>,
    pub events: Vec<Value>,
    referee_recent_events: Vec<Value>,
    specs: BTreeMap<String, Spec>,
    pub robot_models: BTreeMap<String, Value>,
    rng: u64,
    next_projectile: u64,
    rules: Value,
    sim: Value,
    pub runes: BTreeMap<String, rulebook::RuneState>,
    pub assemblies: BTreeMap<String, rulebook::AssemblyState>,
    /// Last reported refusal per team, as ("difficulty:reason", time): a cockpit that keeps asking for
    /// an order it cannot have gets one answer per second instead of one per tick.
    assembly_refused_at: BTreeMap<String, (String, f64)>,
    rune_direction: f64,
    rune_angle: f64,
    rune_phase: [f64; 2],
    rune_a: f64,
    rune_omega: f64,
    physics: Option<physics::PhysicsWorld>,
    micro_time: f64,
    micro_dt: f64,
    /// Rules season: 2026 or 2027.
    pub season: u32,
    /// 2027 control positions (outpost and base position of each team).
    pub positions: BTreeMap<String, season2027::PositionState>,
    /// 2027 large rune activations per team (each shrinks the detecting rings).
    rune_large_count: BTreeMap<String, u32>,
}
fn dist(x: f64, y: f64, a: f64, b: f64) -> f64 {
    (x - a).hypot(y - b)
}
fn approach(a: f64, b: f64, delta: f64) -> f64 {
    a + (b - a).clamp(-delta, delta)
}
fn turn(a: f64, b: f64, delta: f64) -> f64 {
    let difference =
        (b - a + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI;
    (a + difference.clamp(-delta, delta)).rem_euclid(std::f64::consts::TAU)
}
pub fn point_in_polygon(x: f64, y: f64, points: &[[f64; 2]]) -> bool {
    let mut inside = false;
    for i in 0..points.len() {
        let a = points[i];
        let b = points[(i + 1) % points.len()];
        let cross = (x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0]);
        if cross.abs() < 1e-8
            && x >= a[0].min(b[0]) - 1e-8
            && x <= a[0].max(b[0]) + 1e-8
            && y >= a[1].min(b[1]) - 1e-8
            && y <= a[1].max(b[1]) + 1e-8
        {
            return true;
        }
        if (a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0] {
            inside = !inside;
        }
    }
    inside
}
fn contains(x: f64, y: f64, t: &Terrain) -> bool {
    x >= t.x - 1e-8
        && x <= t.x + t.w + 1e-8
        && y >= t.y - 1e-8
        && y <= t.y + t.h + 1e-8
        && (t.polygon.is_empty() || point_in_polygon(x, y, &t.polygon))
}
fn terrain_elevation(t: &Terrain, x: f64, y: f64) -> f64 {
    if t.kind == "ramp" {
        let p = if let (Some(a), Some(b)) = (t.ramp_start, t.ramp_end) {
            ((x - a[0]) * (b[0] - a[0]) + (y - a[1]) * (b[1] - a[1]))
                / ((b[0] - a[0]).powi(2) + (b[1] - a[1]).powi(2))
        } else if t.axis == "x" {
            (x - t.x) / t.w
        } else {
            (y - t.y) / t.h
        };
        let p = if t.ramp_start.is_none() && t.direction < 0 {
            1.0 - p
        } else {
            p
        };
        t.base_elevation + (t.elevation - t.base_elevation) * p.clamp(0.0, 1.0)
    } else if t.kind == "bumps" && t.bump_pitch > 0.0 {
        // Figure 4-34: smooth humps whose valleys are flush with the road.
        let phase = ((x - t.x) / t.bump_pitch).rem_euclid(1.0);
        t.elevation + t.bump_height * 0.5 * (1.0 - (std::f64::consts::TAU * phase).cos())
    } else if t.edge_ramp_m > 0.0 && !t.polygon.is_empty() {
        let distance = (0..t.polygon.len())
            .map(|i| {
                let a = t.polygon[i];
                let b = t.polygon[(i + 1) % t.polygon.len()];
                ((x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0])).abs()
                    / ((b[0] - a[0]).hypot(b[1] - a[1]))
            })
            .fold(f64::INFINITY, f64::min);
        t.base_elevation
            + (t.elevation - t.base_elevation) * (distance / t.edge_ramp_m).clamp(0.0, 1.0)
    } else {
        t.elevation
    }
}
/// Progress along a fly slope's 17 degree run (0 at the low end, 1 at the crest).
pub fn fly_slope_progress(arena: &Arena, x: f64, y: f64) -> Option<f64> {
    arena
        .terrain
        .iter()
        .filter(|t| t.id.ends_with("_fly_slope") && contains(x, y, t))
        .find_map(|t| {
            let (a, b) = (t.ramp_start?, t.ramp_end?);
            let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
            Some(((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy))
        })
}
/// R58: ground robots stay out of the highway no-go area unless they flew the slope in.
pub fn forbidden_for(zone: &Value, team: &str, fly_pass: u8) -> bool {
    zone["kind"] == "base"
        || zone["kind"] == "dart_station"
        || zone["kind"] == "pad"
        || (zone["kind"] == "highway" && fly_pass < 2)
        || zone["owner"].as_str().map(|t| t != team).unwrap_or(false)
}
pub fn in_zone_rect(zone: &Value, x: f64, y: f64, margin: f64) -> bool {
    let n = |k: &str| zone[k].as_f64().unwrap_or(0.0);
    x >= n("x") - margin
        && x <= n("x") + n("w") + margin
        && y >= n("y") - margin
        && y <= n("y") + n("h") + margin
}
pub fn height_at(arena: &Arena, x: f64, y: f64) -> f64 {
    field_height(arena, x, y, false)
}
fn field_height(arena: &Arena, x: f64, y: f64, airborne: bool) -> f64 {
    let substrate = y.min(arena.height - y).max(0.0) * arena.floor_slope_deg.to_radians().tan();
    substrate
        + arena
            .terrain
            .iter()
            .filter(|t| contains(x, y, t) && !(airborne && t.kind == "helipad"))
            .map(|t| terrain_elevation(t, x, y))
            .fold(0.0, f64::max)
}
pub fn segment_box(x: f64, y: f64, dx: f64, dy: f64, w: &Wall) -> Option<f64> {
    let (x, y, dx, dy) = wall_coordinates(x, y, dx, dy, w);
    let (mut lo, mut hi): (f64, f64) = (0.0, 1.0);
    for (p, d, a, b) in [(x, dx, w.x, w.x + w.w), (y, dy, w.y, w.y + w.h)] {
        if d.abs() < 1e-12 {
            if p < a || p > b {
                return None;
            }
        } else {
            let (aa, bb) = ((a - p) / d, (b - p) / d);
            lo = lo.max(aa.min(bb));
            hi = hi.min(aa.max(bb));
        }
        if lo > hi {
            return None;
        }
    }
    Some(lo)
}
fn wall_coordinates(x: f64, y: f64, dx: f64, dy: f64, w: &Wall) -> (f64, f64, f64, f64) {
    wall_coordinates_rot(x, y, dx, dy, w, w.rotation.cos(), w.rotation.sin())
}
/// The same transform with the wall's rotation already resolved. A projectile sweep tests every
/// wall, so `cos`/`sin` are hoisted out of the per-shot path.
#[inline]
fn wall_coordinates_rot(x: f64, y: f64, dx: f64, dy: f64, w: &Wall, c: f64, s: f64) -> (f64, f64, f64, f64) {
    let (ox, oy) = (x - w.x - w.w / 2.0, y - w.y - w.h / 2.0);
    (
        w.x + w.w / 2.0 + ox * c + oy * s,
        w.y + w.h / 2.0 - ox * s + oy * c,
        dx * c + dy * s,
        -dx * s + dy * c,
    )
}
pub fn segment_circle(x: f64, y: f64, dx: f64, dy: f64, cx: f64, cy: f64, r: f64) -> Option<f64> {
    let (ox, oy) = (x - cx, y - cy);
    let c = ox * ox + oy * oy - r * r;
    if c <= 0.0 {
        return Some(0.0);
    }
    let a = dx * dx + dy * dy;
    let b = 2.0 * (ox * dx + oy * dy);
    let discriminant = b * b - 4.0 * a * c;
    if a < 1e-12 || discriminant < 0.0 {
        return None;
    }
    let t = (-b - discriminant.sqrt()) / (2.0 * a);
    if (0.0..=1.0).contains(&t) {
        Some(t)
    } else {
        None
    }
}
fn slab(p: f64, d: f64, a: f64, b: f64) -> Option<(f64, f64)> {
    if d.abs() < 1e-12 {
        if p >= a && p <= b {
            Some((0.0, 1.0))
        } else {
            None
        }
    } else {
        let (a, b) = ((a - p) / d, (b - p) / d);
        Some((a.min(b).max(0.0), a.max(b).min(1.0)))
    }
}
/// The 3-D sweep against one wall with its rotation already resolved (see `wall_coordinates_rot`).
#[inline]
#[allow(clippy::too_many_arguments)]
fn segment_box_3d_rot(
    px: f64,
    py: f64,
    pz: f64,
    pradius: f64,
    dx: f64,
    dy: f64,
    dz: f64,
    w: &Wall,
    c: f64,
    s: f64,
) -> Option<f64> {
    let (x, y, dx, dy) = wall_coordinates_rot(px, py, dx, dy, w, c, s);
    let (mut lo, mut hi): (f64, f64) = (0.0, 1.0);
    for (pos, d, a, b) in [
        (x, dx, w.x, w.x + w.w),
        (y, dy, w.y, w.y + w.h),
        (pz, dz, w.bottom - pradius, w.bottom + w.height + pradius),
    ] {
        let (a, b) = slab(pos, d, a, b)?;
        lo = lo.max(a);
        hi = hi.min(b);
        if lo > hi {
            return None;
        }
    }
    Some(lo)
}
fn segment_cylinder(
    origin: [f64; 3],
    pradius: f64,
    dx: f64,
    dy: f64,
    dz: f64,
    cx: f64,
    cy: f64,
    radius: f64,
    bottom: f64,
    top: f64,
) -> Option<f64> {
    let (ox, oy) = (origin[0] - cx, origin[1] - cy);
    let a = dx * dx + dy * dy;
    let b = 2.0 * (ox * dx + oy * dy);
    let c = ox * ox + oy * oy - radius * radius;
    let (enter, exit) = if a < 1e-12 {
        if c > 0.0 {
            return None;
        }
        (0.0, 1.0)
    } else {
        let determinant = b * b - 4.0 * a * c;
        if determinant < 0.0 {
            return None;
        }
        let root = determinant.sqrt();
        (
            ((-b - root) / (2.0 * a)).max(0.0),
            ((-b + root) / (2.0 * a)).min(1.0),
        )
    };
    let (lo, hi) = slab(origin[2], dz, bottom - pradius, top + pradius)?;
    let lo = lo.max(enter);
    let hi = hi.min(exit);
    if lo <= hi {
        Some(lo)
    } else {
        None
    }
}

impl Engine {
    pub fn new(seed: i64, duration: f64, map: &str) -> Result<Self, String> {
        Self::new_with_lineups(seed, duration, map, None)
    }
    pub fn new_with_lineups(
        seed: i64,
        duration: f64,
        map: &str,
        lineups: Option<BTreeMap<String, Vec<Deployment>>>,
    ) -> Result<Self, String> {
        Self::new_with_settings(seed, duration, map, lineups, None)
    }
    pub fn new_with_settings(
        seed: i64,
        duration: f64,
        map: &str,
        lineups: Option<BTreeMap<String, Vec<Deployment>>>,
        settings: Option<Value>,
    ) -> Result<Self, String> {
        let conf = config();
        if let Some(ref teams) = lineups {
            if teams.len() != 2 || !["red", "blue"].iter().all(|t| teams.contains_key(*t)) {
                return Err("Lineups require red and blue".into());
            }
            for list in teams.values() {
                let ids: std::collections::BTreeSet<_> = list.iter().map(|d| d.slot).collect();
                if list.is_empty()
                    || ids.len() != list.len()
                    || list
                        .iter()
                        .any(|d| d.slot == 0 || !conf.robot_designs.contains_key(&d.model.to_string()))
                {
                    return Err(
                        "Each lineup requires unique positive instances with valid models".into(),
                    );
                }
            }
        }
        if !duration.is_finite() || !(1.0..=420.0).contains(&duration) {
            return Err("duration must be in [1,420]; rulebook matches use 420".into());
        }
        let arena = conf.maps.get(map).ok_or("Unknown map")?.clone();
        let season_key = arena.season.clone();
        let season: u32 = season_key.parse().map_err(|_| "Unknown rules season")?;
        let season_conf = conf.seasons.get(&season_key).cloned().unwrap_or(Value::Null);
        // Every deployed model must pass this season's construction inspection.
        if let Some(ref teams) = lineups {
            for list in teams.values() {
                if let Some(d) = list.iter().find(|d| {
                    conf.robot_designs[&d.model.to_string()]["seasons"][&season_key]["legal"].as_bool() == Some(false)
                }) {
                    return Err(format!("Model {} does not pass the {} construction rules", d.model, season_key));
                }
                if season == 2027
                    && list.iter().filter(|d| conf.robot_designs[&d.model.to_string()]["role"] == "aerial").count() > 1
                {
                    return Err("A 2027 team has one hangar platform and at most one aerial system".into());
                }
            }
        }
        let (slot_ids, roles): (Vec<u32>, Vec<&str>) = if season == 2027 {
            (SLOT_IDS_2027.to_vec(), ROLES_2027.to_vec())
        } else {
            (SLOT_IDS.to_vec(), ROLES.to_vec())
        };
        let rules = if season == 2027 { conf.rulebook_2027.clone() } else { conf.rulebook.clone() };
        let specs: BTreeMap<String, Spec> = match season_conf.get("specs") {
            Some(v) => serde_json::from_value(v.clone()).map_err(|e| e.to_string())?,
            None => conf.specs.clone(),
        };
        let mut sim = conf.simulation_assumptions.clone();
        if let Some(overrides) = season_conf["simulation_overrides"].as_object() {
            for (key, value) in overrides {
                sim[key] = value.clone();
            }
        }
        if let Some(settings) = settings.filter(|v| !v.is_null()) {
            let fields: Value = serde_json::from_str(include_str!("../../config/battle_settings.json"))
                .map_err(|e| e.to_string())?;
            for (key, value) in settings.as_object().ok_or("Battle settings must be an object")? {
                let field = fields.as_array().unwrap().iter().find(|f| f["path"] == *key
                    && (f.get("seasons").is_none() || f["seasons"].as_array().unwrap().iter().any(|s| s == &season_key)))
                    .ok_or_else(|| format!("Unknown battle setting: {key}"))?;
                let number = value.as_f64().filter(|n| n.is_finite()
                    && *n >= field["min"].as_f64().unwrap() && *n <= field["max"].as_f64().unwrap())
                    .ok_or_else(|| format!("Invalid battle setting: {key}"))?;
                let pointer = format!("/{}", key.replace('.', "/"));
                *sim.pointer_mut(&pointer).ok_or_else(|| format!("Missing battle setting: {key}"))? = json!(number);
            }
        }
        let mut robots = Vec::new();
        let fixed_slots = lineups.as_ref().map(|teams| teams.values().all(|items| {
            items.len()==slot_ids.len() && slot_ids.iter().all(|slot| items.iter().any(|item| item.slot==*slot &&
                conf.robot_designs[&item.model.to_string()]["role"]==roles[slot_ids.iter().position(|s|s==slot).unwrap()]))
        })).unwrap_or(true);
        for team in ["red", "blue"] {
            let deployments: Vec<Deployment> = lineups
                .as_ref()
                .map(|l| l[team].clone())
                .unwrap_or_else(|| {
                    slot_ids
                        .iter()
                        .map(|s| Deployment {
                            slot: *s,
                            model: season_conf["default_lineup"][s.to_string()].as_u64().map(|m| m as u32).unwrap_or(*s),
                        })
                        .collect()
                });
            let (mut ground, mut air) = (0usize, 0usize);
            let ground_count=deployments.iter().filter(|d|conf.robot_designs[&d.model.to_string()]["role"]!="aerial").count();
            let air_count=deployments.len()-ground_count;
            for deployment in deployments {
                let slot = deployment.slot;
                let d = &conf.robot_designs[&deployment.model.to_string()];
                let role = d["role"].as_str().ok_or("Model role missing")?;
                let spec = specs.get(role).ok_or("Role has no spec in this season")?;
                let pos = if fixed_slots {
                    arena.spawn_points[team][&slot.to_string()]
                } else if role == "aerial" && season == 2027 {
                    let h = &arena.hangars[team];
                    [h["x"].as_f64().unwrap_or(14.0), h["y"].as_f64().unwrap_or(7.5)]
                } else if role == "aerial" {
                    let columns=(air_count as f64).sqrt().ceil().max(1.0) as usize;
                    let x = 1.4 + (air%columns) as f64 * (0.9f64).min((arena.width/2.0-2.5)/columns as f64);
                    let y = 1.85 + (air/columns) as f64 * (0.9f64).min((arena.height-3.7)/columns as f64);
                    air += 1;
                    if team == "red" {
                        [x, y]
                    } else {
                        [arena.width - x, arena.height - y]
                    }
                } else {
                    let (x,y)=if ground_count<=12 {([0.9,1.9,3.5][ground%3],[5.3,6.3,8.7,9.7][ground/3])}
                    else {
                        let columns=(ground_count as f64).sqrt().ceil() as usize;
                        let rows=ground_count.div_ceil(columns);
                        (0.9+(ground%columns) as f64*(1.05f64).min((arena.width/2.0-1.8)/(columns-1).max(1) as f64),
                         5.3+(ground/columns) as f64*(1.05f64).min((arena.height-6.2)/(rows-1).max(1) as f64))
                    };
                    ground += 1;
                    if team == "red" {
                        [x, y]
                    } else {
                        [arena.width - x, arena.height - y]
                    }
                };
                let size: [f64; 3] = serde_json::from_value(d["chassis_size_m"].clone()).unwrap();
                robots.push(Robot {
                    id: format!("{team}-{slot}"),
                    slot,
                    model_slot: (lineups.is_some() || deployment.model != slot).then_some(deployment.model),
                    team: team.into(),
                    role: role.into(),
                    x: pos[0],
                    y: pos[1],
                    z: if season == 2027 && role == "aerial" {
                        arena.hangars[team]["z"].as_f64().unwrap_or(1.6)
                    } else {
                        height_at(&arena, pos[0], pos[1])
                    },
                    vx: 0.0,
                    vy: 0.0,
                    vz: 0.0,
                    hp: spec.hp,
                    max_hp: spec.hp,
                    ammo: if season == 2027 { rules["initial_ammo"][role].as_u64().unwrap_or(0) as u32 } else { spec.ammo },
                    aim: if team == "red" {
                        0.0
                    } else {
                        std::f64::consts::PI
                    },
                    pitch: 0.04,
                    heading: if team == "red" {
                        0.0
                    } else {
                        std::f64::consts::PI
                    },
                    radius: size[0].hypot(size[1]) / 2.0,
                    cooldown: 0.0,
                    skill_cooldown: 0.0,
                    shield: 0.0,
                    energy: if season == 2026 && ["hero", "infantry"].contains(&role) {
                        20000.0
                    } else {
                        0.0
                    },
                    heat: 0.0,
                    overheated: false,
                    dash: 0.0,
                    armor: 0.0,
                    level: 1,
                    kills: 0,
                    damage: 0.0,
                    alive: role != "aerial" || season == 2027,
                    upgrades: BTreeMap::new(),
                    experience: 0.0,
                    profile: d["hero_profile"].as_str().unwrap().into(),
                    chassis_profile: if season == 2027 && role == "infantry" {
                        rules["profiles"]["defaults"]["infantry_chassis"].as_str().unwrap_or("hp").into()
                    } else { d["chassis_profile"].as_str().unwrap().into() },
                    weapon_profile: if season == 2027 && role == "infantry" {
                        rules["profiles"]["defaults"]["infantry_weapon"].as_str().unwrap_or("cooling").into()
                    } else { d["weapon_profile"].as_str().unwrap().into() },
                    profile_locked: false,
                    power_limit: if season == 2027 {
                        50.0
                    } else {
                        conf.rulebook["profiles"]["engineer"]["power_w"].as_f64().unwrap_or(120.0)
                    },
                    power_w: 0.0,
                    buffer_energy_j: 60.0,
                    power_cutoff_until: 0.0,
                    power_boost_until: 0.0,
                    max_heat: spec.max_heat,
                    cooling_rate: spec.cooling,
                    heat_permanent_lock: false,
                    overspeed_lock_until: 0.0,
                    overspeed_permanent_lock: false,
                    overspeed_locked: false,
                    launcher_fault: false,
                    yellow_cards: 0,
                    last_yellow_time: -1000.0,
                    yellow_damage_ratio: 0.0,
                    fpv_blocked_until: 0.0,
                    red_card: false,
                    last_shot_time: -1000.0,
                    last_damage_time: -1000.0,
                    death_time: -1000.0,
                    armor_hit_times: vec![-1000.0; 4],
                    invulnerable_until: 0.0,
                    invulnerable: false,
                    weak: false,
                    weak_until: None,
                    respawn_progress: 0.0,
                    respawn_required: 0.0,
                    instant_revives: 0,
                    pending_ammo: Vec::new(),
                    pending_heal: None,
                    deployed: false,
                    deploy_at: None,
                    support_active: false,
                    support_time: if role == "aerial" { 30.0 } else { 0.0 },
                    support_paid_fraction: 0.0,
                    carried_units: 0,
                    unit_pickup: None,
                    attack_multiplier: 1.0,
                    defense_buff: 0.0,
                    vulnerability: 0.0,
                    cooling_multiplier: 1.0,
                    cooling_bonus: 0.0,
                    assembly_invulnerable_spent: 0.0,
                    zone_contacts: BTreeMap::new(),
                    last_raw_zones: Vec::new(),
                    crossings: BTreeMap::new(),
                    crossing_firsts: Vec::new(),
                    crossing_defense: 0.0,
                    crossing_defense_until: 0.0,
                    crossing_cooling_until: 0.0,
                    road_cooldown_until: 0.0,
                    fly_pass: 0,
                    fortress_hold: 0.0,
                    fortress_last_contact: -1000.0,
                    design_name: d["name"].as_str().unwrap().into(),
                    design_focus: d["focus"].as_str().unwrap().into(),
                    model_asset: d["asset"].as_str().unwrap().into(),
                    mass_kg: d["mass_kg"].as_f64().unwrap(),
                    bare_mass_kg: d["bare_mass_kg"].as_f64().unwrap(),
                    chassis_size_m: size,
                    quaternion: if team == "red" {
                        [0.0, 0.0, 0.0, 1.0]
                    } else {
                        [0.0, 0.0, 1.0, 0.0]
                    },
                    angular_velocity: [0.0; 3],
                    suspension: [0.09; 4],
                    wheel_loads_n: [0.0; 4],
                    wheel_angular_velocity: [0.0; 4],
                    grounded: role != "aerial",
                    motor_power_w: 0.0,
                    battery_remaining_wh: d["battery_wh"].as_f64().unwrap(),
                    supercap_energy_j: d["supercap_nominal_j"].as_f64().unwrap(),
                    charging_power_w: 0.0,
                    arm_extension: 0.0,
                    crouched: false,
                    muzzle_height_m: d["muzzle_z_m"].as_f64().unwrap(),
                    muzzle_reach_m: d["muzzle_reach_m"].as_f64().unwrap(),
                    armor_center_z_m: d["armor_center_z_m"].as_f64().unwrap(),
                    armor_side_center_z_m: d["armor_side_center_z_m"].as_f64(),
                    armor_inset_m: d["armor_inset_m"].as_f64().unwrap(),
                    armor_mount_offset_m: d["armor_mount_offset_m"].as_f64().unwrap_or(0.0),
                    armor_hit_face_offset_m: d["armor_hit_face_offset_m"].as_f64().unwrap_or(0.0),
                    // The 2027 rulebook caps an infantry chassis at 5 m/s: a design that asks for
                    // more is driven at the cap instead of being fielded faster than the rules allow.
                    speed_reference_mps: {
                        let configured = d["speed_reference_mps"].as_f64().unwrap();
                        match rules["speed_limits"]["infantry_mps"].as_f64() {
                            Some(cap) if role == "infantry" => configured.min(cap),
                            _ => configured,
                        }
                    },
                    arm_joints: if d["arm"].is_object() {
                        physics::arm_stowed(&d["arm"])
                    } else {
                        [0.0; 7]
                    },
                    arm_tip: [0.0; 3],
                    arm_mode: if d["arm"].is_object() {
                        "stow".into()
                    } else {
                        String::new()
                    },
                    arm2_joints: if d["arm_count"].as_u64().unwrap_or(0)==2 {physics::arm_stowed(&d["arm2"])} else {[0.0;7]},
                    arm2_tip:[0.0;3], arm2_mode: if d["arm_count"]==2 {"stow".into()} else {String::new()},
                    arm_count:d["arm_count"].as_u64().unwrap_or(if d["arm"].is_object() {1} else {0}) as u32,
                    deploy_allowed:d["deploy_allowed"].as_bool().unwrap_or(role=="hero"),
                    transform_allowed:d["transform_allowed"].as_bool().unwrap_or(false),
                    transform_progress:0.0,steering_angles:[0.0;4],chassis_type:d["chassis_type"].as_str().unwrap_or("mecanum").into(),
                    rescue_target: None,
                    rescue_contact: false,
                    upright: 1.0,
                    tipped: false,
                    powered: true,
                    legs: if d["chassis_type"] == "wheel_legged" {
                        [d["leg_nominal_m"].as_f64().unwrap(); 2]
                    } else {
                        [0.0; 2]
                    },
                    step_climb_m: if d["chassis_type"] == "wheel_legged" {
                        d["step_climb_m"].as_f64().unwrap_or(0.0)
                    } else {
                        0.0
                    },
                    hip_angle: (d["leg_type"] == "serial").then_some(0.0),
                    self_righting: false,
                    launcher: if season == 2027 && role == "heavy" { "locked".into() } else { String::new() },
                    launcher_progress: 0.0,
                    posture: if season == 2027 && role == "sentry" { "move".into() } else { String::new() },
                    posture_enhanced: false,
                    posture_time: [0.0; 3],
                    posture_enhanced_time: [0.0; 3],
                    posture_ready_at: 0.0,
                    supply_grants_taken: 0,
                    respawned_at: 0.0,
                    crossing_buffs: BTreeMap::new(),
                    drones: if season == 2027 && role == "aerial" {
                        season2027::make_drones(team, slot, d, &arena)
                    } else {
                        Vec::new()
                    },
                });
            }
        }
        let mut bases = BTreeMap::new();
        let mut outposts = BTreeMap::new();
        let base_hp = rules["structures"]["base_hp"].as_f64().unwrap_or(5000.0);
        let outpost_hp = rules["structures"]["outpost_hp"].as_f64().unwrap_or(1500.0);
        for team in ["red", "blue"] {
            let b = arena.bases[team];
            let o = arena.outposts[team];
            bases.insert(
                team.into(),
                Base {
                    x: b[0],
                    y: b[1],
                    z: height_at(&arena, b[0], b[1]),
                    hp: base_hp,
                    max_hp: base_hp,
                    radius: 0.49,
                    shield_hp: 0.0,
                    armor_open: false,
                    armor_open_progress: 0.0,
                    ever_destroyed: false,
                    rotation: 0.0,
                    yaw: structures::base_yaw(team),
                    rotation_stopped: false,
                    rotation_stop_at: -1000.0,
                    rotation_stop_angle: 0.0,
                    armor_hit_times: vec![-1000.0; 6],
                    owner: None,
                },
            );
            outposts.insert(
                team.into(),
                Base {
                    x: o[0],
                    y: o[1],
                    z: height_at(&arena, o[0], o[1]) - arena.outpost_plinth_m,
                    hp: outpost_hp,
                    max_hp: outpost_hp,
                    radius: 0.30,
                    shield_hp: 0.0,
                    armor_open: true,
                    armor_open_progress: 1.0,
                    ever_destroyed: false,
                    rotation: 0.0,
                    yaw: 0.0,
                    rotation_stopped: false,
                    rotation_stop_at: -1000.0,
                    rotation_stop_angle: 0.0,
                    armor_hit_times: vec![-1000.0; 6],
                    owner: (season == 2027).then(|| team.to_string()),
                },
            );
        }
        let zones = arena
            .zones
            .iter()
            .map(|z| (z.id.clone(), ZoneState::default()))
            .collect();
        let robot_models = robots.iter().map(|r|(r.design_slot().to_string(),conf.robot_designs[&r.design_slot().to_string()].clone())).collect();
        let source_index = {
            let mut index: HashMap<String, usize> = HashMap::with_capacity(robots.len());
            // First match wins, exactly like the linear search this replaces.
            for (i, r) in robots.iter().enumerate() {
                index.entry(r.id.clone()).or_insert(i);
            }
            index
        };
        let drone_radius = robots
            .iter()
            .map(|r| {
                if r.drones.is_empty() {
                    return 0.0;
                }
                conf.robot_designs[&r.design_slot().to_string()]["drone"]["radius_m"]
                    .as_f64()
                    .unwrap_or(0.07)
            })
            .collect::<Vec<f64>>();
        let mut engine = Self {
            seed,
            duration,
            tick: 0,
            status: "ready".into(),
            winner: None,
            reason: None,
            arena,
            robots,
            bases,
            outposts,
            teams: [
                (
                    "red".into(),
                    Team {
                        level_cap: 5,
                        ..Default::default()
                    },
                ),
                (
                    "blue".into(),
                    Team {
                        level_cap: 5,
                        ..Default::default()
                    },
                ),
            ]
            .into(),
            zones,
            projectiles: Vec::new(),
            source_index,
            drone_radius,
            events: Vec::new(),
            referee_recent_events: Vec::new(),
            robot_models,
            specs,
            rng: seed as u64 ^ 0x9e3779b97f4a7c15,
            next_projectile: 1,
            rules,
            sim,
            runes: [
                ("red".into(), rulebook::RuneState::new()),
                ("blue".into(), rulebook::RuneState::new()),
            ]
            .into(),
            assemblies: [
                ("red".into(), rulebook::AssemblyState::new()),
                ("blue".into(), rulebook::AssemblyState::new()),
            ]
            .into(),
            assembly_refused_at: BTreeMap::new(),
            rune_direction: if seed & 1 == 0 { 1.0 } else { -1.0 },
            rune_angle: 0.0,
            rune_phase: [0.0; 2],
            rune_a: 0.9,
            rune_omega: 1.95,
            physics: None,
            micro_time: 0.0,
            micro_dt: DT,
            season,
            positions: BTreeMap::new(),
            rune_large_count: BTreeMap::new(),
        };
        if season == 2027 {
            engine.init_2027();
        }
        // One seeded motor drives both faces for the entire match, independently of
        // activation requests. Shooting or starting the other face cannot reset it.
        engine.rune_a = 0.780 + engine.random() * (1.045 - 0.780);
        engine.rune_omega = 1.884 + engine.random() * (2.0 - 1.884);
        engine.rune_phase = [
            engine.random() * std::f64::consts::TAU,
            engine.random() * std::f64::consts::TAU,
        ];
        for i in 0..engine.robots.len() {
            if season == 2027 {
                engine.apply_performance_2027(i, false);
            } else {
                engine.apply_performance(i, false);
            }
        }
        if season == 2027 {
            engine.update_buffs_2027();
        } else {
            engine.update_rulebook_buffs();
        }
        engine.physics = Some(physics::PhysicsWorld::new(
            &engine.arena,
            &engine.robots,
            &conf.robot_designs,
        ));
        Ok(engine)
    }
    pub fn time(&self) -> f64 {
        (((self.tick as f64 * DT) + self.micro_time) * 1e9).round() / 1e9
    }
    fn roster(&self) -> Value {
        if self.robots.iter().any(|r| r.model_slot.is_some()) {
            json!({"red":self.robots.iter().filter(|r|r.team=="red").map(|r|r.slot).collect::<Vec<_>>(),
                   "blue":self.robots.iter().filter(|r|r.team=="blue").map(|r|r.slot).collect::<Vec<_>>()})
        } else if self.season == 2027 {
            json!(SLOT_IDS_2027)
        } else {
            json!(SLOT_IDS)
        }
    }
    pub fn rules_profile(&self) -> &'static str {
        if self.season == 2027 {
            "rmuc-2027-v1.0.0-arena-v1"
        } else {
            "rmuc-2026-v1.0.0-arena-v3"
        }
    }
    pub fn snapshot(&self) -> Value {
        let mut frame = self.dynamic_snapshot();
        for (key, value) in self.static_snapshot() {
            frame[key] = value;
        }
        frame
    }
    /// Frame fields that never change during a match.
    pub fn static_snapshot(&self) -> [(&'static str, Value); 4] {
        [
            ("arena", json!(self.arena)),
            ("robot_models", json!(self.robot_models)),
            ("rulebook_rules", self.public_rule_summary()),
            ("simulation_assumptions", self.sim.clone()),
        ]
    }
    /// The snapshot without its static fields; transports send those once per match.
    pub fn dynamic_snapshot(&self) -> Value {
        static RULES_HASH: std::sync::OnceLock<String> = std::sync::OnceLock::new();
        let roster = self.roster();
        let mut frame = json!({"api_version":3,"engine":"rust","rules":self.rules_profile(),"tick":self.tick,
        "rules_hash":RULES_HASH.get_or_init(|| format!("{:x}",Sha256::digest(include_bytes!("../../config/rules.json")))),
        "engine_hash":self.engine_hash(),
        "time":self.time(),"dt":DT,"remaining":(self.duration-self.time()).max(0.0),"duration":self.duration,"seed":self.seed,
        "status":self.status,"winner":self.winner,"reason":self.reason,"robots":self.robots,
        "bases":self.base_snapshots(),"outposts":self.outpost_snapshots(),"runes":self.rune_snapshots(),"assemblies":self.assembly_snapshots(),"roster":roster,"teams":self.teams,"zones":self.zones,"projectiles":self.projectiles,"events":self.events});
        if self.season == 2027 {
            frame["season"] = json!("2027");
            frame["positions"] = self.position_snapshots();
        }
        frame
    }
    pub fn digest(&self) -> String {
        format!(
            "{:x}",
            Sha256::digest(serde_json::to_vec(&self.snapshot()).unwrap())
        )
    }
    fn random(&mut self) -> f64 {
        self.rng = self.rng.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.rng;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        ((z ^ (z >> 31)) >> 11) as f64 / (1u64 << 53) as f64
    }
    fn event(&mut self, kind: &str, mut data: Value) {
        data["type"] = json!(kind);
        data["tick"] = json!(self.tick);
        data["time"] = json!(self.time());
        data["id"] = json!(format!("{}:{}", self.tick, self.events.len()));
        self.referee_recent_events.push(data.clone());
        self.events.push(data);
    }
    fn referee_telemetry(&self, team: &str) -> Value {
        let health: Vec<Value> = self
            .robots
            .iter()
            .map(|r| {
                json!({"id":r.id,"slot":r.slot,"team":r.team,"role":r.role,
                "hp":r.hp,"max_hp":r.max_hp,"alive":r.alive,"yellow_cards":r.yellow_cards,"red_card":r.red_card})
            })
            .collect();
        let public_kinds = [
            "destroy",
            "respawn",
            "finish",
            "outpost_rebuilt",
            "rune_activated",
            "assembly_success",
            "yellow_card",
            "yellow_both",
            "red_card",
            "penalty_damage",
            "forfeit",
            "base_armor_opened",
            "control_changed",
            "outpost_destroyed",
            "launcher_installed",
            "drone_strike",
        ];
        let own_kinds = [
            "income",
            "ammo_purchase",
            "ammo_delivered",
            "remote_heal",
            "level_up",
            "overheat",
            "unit_pickup",
            "assembly_start",
            "assembly_failed",
            "rescue_grip",
            "rescue_complete",
            "jump",
            "overspeed",
            "launcher_speed_rejected",
            "launcher_unlocked",
            "launcher_fitted",
            "posture",
            "drone_hit",
        ];
        let events: Vec<Value> = self
            .referee_recent_events
            .iter()
            .filter_map(|e| {
                let kind = e["type"].as_str().unwrap_or("");
                let own = e["team"].as_str() == Some(team)
                    || e["robot"]
                        .as_str()
                        .map(|id| id.starts_with(&format!("{team}-")))
                        .unwrap_or(false);
                if !public_kinds.contains(&kind) && !(own && own_kinds.contains(&kind)) {
                    return None;
                }
                let mut safe = serde_json::Map::new();
                for key in [
                    "id",
                    "type",
                    "tick",
                    "time",
                    "team",
                    "robot",
                    "killer",
                    "winner",
                    "reason",
                    "amount",
                    "difficulty",
                    "target",
                    "level",
                    "hp",
                    "card_count",
                    "fpv_blocked_until",
                    "damage",
                    "position",
                    "control",
                    "previous",
                    "drone",
                    "posture",
                    "enhanced",
                    "valid",
                    "kind",
                ] {
                    if let Some(v) = e.get(key) {
                        safe.insert(key.into(), v.clone());
                    }
                }
                Some(Value::Object(safe))
            })
            .collect();
        let mut info = json!({"api_version":1,"tick":self.tick,"time":self.time(),"remaining":(self.duration-self.time()).max(0.0),
            "status":self.status,"winner":self.winner,"reason":self.reason,"robots":health,
            "bases":self.base_snapshots(),"outposts":self.outposts,"economy":self.teams[team],"events":events});
        if self.season == 2027 {
            // Control is shown to both teams on the referee client (5.6.2).
            info["positions"] = self.position_snapshots();
            info["control"] = json!({"red":self.teams["red"].control,"blue":self.teams["blue"].control});
            info["logistics"] = json!(self.logistics(team));
            info["logistics_effects"] = self.logistics_effects(team);
        }
        info
    }
    /// Points a camera can see on a robot: body centre, gimbal top and the four armor
    /// faces, in the robot's current pose. Any clear sight line makes it visible.
    fn sight_points(r: &Robot) -> Vec<[f64; 3]> {
        let q = &r.quaternion;
        let rotate = |v: [f64; 3]| {
            let (x, y, z, w) = (q[0], q[1], q[2], q[3]);
            let t = [
                2.0 * (y * v[2] - z * v[1]),
                2.0 * (z * v[0] - x * v[2]),
                2.0 * (x * v[1] - y * v[0]),
            ];
            [
                v[0] + w * t[0] + (y * t[2] - z * t[1]),
                v[1] + w * t[1] + (z * t[0] - x * t[2]),
                v[2] + w * t[2] + (x * t[1] - y * t[0]),
            ]
        };
        let armor = r.armor_center_z_m.max(0.20);
        let (hx, hy) = (r.chassis_size_m[0] * 0.45, r.chassis_size_m[1] * 0.45);
        let mut local = vec![
            [0.0, 0.0, armor],
            [0.0, 0.0, r.muzzle_height_m.max(armor) + 0.06],
        ];
        if r.role != "aerial" {
            local.extend([
                [hx, 0.0, armor],
                [-hx, 0.0, armor],
                [0.0, hy, armor],
                [0.0, -hy, armor],
            ]);
        }
        local
            .into_iter()
            .map(|v| {
                let w = rotate(v);
                [r.x + w[0], r.y + w[1], r.z + w[2]]
            })
            .collect()
    }
    fn eye(r: &Robot) -> [f64; 3] {
        // FPV camera on the gimbal; the engineer's sits on its lift mast.
        let height = if r.role == "engineer" {
            0.50
        } else {
            r.muzzle_height_m + 0.08
        };
        let offset=Self::camera_rotation(r)*rapier3d::na::Vector3::new(0.0,0.0,height);
        [r.x+offset.x, r.y+offset.y, r.z+offset.z]
    }
    pub(crate) fn effective_vision(&self, r: &Robot) -> f64 {
        self.specs[&r.role].vision * if r.overheated && r.heat > 1e-8 {
            self.sim["overheat_vision_range_multiplier"].as_f64().unwrap_or(0.35)
        } else {1.0}
    }
    fn visible(&self, a: &Robot, b: &Robot) -> bool {
        if dist(a.x, a.y, b.x, b.y) > self.effective_vision(a) {
            return false;
        }
        if let Some(world) = &self.physics {
            let i = self.robots.iter().position(|r| r.id == a.id).unwrap();
            let j = self.robots.iter().position(|r| r.id == b.id).unwrap();
            let eye = Self::eye(a);
            return Self::sight_points(b)
                .into_iter()
                .any(|p| self.point_in_camera(a,p) && world.visible(i, j, eye, p));
        }
        if !Self::sight_points(b).into_iter().any(|p|self.point_in_camera(a,p)){return false;}
        self.arena.obstacles.iter().all(|w| {
            segment_box(a.x, a.y, b.x - a.x, b.y - a.y, w)
                .map(|t| {
                    let z = a.z + 1.05 + (b.z + 0.65 - a.z - 1.05) * t;
                    z < w.bottom || z > w.bottom + w.height
                })
                .unwrap_or(true)
        })
    }
    pub fn observation(&self, team: &str, slots: Option<&[u32]>) -> Value {
        let mut observation = self.observation_without_arena(team, slots);
        observation["arena"] = json!(self.arena);
        observation
    }
    /// `observation` minus the static arena map, which a transport can encode once per match.
    pub fn observation_without_arena(&self, team: &str, slots: Option<&[u32]>) -> Value {
        let friends: Vec<&Robot> = self.robots.iter().filter(|r| r.team == team).collect();
        let observers: Vec<&Robot> = friends
            .iter()
            .copied()
            .filter(|r| {
                r.alive
                    && !r.deployed
                    && self.time() >= r.fpv_blocked_until
                    && slots.map(|s| s.contains(&r.slot)).unwrap_or(true)
                    // A 2027 hangar has no camera, and its automatic aerial robots report nothing.
                    && !(self.season == 2027 && r.role == "aerial")
            })
            .collect();
        // Every robot (either team, alive or wrecked) that an own camera can see, with
        // everything its exterior shows: pose, motion, gimbal, arm, legs and lamps.
        let mut vision = serde_json::Map::new();
        let mut seen: BTreeMap<String, Vec<u32>> = BTreeMap::new();
        for a in &observers {
            let (mut allies, mut foes) = (Vec::new(), Vec::new());
            for r in self
                .robots
                .iter()
                .filter(|r| r.id != a.id && self.visible(a, r))
            {
                if r.team == team {
                    allies.push(r.id.clone());
                } else {
                    foes.push(r.id.clone());
                    seen.entry(r.id.clone()).or_default().push(a.slot);
                }
            }
            vision.insert(a.slot.to_string(), json!({"enemies":foes,"allies":allies,"range_m":self.effective_vision(a),"fpv_dimmed":a.overheated&&a.heat>1e-8,"camera":self.camera_metadata(a)}));
        }
        let enemies: Vec<Value> = self
            .robots
            .iter()
            .filter(|r| r.team != team && seen.contains_key(&r.id))
            .map(|r| {
                let mut v = json!({"id":r.id,"slot":r.slot,"team":r.team,"role":r.role,"x":r.x,"y":r.y,"z":r.z,
                "vx":r.vx,"vy":r.vy,"vz":r.vz,"angular_velocity":r.angular_velocity,"hp":r.hp,"max_hp":r.max_hp,"alive":r.alive,"powered":r.powered,
                "targetable":r.role!="aerial"&&r.alive,"radius":r.radius,"quaternion":r.quaternion,"heading":r.heading,"aim":r.aim,"pitch":r.pitch,
                "tipped":r.tipped,"upright":r.upright,"grounded":r.grounded,"crouched":r.crouched,"deployed":r.deployed,"invulnerable":r.invulnerable,
                "carried_units":r.carried_units,"chassis_size_m":r.chassis_size_m,"armor_center_z_m":r.armor_center_z_m,"muzzle_height_m":r.muzzle_height_m,
                "design_name":r.design_name,"model_slot":r.model_slot,"transform_allowed":r.transform_allowed,"transform_progress":r.transform_progress,
                "steering_angles":r.steering_angles,"chassis_type":r.chassis_type,"seen_by":seen[&r.id]});
                if r.role == "engineer" {
                    v["arm_joints"] = json!(r.arm_joints);
                    v["arm_tip"] = json!(r.arm_tip);
                    v["arm_mode"] = json!(r.arm_mode);
                    v["arm_count"]=json!(r.arm_count);
                    if r.arm_count==2 {v["arm2_joints"]=json!(r.arm2_joints);v["arm2_tip"]=json!(r.arm2_tip);v["arm2_mode"]=json!(r.arm2_mode);}
                }
                if r.legs != [0.0; 2] {
                    v["legs"] = json!(r.legs);
                }
                if let Some(z) = r.armor_side_center_z_m {
                    v["armor_side_center_z_m"] = json!(z);
                }
                if let Some(hip) = r.hip_angle {
                    v["hip_angle"] = json!(hip);
                    v["self_righting"] = json!(r.self_righting);
                }
                v
            })
            .collect();
        let bullets: Vec<&Projectile> = self
            .projectiles
            .iter()
            .filter(|p| {
                p.team == team
                    || observers.iter().any(|a| {
                        dist(a.x, a.y, p.x, p.y) < self.effective_vision(a)
                            && self.point_in_camera(a,[p.x,p.y,p.z])
                            && self.physics.as_ref().unwrap().visible_point(
                                self.robots.iter().position(|r| r.id == a.id).unwrap(),
                                Self::eye(a),
                                [p.x, p.y, p.z],
                            )
                    })
            })
            .collect();
        let observed_models:BTreeMap<String,Value>=self.robots.iter()
            .filter(|r|r.team==team||seen.contains_key(&r.id))
            .map(|r|{let key=r.design_slot().to_string();(key.clone(),self.robot_models[&key].clone())}).collect();
        let enemy_drones: Vec<Value> = if self.season == 2027 {
            self.robots.iter().filter(|r| r.team != team).flat_map(|r| r.drones.iter())
                .filter(|d| d.state == "flying" || d.state == "down")
                .filter(|d| observers.iter().any(|a| dist(a.x, a.y, d.x, d.y) < self.effective_vision(a)
                    && self.point_in_camera(a, [d.x, d.y, d.z])
                    && self.physics.as_ref().unwrap().visible_point(self.robots.iter().position(|r| r.id == a.id).unwrap(), Self::eye(a), [d.x, d.y, d.z])))
                .map(|d| json!({"id":d.id,"x":d.x,"y":d.y,"z":d.z,"vx":d.vx,"vy":d.vy,"vz":d.vz,"state":d.state}))
                .collect()
        } else {
            Vec::new()
        };
        let mut observation = json!({"api_version":3,"seed":self.seed,"tick":self.tick,"time":self.time(),"dt":DT,"policy_dt":0.1,
            "remaining":(self.duration-self.time()).max(0.0),"team":team,
            "controlled_ids":friends.iter().filter(|r|slots.map(|s|s.contains(&r.slot)).unwrap_or(true)).map(|r|r.slot.to_string()).collect::<Vec<_>>(),
            "robots":friends,"robot_models":observed_models,"enemies":enemies,"bases":self.base_snapshots(),"outposts":self.outpost_snapshots(),"runes":self.observed_runes(team,&observers),"assemblies":self.assembly_snapshots(),"rulebook_rules":self.public_rule_summary(),"simulation_assumptions":self.sim,"roster":self.roster(),"teams":self.observed_teams(team),"zones":self.zones,
            "projectiles":bullets,"vision":vision,"specs":self.specs,"referee":self.referee_telemetry(team)});
        if self.season == 2027 {
            observation["season"] = json!("2027");
            observation["positions"] = self.position_snapshots();
            observation["enemy_drones"] = json!(enemy_drones);
            // Table 5-19: the same lambda the referee feed shows, with the resolved budgets a
            // strategy or custom terminal needs (bandwidth is data, FPV resolution is a picture).
            observation["logistics"] = json!(self.logistics(team));
            observation["logistics_effects"] = self.logistics_effects(team);
        }
        observation
    }
    #[cfg(test)]
    fn blocked(&self, x: f64, y: f64, radius: f64) -> bool {
        if x < radius
            || x > self.arena.width - radius
            || y < radius
            || y > self.arena.height - radius
        {
            return true;
        }
        self.arena
            .obstacles
            .iter()
            .filter(|w| !w.id.contains("roof"))
            .any(|w| {
                let (x, y, _, _) = wall_coordinates(x, y, 0.0, 0.0, w);
                dist(x, y, x.clamp(w.x, w.x + w.w), y.clamp(w.y, w.y + w.h)) < radius - 1e-9
            })
            || self
                .bases
                .values()
                .any(|b| dist(x, y, b.x, b.y) < radius + b.radius)
    }
    fn normalize(&mut self, raw: &Value, i: usize) -> Action {
        if self.robots[i].red_card {
            return Action::default();
        }
        let id = self.robots[i].id.clone();
        let mut a = Action {
            rpc: raw.clone(),
            pitch: 0.04,
            ..Default::default()
        };
        if !raw.is_object() {
            self.event(
                "invalid_action",
                json!({"robot":id,"reason":"action must be an object"}),
            );
            return a;
        }
        if let Some(mv) = raw.get("move") {
            if let Some(v) = mv
                .as_array()
                .filter(|v| v.len() == 2 || (v.len() == 3 && self.robots[i].role == "aerial"))
            {
                if let (Some(x), Some(y)) = (
                    v[0].as_f64().filter(|v| v.is_finite()),
                    v[1].as_f64().filter(|v| v.is_finite()),
                ) {
                    let (x, y) = (x.clamp(-1.0, 1.0), y.clamp(-1.0, 1.0));
                    let n = x.hypot(y).max(1.0);
                    a.mv = [x / n, y / n];
                    if v.len() == 3 {
                        a.mv_z = v[2].as_f64().unwrap_or(0.0).clamp(-1.0, 1.0);
                    }
                } else {
                    self.event(
                        "invalid_action",
                        json!({"robot":id,"reason":"move must contain finite numbers"}),
                    );
                }
            } else {
                self.event(
                    "invalid_action",
                    json!({"robot":id,"reason":"move must contain two numbers"}),
                );
            }
        }
        for name in ["aim", "pitch"] {
            if let Some(v) = raw.get(name).filter(|v| !v.is_null()) {
                if let Some(n) = v.as_f64().filter(|v| v.is_finite()) {
                    if name == "aim" {
                        a.aim = Some(n.rem_euclid(std::f64::consts::TAU));
                    } else {
                        a.pitch =
                            n.clamp(-std::f64::consts::FRAC_PI_2, std::f64::consts::FRAC_PI_2);
                    }
                } else {
                    self.event(
                        "invalid_action",
                        json!({"robot":id,"reason":format!("{name} must be finite")}),
                    );
                }
            }
        }
        for name in ["fire", "skill"] {
            if let Some(v) = raw.get(name) {
                if let Some(b) = v.as_bool() {
                    if name == "fire" {
                        a.fire = b;
                    } else {
                        a.skill = b;
                    }
                } else {
                    self.event(
                        "invalid_action",
                        json!({"robot":id,"reason":format!("{name} must be boolean")}),
                    );
                }
            }
        }
        a.altitude = raw
            .get("altitude")
            .and_then(Value::as_f64)
            .filter(|v| v.is_finite());
        a.heading = raw
            .get("heading")
            .and_then(Value::as_f64)
            .filter(|v| v.is_finite());
        a.crouch = raw.get("crouch").and_then(Value::as_bool).unwrap_or(false);
        a.transform = raw.get("transform").and_then(Value::as_bool);
        if a.transform.is_some() && !self.robots[i].transform_allowed {self.event("invalid_action",json!({"robot":id,"reason":"This model cannot transform"}));a.transform=None;}
        let number = |key: &str| {
            raw.get(key)
                .and_then(Value::as_f64)
                .filter(|v| v.is_finite())
        };
        a.yaw_rate = number("yaw_rate").map(|v| v.clamp(-12.0, 12.0));
        a.yaw_boost = number("yaw_boost").unwrap_or(0.0).clamp(0.0, 1.0);
        a.coast = raw.get("brake").and_then(Value::as_bool) == Some(false);
        a.leg_height = number("leg_height");
        a.jump = number("jump").unwrap_or(0.0).clamp(0.0, 1.0);
        a.lean = number("lean").unwrap_or(0.0).clamp(-0.35, 0.35);
        a.climb = number("climb").unwrap_or(0.0).clamp(0.0, 1.0);
        if raw.get("jump").and_then(Value::as_bool) == Some(true) {
            a.jump = 1.0;
        }
        if raw.get("climb").and_then(Value::as_bool) == Some(true) {
            a.climb = 1.0;
        }
        let legged = self.robots[i].legs != [0.0; 2];
        if !legged && (a.leg_height.is_some() || a.jump > 0.0 || a.lean != 0.0 || a.climb > 0.0) {
            self.event(
                "invalid_action",
                json!({"robot":id,"reason":"leg_height, jump, lean and climb need the wheel-legged chassis"}),
            );
            (a.leg_height, a.jump, a.lean, a.climb) = (None, 0.0, 0.0, 0.0);
        }
        if a.climb > 0.0 && self.robots[i].step_climb_m <= 0.0 {
            self.event(
                "invalid_action",
                json!({"robot":id,"reason":"this model cannot step-climb"}),
            );
            a.climb = 0.0;
        }
        a.power = raw
            .get("power")
            .and_then(Value::as_f64)
            .filter(|v| v.is_finite())
            .map(|v| v.clamp(0.0, 200.0));
        if a.skill || raw.get("upgrade").filter(|v| !v.is_null()).is_some() {
            self.event("invalid_action",json!({"robot":id,"reason":"generic skills and coin upgrades do not exist in this rules profile"}));
        }
        a
    }
    pub fn finish(&mut self, winner: Option<String>, reason: &str) {
        self.status = "finished".into();
        self.winner = winner;
        self.reason = Some(reason.into());
        self.event("finish", json!({"winner":self.winner,"reason":reason}));
    }
    pub fn step(&mut self, commands: &Value) -> Value {
        self.advance(commands);
        self.snapshot()
    }
    /// One game tick without building a snapshot.
    pub fn advance(&mut self, commands: &Value) {
        if self.status == "finished" {
            return;
        }
        self.status = "running".into();
        self.events.clear();
        self.referee_recent_events
            .retain(|e| e["tick"].as_u64().unwrap_or(0) + 4 >= self.tick);
        let mut actions = Vec::new();
        for i in 0..self.robots.len() {
            let r = &self.robots[i];
            let raw = commands
                .get(&r.team)
                .and_then(|c| c.get(r.slot.to_string()));
            actions.push(self.normalize(raw.unwrap_or(&json!({})), i));
        }
        for team in ["red", "blue"] {
            if let Some(obj) = commands.get(team).and_then(Value::as_object) {
                for key in obj.keys() {
                    if !self.robots.iter().any(|r| r.team == team && r.slot.to_string() == *key) {
                        self.event(
                            "invalid_action",
                            json!({"team":team,"reason":"uncontrolled slot"}),
                        );
                    }
                }
            }
        }
        if self.tick == 0 && commands.get("referee").is_some() {
            for i in 0..self.robots.len() {
                if self.season == 2027 {
                    self.select_profile_2027(i, &actions[i].rpc);
                } else {
                    self.select_profile(i, &actions[i].rpc);
                }
                self.robots[i].profile_locked = true;
            }
        }
        self.process_referee_commands(commands.get("referee"));
        if self.status == "finished" {
            return;
        }
        if self.season == 2027 {
            self.before_step_2027(&actions);
        } else {
            self.rulebook_before_step(&actions);
        }
        for base in self.bases.values_mut() {
            base.armor_open_progress = approach(base.armor_open_progress, if base.armor_open {1.0} else {0.0}, DT / 1.5);
        }
        let goals = self.arm_goals(&actions);
        let mut world = self.physics.take().expect("physics initialized");
        world.update_base_covers(&self.bases);
        self.micro_dt = physics::PHYSICS_DT;
        for substep in 0..6 {
            self.micro_time = (substep + 1) as f64 * physics::PHYSICS_DT;
            let now = self.time();
            world.step(&self.arena, &mut self.robots, &actions, &goals, now);
            self.physics = Some(world);
            if self.season == 2027 {
                self.fire_2027(&actions);
                self.drone_strikes();
                self.keep_drones_clear_2027();
            } else {
                self.rulebook_fire(&actions);
            }
            self.fly_projectiles();
            world = self.physics.take().unwrap();
        }
        for impact in world.take_impacts() {
            self.event("collision", impact);
        }
        for mut event in world.take_arm_events() {
            let kind = event["type"].as_str().unwrap_or("").to_string();
            event.as_object_mut().unwrap().remove("type");
            self.event(&kind, event);
        }
        self.physics = Some(world);
        self.physics
            .as_ref()
            .unwrap()
            .refresh_velocities(&mut self.robots);
        // Publish buffs and protection at the completed frame's time, not its start.
        self.micro_time = DT;
        self.micro_dt = DT;
        if self.season == 2027 {
            self.update_buffs_2027();
            self.advance_pickups();
            self.after_step_2027();
            // The outposts turned this tick: keep the published drones clear of the armor.
            self.keep_drones_clear_2027();
            self.micro_time = 0.0;
            self.tick += 1;
            self.check_winner_2027();
            return;
        }
        self.update_rulebook_buffs();
        self.advance_pickups();
        self.rulebook_after_step();
        self.micro_time = 0.0;
        self.tick += 1;
        self.check_rulebook_winner();
    }
    fn fly_projectiles(&mut self) {
        let projectiles = std::mem::take(&mut self.projectiles);
        let mut remaining = Vec::new();
        // None of this changes while the projectiles fly, so it is resolved once per call instead
        // of once per shot per substep: the wall rotations (`cos`/`sin` per wall would otherwise be
        // recomputed for every sweep), the base shells, the armour plates and the drone radius.
        let walls: Vec<(f64, f64)> = self
            .arena
            .obstacles
            .iter()
            .map(|w| (w.rotation.cos(), w.rotation.sin()))
            .collect();
        let mut covers: Vec<(Wall, f64, f64)> = Vec::new();
        for team in ["red", "blue"] {
            for cover in structures::covers(&self.bases[team]) {
                let (c, s) = (cover.rotation.cos(), cover.rotation.sin());
                covers.push((cover, c, s));
            }
        }
        // x, y, z, outpost
        let mut structures: Vec<(f64, f64, f64, bool)> = Vec::with_capacity(4);
        // plates, outpost, team index, structure still standing, owner is red
        let mut armor: Vec<(Vec<structures::Plate>, bool, usize, bool, bool)> = Vec::with_capacity(4);
        for (ti, team) in ["red", "blue"].into_iter().enumerate() {
            for (outpost, b) in [(false, &self.bases[team]), (true, &self.outposts[team])] {
                structures.push((b.x, b.y, b.z, outpost));
                let plates = if self.season == 2027 {
                    Self::structure_plates_2027(b, outpost)
                } else {
                    structures::plates(b, outpost)
                };
                let owner_red = if self.season == 2027 {
                    b.owner.as_deref().unwrap_or(team) == "red"
                } else {
                    team == "red"
                };
                armor.push((plates, outpost, ti, b.hp > 0.0, owner_red));
            }
        }
        let mut hits: Vec<(f64, u8, usize)> = Vec::new();
        for mut p in projectiles {
            let source = self.source_index.get(&p.source).copied();
            if source.is_none() && self.season != 2027 {
                continue;
            }
            let speed = p.vx.hypot(p.vy);
            let length = (speed * self.micro_dt).min(p.remaining);
            let ratio = if speed > 0.0 {
                length / speed
            } else {
                self.micro_dt
            };
            let mass = if is_42(&p.role) { 0.0445 } else { 0.0032 };
            let drag = 0.5 * 1.225 * 0.47 * std::f64::consts::PI * p.radius * p.radius / mass;
            let velocity = p.vx.hypot(p.vy).hypot(p.vz);
            let (ax, ay, az) = (
                -drag * velocity * p.vx,
                -drag * velocity * p.vy,
                -9.81 - drag * velocity * p.vz,
            );
            let (dx, dy, dz) = (
                p.vx * ratio + 0.5 * ax * ratio * ratio,
                p.vy * ratio + 0.5 * ay * ratio * ratio,
                p.vz * ratio + 0.5 * az * ratio * ratio,
            );
            hits.clear();
            for (w, (c, s)) in self.arena.obstacles.iter().zip(walls.iter()) {
                if let Some(t) = segment_box_3d_rot(p.x, p.y, p.z, p.radius, dx, dy, dz, w, *c, *s) {
                    hits.push((t, 0, 0));
                }
            }
            if let Some(t) =
                self.physics
                    .as_ref()
                    .unwrap()
                    .solid_hit(source.unwrap_or(usize::MAX), [p.x, p.y, p.z], [dx, dy,dz])
            {
                hits.push((t, 5, 0));
            }
            // Only exposed armor plates register HP damage; chassis stops projectiles.
            for (i, r) in self.robots.iter().enumerate() {
                if r.alive && r.role != "aerial" && r.team != p.team {
                    if let Some((t, _)) = physics::armor_hit(r, [p.x, p.y, p.z], [dx, dy, dz]) {
                        hits.push((t, 1, i));
                    }
                }
            }
            let p_red = p.team == "red";
            let enemy = if p_red { "blue" } else { "red" };
            // 2027: flying aerial robots stop projectiles and are pushed by them.
            if self.season == 2027 {
                for (ri, r) in self.robots.iter().enumerate() {
                    if r.drones.is_empty() {
                        continue;
                    }
                    let radius = self.drone_radius[ri];
                    for (k, d) in r.drones.iter().enumerate() {
                        if (d.state == "flying" || d.state == "down") && r.id != p.source {
                            if let Some(t) = segment_cylinder([p.x - d.x, p.y - d.y, p.z - d.z], p.radius,
                                dx, dy, dz, 0.0, 0.0, radius + p.radius, -radius * 0.5, radius * 0.5) {
                                hits.push((t, 6, ri * 8 + k));
                            }
                        }
                    }
                }
            }
            // Finite armor is distinct from solid covers, supports and friendly structures.
            for (cover, c, s) in &covers {
                if let Some(t) = segment_box_3d_rot(p.x, p.y, p.z, p.radius, dx, dy, dz, cover, *c, *s) {
                    hits.push((t, 5, 0));
                }
            }
            for (x, y, z, outpost) in &structures {
                let (outpost, z) = (*outpost, *z);
                if let Some(t) = segment_cylinder([p.x, p.y, p.z], p.radius, dx, dy, dz, *x, *y,
                    if outpost {0.325} else {0.49}, z, z + if outpost {0.055} else {0.1}) {
                    hits.push((t, 5, 0));
                }
                if let Some(t) = segment_cylinder([p.x, p.y, p.z], p.radius, dx, dy, dz, *x, *y, 0.065, z, z + if outpost {1.38} else {1.181}) {
                    hits.push((t, 5, 0));
                }
            }
            for (plates, outpost, ti, live, owner_red) in &armor {
                if plates.is_empty() {
                    continue;
                }
                let hostile = *live && (*owner_red != p_red);
                let kind = if hostile { if *outpost { 3 } else { 2 } } else { 5 };
                let index = if self.season == 2027 { *ti } else { 0 };
                for plate in plates {
                    if let Some(t) = structures::intersection(plate, [p.x,p.y,p.z], [dx,dy,dz]) {
                        hits.push((t, kind, index));
                    }
                }
            }
            if let Some((t, arm)) = self.rune_intersection(&p, dx, dy, dz) {
                hits.push((t, 4, arm));
            }
            // Sample terrain along the swept segment; substep spacing is <= 0.2m.
            let n = (length / 0.2).ceil().max(1.0) as u32;
            for i in 1..=n {
                let t = i as f64 / n as f64;
                if p.z + dz * t <= height_at(&self.arena, p.x + dx * t, p.y + dy * t) {
                    hits.push((t, 0, 0));
                    break;
                }
            }
            hits.sort_by(|a, b| a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)).then(a.2.cmp(&b.2)));
            if let Some(&(t, kind, index)) = hits.first() {
                let (x, y, z) = (p.x + dx * t, p.y + dy * t, (p.z + dz * t).max(0.03));
                if self.season == 2027 {
                    let owner = ["red", "blue"][index.min(1)];
                    match kind {
                        1 => self.robot_hit_2027(index, &p, x, y, z),
                        2 => self.structure_hit_2027(owner, false, &p, x, y, z),
                        3 => self.structure_hit_2027(owner, true, &p, x, y, z),
                        4 => self.rune_hit_2027(&p, index, x, y, z),
                        6 => self.drone_hit(index / 8, index % 8, &p),
                        _ => {}
                    }
                } else if kind == 1 {
                    self.rulebook_robot_hit(index, &p, t, x, y, z);
                } else if kind == 2 {
                    self.rulebook_structure_hit(enemy, false, &p, t, x, y, z);
                } else if kind == 3 {
                    self.rulebook_structure_hit(enemy, true, &p, t, x, y, z);
                } else if kind == 4 {
                    self.rulebook_rune_hit(&p, index, x, y, z);
                }
                self.event("impact", json!({"x":x,"y":y,"z":z,"team":p.team}));
                continue;
            }
            p.x += dx;
            p.y += dy;
            p.z += dz;
            p.vx += ax * ratio;
            p.vy += ay * ratio;
            p.vz += az * ratio;
            p.remaining -= length;
            if p.remaining > 0.0
                && p.x >= 0.0
                && p.x <= self.arena.width
                && p.y >= 0.0
                && p.y <= self.arena.height
            {
                remaining.push(p);
            }
        }
        self.projectiles = remaining;
    }
}

#[cfg(test)]
mod damage_tests;
#[cfg(test)]
mod deck_tests;
#[cfg(test)]
mod realism_tests;
#[cfg(test)]
mod model_tests;
#[cfg(test)]
mod tests;
#[cfg(test)]
mod spin_tests;

#[cfg(test)]
mod vision_tests;
#[cfg(test)]
mod season2027_tests;
