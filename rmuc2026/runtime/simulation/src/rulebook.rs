use super::*;

/// Actuator phases advance one step per 10 Hz RPC settlement.
const ASSEMBLY_SETTLE_S: f64 = 0.1;

#[derive(Clone, Serialize, Deserialize)]
pub struct RuneState {
    pub status: String,
    pub mode: String,
    pub small_chances: u32,
    pub large_chances: u32,
    pub started: f64,
    pub active_until: f64,
    pub hit_deadline: f64,
    pub pair_deadline: f64,
    pub lit: Vec<usize>,
    pub activated_arms: Vec<usize>,
    pub rounds: u32,
    pub hit_count: u32,
    pub ring_sum: f64,
    pub attack: f64,
    pub defense: f64,
    pub cooling: f64,
    #[serde(default)]
    pub last_hit_at: [Option<f64>; 5],
    #[serde(default)]
    pub last_hit_valid: [bool; 5],
}
impl RuneState {
    pub fn new() -> Self {
        Self {
            status: "idle".into(),
            mode: "small".into(),
            small_chances: 1,
            large_chances: 0,
            started: 0.0,
            active_until: 0.0,
            hit_deadline: 0.0,
            pair_deadline: 0.0,
            lit: Vec::new(),
            activated_arms: Vec::new(),
            rounds: 0,
            hit_count: 0,
            ring_sum: 0.0,
            attack: 1.0,
            defense: 0.0,
            cooling: 1.0,
            last_hit_at: [None; 5],
            last_hit_valid: [false; 5],
        }
    }
}
#[derive(Clone, Serialize, Deserialize)]
pub struct AssemblyState {
    pub difficulty: u32,
    pub stage: String,
    pub started: f64,
    pub ready_at: f64,
    pub retry_at: f64,
    pub permanent_lock: bool,
    pub attempt_by_difficulty: BTreeMap<u32, u32>,
    /// Successful assemblies seed the next pose; failed retries keep the same pose.
    #[serde(default)]
    pub successes_by_difficulty: BTreeMap<u32, u32>,
    pub inserted: f64,
    pub translated: f64,
    pub rotation: f64,
    pub target_rotation: f64,
    pub inserted_units: u32,
    pub first_unit_at: f64,
    pub attempt: u32,
    pub pose: [f64; 6],
    pub rebuild_hold: BTreeMap<String, f64>,
    pub interrupted: bool,
    /// Displayed core pose, moving at a finite rate toward `pose` or the initial pose.
    #[serde(default)]
    pub current_pose: [f64; 6],
    #[serde(default)]
    pub confirm_progress_s: f64,
    #[serde(default)]
    pub buffer_until: f64,
    #[serde(default)]
    pub repeat_ready_at: f64,
    /// 2027 steps 3-6 on the own core, and the borrowed core of a level-4 attempt.
    #[serde(default, skip_serializing_if = "is_zero_f")]
    pub lifted: f64,
    #[serde(default, skip_serializing_if = "is_zero_f")]
    pub flipped: f64,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub confirmed: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub second: Option<season2027::CoreProgress>,
    #[serde(default, skip_serializing_if = "no_steps")]
    pub step_times: [Option<f64>; 5],
    /// Units physically inside the core: counted when one is pushed in and released again when the
    /// core spends them on success or salvages them after a failure. While this is non-zero the
    /// difficulty is latched — the operator may not switch grades with a unit already in the socket.
    #[serde(default, skip_serializing_if = "is_zero_u32")]
    pub core_units: u32,
    /// When the core salvages a failed attempt's units back into the team's stock.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub recover_at: Option<f64>,
    /// Why the last attempt failed, kept until the next one starts so the HUD can explain it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub fail_reason: Option<String>,
    /// Next core-overload alarm while a robot blocks the core's path.
    #[serde(default, skip_serializing_if = "is_zero_f")]
    pub overload_at: f64,
}
fn is_zero_u32(v: &u32) -> bool {
    *v == 0
}
fn is_zero_f(v: &f64) -> bool {
    *v == 0.0
}
fn no_steps(v: &[Option<f64>; 5]) -> bool {
    v.iter().all(Option::is_none)
}
impl AssemblyState {
    pub fn core(&self, core: usize) -> season2027::CoreProgress {
        if core == 1 {
            return self.second.clone().unwrap_or_default();
        }
        season2027::CoreProgress {
            inserted: self.inserted,
            lifted: self.lifted,
            flipped: self.flipped,
            rotation: self.rotation,
            target_rotation: self.target_rotation,
            confirm: self.confirm_progress_s,
            confirmed: self.confirmed,
            step_times: self.step_times,
        }
    }
    pub fn set_core(&mut self, core: usize, c: season2027::CoreProgress) {
        if core == 1 {
            self.second = Some(c);
            return;
        }
        self.inserted = c.inserted;
        self.lifted = c.lifted;
        self.flipped = c.flipped;
        self.rotation = c.rotation;
        self.target_rotation = c.target_rotation;
        self.confirm_progress_s = c.confirm;
        self.confirmed = c.confirmed;
        self.step_times = c.step_times;
    }
    pub fn new() -> Self {
        Self {
            difficulty: 0,
            stage: "idle".into(),
            started: 0.0,
            ready_at: 0.0,
            retry_at: 0.0,
            permanent_lock: false,
            attempt_by_difficulty: BTreeMap::new(),
            successes_by_difficulty: BTreeMap::new(),
            inserted: 0.0,
            translated: 0.0,
            rotation: 0.0,
            target_rotation: 0.0,
            inserted_units: 0,
            first_unit_at: 0.0,
            attempt: 0,
            pose: [-0.2, 0.2, 0.8, 0.0, 90.0, 0.0],
            rebuild_hold: BTreeMap::new(),
            interrupted: false,
            current_pose: [-0.2, 0.2, 0.8, 0.0, 90.0, 0.0],
            confirm_progress_s: 0.0,
            buffer_until: 0.0,
            repeat_ready_at: 0.0,
            lifted: 0.0,
            flipped: 0.0,
            confirmed: false,
            second: None,
            step_times: [None; 5],
            core_units: 0,
            recover_at: None,
            fail_reason: None,
            overload_at: 0.0,
        }
    }
}
fn n(v: &Value, k: &str) -> f64 {
    v[k].as_f64().unwrap_or(0.0)
}
/// `x.sin()` kept out of line: beside `x.cos()` LLVM fuses the pair into Apple's
/// `__sincos_stret`, which rounds the last bit differently and would move rune targets.
#[inline(never)]
fn unfused_sin(x: f64) -> f64 {
    x.sin()
}
fn active_until(value: f64, now: f64) -> bool {
    value > now + 1e-8
}
fn opponent(team: &str) -> &str {
    if team == "red" {
        "blue"
    } else {
        "red"
    }
}

impl Engine {
    pub fn engine_hash(&self) -> String {
        // The embedded sources never change at runtime; hashing them per frame cost ~10% of a step.
        static HASH: std::sync::OnceLock<String> = std::sync::OnceLock::new();
        HASH.get_or_init(|| {
            let mut h = Sha256::new();
            h.update(include_bytes!("lib.rs"));
            h.update(include_bytes!("rulebook.rs"));
            h.update(include_bytes!("physics.rs"));
            h.update(include_bytes!("penalties.rs"));
            h.update(include_bytes!("structures.rs"));
            h.update(include_bytes!("pickup.rs"));
            h.update(include_bytes!("season2027.rs"));
            h.update(include_bytes!("vision.rs"));
            h.update(include_bytes!("training_scenario.rs"));
            h.update(include_bytes!("../Cargo.lock"));
            format!("{:x}", h.finalize())
        })
        .clone()
    }
    fn disengaged(&self, i: usize) -> bool {
        let r = &self.robots[i];
        r.alive
            && self.time() - r.last_shot_time >= 6.0 - 1e-8
            && self.time() - r.last_damage_time >= 6.0 - 1e-8
    }
    fn robot_invulnerable(&self, i: usize) -> bool {
        let r = &self.robots[i];
        if !r.alive {
            return false;
        }
        if active_until(r.invulnerable_until, self.time()) {
            return true;
        }
        if r.role != "engineer" || r.weak {
            return false;
        }
        self.arena.zones.iter().any(|z| {
            z.owner.as_deref() == Some(r.team.as_str())
                && (z.kind == "resource"
                    || z.kind == "assembly" && r.assembly_invulnerable_spent + 1e-8 < 180.0)
                && self.zones[&z.id].occupants.contains(&r.id)
        })
    }
    pub(super) fn raw_zone(&self, i: usize, kind: &str, owner: Option<&str>) -> bool {
        if self.season == 2027 {
            return self.raw_zone_2027(i, kind, owner);
        }
        let r = &self.robots[i];
        self.arena.zones.iter().any(|z| {
            z.kind == kind
                && owner.map(|o| z.owner.as_deref() == Some(o)).unwrap_or(true)
                && (if z.polygon.is_empty() {
                    dist(r.x, r.y, z.x, z.y) <= z.radius
                } else {
                    point_in_polygon(r.x, r.y, &z.polygon)
                })
        })
    }
    fn contacted_zone(&self, i: usize, kind: &str, owner: Option<&str>) -> bool {
        let r = &self.robots[i];
        self.arena.zones.iter().any(|z| {
            z.kind == kind
                && owner.map(|o| z.owner.as_deref() == Some(o)).unwrap_or(true)
                && r.zone_contacts
                    .get(&z.id)
                    .map(|t| self.time() - t <= 2.0 + 1e-8)
                    .unwrap_or(false)
        })
    }
    pub(super) fn apply_performance(&mut self, i: usize, add_delta: bool) {
        if self.season == 2027 {
            return self.apply_performance_2027(i, add_delta);
        }
        let r = &self.robots[i];
        let level = (r.level.clamp(1, 10) - 1) as usize;
        let old = r.max_hp;
        let role = r.role.clone();
        let (hp, power, heat, cooling) = match role.as_str() {
            "hero" => {
                let v = &self.rules["profiles"]["hero"][&r.profile][level];
                (
                    n(v, "hp"),
                    n(v, "power_w"),
                    n(v, "max_heat"),
                    n(v, "cooling"),
                )
            }
            "infantry" => {
                let c = &self.rules["profiles"]["infantry_chassis"][&r.chassis_profile][level];
                let w = &self.rules["profiles"]["infantry_weapon"][&r.weapon_profile][level];
                (
                    n(c, "hp"),
                    n(c, "power_w"),
                    n(w, "max_heat"),
                    n(w, "cooling"),
                )
            }
            "engineer" => (250.0, 120.0, 0.0, 0.0),
            _ => {
                let v = &self.rules["profiles"]["aerial"][level];
                (0.0, 0.0, n(v, "max_heat"), n(v, "cooling"))
            }
        };
        let r = &mut self.robots[i];
        r.max_hp = hp;
        r.power_limit = power;
        r.max_heat = heat;
        r.cooling_rate = cooling;
        if add_delta && r.alive {
            r.hp = (r.hp + (hp - old).max(0.0)).min(hp);
        } else if !r.profile_locked && r.alive {
            r.hp = hp;
        }
    }
    fn gain_xp(&mut self, i: usize, value: f64) {
        if self.season == 2027 {
            return self.gain_xp_2027(i, value);
        }
        let r = &self.robots[i];
        if r.role == "engineer" || !r.alive || r.level >= self.teams[&r.team].level_cap {
            return;
        }
        let team = r.team.clone();
        let cap = self.teams[&team].level_cap;
        let threshold = self.rules["experience"]["thresholds"][cap as usize - 1]
            .as_f64()
            .unwrap();
        let room = (threshold - r.experience).max(0.0);
        let base_gain = value.min(room);
        let mut bonus = 0.0;
        if self.runes[&team].status == "active" && self.runes[&team].mode == "small" {
            bonus = base_gain
                .min((room - base_gain).max(0.0))
                .min((1200.0 - self.teams[&team].small_bonus_xp).max(0.0));
            self.teams.get_mut(&team).unwrap().small_bonus_xp += bonus;
        }
        self.robots[i].experience += base_gain + bonus;
        let thresholds = self.rules["experience"]["thresholds"].as_array().unwrap();
        let mut level = self.robots[i].level;
        while level < cap
            && self.robots[i].experience >= thresholds[level as usize].as_f64().unwrap()
        {
            level += 1;
        }
        if level != self.robots[i].level {
            self.robots[i].level = level;
            self.apply_performance(i, true);
            self.event(
                "level_up",
                json!({"robot":self.robots[i].id,"level":level,"team":team}),
            );
        }
    }
    pub(super) fn select_profile(&mut self, i: usize, rpc: &Value) {
        if self.tick != 0 {
            return;
        }
        let Some(profile) = rpc.get("profile") else {
            return;
        };
        let r = &mut self.robots[i];
        if r.role == "hero" {
            if let Some(s) = profile.as_str().filter(|s| ["melee", "ranged"].contains(s)) {
                r.profile = s.into();
            }
        }
        if r.role == "infantry" {
            if let Some(s) = profile["chassis"]
                .as_str()
                .filter(|s| ["hp", "power"].contains(s))
            {
                r.chassis_profile = s.into();
            }
            if let Some(s) = profile["weapon"]
                .as_str()
                .filter(|s| ["burst", "cooling"].contains(s))
            {
                r.weapon_profile = s.into();
            }
        }
        self.apply_performance(i, false);
    }
    pub(super) fn rulebook_before_step(&mut self, actions: &[Action]) {
        self.update_referee_locks();
        let now = self.time();
        let second = self.tick / 20;
        let whole = self.tick % 20 == 0;
        if whole {
            if second == 1 {
                for t in self.teams.values_mut() {
                    t.resources += 400.0;
                }
                self.event("income", json!({"coins":400,"reason":"initial"}));
            }
            for grant in self.rules["economy"]["periodic_schedule"]
                .as_array()
                .unwrap()
                .clone()
            {
                if second == grant["elapsed_s"].as_u64().unwrap() {
                    let coins = n(&grant, "coins");
                    for t in self.teams.values_mut() {
                        t.resources += coins;
                    }
                    self.event("income", json!({"coins":coins,"reason":"schedule"}));
                }
            }
            if [60, 120, 180, 240, 300, 360].contains(&second) {
                for r in &mut self.robots {
                    if r.role == "aerial" {
                        r.support_time += 20.0;
                    }
                }
            }
            if second == 90 {
                for r in self.runes.values_mut() {
                    r.small_chances += 1;
                }
            }
            if [180, 255, 330].contains(&second) {
                for r in self.runes.values_mut() {
                    r.large_chances += 1;
                }
            }
        }
        for t in self.teams.values_mut() {
            if t.tech_income > 0.0 && t.next_tech_income > 0.0 && now + 1e-8 >= t.next_tech_income {
                t.resources += t.tech_income;
                t.next_tech_income += 10.0;
            }
        }
        for i in 0..self.robots.len() {
            if self.robots[i].red_card {
                continue;
            }
            self.select_profile(i, &actions[i].rpc);
            if self.tick == 0 {
                self.robots[i].profile_locked = true;
            }
            let deliveries: Vec<u32> = self.robots[i]
                .pending_ammo
                .iter()
                .filter(|(due, _)| *due <= now + 1e-8)
                .map(|(_, amount)| *amount)
                .collect();
            for amount in deliveries {
                self.robots[i].ammo += amount;
                self.event(
                    "ammo_delivered",
                    json!({"robot":self.robots[i].id,"amount":amount}),
                );
            }
            self.robots[i]
                .pending_ammo
                .retain(|(due, _)| *due > now + 1e-8);
            if self.robots[i]
                .pending_heal
                .map(|due| due <= now + 1e-8)
                .unwrap_or(false)
            {
                self.robots[i].pending_heal = None;
                if self.robots[i].alive {
                    let r = &mut self.robots[i];
                    r.hp = (r.hp + (r.max_hp * 0.6).round()).min(r.max_hp);
                    self.event("remote_heal", json!({"robot":self.robots[i].id}));
                }
            }
            if self.robots[i]
                .deploy_at
                .map(|due| due <= now + 1e-8)
                .unwrap_or(false)
            {
                self.robots[i].deploy_at = None;
                self.robots[i].deployed = true;
            }
            if self.tick % 2 == 0 {
                let r = &mut self.robots[i];
                let cooling =
                    (r.cooling_rate * r.cooling_multiplier).max(r.cooling_rate + r.cooling_bonus);
                r.heat = (r.heat - cooling * 0.1).max(0.0);
                if r.overheated && r.heat <= 1e-8 && !r.heat_permanent_lock {
                    r.overheated = false;
                }
                if r.role != "aerial" && r.alive {
                    let mut limit = r.power_limit;
                    if ["hero", "infantry"].contains(&r.role.as_str()) {
                        if r.energy <= 0.0 {
                            limit = 35.0;
                        } else if r.energy >= 25000.0 {
                            limit = (limit * 1.25).min(200.0);
                        }
                    }
                    if active_until(r.power_boost_until, now) {
                        limit = (limit * 2.0).min(200.0);
                    }
                    r.power_w = if active_until(r.power_cutoff_until, now) || r.deployed {
                        0.0
                    } else {
                        r.power_w
                    };
                    r.buffer_energy_j =
                        (r.buffer_energy_j - (r.power_w - limit) * 0.1).clamp(0.0, 60.0);
                    if r.buffer_energy_j <= 0.0 && r.power_w > limit {
                        r.power_cutoff_until = now + 5.0;
                        r.power_w = 0.0;
                    }
                    if ["hero", "infantry"].contains(&r.role.as_str()) {
                        r.energy = (r.energy - r.power_w * 0.1).max(0.0);
                    }
                }
            }
            let team = self.robots[i].team.clone();
            if self.robots[i].role == "aerial" {
                if self.robots[i].support_active {
                    let r = &mut self.robots[i];
                    if r.support_time > 0.0 {
                        r.support_time = (r.support_time - DT).max(0.0);
                    } else {
                        if r.support_paid_fraction <= 1e-8 {
                            if self.teams[&team].resources >= 1.0 {
                                self.teams.get_mut(&team).unwrap().resources -= 1.0;
                                r.support_paid_fraction = 1.0;
                            } else {
                                r.support_active = false;
                            }
                        }
                        r.support_paid_fraction = (r.support_paid_fraction - DT).max(0.0);
                    }
                }
                self.robots[i].alive = self.robots[i].support_active;
            } else if !self.robots[i].alive {
                let multiplier =
                    if self.raw_zone(i, "supply", Some(&team)) || self.bases[&team].hp < 2000.0 {
                        4.0
                    } else {
                        1.0
                    };
                self.robots[i].respawn_progress += multiplier * DT;
                if self.robots[i].respawn_progress + 1e-8 >= self.robots[i].respawn_required {
                    self.respawn(i, false);
                }
            } else if whole && self.raw_zone(i, "supply", Some(&team)) {
                let fraction = if now >= 240.0 && self.disengaged(i) {
                    0.25
                } else {
                    0.1
                };
                let r = &mut self.robots[i];
                r.hp = (r.hp + (r.max_hp * fraction).round()).min(r.max_hp);
            }
            if self.tick % 2 == 0
                && self.robots[i].alive
                && ["hero", "infantry"].contains(&self.robots[i].role.as_str())
                && self.raw_zone(i, "supply", Some(&team))
            {
                let input = self.robots[i].charging_power_w.min(120.0);
                let r = &mut self.robots[i];
                let net = (input - r.power_w).max(0.0);
                r.energy = (r.energy + 8.0 * net * 0.1).min(40000.0);
            }
            if self.tick % 2 == 0 {
                self.rulebook_rpc(i, &actions[i].rpc);
            }
        }
        self.update_rune_motor();
    }
    fn respawn(&mut self, i: usize, instant: bool) {
        let now = self.time();
        let cost=if instant {(now/60.0).ceil()*80.0+self.robots[i].level as f64*20.0}else{0.0};
        let r = &mut self.robots[i];
        r.alive = true;
        r.hp = if instant {
            r.max_hp
        } else {
            (r.max_hp * 0.1).round()
        };
        r.weak = !instant;
        r.invulnerable_until = now + if instant { 3.0 } else { 30.0 };
        r.invulnerable = true;
        r.shield = if instant { 3.0 } else { 30.0 };
        r.respawn_progress = 0.0;
        r.heat = 0.0;
        r.overheated = r.heat_permanent_lock;
        r.zone_contacts.clear();
        if instant {
            r.instant_revives += 1;
            r.power_boost_until = now + 4.0;
        }
        self.event(
            "respawn",
            json!({"robot":self.robots[i].id,"team":self.robots[i].team,"instant":instant,"weak":!instant,
                "cost":cost,"hp":self.robots[i].hp,"max_hp":self.robots[i].max_hp}),
        );
    }
    fn rpc_error(&mut self, i: usize, message: &str) {
        self.event(
            "invalid_action",
            json!({"robot":self.robots[i].id,"reason":message}),
        );
    }
    fn rulebook_rpc(&mut self, i: usize, rpc: &Value) {
        let now = self.time();
        let r = self.robots[i].clone();
        if r.red_card {
            return;
        }
        let team = r.team.clone();
        if let Some(active) = rpc.get("support").and_then(Value::as_bool) {
            if r.role == "aerial" {
                self.robots[i].support_active = active;
                self.robots[i].alive = active;
            } else {
                self.rpc_error(i, "support is available only to aerial robot 6");
            }
        }
        if rpc.get("revive").and_then(Value::as_bool) == Some(true) {
            if r.role != "aerial" && !r.alive {
                let cost = (now / 60.0).ceil() * 80.0 + r.level as f64 * 20.0;
                if self.teams[&team].resources >= cost {
                    self.teams.get_mut(&team).unwrap().resources -= cost;
                    self.respawn(i, true);
                }
            }
        }
        if rpc.get("remote_heal").and_then(Value::as_bool) == Some(true) && r.pending_heal.is_none()
        {
            if ["hero", "infantry"].contains(&r.role.as_str()) && self.disengaged(i) {
                let cost = 50.0 + (now / 60.0 * 20.0).ceil();
                if self.teams[&team].resources >= cost {
                    self.teams.get_mut(&team).unwrap().resources -= cost;
                    self.robots[i].pending_heal = Some(now + 6.0);
                }
            }
        }
        if let Some(order) = rpc.get("buy_ammo") {
            if ["hero", "infantry"].contains(&r.role.as_str()) && r.alive {
                let remote = order["remote"].as_bool().unwrap_or(false);
                let allowed = if remote {
                    self.disengaged(i)
                } else {
                    self.can_local_buy(i)
                };
                let kind = if r.role == "hero" { "42mm" } else { "17mm" };
                let v = &self.rules["economy"]["ammo"][kind];
                let batch = n(
                    v,
                    if remote {
                        "remote_batch"
                    } else {
                        "local_batch"
                    },
                ) as u32;
                let amount = order["amount"].as_u64().unwrap_or(batch as u64).min(1000) as u32;
                let unit_cost = n(v, if remote { "remote_cost" } else { "local_cost" });
                let cap = n(v, "team_purchase_cap") as u32;
                let purchased = if r.role == "hero" {
                    self.teams[&team].purchased_42
                } else {
                    self.teams[&team].purchased_17
                };
                let cost = if batch > 0 {
                    amount as f64 / batch as f64 * unit_cost
                } else {
                    f64::INFINITY
                };
                let refusal = if !allowed {
                    Some(if remote { "远程补给需脱离交战 6 秒".to_string() } else { "需在本方基地或补给区内".to_string() })
                } else if amount == 0 || batch == 0 {
                    Some("弹药数量必须大于 0".to_string())
                } else if purchased + amount > cap {
                    Some(format!("本队 {} 弹购买量已达上限（{}/{}）", kind, purchased, cap))
                } else if self.teams[&team].resources < cost {
                    Some(format!("金币不足：需 {:.0}，现有 {:.0}", cost, self.teams[&team].resources))
                } else {
                    None
                };
                if let Some(reason) = refusal {
                    self.event("ammo_refused", json!({"robot":r.id,"reason":reason}));
                } else {
                    let t = self.teams.get_mut(&team).unwrap();
                    t.resources -= cost;
                    if r.role == "hero" {
                        t.purchased_42 += amount;
                    } else {
                        t.purchased_17 += amount;
                    }
                    if remote {
                        self.robots[i].pending_ammo.push((now + 6.0, amount));
                    } else {
                        self.robots[i].ammo += amount;
                    }
                    self.event(
                        "ammo_purchase",
                        json!({"robot":r.id,"amount":amount,"cost":cost,"remote":remote}),
                    );
                }
            } else {
                self.event("ammo_refused", json!({"robot":r.id,"reason":"该型号不能购买弹药"}));
            }
        }
        if let Some(deploy) = rpc.get("deploy").and_then(Value::as_bool) {
            if r.role == "hero" && r.profile == "ranged" && r.deploy_allowed {
                if !deploy {
                    self.robots[i].deployed = false;
                    self.robots[i].deploy_at = None;
                } else if !r.deployed && r.deploy_at.is_none() && self.own_deploy_area(i) {
                    self.robots[i].deploy_at = Some(now + 2.0);
                }
            }
            else if deploy && r.role=="hero" && !r.deploy_allowed {
                self.event("invalid_action",json!({"robot":r.id,"reason":"This model has no deployment mechanism"}));
            }
        }
        self.request_pickup(i, rpc);
        self.assembly_rpc(i, rpc);
        if rpc.get("rune_start").and_then(Value::as_bool) == Some(true)
            && r.role == "infantry"
            && r.alive
            && !r.weak
        {
            self.start_rune(&team);
        }
    }
    fn can_local_buy(&self, i: usize) -> bool {
        let r = &self.robots[i];
        !r.weak
            && (self.contacted_zone(i, "base", Some(&r.team))
                || self.contacted_zone(i, "supply", Some(&r.team))
                || self.outpost_buff_allowed(i))
    }
    fn own_deploy_area(&self, i: usize) -> bool {
        let r = &self.robots[i];
        if r.team == "red" {
            r.x < 14.0
        } else {
            r.x > 14.0
        }
    }
    fn outpost_buff_allowed(&self, i: usize) -> bool {
        let r = &self.robots[i];
        let own = self.outposts[&r.team].hp > 0.0;
        own && (self.contacted_zone(i, "outpost", Some(&r.team))
            || (self.time() < 300.0
                && self.outposts[opponent(&r.team)].hp <= 0.0
                && self.contacted_zone(i, "outpost", Some(opponent(&r.team)))))
    }
    #[cfg(test)]
    pub(super) fn blocked_for(&self, r: &Robot, x: f64, y: f64) -> bool {
        if r.role == "aerial" {
            return false;
        }
        if self.blocked(x, y, r.radius) {
            return true;
        }
        self.arena
            .forbidden
            .iter()
            .any(|z| forbidden_for(z, &r.team, r.fly_pass) && in_zone_rect(z, x, y, r.radius))
    }
    pub(super) fn update_rulebook_buffs(&mut self) {
        let now = self.time();
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            if r.role == "aerial" || !r.alive {
                self.robots[i].zone_contacts.clear();
                self.robots[i].last_raw_zones.clear();
                continue;
            }
            let raw: Vec<String> = self
                .arena
                .zones
                .iter()
                .filter(|z| {
                    if z.polygon.is_empty() {
                        dist(r.x, r.y, z.x, z.y) <= z.radius
                    } else {
                        point_in_polygon(r.x, r.y, &z.polygon)
                    }
                })
                .map(|z| z.id.clone())
                .collect();
            for zone in self.arena.zones.clone() {
                if raw.contains(&zone.id) {
                    self.robots[i].zone_contacts.insert(zone.id.clone(), now);
                    if !r.weak && !r.last_raw_zones.contains(&zone.id) {
                        if let (Some(group), Some(order)) = (&zone.group, zone.order) {
                            self.process_crossing(i, group, order, &zone.kind);
                        }
                    }
                }
            }
            self.robots[i].last_raw_zones = raw;
            self.robots[i]
                .zone_contacts
                .retain(|_, time| now - *time <= 2.0 + 1e-8);
            if r.weak
                && (self.contacted_zone(i, "base", Some(&r.team))
                    || self.contacted_zone(i, "supply", Some(&r.team))
                    || self.outpost_buff_allowed(i))
            {
                self.robots[i].weak = false;
                self.robots[i].invulnerable_until = 0.0;
            }
        }
        // Central areas are exclusive; all other points use their stated ownership.
        for z in self.arena.zones.clone() {
            let candidates: Vec<usize> = self
                .robots
                .iter()
                .enumerate()
                .filter(|(_, r)| {
                    r.role != "aerial" && r.alive && !r.weak && r.zone_contacts.contains_key(&z.id)
                })
                .map(|(i, _)| i)
                .collect();
            let mut occupants: Vec<String> = Vec::new();
            let mut owner = z.owner.clone();
            if z.kind == "central" {
                let old = self.zones[&z.id].owner.clone();
                owner = old.filter(|t| candidates.iter().any(|i| self.robots[*i].team == *t));
                if owner.is_none() {
                    owner = candidates
                        .iter()
                        .filter(|i| self.robots[**i].role != "engineer")
                        .min_by_key(|i| self.robots[**i].id.clone())
                        .map(|i| self.robots[*i].team.clone());
                }
                for i in &candidates {
                    if Some(&self.robots[*i].team) == owner.as_ref()
                        && self.robots[*i].role != "engineer"
                    {
                        occupants.push(self.robots[*i].id.clone());
                    }
                }
            } else {
                for i in &candidates {
                    let r = &self.robots[*i];
                    let permitted = match z.kind.as_str() {
                        "supply" | "base" | "trapezoid" => z.owner.as_deref() == Some(&r.team),
                        "assembly" | "resource" => {
                            r.role == "engineer" && z.owner.as_deref() == Some(&r.team)
                        }
                        "outpost" => self.outpost_buff_allowed(*i),
                        "fortress" => {
                            r.role == "infantry"
                                && z.owner
                                    .as_ref()
                                    .map(|o| self.outposts[o].ever_destroyed)
                                    .unwrap_or(false)
                                && (z.owner.as_deref() == Some(&r.team)
                                    || now >= 180.0
                                        && self.outposts[z.owner.as_ref().unwrap()].hp <= 0.0)
                        }
                        _ => true,
                    };
                    if permitted {
                        occupants.push(r.id.clone());
                    }
                }
            }
            if z.kind == "fortress" {
                if let Some(own) = &z.owner {
                    let mut own_occupants: Vec<String> = occupants
                        .iter()
                        .filter(|id| id.starts_with(own))
                        .cloned()
                        .collect();
                    own_occupants.sort();
                    let keep = own_occupants.first().cloned();
                    occupants.retain(|id| !id.starts_with(own) || Some(id) == keep.as_ref());
                }
            }
            let contested = candidates.iter().any(|i| self.robots[*i].team == "red")
                && candidates.iter().any(|i| self.robots[*i].team == "blue");
            let state = self.zones.get_mut(&z.id).unwrap();
            state.owner = owner;
            state.progress = if occupants.is_empty() { 0.0 } else { 1.0 };
            state.contested = contested;
            state.occupants = occupants;
        }
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            let team = &r.team;
            // Expiry must precede a new contact: re-entering on the first frame
            // beyond the 3 s pause must not revive the old occupation timer.
            if now - self.robots[i].fortress_last_contact > 3.0 + 1e-8 {
                self.robots[i].fortress_hold = 0.0;
            }
            let rune = &self.runes[team];
            let mut defense = self.teams[team].tech_defense;
            let mut attack = 1.0_f64;
            let mut cooling = 1.0_f64;
            let mut extra = 0.0_f64;
            let mut vuln = 0.0_f64;
            if rune.status == "active" {
                defense = defense.max(rune.defense);
                attack = attack.max(rune.attack);
                cooling = cooling.max(rune.cooling);
            }
            if r.role == "engineer" && now < 180.0 {
                defense = defense.max(0.5);
            }
            if r.deployed {
                defense = defense.max(0.25);
            }
            if active_until(r.crossing_defense_until, now) {
                defense = defense.max(r.crossing_defense);
            }
            if active_until(r.crossing_cooling_until, now) {
                cooling = cooling.max(2.0);
            }
            if r.alive && !r.weak && r.role != "aerial" {
                for zone in self.arena.zones.clone() {
                    if !self.zones[&zone.id].occupants.contains(&r.id) {
                        continue;
                    }
                    match zone.kind.as_str() {
                        "base" | "trapezoid" => defense = defense.max(0.5),
                        "central" | "outpost" => defense = defense.max(0.25),
                        "resource" => {}
                        "assembly" => {
                            if r.assembly_invulnerable_spent < 180.0 {
                                self.robots[i].assembly_invulnerable_spent =
                                    (r.assembly_invulnerable_spent + DT).min(180.0);
                            }
                        }
                        "fortress" => {
                            if zone.owner.as_deref() == Some(team) {
                                defense = defense.max(0.5);
                                let delta =
                                    (self.bases[team].max_hp - self.bases[team].hp).max(0.0);
                                extra = extra.max((delta / 40.0).floor().min(75.0));
                                let cap = (100.0 + 2.0 * (delta / 15.0).floor()).min(500.0) as u32;
                                let t = self.teams.get_mut(team).unwrap();
                                if cap > t.fortress_capacity {
                                    t.fortress_ammo += cap - t.fortress_capacity;
                                }
                                t.fortress_capacity = cap;
                                t.fortress_ammo = t.fortress_ammo.min(cap);
                            } else {
                                vuln = vuln.max(1.0);
                                let elapsed = (now - self.robots[i].fortress_last_contact).max(0.0);
                                if elapsed <= DT + 1e-8 {
                                    self.robots[i].fortress_hold += elapsed;
                                }
                                self.robots[i].fortress_last_contact = now;
                                if self.robots[i].fortress_hold >= 20.0 - 1e-8
                                    && !self.bases[zone.owner.as_ref().unwrap()].armor_open
                                {
                                    self.bases
                                        .get_mut(zone.owner.as_ref().unwrap())
                                        .unwrap()
                                        .armor_open = true;
                                    self.event("base_armor_opened",json!({"team":zone.owner,"robot":r.id,"reason":"enemy_fortress"}));
                                }
                            }
                        }
                        _ => {}
                    }
                }
            }
            let inv = self.robot_invulnerable(i);
            let actor = &mut self.robots[i];
            actor.attack_multiplier = attack;
            actor.defense_buff = defense;
            actor.vulnerability = vuln;
            actor.cooling_multiplier = cooling;
            actor.cooling_bonus = extra;
            actor.invulnerable = inv;
            actor.shield = if inv {
                (r.invulnerable_until - now).max(0.05)
            } else {
                0.0
            };
        }
    }
    fn process_crossing(&mut self, i: usize, group: &str, order: u32, kind: &str) {
        let now = self.time();
        let v = &self.rules["buffs"]["crossings"][kind];
        let reverse_ok = ["fly", "tunnel"].contains(&kind);
        let order_key = format!("{group}:order");
        let previous = self.robots[i].crossings.get(group).copied();
        let previous_order = self.robots[i].crossings.get(&order_key).copied();
        let complete = previous
            .map(|t| now - t <= n(v, "window_s") + 1e-8)
            .unwrap_or(false)
            && previous_order.map(|o| o != order as f64).unwrap_or(false)
            && (reverse_ok || order == 1);
        if !complete {
            if reverse_ok || order == 0 {
                self.robots[i].crossings.insert(group.into(), now);
                self.robots[i].crossings.insert(order_key, order as f64);
            }
            return;
        }
        if kind == "road" && active_until(self.robots[i].road_cooldown_until, now) {
            return;
        }
        self.robots[i].crossings.remove(group);
        self.robots[i].crossings.remove(&order_key);
        let duration = n(v, "duration_s");
        if kind == "tunnel" {
            self.robots[i].crossing_cooling_until = now + duration;
        } else {
            let existing = active_until(self.robots[i].crossing_defense_until, now);
            let previous = (self.robots[i].crossing_defense_until - now).max(0.0);
            self.robots[i].crossing_defense = if existing { 0.5 } else { n(v, "defense") };
            self.robots[i].crossing_defense_until = now + duration.max(previous);
            if kind == "road" {
                self.robots[i].road_cooldown_until = now + 15.0;
            }
        }
        if !self.robots[i].crossing_firsts.contains(&kind.to_owned()) {
            self.robots[i].crossing_firsts.push(kind.into());
            self.gain_xp(i, 300.0);
        }
        self.event(
            "terrain_buff",
            json!({"robot":self.robots[i].id,"kind":kind}),
        );
    }
    pub(super) fn dispersed_shot_direction(&mut self, direction: [f64; 3], role: &str) -> [f64; 3] {
        let caliber = if role == "hero" { "42mm" } else { "17mm" };
        let half_angle = n(&self.sim["projectile_spread_half_angle_deg"], caliber).to_radians();
        if half_angle <= 0.0 {
            return direction;
        }
        // Uniform solid-angle cone about the actual (possibly tilted) muzzle axis.
        // Angular spread gives a footprint proportional to range, without changing
        // muzzle speed or adding artificial sideways forces during flight.
        let cosine = 1.0 - self.random() * (1.0 - half_angle.cos());
        let sine = (1.0 - cosine * cosine).max(0.0).sqrt();
        let azimuth = self.random() * std::f64::consts::TAU;
        let axis = rapier3d::na::Vector3::from(direction).normalize();
        let reference = if axis.z.abs() < 0.9 {
            rapier3d::na::Vector3::z()
        } else {
            rapier3d::na::Vector3::x()
        };
        let u = axis.cross(&reference).normalize();
        let v = axis.cross(&u);
        let shot = axis * cosine + (u * azimuth.cos() + v * azimuth.sin()) * sine;
        [shot.x, shot.y, shot.z]
    }
    pub(super) fn rulebook_fire(&mut self, actions: &[Action]) {
        let now = self.time();
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            let a = &actions[i];
            if !a.fire
                || !r.alive
                || r.red_card
                || r.overspeed_permanent_lock
                || active_until(r.overspeed_lock_until, now)
                || r.role == "engineer"
                || r.weak
                || r.overheated
                || r.heat_permanent_lock
                || r.cooldown > 1e-8
                || r.transform_allowed && r.transform_progress>1e-8 && r.transform_progress<1.0-1e-8
            {
                continue;
            }
            let reserve = r.role == "infantry"
                && self.contacted_zone(i, "fortress", Some(&r.team))
                && self.outposts[&r.team].ever_destroyed
                && self.zones[&format!("{}_fortress", r.team)]
                    .occupants
                    .contains(&r.id)
                && self.teams[&r.team].fortress_ammo > 0;
            if r.ammo == 0 && !reserve {
                continue;
            }
            if r.role == "aerial" {
                let pad = self.arena.pads[&r.team];
                if !r.support_active || dist(r.x, r.y, pad[0], pad[1]) < 0.6 && r.z < 1.5 {
                    continue;
                }
            }
            let spec = &self.specs[&r.role];
            let heat = spec.heat_per_shot;
            let speed = if r.role == "hero" && r.deployed {
                16.5
            } else {
                spec.projectile_speed
            };
            let damage = spec.damage;
            let cooldown = spec.cooldown;
            let caliber = if r.role == "hero" { "42mm" } else { "17mm" };
            let limit = n(
                &self.rules["projectiles"][caliber],
                if r.deployed {
                    "deployed_muzzle_limit_mps"
                } else {
                    "muzzle_limit_mps"
                },
            );
            let penalty = match self.overspeed_penalty(r.role == "hero", speed, limit) {
                Ok(penalty) => penalty,
                Err(_) => {
                    self.robots[i].launcher_fault = true;
                    if !self
                        .events
                        .iter()
                        .any(|e| e["type"] == "launcher_speed_rejected" && e["robot"] == r.id)
                    {
                        self.event("launcher_speed_rejected",json!({"robot":r.id,"team":r.team,"speed":speed,"reason":"unsupported hardware launch speed"}));
                    }
                    continue;
                }
            };
            if reserve {
                self.teams.get_mut(&r.team).unwrap().fortress_ammo -= 1;
            } else {
                self.robots[i].ammo -= 1;
            }
            self.robots[i].cooldown = cooldown;
            self.robots[i].heat += heat;
            self.robots[i].last_shot_time = now;
            let q2 = r.max_heat + if r.role == "hero" { 200.0 } else { 100.0 };
            if self.robots[i].heat >= q2 {
                self.robots[i].heat_permanent_lock = true;
                self.robots[i].overheated = true;
            } else if self.robots[i].heat > r.max_heat {
                self.robots[i].overheated = true;
                self.event(
                    "overheat",
                    json!({"robot":r.id,"heat":self.robots[i].heat,"limit":r.max_heat}),
                );
            }
            self.gain_xp(i, if r.role == "hero" { 10.0 } else { 1.0 });
            let (origin, direction) = physics::muzzle(&r);
            let direction = self.dispersed_shot_direction(direction, &r.role);
            let angle = direction[1].atan2(direction[0]);
            let pitch = direction[2].atan2(direction[0].hypot(direction[1]));
            let z = origin[2];
            self.projectiles.push(Projectile {
                id: self.next_projectile,
                source: r.id.clone(),
                team: r.team.clone(),
                role: r.role.clone(),
                x: origin[0],
                y: origin[1],
                z,
                vx: direction[0] * speed + r.vx,
                vy: direction[1] * speed + r.vy,
                vz: direction[2] * speed + r.vz,
                remaining: 40.0,
                damage: damage * r.attack_multiplier,
                radius: if r.role == "hero" { 0.02125 } else { 0.0084 },
                deployed: r.deployed,
            });
            self.next_projectile += 1;
            if let Some(seconds) = penalty {
                self.record_overspeed(i, speed, seconds);
            }
            let mass = if r.role == "hero" { 0.0445 } else { 0.0032 };
            if let Some(world) = self.physics.as_mut() {
                world.recoil(
                    i,
                    rapier3d::na::Vector3::new(
                        -direction[0] * speed * mass,
                        -direction[1] * speed * mass,
                        -direction[2] * speed * mass,
                    ),
                    rapier3d::na::Point3::new(origin[0], origin[1], origin[2]),
                );
            }
            self.event("shot",json!({"robot":r.id,"team":r.team,"role":r.role,"x":origin[0],"y":origin[1],"z":z,"muzzle_origin":true,"angle":angle,"pitch":pitch}));
        }
    }
    fn hero_42_damage_enabled(&self, team: &str) -> bool {
        let hero = self
            .robots
            .iter()
            .find(|r| r.team == team && r.role == "hero")
            .unwrap();
        if self.time() - hero.last_shot_time >= 4.0 {
            return false;
        }
        if !hero.alive && self.time() - hero.death_time >= 3.0 {
            return false;
        }
        true
    }
    fn damage_record(&mut self, source: usize, amount: f64, xp_rate: f64) {
        let team = self.robots[source].team.clone();
        self.robots[source].damage += amount;
        let t = self.teams.get_mut(&team).unwrap();
        t.attack_damage += amount;
        t.score = t.attack_damage;
        self.gain_xp(source, amount * xp_rate);
    }
    pub(super) fn rulebook_robot_hit(
        &mut self,
        index: usize,
        p: &Projectile,
        _t: f64,
        x: f64,
        y: f64,
        z: f64,
    ) {
        let target = &self.robots[index];
        if !target.alive || target.role == "aerial" || self.robot_invulnerable(index) {
            return;
        }
        if p.role == "hero" && !self.hero_42_damage_enabled(&p.team) {
            return;
        }
        let face = physics::face_at_point(target, [x, y, z]);
        let normal = physics::armor_normal(target, face);
        // A spinning armor plate has a different velocity from the chassis centre.
        let velocity = self
            .physics
            .as_ref()
            .unwrap()
            .velocity_at_point(index, [x, y, z]);
        let normal_speed = -((p.vx - velocity.x) * normal.x
            + (p.vy - velocity.y) * normal.y
            + (p.vz - velocity.z) * normal.z);
        if normal_speed <= if p.role == "hero" { 10.0 } else { 12.0 } {
            return;
        }
        let interval = if p.role == "hero" { 0.2 } else { 0.05 };
        if self.time() - target.armor_hit_times[face] < interval - 1e-8 {
            return;
        }
        let amount = (p.damage * (1.0 - target.defense_buff + target.vulnerability))
            .round()
            .max(0.0)
            .min(target.hp);
        if amount <= 0.0 {
            return;
        }
        let source = self.robots.iter().position(|r| r.id == p.source).unwrap();
        let victim_level = target.level;
        let killer_level = self.robots[source].level;
        let now = self.time();
        let r = &mut self.robots[index];
        r.hp -= amount;
        r.last_damage_time = now;
        r.armor_hit_times[face] = now;
        self.damage_record(source, amount, 4.0);
        if p.deployed {
            self.gain_xp(source, 100.0);
        }
        self.event(
            "hit",
            json!({"robot":self.robots[index].id,"source":p.source,"team":p.team,"x":x,"y":y,"z":z,"damage":amount}),
        );
        if self.robots[index].hp <= 0.0 {
            let r = &mut self.robots[index];
            r.alive = false;
            r.death_time = now;
            r.heat = 0.0;
            r.buffer_energy_j = 0.0;
            r.vx = 0.0;
            r.vy = 0.0;
            r.zone_contacts.clear();
            r.pending_heal = None;
            r.deploy_at = None;
            r.deployed = false;
            r.respawn_progress = 0.0;
            r.respawn_required = (10.0 + now / 10.0 + 20.0 * r.instant_revives as f64).round();
            self.robots[source].kills += 1;
            self.teams.get_mut(&p.team).unwrap().kills += 1;
            let gap = victim_level.saturating_sub(killer_level);
            self.gain_xp(
                source,
                50.0 * victim_level as f64 * (1.0 + 0.2 * gap as f64),
            );
            self.event("destroy",json!({"robot":self.robots[index].id,"killer":p.source,"team":p.team,"x":x,"y":y,"z":z}));
        }
    }
    pub(super) fn rulebook_structure_hit(
        &mut self,
        team: &str,
        outpost: bool,
        p: &Projectile,
        _t: f64,
        x: f64,
        y: f64,
        z: f64,
    ) {
        if p.role == "hero" && !self.hero_42_damage_enabled(&p.team) {
            return;
        }
        if !outpost && self.outposts[team].hp > 0.0 {
            return;
        }
        let b = if outpost {
            &self.outposts[team]
        } else {
            &self.bases[team]
        };
        if b.hp <= 0.0 {
            return;
        }
        if !outpost && !b.armor_open && p.role != "hero" {
            return;
        }
        let plate = structures::plates(b, outpost).into_iter().find(|plate| {
            let (u, v, depth) = structures::coordinates(plate, [x,y,z]);
            depth.abs() < 0.003 && u.abs() <= plate.width / 2.0 && v.abs() <= plate.height / 2.0
        });
        let Some(plate) = plate else { return; };
        if plate.heavy_only && p.role != "hero" { return; }
        let normal_speed = -(p.vx * plate.normal[0] + p.vy * plate.normal[1] + p.vz * plate.normal[2]);
        if normal_speed <= if p.role == "hero" {10.0} else {12.0} { return; }
        let face = plate.face;
        let interval = if p.role == "hero" {0.2} else {0.05};
        if self.time() - b.armor_hit_times[face] < interval - 1e-8 { return; }
        let (u, v, _) = structures::coordinates(&plate, [x,y,z]);
        let weak = plate.weakspot && u.abs() <= 0.005 && v.abs() <= 0.005;
        let mut attack = p.damage / if p.role == "hero" { 200.0 } else { 20.0 };
        if weak {
            attack = attack.max(1.5);
        }
        if p.deployed && !outpost {
            attack = attack.max(1.5);
        }
        let rune = &self.runes[team];
        let defense = self.teams[team]
            .tech_defense
            .max(if rune.status == "active" {
                rune.defense
            } else {
                0.0
            });
        let damage = ((if p.role == "hero" { 200.0 } else { 20.0 }) * attack * (1.0 - defense))
            .round()
            .max(0.0);
        let source = self.robots.iter().position(|r| r.id == p.source).unwrap();
        let now = self.time();
        let b = if outpost {
            self.outposts.get_mut(team).unwrap()
        } else {
            self.bases.get_mut(team).unwrap()
        };
        let shield_taken = damage.min(b.shield_hp);
        b.shield_hp -= shield_taken;
        let hp_taken = (damage - shield_taken).min(b.hp);
        b.hp -= hp_taken;
        b.armor_hit_times[face] = now;
        let actual = shield_taken + hp_taken;
        if actual <= 0.0 {
            return;
        }
        if b.hp <= 0.0 {
            b.ever_destroyed = true;
        }
        if !outpost {
            if b.hp <= 2000.0 {
                b.armor_open = true;
            }
            let t = self.teams.get_mut(team).unwrap();
            let before = (t.base_loss_total / 1000.0).floor() as u32;
            t.base_loss_total += hp_taken;
            t.rebuild_credits += (t.base_loss_total / 1000.0).floor() as u32 - before;
        }
        self.damage_record(source, actual, if outpost { 2.0 } else { 0.5 });
        if p.deployed {
            self.gain_xp(source, 100.0);
        }
        self.event(if outpost{"outpost_hit"}else{"base_hit"},json!({"team":p.team,"robot":p.source,"target_team":team,"x":x,"y":y,"z":z,"damage":actual,"weakspot":weak}));
    }
    fn arm_design(&self) -> Value {
        self.robots
            .iter()
            .position(|r| r.arm_count > 0)
            .and_then(|i| self.physics.as_ref().map(|w| w.design(i)["arm"].clone()))
            .unwrap_or(Value::Null)
    }
    /// World position of the core entry centre E and its unit normal e (5.3.3).
    pub(super) fn core_entry(&self, team: &str) -> Option<([f64; 3], [f64; 3], [f64; 3])> {
        let frame = &self.arena.assembly_frames[team];
        let v = |key: &str, k: usize| frame[key][k].as_f64();
        let (ox, oy, xx, xy, yx, yy) = (
            v("origin", 0)?,
            v("origin", 1)?,
            v("x_axis", 0)?,
            v("x_axis", 1)?,
            v("y_axis", 0)?,
            v("y_axis", 1)?,
        );
        let [x, y, z, theta, phi, _] = self.assemblies[team].current_pose;
        let floor = oy.min(self.arena.height - oy) * self.arena.floor_slope_deg.to_radians().tan();
        let (theta, phi) = (theta.to_radians(), phi.to_radians());
        let (ex, ey, ez) = (phi.sin() * theta.cos(), phi.sin() * theta.sin(), phi.cos());
        let normal = [ex * xx + ey * yx, ex * xy + ey * yy, ez];
        // The unit slides parallel to the entry face, horizontally where possible.
        let mut slide = [-normal[1], normal[0], 0.0];
        let length = slide[0].hypot(slide[1]);
        if length < 1e-6 {
            slide = [yx, yy, 0.0];
        } else {
            slide = [slide[0] / length, slide[1] / length, 0.0];
        }
        Some((
            [
                ox + x * xx + y * yx,
                oy + x * xy + y * yy,
                floor + n(frame, "z0_m") + z,
            ],
            normal,
            slide,
        ))
    }
    /// Advance each core toward its commanded pose and derive the engineers' arm goals.
    pub(super) fn arm_goals(&mut self, actions: &[Action]) -> Vec<physics::ArmGoal> {
        let initial = [-0.2, 0.2, 0.8, 0.0, 90.0, 0.0];
        let now = self.time();
        let mut overloaded: Vec<&'static str> = Vec::new();
        for team in ["red", "blue"] {
            let active = ["moving", "inserting", "buffered"]
                .contains(&self.assemblies[team].stage.as_str());
            // Blocked: hold the pose (the "brief hover") and sound the alarm at most once per interval.
            if active && self.core_path_blocked(team) {
                let s = self.assemblies.get_mut(team).unwrap();
                if now + 1e-8 >= s.overload_at {
                    s.overload_at = now + n(&self.rules["assembly"], "overload_alarm_s");
                    overloaded.push(team);
                }
                continue;
            }
            let s = self.assemblies.get_mut(team).unwrap();
            let target = if ["moving", "inserting", "buffered"].contains(&s.stage.as_str()) {
                s.pose
            } else {
                initial
            };
            for k in 0..6 {
                let rate = if k < 3 { 0.25 } else { 90.0 };
                s.current_pose[k] = approach(s.current_pose[k], target[k], rate * DT);
            }
        }
        for team in overloaded {
            self.event("core_overload", json!({"team":team,"reason":"path_blocked"}));
        }
        let mut goals = vec![physics::ArmGoal::default(); self.robots.len()];
        for i in 0..self.robots.len() {
            let r = &self.robots[i];
            if r.arm_count == 0 {
                continue;
            }
            let rpc = &actions[i].rpc;
            let team = r.team.clone();
            let goal = &mut goals[i];
            goal.mode = "stow".into();
            let rescue = match rpc.get("rescue") {
                Some(Value::Number(slot)) => slot.as_u64().map(|s| format!("{team}-{s}")),
                Some(Value::String(id)) => Some(id.clone()),
                _ => None,
            };
            if let Some(id) = rescue {
                let target = self.robots.iter().position(|o| {
                    o.id == id && o.team == team && o.role != "aerial" && o.id != r.id
                });
                if let (Some(j), true) = (target, r.alive && !r.weak) {
                    goal.mode = "rescue".into();
                    goal.target = Some(j);
                }
            }
            // Direct arm control: joint targets, or a tool-tip pose in the world/robot frame.
            if let (true, Some(cmd)) = (
                goal.mode == "stow",
                rpc.get("arm").filter(|v| v.is_object()),
            ) {
                let vector = |key: &str, len: usize| {
                    cmd.get(key)
                        .and_then(Value::as_array)
                        .filter(|v| v.len() == len)
                        .and_then(|v| {
                            v.iter()
                                .map(|x| x.as_f64().filter(|x| x.is_finite()))
                                .collect::<Option<Vec<f64>>>()
                        })
                };
                if cmd.get("stow").and_then(Value::as_bool) == Some(true) {
                    goal.mode = "manual".into();
                    goal.joints = Some(physics::arm_stowed(&self.arm_design()));
                } else if let Some(q) = vector("joints", 7) {
                    goal.mode = "manual".into();
                    goal.joints = Some(std::array::from_fn(|k| q[k]));
                } else if let Some(tip) = vector("tip", 3) {
                    let local = cmd.get("frame").and_then(Value::as_str) == Some("robot");
                    let dir = vector("dir", 3).unwrap_or_else(|| {
                        if local {
                            vec![1.0, 0.0, 0.0]
                        } else {
                            vec![r.heading.cos(), r.heading.sin(), 0.0]
                        }
                    });
                    goal.mode = "manual".into();
                    goal.point = Some([tip[0], tip[1], tip[2]]);
                    goal.dir = Some([dir[0], dir[1], dir[2]]);
                    goal.local = local;
                    goal.roll = cmd
                        .get("roll")
                        .and_then(Value::as_f64)
                        .filter(|v| v.is_finite())
                        .unwrap_or(0.0);
                    goal.grip = cmd
                        .get("grip")
                        .and_then(Value::as_f64)
                        .filter(|v| v.is_finite())
                        .unwrap_or(0.0);
                }
            }
            let state = &self.assemblies[&team];
            let mut secondary_core = None;
            if self.season == 2027 {
                // 2027: the tool follows the selected core's step progress; a dual-arm heavy
                // robot works the borrowed level-4 core with its second arm.
                if goal.mode == "stow" && ["moving", "inserting"].contains(&state.stage.as_str()) && self.raw_zone(i, "assembly", Some(&team)) {
                    let choice = rpc.get("assembly_core").and_then(Value::as_u64).unwrap_or(0);
                    let core = if state.difficulty == 4 && r.arm_count < 2 { choice.min(1) as usize } else { 0 };
                    if let Some((point, dir, roll)) = self.core_goal_2027(&team, core) {
                        goal.mode = "assembly".into();
                        goal.point = Some(point);
                        goal.dir = Some(dir);
                        goal.roll = roll;
                    }
                    if state.difficulty == 4 && r.arm_count == 2 {
                        secondary_core = self.core_goal_2027(&team, 1);
                    }
                }
            } else if goal.mode == "stow"
                && ["moving", "inserting", "buffered"].contains(&state.stage.as_str())
                && self.raw_zone(i, "assembly", Some(&team))
            {
                if let Some((entry, normal, slide)) = self.core_entry(&team) {
                    // Hold the unit at the mouth while the core moves, then push it in and slide it.
                    let depth = if state.stage == "moving"
                        || state.stage == "buffered" && self.time() < state.ready_at
                    {
                        -0.06
                    } else {
                        state.inserted
                    };
                    goal.mode = "assembly".into();
                    goal.point = Some(std::array::from_fn(|k| {
                        entry[k] - normal[k] * depth + slide[k] * state.translated
                    }));
                    goal.dir = Some(normal.map(|v| -v));
                    goal.roll = (state.current_pose[5] + state.rotation).to_radians();
                }
            }
            let mut secondary=physics::ArmGoal::default();secondary.mode="stow".into();
            if let Some((point,dir,roll))=secondary_core {secondary.mode="assembly".into();secondary.point=Some(point);secondary.dir=Some(dir);secondary.roll=roll;}
            if r.arm_count==2 {if let Some(cmd)=rpc.get("arm2").filter(|v|v.is_object()) {
                if let Some(g)=physics::manual_arm_goal(cmd,r,&self.physics.as_ref().unwrap().design(i)["arm2"]) {secondary=g;}
            }}
            if let Some(pickup) = &r.unit_pickup {
                let selected=if pickup.arm==1 {&mut secondary} else {&mut *goal};
                if selected.mode == "stow" {
                    if pickup.stage == "carrying" {
                        // The third unit rides in the gripper: hold it out in front of the mast so
                        // it stays visible and out of the way until a core takes it. An assembly,
                        // rescue or manual command claimed above keeps priority.
                        let key = if pickup.arm == 1 { "arm2" } else { "arm" };
                        let y = self.physics.as_ref().unwrap().design(i)[key]["base_m"]
                            .get(1)
                            .and_then(Value::as_f64)
                            .unwrap_or(0.0);
                        selected.mode = "manual".into();
                        selected.point = Some([0.45, y, 0.72]);
                        selected.dir = Some([1.0, 0.0, 0.0]);
                        selected.local = true;
                        selected.roll = 0.0;
                        selected.grip = 0.0;
                    } else {
                        selected.mode = "pick".into();
                        if pickup.stage == "stowing" {
                            selected.joints = Some(physics::arm_stowed(&self.physics.as_ref().unwrap().design(i)[if pickup.arm==0 {"arm"}else{"arm2"}]));
                        } else {
                            selected.point = Some([pickup.point[0], pickup.point[1], pickup.point[2]
                                + if pickup.stage == "lifting" {0.18} else {0.0}]);
                            selected.dir = Some(pickup.direction);
                            selected.grip = if pickup.stage == "approaching" {1.0} else {0.0};
                        }
                    }
                }
            }
            if r.arm_count==2 {goal.secondary=Some(Box::new(secondary));}
            let target = goal.target.map(|j| self.robots[j].id.clone());
            self.robots[i].rescue_target = target;
        }
        goals
    }
    pub(super) fn assembly_difficulty(&self, difficulty: u32) -> &Value {
        &self.sim["assembly_difficulties"][difficulty.saturating_sub(1).min(3) as usize]
    }
    pub(super) fn assembly_motion_blocked(&self, i: usize) -> bool {
        let r = &self.robots[i];
        let profile = self.assembly_difficulty(self.assemblies[&r.team].difficulty);
        r.tipped || r.upright < n(profile, "min_upright")
            || (r.vx * r.vx + r.vy * r.vy + r.vz * r.vz).sqrt()
                > n(profile, "chassis_speed_mps")
            || r.angular_velocity.iter().map(|v| v * v).sum::<f64>().sqrt()
                > n(profile, "chassis_angular_speed_rad_s")
    }
    fn assembly_alignment_metrics(&self, i: usize) -> Option<(f64, f64, f64)> {
        let r = &self.robots[i];
        let s = &self.assemblies[&r.team];
        let (entry, normal, slide) = self.core_entry(&r.team)?;
        let expected: [f64; 3] = std::array::from_fn(|k| {
            entry[k] - normal[k] * s.inserted + slide[k] * s.translated
        });
        let error = r.arm_tip.iter().zip(expected).map(|(a,b)| (a-b).powi(2)).sum::<f64>().sqrt();
        let direction = physics::arm_direction(r);
        let cosine = direction.iter().zip(normal).map(|(a,b)| -a*b).sum::<f64>().clamp(-1.0,1.0);
        let roll_error = (r.arm_joints[5] - (s.current_pose[5] + s.rotation).to_radians()
            + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI;
        Some((error, cosine.acos().to_degrees(), roll_error.abs().to_degrees()))
    }
    // Progress commands alone cannot exchange a unit: the real tool must reach and face the core.
    fn assembly_alignment_error(&self, i: usize) -> Option<&'static str> {
        let r=&self.robots[i];
        let profile=self.assembly_difficulty(self.assemblies[&r.team].difficulty);
        let Some((position,direction,roll))=self.assembly_alignment_metrics(i) else {return Some("arm_out_of_reach");};
        if position>n(profile,"tip_tolerance_m") {return Some("arm_out_of_reach");}
        if direction > n(profile, "tool_tolerance_deg")
            || roll > n(profile, "tool_tolerance_deg") {
            return Some("arm_misaligned");
        }
        None
    }
    fn assembly_rpc(&mut self, i: usize, rpc: &Value) {
        let r = self.robots[i].clone();
        if r.role != "engineer" {
            return;
        }
        let team = r.team.clone();
        let now = self.time();
        if !r.alive || r.weak || !self.raw_zone(i, "assembly", Some(&team)) {
            self.assemblies.get_mut(&team).unwrap().confirm_progress_s = 0.0;
            return;
        }
        if let Some(difficulty) = rpc
            .get("assembly")
            .and_then(Value::as_u64)
            .filter(|d| (1..=4).contains(d))
        {
            let d = difficulty as u32;
            let state = &self.assemblies[&team];
            // A unit already pushed into the socket latches the grade: the operator may not switch
            // difficulty until that attempt has resolved.
            let core_free = state.inserted <= 1e-9;
            let eligible = core_free
                && (state.stage == "idle" || state.stage == "complete" || state.stage == "failed");
            let completed = &self.teams[&team].completed_difficulties;
            let time_ok = now >= (d - 1) as f64 * 60.0;
            let prev_ok = d == 1 || completed.contains(&(d - 1));
            // Repeat farming needs a reset cycle; a newly unlocked difficulty may start immediately.
            let reset_ok = !completed.contains(&d) || now + 1e-8 >= state.repeat_ready_at;
            let stock = r.carried_units >= if d == 4 { 2 } else { 1 };
            let unlocked = d != 4
                || (!completed.contains(&4) && now >= state.retry_at && !state.permanent_lock);
            if eligible
                && time_ok
                && prev_ok
                && reset_ok
                && stock
                && unlocked
                && self.raw_zone(i, "assembly", Some(&team))
            {
                let other = opponent(&team);
                let other_state = &self.assemblies[other];
                let other_active =
                    ["moving", "inserting", "buffered"].contains(&other_state.stage.as_str());
                if d == 4 && other_active && other_state.difficulty == 4 {
                    return;
                }
                let interrupted = d == 4 && other_active;
                let delay = if interrupted { 15.0 } else { 0.0 };
                let attempt = self.assemblies[&team]
                    .attempt_by_difficulty
                    .get(&d)
                    .copied()
                    .unwrap_or(0)
                    + 1;
                // Pose sequence is identical for both teams at the same difficulty/attempt.
                let mut rng =
                    (self.seed as u64) ^ (attempt as u64 * 0x9e3779b9) ^ (d as u64 * 0xbf58476d);
                let mut sample = |low: f64, high: f64| {
                    rng ^= rng << 13;
                    rng ^= rng >> 7;
                    rng ^= rng << 17;
                    low + (high - low) * (rng >> 11) as f64 / (1u64 << 53) as f64
                };
                let profile = self.assembly_difficulty(d);
                let ranges = &profile["pose_ranges"];
                let mut pose = [0.0; 6];
                for (index, key) in ["x", "y", "z", "theta", "phi", "alpha"].iter().enumerate() {
                    pose[index] = sample(ranges[key][0].as_f64().unwrap(), ranges[key][1].as_f64().unwrap());
                }
                let rotation = &profile["target_rotation_deg"];
                // The unit may turn 90° about Q either way, so the sampled magnitude takes a random
                // sign instead of always turning counter-clockwise.
                let allowed = n(&self.rules["assembly"], "allowed_rotation_deg");
                let magnitude = sample(rotation[0].as_f64().unwrap(), rotation[1].as_f64().unwrap())
                    .abs()
                    .min(allowed);
                let angle = if sample(0.0, 1.0) < 0.5 { -magnitude } else { magnitude };
                let state = self.assemblies.get_mut(&team).unwrap();
                state.difficulty = d;
                state.stage = "moving".into();
                state.ready_at = now + delay + n(&self.sim, "core_move_delay_s");
                state.started = state.ready_at;
                state.inserted = 0.0;
                state.translated = 0.0;
                state.rotation = 0.0;
                state.target_rotation = angle;
                state.inserted_units = 0;
                state.first_unit_at = 0.0;
                state.attempt = attempt;
                state.attempt_by_difficulty.insert(d, attempt);
                state.pose = pose;
                state.interrupted = interrupted;
                state.confirm_progress_s = 0.0;
                state.buffer_until = 0.0;
                state.fail_reason = None;
                if interrupted {
                    self.assemblies.get_mut(other).unwrap().buffer_until = now + 15.0;
                    self.assemblies.get_mut(other).unwrap().stage = "buffered".into();
                }
                self.event(
                    "assembly_start",
                    json!({"team":team,"difficulty":d,"pose":pose}),
                );
            }
        }
        let state = self.assemblies[&team].clone();
        // Rule 89: the operator may pull the unit back out of the socket; the core senses the unit
        // leaving and the attempt fails at once.
        if rpc.get("assembly_extract").and_then(Value::as_bool) == Some(true) {
            if ["inserting", "buffered"].contains(&state.stage.as_str())
                && state.inserted > 1e-9
                && self.assembly_alignment_error(i).is_none()
            {
                self.fail_assembly(&team, "unit_extracted");
            }
            return;
        }
        if !["inserting", "buffered"].contains(&state.stage.as_str())
            || state.stage == "buffered" && (now < state.ready_at || now >= state.buffer_until)
            || !self.raw_zone(i, "assembly", Some(&team))
        {
            return;
        }
        if self.assembly_motion_blocked(i) {
            self.assemblies.get_mut(&team).unwrap().confirm_progress_s = 0.0;
            return;
        }
        let depth = n(&self.rules["assembly"], "insert_depth_m");
        let translation = n(&self.rules["assembly"], "translation_m");
        let profile = self.assembly_difficulty(state.difficulty).clone();
        let tolerance = n(&profile, "rotation_tolerance_deg");
        // RPCs settle at 10 Hz. A held command moves one actuator phase per
        // settlement; a combined action cannot insert, slide and confirm at once.
        if state.inserted < depth - 1e-8 {
            if rpc.get("assembly_insert").and_then(Value::as_bool) == Some(true) {
                let s = self.assemblies.get_mut(&team).unwrap();
                s.inserted = (s.inserted + n(&self.sim, "arm_insert_speed_mps") * ASSEMBLY_SETTLE_S)
                    .min(depth);
            }
            return;
        }
        if state.difficulty >= 2 && state.translated < translation - 1e-8 {
            if rpc.get("assembly_translate").and_then(Value::as_bool) == Some(true) {
                let s = self.assemblies.get_mut(&team).unwrap();
                s.translated =
                    (s.translated + n(&self.sim, "arm_translate_speed_mps") * ASSEMBLY_SETTLE_S)
                        .min(translation);
            }
            return;
        }
        if state.difficulty >= 3
            && (state.rotation - state.target_rotation).abs() > tolerance + 1e-8
        {
            if let Some(angle) = rpc
                .get("assembly_rotate")
                .and_then(Value::as_f64)
                .filter(|a| a.is_finite())
            {
                let s = self.assemblies.get_mut(&team).unwrap();
                s.rotation = approach(
                    s.rotation,
                    angle.clamp(
                        -n(&self.rules["assembly"], "allowed_rotation_deg"),
                        n(&self.rules["assembly"], "allowed_rotation_deg"),
                    ),
                    n(&self.sim, "arm_rotate_speed_deg_s") * ASSEMBLY_SETTLE_S,
                );
            }
            return;
        }
        let alignment_error = self.assembly_alignment_error(i);
        let s = self.assemblies.get_mut(&team).unwrap();
        if rpc.get("assembly_confirm").and_then(Value::as_bool) != Some(true)
            || r.carried_units == 0
            || alignment_error.is_some()
        {
            s.confirm_progress_s = 0.0;
            return;
        }
        s.confirm_progress_s += ASSEMBLY_SETTLE_S;
        if s.confirm_progress_s < n(&profile, "confirm_hold_s") - 1e-8 {
            return;
        }
        self.robots[i].carried_units -= 1;
        if state.difficulty == 4 && state.inserted_units == 0 {
            let s = self.assemblies.get_mut(&team).unwrap();
            s.inserted_units = 1;
            s.first_unit_at = now;
            s.inserted = 0.0;
            s.translated = 0.0;
            s.rotation = 0.0;
            s.confirm_progress_s = 0.0;
            self.event("assembly_first_core", json!({"team":team}));
            return;
        }
        if state.difficulty == 4 && now - state.first_unit_at > self.secondary_core_window_s(state.target_rotation) + 1e-8 {
            self.fail_assembly(&team, "secondary_window");
            return;
        }
        self.complete_assembly(&team, state.difficulty);
    }
    fn complete_assembly(&mut self, team: &str, difficulty: u32) {
        let now = self.time();
        let first = !self.teams[team]
            .completed_difficulties
            .contains(&difficulty);
        let income = if first {
            self.rules["assembly"]["first_income_per_period"][difficulty as usize - 1]
                .as_f64()
                .unwrap()
        } else {
            self.rules["assembly"]["repeat_income_per_period"][difficulty as usize - 1]
                .as_f64()
                .unwrap()
        };
        let t = self.teams.get_mut(team).unwrap();
        t.tech_income += income;
        if t.next_tech_income <= 0.0 {
            t.next_tech_income = now + 10.0;
        }
        if first {
            t.completed_difficulties.push(difficulty);
            if difficulty == 2 {
                t.level_cap = 7;
            }
            if difficulty >= 3 {
                t.level_cap = 10;
                t.tech_defense = if difficulty == 4 { 0.5 } else { 0.25 };
            }
        }
        if difficulty == 4 && first {
            let b = self.bases.get_mut(team).unwrap();
            let room = (b.max_hp - b.hp).max(0.0);
            let heal = 2000.0_f64.min(room);
            b.hp += heal;
            b.shield_hp += 2000.0 - heal;
        }
        let s = self.assemblies.get_mut(team).unwrap();
        s.stage = "complete".into();
        s.repeat_ready_at = now + n(&self.sim, "assembly_repeat_reset_s");
        if difficulty == 4 {
            let other = opponent(team);
            if self.assemblies[other].stage == "buffered" {
                self.assemblies.get_mut(other).unwrap().stage = "inserting".into();
            }
        }
        self.event("assembly_success",json!({"team":team,"difficulty":difficulty,"first":first,"income_per_10s":self.teams[team].tech_income}));
    }
    /// The level-4 second core has to finish its whole actuator pass inside the window: push the unit
    /// in, slide it, rotate the tool to the pose angle and hold the confirmation. Those phases are paced
    /// by the rulebook's own actuator speeds, which already add up to the configured 3 s, so the
    /// configured value only works as a floor — with a fixed 3 s every pose above a few degrees of tool
    /// rotation would be physically impossible, and the arm still needs a settlement per phase change.
    fn secondary_core_window_s(&self, target_rotation: f64) -> f64 {
        let rules = &self.rules["assembly"];
        let insert = n(rules, "insert_depth_m") / n(&self.sim, "arm_insert_speed_mps");
        let translate = n(rules, "translation_m") / n(&self.sim, "arm_translate_speed_mps");
        let rotate = target_rotation.abs().clamp(0.0, 90.0) / n(&self.sim, "arm_rotate_speed_deg_s");
        let confirm = n(&self.assembly_difficulty(4), "confirm_hold_s");
        let budget = insert + translate + rotate + confirm + 4.0 * ASSEMBLY_SETTLE_S;
        n(rules, "difficulty_4_secondary_insert_window_s").max(budget)
    }
    /// The core sweeps its entry through the plinth without any obstacle sensing, so a robot standing
    /// on that path stalls it: the move holds and an overload alarm repeats until the way clears. The
    /// heavy robot working the core is exempt — it is the one holding the unit, not an obstacle.
    fn core_path_blocked(&self, team: &str) -> bool {
        let Some((entry, _, _)) = self.core_entry(team) else {
            return false;
        };
        let clearance = n(&self.rules["assembly"], "overload_clearance_m");
        self.robots.iter().any(|r| {
            r.alive && !r.red_card && r.role != "heavy"
                && (r.x - entry[0]).hypot(r.y - entry[1]) < clearance
        })
    }
    /// A failed attempt is not the end of the unit: the core holds it until it salvages it back into
    /// the team's stock, which is also when the difficulty unlocks again.
    pub(super) fn salvage_core_units(&mut self, team: &str) {
        let now = self.time();
        let units = self.assemblies[team].core_units;
        if units == 0 || !self.assemblies[team].recover_at.is_some_and(|at| now + 1e-8 >= at) {
            return;
        }
        let s = self.assemblies.get_mut(team).unwrap();
        s.core_units = 0;
        s.recover_at = None;
        if let Some(left) = self.teams.get_mut(team).unwrap().energy_units_left.as_mut() {
            *left += units;
        }
        self.event("assembly_units_recovered", json!({"team":team,"units":units}));
    }
    fn fail_assembly(&mut self, team: &str, reason: &str) {
        let now = self.time();
        let retry_lock = n(&self.rules["assembly"], "difficulty_4_retry_lock_s");
        let s = self.assemblies.get_mut(team).unwrap();
        s.stage = "failed".into();
        s.fail_reason = Some(reason.into());
        if s.difficulty == 4 { s.retry_at = now + retry_lock; }
        if s.interrupted {
            s.permanent_lock = true;
            self.teams.get_mut(team).unwrap().tech_income -= 25.0;
        }
        let other = opponent(team);
        if self.assemblies[other].stage == "buffered" {
            self.assemblies.get_mut(other).unwrap().stage = "inserting".into();
        }
        self.event("assembly_failed", json!({"team":team,"difficulty":self.assemblies[team].difficulty}));
    }
    pub(super) fn start_rune(&mut self, team: &str) {
        let now = self.time();
        let mode = if now < 180.0 { "small" } else { "large" };
        let s = &self.runes[team];
        if s.status == "active" || s.status == "activating" {
            return;
        }
        let chances = if mode == "small" {
            s.small_chances
        } else {
            s.large_chances
        };
        if chances == 0 {
            return;
        }
        let first = (self.random() * 5.0) as usize % 5;
        let second = (first + 1 + (self.random() * 4.0) as usize) % 5;
        let s = self.runes.get_mut(team).unwrap();
        s.mode = mode.into();
        s.status = "activating".into();
        s.started = now;
        s.hit_deadline = now + 2.5;
        s.pair_deadline = 0.0;
        s.hit_count = 0;
        s.rounds = 0;
        s.activated_arms.clear();
        s.last_hit_at = [None; 5];
        s.last_hit_valid = [false; 5];
        s.ring_sum = 0.0;
        s.lit = if mode == "small" {
            vec![first]
        } else {
            vec![first, second]
        };
        if mode == "small" {
            s.small_chances -= 1;
        } else {
            s.large_chances -= 1;
        }
        self.event("rune_start", json!({"team":team,"mode":mode}));
    }
    pub(super) fn update_rune_motor(&mut self) {
        let now = self.time();
        // Integrate the smooth, nonuniform shaft speed over this frame. It keeps
        // running while idle and while buffs are active, on both faces together.
        let delta = self.rune_motor_integral(now, now + DT);
        self.rune_angle =
            (self.rune_angle + delta * self.rune_direction).rem_euclid(std::f64::consts::TAU);
        for team in ["red", "blue"] {
            let state = self.runes[team].clone();
            if state.status == "active" && now >= state.active_until {
                let r = self.runes.get_mut(team).unwrap();
                r.status = "idle".into();
                r.activated_arms.clear();
                r.lit.clear();
                r.last_hit_at = [None; 5];
                r.last_hit_valid = [false; 5];
                r.attack = 1.0;
                r.defense = 0.0;
                r.cooling = 1.0;
            }
            if state.status == "activating" {
                if now - state.started >= 20.0 {
                    let r = self.runes.get_mut(team).unwrap();
                    r.status = "idle".into();
                    r.activated_arms.clear();
                    r.lit.clear();
                } else if state.mode == "large"
                    && state.pair_deadline > 0.0
                    && now >= state.pair_deadline
                {
                    self.next_rune_pair(team);
                } else if state.pair_deadline == 0.0 && now >= state.hit_deadline {
                    self.reset_rune_pattern(team);
                }
            }
        }
    }
    fn rune_motor_rates(&self, now: f64) -> (f64, f64) {
        if self.season == 2027 {
            return self.rune_motor_rates_2027(now);
        }
        let motor = &self.sim["rune_motor"];
        let weight = n(motor, "secondary_weight");
        let secondary = self.rune_omega * n(motor, "secondary_frequency_ratio");
        let a = self.rune_omega * now + self.rune_phase[0];
        let b = secondary * now + self.rune_phase[1];
        let wave = (1.0 - weight) * a.sin() + weight * b.sin();
        let derivative = (1.0 - weight) * self.rune_omega * a.cos() + weight * secondary * b.cos();
        let small_mean = n(&self.rules["rune"], "small_speed_rad_s");
        let small_amp = n(motor, "small_amplitude_rad_s");
        let large_mean = 2.090 - self.rune_a;
        let transition = n(motor, "mode_transition_s").max(DT);
        let u = ((now - n(&self.rules["rune"], "small_until_s")) / transition).clamp(0.0, 1.0);
        let blend = u * u * (3.0 - 2.0 * u);
        let blend_rate = 6.0 * u * (1.0 - u) / transition;
        let mean = small_mean + (large_mean - small_mean) * blend;
        let amp = small_amp + (self.rune_a - small_amp) * blend;
        (
            mean + amp * wave,
            amp * derivative
                + blend_rate * (large_mean - small_mean + (self.rune_a - small_amp) * wave),
        )
    }
    fn rune_motor_integral(&self, start: f64, end: f64) -> f64 {
        let (a, _) = self.rune_motor_rates(start);
        let (b, _) = self.rune_motor_rates((start + end) * 0.5);
        let (c, _) = self.rune_motor_rates(end);
        (end - start) * (a + 4.0 * b + c) / 6.0
    }
    pub(super) fn assembly_snapshots(&self) -> Value {
        let mut result = serde_json::to_value(&self.assemblies).unwrap();
        if self.season == 2027 {
            for team in ["red", "blue"] {
                self.assembly_snapshot_2027(team, &mut result[team]);
            }
            return result;
        }
        for (team, s) in &self.assemblies {
            let insert =
                (s.inserted / n(&self.rules["assembly"], "insert_depth_m")).clamp(0.0, 1.0);
            let translate =
                (s.translated / n(&self.rules["assembly"], "translation_m")).clamp(0.0, 1.0);
            let profile = self.assembly_difficulty(s.difficulty);
            let aligned = (s.rotation - s.target_rotation).abs()
                <= n(profile, "rotation_tolerance_deg") + 1e-8;
            let rotate = if aligned {
                1.0
            } else {
                (1.0 - (s.rotation - s.target_rotation).abs() / s.target_rotation.max(1.0))
                    .clamp(0.0, 1.0)
            };
            let confirm =
                (s.confirm_progress_s / n(profile, "confirm_hold_s")).clamp(0.0, 1.0);
            let (operation, phase_progress) = match s.stage.as_str() {
                "moving" | "buffered" if self.time() < s.ready_at => (
                    "positioning",
                    (1.0 - (s.ready_at - self.time()) / n(&self.sim, "core_move_delay_s"))
                        .clamp(0.0, 1.0),
                ),
                "inserting" | "buffered" if insert < 1.0 - 1e-8 => ("inserting", insert),
                "inserting" | "buffered" if s.difficulty >= 2 && translate < 1.0 - 1e-8 => {
                    ("translating", translate)
                }
                "inserting" | "buffered" if s.difficulty >= 3 && !aligned => ("rotating", rotate),
                "inserting" | "buffered" => ("verifying", confirm),
                "complete" => ("complete", 1.0),
                "failed" => ("failed", 0.0),
                _ => ("idle", 0.0),
            };
            let phases = 2.0 + (s.difficulty >= 2) as u8 as f64 + (s.difficulty >= 3) as u8 as f64;
            let units = if s.difficulty == 4 { 2.0 } else { 1.0 };
            let progress = if s.stage == "complete" {
                1.0
            } else if ["inserting", "buffered"].contains(&s.stage.as_str()) {
                (s.inserted_units as f64
                    + (insert
                        + confirm
                        + if s.difficulty >= 2 { translate } else { 0.0 }
                        + if s.difficulty >= 3 { rotate } else { 0.0 })
                        / phases)
                    / units
            } else {
                0.0
            };
            result[team]["operation"] = json!(operation);
            result[team]["operation_progress"] = json!(phase_progress);
            result[team]["progress"] = json!(progress.clamp(0.0, 1.0));
            let engineer = self.robots.iter().enumerate().find(|(i, r)| {
                r.team == *team && r.role == "engineer" && r.alive && !r.weak
                    && self.raw_zone(*i, "assembly", Some(team))
            });
            let blocked = if ["inserting", "buffered"].contains(&s.stage.as_str()) {
                engineer.map(|(i, _)| if self.assembly_motion_blocked(i) {
                    Some("chassis_unstable")
                } else if operation == "verifying" {
                    self.assembly_alignment_error(i)
                } else { None }).unwrap_or(Some("engineer_absent"))
            } else { None };
            result[team]["blocked_reason"] = json!(blocked);
            result[team]["repeat_cooldown_s"] = json!((s.repeat_ready_at - self.time()).max(0.0));
            result[team]["core_units"] = json!(s.core_units);
            result[team]["recover_in_s"] = json!(s.recover_at.map(|at| (at - self.time()).max(0.0)));
            result[team]["difficulty_locked"] = json!(s.inserted > 1e-9);
            result[team]["requirements"] = profile.clone();
            result[team]["alignment"] = engineer.and_then(|(i,_)| self.assembly_alignment_metrics(i))
                .map(|(position,direction,roll)|json!({"position_error_m":position,"direction_error_deg":direction,"roll_error_deg":roll}))
                .unwrap_or(Value::Null);
            result[team]["attempt_remaining_s"] = if ["moving", "inserting", "buffered"].contains(&s.stage.as_str()) {
                json!((s.started + n(profile, "attempt_timeout_s") - self.time()).max(0.0))
            } else { Value::Null };
        }
        result
    }
    fn outpost_rebuild_required(&self, role: &str) -> f64 {
        n(
            &self.rules["structures"],
            if role == "engineer" {
                "rebuild_hold_engineer_s"
            } else {
                "rebuild_hold_hero_infantry_s"
            },
        )
    }
    pub(super) fn base_snapshots(&self) -> Value {
        let mut result = json!(self.bases);
        for team in ["red", "blue"] {
            result[team]["armor_plates"] = json!(structures::plates(&self.bases[team], false));
            if self.season == 2027 {
                // RMUC 2027: every exposed base module accepts 17 mm, 42 mm and aerial impacts.
                for plate in result[team]["armor_plates"].as_array_mut().unwrap() {
                    plate["heavy_only"] = json!(false);
                    plate["weakspot"] = json!(false);
                }
            }
        }
        result
    }
    pub(super) fn outpost_snapshots(&self) -> Value {
        let mut result = serde_json::to_value(&self.outposts).unwrap();
        for team in ["red", "blue"] {
            if self.season == 2027 {
                result[team]["armor_plates"] = json!(Self::structure_plates_2027(&self.outposts[team], true));
                for plate in result[team]["armor_plates"].as_array_mut().unwrap() {
                    plate["weakspot"] = json!(false);
                }
                result[team]["rebuild"] = Value::Null;
                continue;
            }
            result[team]["armor_plates"] = json!(structures::plates(&self.outposts[team], true));
            result[team]["rebuild"] = Value::Null;
            if self.outposts[team].hp > 0.0
                || self.teams[team].rebuild_credits == 0
                || self.time() >= n(&self.rules["structures"], "rebuild_disabled_at_s")
            {
                continue;
            }
            let mut best = 0.0;
            for (id, elapsed) in &self.assemblies[team].rebuild_hold {
                let Some((i, robot)) = self.robots.iter().enumerate().find(|(_, r)| &r.id == id)
                else {
                    continue;
                };
                if !robot.alive
                    || robot.weak
                    || robot.role == "aerial"
                    || !self.raw_zone(i, "outpost", Some(team))
                {
                    continue;
                }
                let required = self.outpost_rebuild_required(&robot.role);
                let progress = (*elapsed / required).clamp(0.0, 1.0);
                if progress > best {
                    best = progress;
                    result[team]["rebuild"] = json!({"robot":id,"elapsed":elapsed,"required":required,"progress":progress});
                }
            }
        }
        result
    }
    fn reset_rune_pattern(&mut self, team: &str) {
        let now = self.time();
        let mode = self.runes[team].mode.clone();
        let a = (self.random() * 5.0) as usize % 5;
        let b = (a + 1 + (self.random() * 4.0) as usize) % 5;
        let r = self.runes.get_mut(team).unwrap();
        r.hit_count = 0;
        r.rounds = 0;
        r.activated_arms.clear();
        r.ring_sum = 0.0;
        r.hit_deadline = now + 2.5;
        r.pair_deadline = 0.0;
        r.lit = if mode == "small" { vec![a] } else { vec![a, b] };
        self.event("rune_reset", json!({"team":team}));
    }
    fn next_rune_pair(&mut self, team: &str) {
        self.runes.get_mut(team).unwrap().rounds += 1;
        if self.runes[team].rounds >= 5 {
            self.activate_rune(team);
            return;
        }
        let now = self.time();
        let a = (self.random() * 5.0) as usize % 5;
        let b = (a + 1 + (self.random() * 4.0) as usize) % 5;
        let r = self.runes.get_mut(team).unwrap();
        r.lit = vec![a, b];
        r.hit_deadline = now + 2.5;
        r.pair_deadline = 0.0;
    }
    /// World positions of a team's five rune targets at the current shaft angle.
    pub(super) fn rune_target_points(&self, team: &str) -> [[f64; 3]; 5] {
        let center_x = n(&self.arena.rune, "x");
        let center_y = n(&self.arena.rune, "y");
        let center_z = n(&self.arena.rune, "z");
        let yaw = n(&self.arena.rune, "axis_yaw");
        let (nx, ny) = (yaw.cos(), yaw.sin());
        // Coaxial faces (5.5.2): red lamps face -axis, blue +axis, each face_offset from the hub.
        let offset = self.arena.rune["face_offset_m"].as_f64().unwrap_or(0.025);
        let radius = self.arena.rune["arm_radius_m"].as_f64().unwrap_or(0.7);
        let sign = if team == "red" { 1.0 } else { -1.0 };
        std::array::from_fn(|arm| {
            let angle = self.rune_angle + arm as f64 * std::f64::consts::TAU / 5.0;
            [
                center_x - sign * offset * nx - ny * radius * angle.cos(),
                center_y - sign * offset * ny + nx * radius * angle.cos(),
                center_z + radius * unfused_sin(angle),
            ]
        })
    }
    pub(super) fn rune_snapshots(&self) -> Value {
        let mut result = serde_json::Map::new();
        for team in ["red", "blue"] {
            let r = &self.runes[team];
            let points = self.rune_target_points(team);
            let targets:Vec<Value>=(0..5).map(|arm|{let [x,y,z]=points[arm];json!({"arm":arm,"x":x,"y":y,"z":z,"radius":0.15,"lit":r.lit.contains(&arm),"hit":(r.status=="activating"||r.status=="active")&&r.activated_arms.contains(&arm),"last_hit_at":r.last_hit_at[arm],"last_hit_valid":r.last_hit_valid[arm]})}).collect();
            let mut value = serde_json::to_value(r).unwrap();
            value["targets"] = json!(targets);
            value["angle"] = json!(self.rune_angle);
            let (speed, acceleration) = self.rune_motor_rates(self.time());
            value["angular_velocity"] = json!(speed * self.rune_direction);
            value["angular_acceleration"] = json!(acceleration * self.rune_direction);
            result.insert(team.into(), value);
        }
        Value::Object(result)
    }
    pub(super) fn observed_runes(&self, team: &str, observers: &[&Robot]) -> Value {
        let all = self.rune_snapshots();
        let mut own = all[team].clone();
        let visible = observers.iter().any(|r| {
            dist(r.x, r.y, n(&self.arena.rune, "x"), n(&self.arena.rune, "y"))
                <= self.effective_vision(r)
        });
        if !visible {
            own["targets"] = json!([]);
            own["angle"] = Value::Null;
            own["angular_velocity"] = Value::Null;
            own["angular_acceleration"] = Value::Null;
        }
        json!({team:own})
    }
    pub(super) fn observed_teams(&self, team: &str) -> Value {
        let mut all = serde_json::to_value(&self.teams).unwrap();
        let other = opponent(team);
        all[other] = json!({"attack_damage":self.teams[other].attack_damage,"score":self.teams[other].attack_damage});
        if self.season == 2027 {
            all[other]["control"] = json!(self.teams[other].control);
        }
        all
    }
    pub(super) fn rune_intersection(
        &self,
        p: &Projectile,
        dx: f64,
        dy: f64,
        dz: f64,
    ) -> Option<(f64, usize)> {
        let yaw = n(&self.arena.rune, "axis_yaw");
        let (nx, ny) = (yaw.cos(), yaw.sin());
        let normal_speed = dx * nx + dy * ny;
        if normal_speed.abs() < 1e-9 {
            return None;
        }
        let sign = if p.team == "red" { 1.0 } else { -1.0 };
        if normal_speed * sign <= 0.0 {
            return None;
        }
        let mut hits = Vec::new();
        for (arm, [x, y, z]) in self.rune_target_points(&p.team).into_iter().enumerate() {
            let t = ((x - p.x) * nx + (y - p.y) * ny) / normal_speed;
            if (0.0..=1.0).contains(&t) {
                let tangent = -(p.x + dx * t - x) * ny + (p.y + dy * t - y) * nx;
                let distance = tangent.hypot(p.z + dz * t - z);
                if distance <= 0.15 + p.radius {
                    hits.push((t, arm));
                }
            }
        }
        hits.into_iter().min_by(|a, b| a.0.total_cmp(&b.0))
    }
    pub(super) fn rulebook_rune_hit(&mut self, p: &Projectile, arm: usize, x: f64, y: f64, z: f64) {
        let now = self.time();
        let yaw = n(&self.arena.rune, "axis_yaw");
        let (nx, ny) = (yaw.cos(), yaw.sin());
        let valid = p.role != "hero"
            && (p.vx * nx + p.vy * ny).abs() > 12.0
            && self.runes[&p.team].status == "activating"
            && self.runes[&p.team].lit.contains(&arm);
        let feedback = self.runes.get_mut(&p.team).unwrap();
        feedback.last_hit_at[arm] = Some(now);
        feedback.last_hit_valid[arm] = valid;
        self.event(
            "rune_hit",
            json!({"team":p.team,"robot":p.source,"arm":arm,"valid":valid,"x":x,"y":y,"z":z}),
        );
        if p.role == "hero" {
            self.event(
                "invalid_action",
                json!({"robot":p.source,"reason":"42mm cannot hit the rune"}),
            );
            return;
        }
        if (p.vx * nx + p.vy * ny).abs() <= 12.0 {
            return;
        }
        let team = p.team.clone();
        let state = self.runes[&team].clone();
        if state.status != "activating" {
            return;
        }
        if !state.lit.contains(&arm) {
            self.reset_rune_pattern(&team);
            return;
        }
        let [tx, ty, tz] = self.rune_target_points(&team)[arm];
        let tangent = -(x - tx) * ny + (y - ty) * nx;
        let offset = tangent.hypot(z - tz);
        let ring = (10.0 - (offset / 0.015).floor()).clamp(1.0, 10.0);
        let r = self.runes.get_mut(&team).unwrap();
        r.hit_count += 1;
        r.activated_arms.push(arm);
        r.ring_sum += ring;
        r.lit.retain(|i| *i != arm);
        if state.mode == "small" {
            if r.hit_count >= 5 {
                self.activate_rune(&team);
            } else {
                let done = self.runes[&team].activated_arms.clone();
                let remaining: Vec<usize> = (0..5).filter(|i| !done.contains(i)).collect();
                let selected =
                    remaining[(self.random() * remaining.len() as f64) as usize % remaining.len()];
                let r = self.runes.get_mut(&team).unwrap();
                r.lit = vec![selected];
                r.hit_deadline = now + 2.5;
            }
        } else if state.pair_deadline == 0.0 {
            self.runes.get_mut(&team).unwrap().pair_deadline = now + 1.0;
        } else {
            self.next_rune_pair(&team);
        }
    }
    fn activate_rune(&mut self, team: &str) {
        let now = self.time();
        let state = self.runes[team].clone();
        let small = state.mode == "small";
        let mut attack = 1.0;
        let mut defense = 0.25;
        let mut cooling = 1.0;
        let duration;
        if small {
            duration = 45.0;
            self.teams.get_mut(team).unwrap().small_bonus_xp = 0.0;
        } else {
            let average = state.ring_sum / state.hit_count.max(1) as f64;
            for band in self.rules["rune"]["large_bands"].as_array().unwrap() {
                if average <= n(band, "max") {
                    attack = n(band, "attack");
                    defense = n(band, "defense");
                    cooling = n(band, "cooling");
                    break;
                }
            }
            let count = state.hit_count.clamp(5, 10);
            duration = self.rules["rune"]["duration_by_hits"][count.to_string()]
                .as_f64()
                .unwrap();
        }
        let r = self.runes.get_mut(team).unwrap();
        r.status = "active".into();
        r.active_until = now + duration;
        r.attack = attack;
        r.defense = defense;
        r.cooling = cooling;
        r.lit.clear();
        if !small {
            let recipients: Vec<usize> = self
                .robots
                .iter()
                .enumerate()
                .filter(|(_, r)| r.team == team && r.alive && if self.season == 2027 { r.role == "infantry" } else { r.role != "engineer" })
                .map(|(i, _)| i)
                .collect();
            let amount = 750.0 / recipients.len().max(1) as f64;
            for i in recipients {
                self.gain_xp(i, amount);
            }
        }
        self.event("rune_activated",json!({"team":team,"mode":state.mode,"duration":duration,"attack":attack,"defense":defense,"cooling":cooling}));
    }
    pub(super) fn rulebook_after_step(&mut self) {
        self.update_referee_locks();
        let now = self.time();
        for team in ["red", "blue"] {
            self.salvage_core_units(team);
            let s = self.assemblies[team].clone();
            if s.stage == "moving" && now >= s.ready_at {
                self.assemblies.get_mut(team).unwrap().stage = "inserting".into();
            }
            if s.stage == "buffered" && now >= s.buffer_until {
                self.assemblies.get_mut(team).unwrap().stage = "idle".into();
                self.assemblies.get_mut(team).unwrap().confirm_progress_s = 0.0;
            }
            let timeout = n(self.assembly_difficulty(s.difficulty), "attempt_timeout_s");
            if s.stage == "inserting"
                && (now - s.started >= timeout
                    || (s.difficulty == 4 && s.inserted_units == 1
                        && now - s.first_unit_at > self.secondary_core_window_s(s.target_rotation)))
            {
                let reason = if now - s.started >= timeout { "timeout" } else { "secondary_window" };
                self.fail_assembly(team, reason);
            }
            if !self.robots.iter().enumerate().any(|(i, r)| {
                r.team == team
                    && r.role == "engineer"
                    && r.alive
                    && !r.weak
                    && self.raw_zone(i, "assembly", Some(team))
            }) {
                self.assemblies.get_mut(team).unwrap().confirm_progress_s = 0.0;
            }
            if self.outposts[team].hp <= 0.0
                && self.teams[team].rebuild_credits > 0
                && now < n(&self.rules["structures"], "rebuild_disabled_at_s")
            {
                for i in 0..self.robots.len() {
                    let r = &self.robots[i];
                    if r.team != team
                        || r.role == "aerial"
                        || !r.alive
                        || r.weak
                        || !self.raw_zone(i, "outpost", Some(team))
                    {
                        continue;
                    }
                    let required = self.outpost_rebuild_required(&r.role);
                    let id = r.id.clone();
                    let hold = self
                        .assemblies
                        .get_mut(team)
                        .unwrap()
                        .rebuild_hold
                        .entry(id)
                        .or_insert(0.0);
                    *hold += DT;
                    if *hold >= required - 1e-8 {
                        let hp = n(&self.rules["structures"], "rebuild_hp");
                        self.outposts.get_mut(team).unwrap().hp = hp;
                        self.teams.get_mut(team).unwrap().rebuild_credits -= 1;
                        self.assemblies.get_mut(team).unwrap().rebuild_hold.clear();
                        self.event("outpost_rebuilt", json!({"team":team,"hp":hp}));
                        break;
                    }
                }
                let active: Vec<String> = self
                    .robots
                    .iter()
                    .filter(|r| {
                        r.team == team
                            && r.alive
                            && !r.weak
                            && r.role != "aerial"
                            && self.raw_zone(
                                self.robots.iter().position(|x| x.id == r.id).unwrap(),
                                "outpost",
                                Some(team),
                            )
                    })
                    .map(|r| r.id.clone())
                    .collect();
                self.assemblies
                    .get_mut(team)
                    .unwrap()
                    .rebuild_hold
                    .retain(|id, _| active.contains(id));
            } else {
                self.assemblies.get_mut(team).unwrap().rebuild_hold.clear();
            }
            let stop = self.outposts[team].ever_destroyed
                || self.bases[opponent(team)].armor_open
                || now >= 180.0;
            let outpost = self.outposts.get_mut(team).unwrap();
            if stop || outpost.rotation_stopped {
                if !outpost.rotation_stopped {
                    outpost.rotation_stopped = true;
                    outpost.rotation_stop_at = now;
                    outpost.rotation_stop_angle = outpost.rotation;
                }
                let fraction = ((now - outpost.rotation_stop_at) / 10.0).clamp(0.0, 1.0);
                let start = (outpost.rotation_stop_angle + std::f64::consts::PI)
                    .rem_euclid(std::f64::consts::TAU)
                    - std::f64::consts::PI;
                outpost.rotation = (start * (1.0 - fraction)).rem_euclid(std::f64::consts::TAU);
            } else {
                outpost.rotation = (outpost.rotation
                    + 0.8 * std::f64::consts::PI * (now / 5.0).min(1.0) * DT * self.rune_direction)
                    .rem_euclid(std::f64::consts::TAU);
            }
        }
    }
    pub(super) fn check_rulebook_winner(&mut self) {
        let red = &self.bases["red"];
        let blue = &self.bases["blue"];
        if red.hp <= 0.0 || blue.hp <= 0.0 {
            let winner = if red.hp == blue.hp {
                None
            } else {
                Some(if red.hp > blue.hp { "red" } else { "blue" }.into())
            };
            self.finish(winner, "base_destroyed");
            return;
        }
        if self.time() < self.duration {
            return;
        }
        let metric = |team: &str| {
            let o = &self.outposts[team];
            (
                self.bases[team].hp,
                if o.ever_destroyed { 0 } else { 1 },
                if o.ever_destroyed { 0.0 } else { o.hp },
                self.teams[team].attack_damage,
                self.robots
                    .iter()
                    .filter(|r| r.team == team && r.alive && r.role != "aerial")
                    .map(|r| r.hp)
                    .sum::<f64>(),
            )
        };
        let winner = match metric("red").partial_cmp(&metric("blue")).unwrap() {
            std::cmp::Ordering::Greater => Some("red".into()),
            std::cmp::Ordering::Less => Some("blue".into()),
            _ => None,
        };
        self.finish(winner, "timeout");
    }
    pub(super) fn public_rule_summary(&self) -> Value {
        if self.season == 2027 {
            return self.public_rule_summary_2027();
        }
        json!({"source":self.rules["source"],"construction_source":self.rules["construction"]["source"],"physical_design_revision":"construction-2026-v1","physics_hz":120,"adaptations":self.rules["adaptations"],"match":self.rules["match"],"profile_defaults":self.rules["profiles"]["defaults"],"full_rules_available":"sdk.RULES, sdk.DESIGNS and protected arena.json"})
    }
}

#[cfg(test)]
#[path = "mechanism_tests.rs"]
mod mechanism_tests;

#[cfg(test)]
#[path = "rule_audit_tests.rs"]
mod rule_audit_tests;

#[cfg(test)]
mod rule_tests {
    use super::*;
    fn match_() -> Engine {
        Engine::new(42, 420.0, "rmuc2026").unwrap()
    }
    #[test]
    fn coins_use_manual_timestamps() {
        let mut m = match_();
        m.tick = 20;
        m.step(&json!({}));
        assert_eq!(m.teams["red"].resources, 400.0);
        m.tick = 1220;
        m.step(&json!({}));
        assert_eq!(m.teams["red"].resources, 450.0);
        m.tick = 7220;
        m.step(&json!({}));
        assert_eq!(m.teams["red"].resources, 600.0);
    }
    #[test]
    fn ammo_purchase_budgets_and_remote_delay() {
        let mut m = match_();
        m.teams.get_mut("red").unwrap().resources = 500.0;
        m.robots[0].x = 2.1;
        m.robots[0].y = 5.9;
        m.update_rulebook_buffs();
        m.rulebook_rpc(0, &json!({"buy_ammo":{"amount":2,"remote":false}}));
        assert_eq!((m.robots[0].ammo, m.teams["red"].resources), (2, 480.0));
        m.rulebook_rpc(0, &json!({"buy_ammo":{"amount":10,"remote":true}}));
        assert_eq!((m.robots[0].ammo, m.teams["red"].resources), (2, 330.0));
        m.tick = 120;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[0].ammo, 12);
    }
    #[test]
    fn remote_heal_cost_roundup_is_inside_multiplication() {
        let mut m = match_();
        m.tick = 200;
        m.teams.get_mut("red").unwrap().resources = 1000.0;
        m.robots[0].hp = 10.0;
        m.rulebook_rpc(0, &json!({"remote_heal":true}));
        assert_eq!(m.teams["red"].resources, 946.0);
        m.tick = 320;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[0].hp, 100.0);
    }
    #[test]
    fn heat_settles_at_10hz_and_unlocks_at_zero() {
        let mut m = match_();
        m.robots[0].heat = 101.0;
        m.robots[0].overheated = true;
        m.tick = 1;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[0].heat, 101.0);
        m.tick = 2;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[0].heat, 99.0);
        assert!(m.robots[0].overheated);
        m.robots[0].heat = 1.0;
        m.tick = 4;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[0].heat, 0.0);
        assert!(!m.robots[0].overheated);
    }
    #[test]
    fn armor_damage_and_engineer_has_no_weapon() {
        let mut m = match_();
        m.robots[1].ammo = 100;
        m.step(&json!({"red":{"2":{"fire":true}}}));
        assert!(m.projectiles.is_empty());
        assert_eq!(m.robots[1].ammo, 100);
        let index = 6;
        let target = m.robots[index].clone();
        let p = Projectile {
            id: 1,
            source: "red-1".into(),
            team: "red".into(),
            role: "hero".into(),
            x: 0.0,
            y: 0.0,
            z: 0.5,
            vx: 12.0,
            vy: 0.0,
            vz: 0.0,
            remaining: 10.0,
            damage: 200.0,
            radius: 0.02125,
            deployed: false,
        };
        m.robots[0].last_shot_time = 0.0;
        m.robots[index].heading = std::f64::consts::PI;
        m.robots[index].defense_buff = 0.0;
        m.rulebook_robot_hit(index, &p, 0.0, target.x - 0.28, target.y, target.z + 0.5);
        assert!(!m.robots[index].alive);
        assert_eq!(m.robots[index].respawn_required, 10.0);
        assert_eq!(m.teams["red"].resources, 0.0);
        assert_eq!(m.teams["red"].attack_damage, 150.0);
    }
    #[test]
    fn natural_respawn_is_weak_at_death_location() {
        let mut m = match_();
        m.robots[0].alive = false;
        m.robots[0].hp = 0.0;
        m.robots[0].respawn_required = 10.0;
        m.robots[0].respawn_progress = 9.99;
        let pos = (m.robots[0].x, m.robots[0].y);
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert!(m.robots[0].weak);
        assert_eq!(m.robots[0].hp, 15.0);
        assert_eq!((m.robots[0].x, m.robots[0].y), pos);
        assert_eq!(m.robots[0].invulnerable_until, 30.0);
    }
    #[test]
    fn base_protection_and_42mm_inactivity_mask() {
        let mut m = match_();
        m.robots[0].last_shot_time = 0.0;
        let point = structures::plates(&m.bases["blue"],false)[0].center;
        let p = Projectile {
            id: 1,
            source: "red-1".into(),
            team: "red".into(),
            role: "hero".into(),
            x: 25.0,
            y: 7.5,
            z: 1.05,
            // The blue base faces the field, so an incoming shot travels +x.
            vx: 12.0,
            vy: 0.0,
            vz: 0.0,
            remaining: 10.0,
            damage: 200.0,
            radius: 0.02125,
            deployed: false,
        };
        m.rulebook_structure_hit("blue", false, &p, 0.0, point[0], point[1]+0.02, point[2]);
        assert_eq!(m.bases["blue"].hp, 5000.0);
        m.outposts.get_mut("blue").unwrap().hp = 0.0;
        m.rulebook_structure_hit("blue", false, &p, 0.0, point[0], point[1]+0.02, point[2]);
        assert!(m.bases["blue"].hp < 5000.0);
        let hp = m.bases["blue"].hp;
        m.tick = 80;
        m.rulebook_structure_hit("blue", false, &p, 0.0, point[0], point[1]+0.02, point[2]);
        assert_eq!(m.bases["blue"].hp, hp);
    }
    #[test]
    fn level_caps_and_hp_delta() {
        let mut m = match_();
        m.gain_xp(0, 10000.0);
        assert_eq!(m.robots[0].level, 5);
        let experience = m.robots[0].experience;
        m.gain_xp(0, 100.0);
        assert_eq!(m.robots[0].experience, experience);
        m.complete_assembly("red", 2);
        assert_eq!(m.teams["red"].level_cap, 7);
        m.complete_assembly("red", 3);
        assert_eq!(m.teams["red"].level_cap, 10);
    }
    #[test]
    fn engineer_tiers_and_virtual_shield() {
        let mut m = match_();
        m.complete_assembly("red", 1);
        assert_eq!(m.teams["red"].tech_income, 50.0);
        m.complete_assembly("red", 1);
        assert_eq!(m.teams["red"].tech_income, 55.0);
        m.complete_assembly("red", 2);
        m.complete_assembly("red", 3);
        m.complete_assembly("red", 4);
        assert_eq!(m.teams["red"].tech_income, 155.0);
        assert_eq!(m.teams["red"].tech_defense, 0.5);
        assert_eq!(m.bases["red"].shield_hp, 2000.0);
    }
    #[test]
    fn aircraft_budget_and_no_extra_ammo() {
        let mut m = match_();
        let i = 4;
        m.rulebook_rpc(
            i,
            &json!({"support":true,"buy_ammo":{"amount":100,"remote":true}}),
        );
        assert_eq!(m.robots[i].ammo, 750);
        m.robots[i].support_time = 0.0;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert!(!m.robots[i].support_active);
        m.tick = 1200;
        m.rulebook_before_step(&vec![Action::default(); 12]);
        assert_eq!(m.robots[i].support_time, 20.0);
    }
    #[test]
    fn rune_arms_are_unique_and_small_buff_is_exact() {
        let mut m = match_();
        m.start_rune("red");
        let p = Projectile {
            id: 1,
            source: "red-3".into(),
            team: "red".into(),
            role: "infantry".into(),
            x: 0.0,
            y: 7.5,
            z: 2.7,
            vx: 25.0,
            vy: 0.0,
            vz: 0.0,
            remaining: 10.0,
            damage: 20.0,
            radius: 0.0084,
            deployed: false,
        };
        let mut arms = Vec::new();
        for _ in 0..5 {
            let arm = m.runes["red"].lit[0];
            arms.push(arm);
            let target = &m.rune_snapshots()["red"]["targets"][arm];
            m.rulebook_rune_hit(&p, arm, n(target, "x"), n(target, "y"), n(target, "z"));
            let feedback = m.rune_snapshots();
            assert_eq!(feedback["red"]["targets"][arm]["hit"], true);
            assert_eq!(feedback["red"]["targets"][arm]["last_hit_valid"], true);
            assert_eq!(
                feedback["red"]["targets"][arm]["last_hit_at"],
                json!(m.time())
            );
        }
        arms.sort();
        arms.dedup();
        assert_eq!(arms.len(), 5);
        assert_eq!(m.runes["red"].defense, 0.25);
        assert_eq!(m.runes["red"].active_until, 45.0);
        assert_eq!(m.runes["red"].status, "active");
        assert_eq!(m.runes["blue"].status, "idle");
        m.tick = 899; // One tick before the 45 second buff ends.
        m.update_rune_motor();
        assert_eq!(m.runes["red"].status, "active");
        assert!(m.rune_snapshots()["red"]["targets"]
            .as_array()
            .unwrap()
            .iter()
            .all(|t| t["hit"] == true));
        m.tick = 900;
        m.update_rune_motor();
        assert_eq!(m.runes["red"].status, "idle");
        assert!(m.runes["red"].activated_arms.is_empty());
        assert!(m.rune_snapshots()["red"]["targets"]
            .as_array()
            .unwrap()
            .iter()
            .all(|t| t["hit"] == false && t["last_hit_at"].is_null()));
    }
    #[test]
    fn wrong_rune_target_records_impact_feedback_and_clears_progress() {
        let mut m = match_();
        m.start_rune("red");
        let mut p = Projectile {
            id: 1,
            source: "red-3".into(),
            team: "red".into(),
            role: "infantry".into(),
            x: 0.0,
            y: 7.5,
            z: 2.7,
            vx: 25.0,
            vy: 0.0,
            vz: 0.0,
            remaining: 10.0,
            damage: 20.0,
            radius: 0.0084,
            deployed: false,
        };
        let hit = |m: &mut Engine, p: &Projectile, arm: usize| {
            let t = m.rune_snapshots()["red"]["targets"][arm].clone();
            m.rulebook_rune_hit(p, arm, n(&t, "x"), n(&t, "y"), n(&t, "z"));
        };
        let first = m.runes["red"].lit[0];
        hit(&mut m, &p, first);
        let wrong = (m.runes["red"].lit[0] + 1) % 5;
        m.tick = 2;
        hit(&mut m, &p, wrong);
        assert!(m.runes["red"].activated_arms.is_empty());
        let t = &m.rune_snapshots()["red"]["targets"][wrong];
        assert_eq!(t["last_hit_at"], json!(0.1));
        assert_eq!(t["last_hit_valid"], false);
        assert!(m
            .events
            .iter()
            .any(|e| e["type"] == "rune_hit" && e["valid"] == false));
        // Unsupported 42 mm shots still visibly strike the plate but do not advance it.
        p.role = "hero".into();
        let arm = m.runes["red"].lit[0];
        hit(&mut m, &p, arm);
        assert!(!m.runes["red"].last_hit_valid[arm]);
        assert_eq!(m.runes["red"].hit_count, 0);
    }
    #[test]
    fn rune_activation_timeout_clears_partial_lights() {
        let mut m = match_();
        m.start_rune("red");
        m.runes.get_mut("red").unwrap().activated_arms = vec![0, 1];
        m.tick = 400;
        m.update_rune_motor();
        assert_eq!(m.runes["red"].status, "idle");
        assert!(m.runes["red"].activated_arms.is_empty());
        assert!(m.runes["red"].lit.is_empty());
    }
}
