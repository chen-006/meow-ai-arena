//! RMUC 2027 V1.0.0 referee rules (rules manual 2026-10-15). The 2026 rules stay in
//! `rulebook.rs`; shared physics, projectiles, vision and the rune motor are reused.
//! Darts, radar and electronic countermeasures are not part of this arena.
use super::*;

#[cfg(test)]
#[path = "combat_2027_regression_tests.rs"]
mod combat_2027_regression_tests;

fn n(v: &Value, k: &str) -> f64 {
    v[k].as_f64().unwrap_or(0.0)
}
fn opponent(team: &str) -> &'static str {
    if team == "red" {
        "blue"
    } else {
        "red"
    }
}
fn active_until(value: f64, now: f64) -> bool {
    value > now + 1e-8
}
const POSTURES: [&str; 3] = ["attack", "defense", "move"];

/// One autonomous aerial robot of a 2027 aerial system.
#[derive(Clone, Serialize, Deserialize)]
pub struct Drone {
    pub id: String,
    pub index: u32,
    /// docked (in the hangar), flying, down (resting outside, can relaunch) or disabled.
    pub state: String,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    pub quaternion: [f64; 4],
    pub battery_wh: f64,
    pub integrity: f64,
    pub strikes: u32,
    pub power_w: f64,
    pub dock: [f64; 3],
    #[serde(default)]
    pub recover_until: f64,
    #[serde(default)]
    pub last_strike: f64,
    #[serde(default)]
    pub launch_until: f64,
}

pub fn make_drones(team: &str, slot: u32, design: &Value, arena: &Arena) -> Vec<Drone> {
    let h = &arena.hangars[team];
    let (hx, hy, hz) = (n(h, "x"), n(h, "y"), n(h, "z"));
    let drone = &design["drone"];
    let count = design["drone_count"].as_u64().unwrap_or(0) as usize;
    let offsets = [[-0.075, -0.075], [0.075, -0.075], [-0.075, 0.075], [0.075, 0.075]];
    (0..count.min(4))
        .map(|k| {
            // Docking cradles on the hangar roof, above its 200 mm body.
            let dock = [hx + offsets[k][0], hy + offsets[k][1], hz + 0.21 + n(drone, "radius_m")];
            Drone {
                id: format!("{team}-{slot}-d{}", k + 1),
                index: k as u32 + 1,
                state: "docked".into(),
                x: dock[0],
                y: dock[1],
                z: dock[2],
                vx: 0.0,
                vy: 0.0,
                vz: 0.0,
                quaternion: [0.0, 0.0, 0.0, 1.0],
                battery_wh: n(drone, "battery_wh"),
                integrity: n(drone, "impact_integrity"),
                strikes: 0,
                power_w: 0.0,
                dock,
                recover_until: 0.0,
                last_strike: -1000.0,
                launch_until: 0.0,
            }
        })
        .collect()
}

/// A control position (5.6.2): its original owner, who controls it and how firmly.
#[derive(Clone, Serialize, Deserialize, Default)]
pub struct PositionState {
    pub owner: String,
    pub kind: String,
    pub zones: Vec<String>,
    /// "full" or "temporary".
    pub control: String,
    pub controller: String,
    /// Seconds toward the pending transition (capture by the other team, or release).
    pub progress: f64,
    pub active: Vec<String>,
}

impl Engine {
    pub(super) fn init_2027(&mut self) {
        let positions = self.arena.positions.as_object().cloned().unwrap_or_default();
        for (id, p) in positions {
            let owner = p["owner"].as_str().unwrap_or("red").to_string();
            self.positions.insert(
                id,
                PositionState {
                    owner: owner.clone(),
                    kind: p["kind"].as_str().unwrap_or("").into(),
                    zones: p["zones"].as_array().map(|z| z.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_default(),
                    control: "full".into(),
                    controller: owner,
                    progress: 0.0,
                    active: Vec::new(),
                },
            );
        }
        let units = self.rules["assembly"]["units_per_resource_station"].as_u64().unwrap_or(6) as u32;
        for team in ["red", "blue"] {
            let base = self.bases[team].max_hp;
            let t = self.teams.get_mut(team).unwrap();
            t.control = Some(0.0);
            t.base_lowest_hp = Some(base);
            t.max_base_gap = Some(0.0);
            t.max_control_gap = Some(0.0);
            t.energy_units_left = Some(units);
            t.launcher_unlock_s = Some(300.0);
        }
    }

    // ---- Attributes -------------------------------------------------------------------
    fn heavy_stage(&self, i: usize) -> usize {
        let r = &self.robots[i];
        let done = &self.teams[&r.team].completed_difficulties;
        if done.contains(&3) || r.launcher == "installed" {
            3
        } else if done.contains(&2) {
            2
        } else if done.contains(&1) {
            1
        } else {
            0
        }
    }
    pub(super) fn apply_performance_2027(&mut self, i: usize, add_delta: bool) {
        let r = &self.robots[i];
        let old = r.max_hp;
        let level = (r.level.clamp(1, 10) - 1) as usize;
        let (hp, power, heat, cooling) = match r.role.as_str() {
            "heavy" => {
                let v = &self.rules["heavy"]["stages"][self.heavy_stage(i)];
                (n(v, "hp"), n(v, "power_w"), n(v, "max_heat"), n(v, "cooling"))
            }
            "infantry" => {
                let c = &self.rules["profiles"]["infantry_chassis"][&r.chassis_profile][level];
                let w = &self.rules["profiles"]["infantry_weapon"][&r.weapon_profile][level];
                (n(c, "hp"), n(c, "power_w"), n(w, "max_heat"), n(w, "cooling"))
            }
            "sentry" => {
                let v = &self.rules["sentry"]["automatic"];
                (n(v, "hp"), n(v, "power_w"), n(v, "max_heat"), n(v, "cooling"))
            }
            _ => (0.0, 0.0, 0.0, 0.0),
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
    pub(super) fn select_profile_2027(&mut self, i: usize, rpc: &Value) {
        if self.tick != 0 || self.robots[i].role != "infantry" {
            return;
        }
        let Some(profile) = rpc.get("profile") else { return };
        let r = &mut self.robots[i];
        if let Some(s) = profile["chassis"].as_str().filter(|s| ["hp", "power"].contains(s)) {
            r.chassis_profile = s.into();
        }
        if let Some(s) = profile["weapon"].as_str().filter(|s| ["burst", "cooling"].contains(s)) {
            r.weapon_profile = s.into();
        }
        self.apply_performance_2027(i, false);
    }
    /// Logistics coefficient lambda = 1 - lost base HP / 20000 (table 5-19).
    pub(super) fn logistics(&self, team: &str) -> f64 {
        let b = &self.bases[team];
        (1.0 - (b.max_hp - b.hp).max(0.0) / n(&self.rules["logistics"], "base_loss_divisor_hp").max(1.0)).clamp(0.05, 1.0)
    }
    /// Table 5-19: robot interaction bandwidth is x·λ bytes per message.
    pub(super) fn interaction_bandwidth_bytes(&self, team: &str) -> u32 {
        let base = self.rules["logistics"]["interaction_bandwidth_bytes"]
            .as_f64()
            .filter(|bytes| *bytes > 0.0)
            .unwrap_or(512.0);
        (base * self.logistics(team)).floor() as u32
    }
    /// Table 5-19: the FPV picture keeps x·λ² pixels, i.e. each side scales by λ before the
    /// client upscales it bilinearly. Canvas UI is not part of the video feed and stays sharp.
    pub(super) fn fpv_video(&self, team: &str) -> Value {
        let lambda = self.logistics(team);
        let px = &self.rules["logistics"]["video_resolution_px"];
        let width = px.get(0).and_then(Value::as_f64).unwrap_or(1920.0);
        let height = px.get(1).and_then(Value::as_f64).unwrap_or(1080.0);
        json!({"width":(width * lambda).round() as u32,"height":(height * lambda).round() as u32,
            "pixels_scale":lambda * lambda})
    }
    /// Everything table 5-19 scales, resolved against this team's current base HP.
    pub(super) fn logistics_effects(&self, team: &str) -> Value {
        let lambda = self.logistics(team);
        json!({
            "respawn_time_scale": 1.0 / lambda,
            "supply_heal_scale": lambda,
            "remote_heal_scale": lambda,
            "interaction_bandwidth_scale": lambda,
            "interaction_bandwidth_bytes": self.interaction_bandwidth_bytes(team),
            "fpv_resolution": self.fpv_video(team),
        })
    }
    pub(super) fn gain_xp_2027(&mut self, i: usize, value: f64) {
        let r = &self.robots[i];
        if r.role != "infantry" || !r.alive || value <= 0.0 || r.level >= self.teams[&r.team].level_cap {
            return;
        }
        let team = r.team.clone();
        let cap = self.teams[&team].level_cap;
        let thresholds = self.rules["experience"]["thresholds"].as_array().unwrap().clone();
        let room = (thresholds[cap as usize - 1].as_f64().unwrap() - r.experience).max(0.0);
        let base_gain = value.min(room);
        let mut bonus = 0.0;
        if self.runes[&team].status == "active" && self.runes[&team].mode == "small" {
            bonus = base_gain.min((room - base_gain).max(0.0)).min((1200.0 - self.teams[&team].small_bonus_xp).max(0.0));
            self.teams.get_mut(&team).unwrap().small_bonus_xp += bonus;
        }
        self.robots[i].experience += base_gain + bonus;
        let mut level = self.robots[i].level;
        while level < cap && self.robots[i].experience >= thresholds[level as usize].as_f64().unwrap() {
            level += 1;
        }
        if level != self.robots[i].level {
            self.robots[i].level = level;
            self.apply_performance_2027(i, true);
            self.event("level_up", json!({"robot":self.robots[i].id,"level":level,"team":team}));
        }
    }
    fn can_gain_xp_2027(&self, i: usize) -> bool {
        let r = &self.robots[i];
        r.role == "infantry" && r.alive && !r.red_card && r.level < self.teams[&r.team].level_cap
    }
    /// Table 5-14: only living infantry can receive unattributed XP. None denotes
    /// non-projectile damage; infantry cannot normally deal 42 mm damage.
    fn fallback_xp_recipients_2027(&self, team: &str, caliber: Option<&str>) -> Vec<usize> {
        if caliber == Some("42mm") {
            return Vec::new();
        }
        self.robots.iter().enumerate()
            .filter(|(i, r)| r.team == team && self.can_gain_xp_2027(*i))
            .map(|(i, _)| i).collect()
    }
    pub(super) fn award_damage_xp_2027(&mut self, source: Option<usize>, team: &str, xp: f64, caliber: Option<&str>) {
        if let Some(i) = source.filter(|i| self.robots[*i].team == team && self.can_gain_xp_2027(*i)) {
            self.gain_xp_2027(i, xp);
            return;
        }
        let recipients = self.fallback_xp_recipients_2027(team, caliber);
        if recipients.is_empty() {
            return;
        }
        let share = (xp / recipients.len() as f64 * 10.0).round() / 10.0;
        for i in recipients {
            self.gain_xp_2027(i, share);
        }
    }
    pub(super) fn award_kill_xp_2027(&mut self, source: Option<usize>, team: &str, victim_level: u32, caliber: Option<&str>) {
        let direct = source.filter(|i| self.robots[*i].team == team && self.can_gain_xp_2027(*i));
        let recipients = match direct {
            Some(i) => vec![i],
            None => self.fallback_xp_recipients_2027(team, caliber),
        };
        if recipients.is_empty() {
            return;
        }
        let killer_level = if let Some(i) = direct {
            self.xp_level(i)
        } else {
            let average = recipients.iter().map(|i| self.robots[*i].experience).sum::<f64>() / recipients.len() as f64;
            self.rules["experience"]["thresholds"].as_array().unwrap().iter()
                .take_while(|threshold| threshold.as_f64().unwrap() <= average).count().max(1) as u32
        };
        let gap = victim_level.saturating_sub(killer_level);
        let xp = n(&self.rules["experience"], "kill_base_xp") * victim_level as f64
            * (1.0 + n(&self.rules["experience"], "kill_level_gap_factor") * gap as f64);
        let share = if direct.is_some() { xp } else { (xp / recipients.len() as f64 * 10.0).round() / 10.0 };
        for i in recipients {
            self.gain_xp_2027(i, share);
        }
    }
    /// The level a robot counts as when killed (sentry counts as level 1).
    fn xp_level(&self, i: usize) -> u32 {
        if self.robots[i].role == "sentry" { 1 } else { self.robots[i].level.max(1) }
    }

    // ---- Control positions ------------------------------------------------------------
    pub(super) fn controls(&self, position: &str, team: &str) -> bool {
        self.positions.get(position).map(|p| p.controller == team).unwrap_or(false)
    }
    fn fully_controls(&self, position: &str, team: &str) -> bool {
        self.positions.get(position).map(|p| p.controller == team && p.control == "full").unwrap_or(false)
    }
    /// Own fortress card works while the team does not control its own outpost position.
    pub(super) fn fortress_active(&self, team: &str) -> bool {
        !self.controls(&format!("{team}_outpost"), team)
    }
    fn enemy_fortress_allowed(&self, attacker: &str, owner: &str) -> bool {
        self.time() >= n(&self.rules["buffs"]["fortress"], "enemy_activation_after_s") - 1e-8
            && self.controls(&format!("{owner}_outpost"), attacker)
    }
    /// A started enemy-fortress count stays live while the pilot keeps touching the card. Control of
    /// the owner's outpost position is what lets the card be *activated*; the 10 s count itself only
    /// needs presence (and is cleared after `enemy_timer_retention_s` away), which is what the rule's
    /// "持续 10 s / 离开超过 3 s 计时清零" describes. Without this, a lone attacker loses the
    /// position five seconds after leaving it and can never finish the ten-second count.
    fn enemy_fortress_hold_ongoing(&self, i: usize, owner: &str) -> bool {
        let r = &self.robots[i];
        let retention = n(&self.rules["buffs"]["fortress"], "enemy_timer_retention_s");
        r.fortress_hold > 1e-9
            && self.time() - r.fortress_last_contact <= retention + 1e-8
            && !self.bases[owner].armor_open
    }
    fn update_positions(&mut self) {
        let now = self.time();
        let capture = n(&self.rules["control"], "capture_s");
        let ids: Vec<String> = self.positions.keys().cloned().collect();
        for id in ids {
            let p = self.positions[&id].clone();
            // A team is active on a position while any of its robots activates one of its cards
            // (occupants already apply exclusivity, permissions and the 2 s release delay).
            let mut active = Vec::new();
            for team in ["red", "blue"] {
                let prefix = format!("{team}-");
                if p.zones.iter().any(|z| self.zones.get(z).map(|s| s.occupants.iter().any(|o| o.starts_with(&prefix))).unwrap_or(false)) {
                    active.push(team.to_string());
                }
            }
            let _ = now;
            let holder = p.controller.clone();
            let other = opponent(&holder).to_string();
            let holder_on = active.contains(&holder);
            let other_on = active.contains(&other);
            let s = self.positions.get_mut(&id).unwrap();
            s.active = active;
            let mut change: Option<(String, String)> = None;
            if p.control == "full" {
                if other_on && !holder_on {
                    s.progress += DT;
                } else if !other_on {
                    s.progress = (s.progress - DT).max(0.0);
                }
                if s.progress >= capture - 1e-8 {
                    change = Some(("temporary".into(), other.clone()));
                }
            } else if holder_on && other_on {
                // Table 5-21 pauses both advance and rollback while both teams activate.
            } else if !holder_on {
                s.progress += DT;
                if s.progress >= capture - 1e-8 {
                    // A temporary holder absent for 5 s yields full control to the other team;
                    // a base position can only ever be fully held by its original owner.
                    let next = if p.kind == "base" { p.owner.clone() } else { other.clone() };
                    change = Some(("full".into(), next));
                }
            } else {
                s.progress = (s.progress - DT).max(0.0);
            }
            if let Some((control, controller)) = change {
                self.set_position(&id, &control, &controller);
            }
        }
        // Control degree: both outpost positions +1/s, both base positions +3/s.
        for team in ["red", "blue"] {
            let other = opponent(team);
            let mut rate = 0.0;
            if self.controls(&format!("{team}_outpost"), team) && self.controls(&format!("{other}_outpost"), team) {
                rate += n(&self.rules["control"], "outpost_pair_rate_per_s");
            }
            if self.controls(&format!("{team}_base"), team) && self.controls(&format!("{other}_base"), team) {
                rate += n(&self.rules["control"], "base_pair_rate_per_s");
            }
            let t = self.teams.get_mut(team).unwrap();
            t.control = Some(t.control.unwrap_or(0.0) + rate * DT);
        }
    }
    fn set_position(&mut self, id: &str, control: &str, controller: &str) {
        let previous = self.positions[id].clone();
        {
            let s = self.positions.get_mut(id).unwrap();
            s.control = control.into();
            s.controller = controller.into();
            s.progress = 0.0;
        }
        // A full-control change while the outpost lies destroyed rebuilds it for the new holder.
        if previous.kind == "outpost" && control == "full" {
            let team = previous.owner.clone();
            if self.outposts[&team].hp <= 0.0 {
                let hp = n(&self.rules["structures"], "rebuilt_outpost_hp");
                let o = self.outposts.get_mut(&team).unwrap();
                o.hp = hp;
                o.max_hp = hp;
                o.owner = Some(controller.into());
                o.armor_hit_times = vec![-1000.0; 6];
                self.event("outpost_rebuilt", json!({"team":controller,"position":id,"hp":hp}));
            }
        }
        self.event("control_changed", json!({"position":id,"team":controller,"control":control,"previous":previous.controller,"previous_control":previous.control}));
    }
    pub(super) fn outpost_destroyed(&mut self, owner: &str) {
        let id = format!("{owner}_outpost");
        let Some(p) = self.positions.get(&id).cloned() else { return };
        if p.control == "full" {
            self.set_position(&id, "temporary", &p.controller);
        } else {
            self.set_position(&id, "full", &p.controller);
        }
    }

    // ---- Per-tick rules before physics ------------------------------------------------
    pub(super) fn before_step_2027(&mut self, actions: &[Action]) {
        self.update_referee_locks();
        let now = self.time();
        let second = self.tick / 20;
        let whole = self.tick % 20 == 0;
        if whole {
            let e = self.rules["economy"].clone();
            if second as f64 == n(&e, "initial_coin_time_s") {
                let coins = n(&e, "initial_coins");
                for t in self.teams.values_mut() {
                    t.resources += coins;
                }
                self.event("income", json!({"coins":coins,"reason":"initial"}));
            }
            for grant in e["periodic_schedule"].as_array().unwrap() {
                if second == grant["elapsed_s"].as_u64().unwrap() {
                    let coins = n(grant, "coins");
                    for t in self.teams.values_mut() {
                        t.resources += coins;
                    }
                    self.event("income", json!({"coins":coins,"reason":"schedule"}));
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
            self.select_profile_2027(i, &actions[i].rpc);
            if self.tick == 0 {
                self.robots[i].profile_locked = true;
            }
            let team = self.robots[i].team.clone();
            if self.robots[i].alive && self.robots[i].weak_until.map(|until| now + 1e-8 >= until).unwrap_or(false) {
                self.robots[i].weak = false;
                self.robots[i].weak_until = None;
            }
            let deliveries: Vec<u32> = self.robots[i].pending_ammo.iter().filter(|(due, _)| *due <= now + 1e-8).map(|(_, a)| *a).collect();
            for amount in deliveries {
                self.robots[i].ammo += amount;
                self.event("ammo_delivered", json!({"robot":self.robots[i].id,"amount":amount}));
            }
            self.robots[i].pending_ammo.retain(|(due, _)| *due > now + 1e-8);
            if self.robots[i].pending_heal.map(|due| due <= now + 1e-8).unwrap_or(false) {
                self.robots[i].pending_heal = None;
                if self.robots[i].alive {
                    let ratio = n(&self.rules["heal"], "remote_ratio") * self.logistics(&team);
                    let r = &mut self.robots[i];
                    r.hp = (r.hp + (r.max_hp * ratio).round()).min(r.max_hp);
                    self.event("remote_heal", json!({"robot":self.robots[i].id}));
                }
            }
            if self.robots[i].role == "aerial" {
                if self.tick % 2 == 0 {
                    self.rulebook_rpc_2027(i, &actions[i].rpc);
                }
                continue;
            }
            if self.tick % 2 == 0 {
                self.settle_heat_power_2027(i);
            }
            if !self.robots[i].alive {
                let fast = self.contacted_zone_2027(i, "supply", Some(&team))
                    || self.bases[&team].hp < n(&self.rules["respawn"], "fast_when_base_hp_below");
                let rate = if fast { n(&self.rules["respawn"], "fast_progress_per_s") } else { 1.0 };
                self.robots[i].respawn_progress += rate * DT;
                if self.robots[i].respawn_progress + 1e-8 >= self.robots[i].respawn_required {
                    self.respawn_2027(i, false);
                }
            } else if whole {
                let supply = self.contacted_zone_2027(i, "supply", Some(&team));
                let rate = self.supply_heal_rate(i);
                let outpost = !supply && self.on_controlled_outpost(i);
                let fraction = if supply { rate } else if outpost { rate * n(&self.rules["heal"], "outpost_ratio_of_supply") } else { 0.0 };
                if fraction > 0.0 && !self.robots[i].weak {
                    let r = &mut self.robots[i];
                    r.hp = (r.hp + (r.max_hp * fraction).round()).min(r.max_hp);
                }
            }
            // Sentry: 100 allowed rounds per elapsed minute, collected on its supply card.
            if self.robots[i].role == "sentry" && self.robots[i].alive && !self.robots[i].weak && self.contacted_zone_2027(i, "supply", Some(&team)) {
                let interval = n(&self.rules["sentry"]["supply_grant"], "interval_s").max(1.0);
                let due = (now / interval).floor() as u32;
                let taken = self.robots[i].supply_grants_taken;
                if due > taken {
                    let amount = (due - taken) * n(&self.rules["sentry"]["supply_grant"], "amount") as u32;
                    self.robots[i].ammo += amount;
                    self.robots[i].supply_grants_taken = due;
                    self.event("ammo_delivered", json!({"robot":self.robots[i].id,"amount":amount,"reason":"sentry_supply"}));
                }
            }
            if self.tick % 2 == 0 {
                self.rulebook_rpc_2027(i, &actions[i].rpc);
            }
        }
        self.update_rune_motor_2027();
    }
    fn supply_heal_rate(&self, i: usize) -> f64 {
        let r = &self.robots[i];
        let h = &self.rules["heal"];
        let base = if self.time() >= n(h, "late_start_s") && self.disengaged_2027(i) {
            n(h, "late_disengaged_supply_ratio_per_s")
        } else {
            n(h, "supply_ratio_per_s")
        };
        base * self.logistics(&r.team)
    }
    fn on_controlled_outpost(&self, i: usize) -> bool {
        let r = &self.robots[i];
        ["red", "blue"].iter().any(|owner| {
            self.fully_controls(&format!("{owner}_outpost"), &r.team) && self.contacted_zone_2027(i, "outpost", Some(owner))
        })
    }
    fn disengaged_2027(&self, i: usize) -> bool {
        let r = &self.robots[i];
        r.alive && self.time() - r.last_shot_time >= 6.0 - 1e-8 && self.time() - r.last_damage_time >= 6.0 - 1e-8
    }
    /// Posture effect values: (cooling multiplier, cooling ratio, power ratio, power multiplier,
    /// fixed power, defense, vulnerability, no heat).
    fn posture_effect(&self, r: &Robot) -> Option<Value> {
        if r.role != "sentry" || r.posture.is_empty() {
            return None;
        }
        let k = POSTURES.iter().position(|p| *p == r.posture)?;
        let decay = n(&self.rules["sentry"]["posture"], "decay_after_s");
        let state = if r.posture_enhanced { "enhanced" } else if r.posture_time[k] > decay + 1e-8 { "decayed" } else { "normal" };
        Some(self.rules["sentry"]["posture"][&r.posture][state].clone())
    }
    /// Buffer energy ceiling (rulebook p.67): the power settlement drains into this reserve and a
    /// destroyed chassis is reset back to it.
    fn buffer_capacity_2027(&self) -> f64 {
        n(&self.rules["power"], "buffer_capacity_j")
    }
    fn settle_heat_power_2027(&mut self, i: usize) {
        let now = self.time();
        let r = self.robots[i].clone();
        let capacity = self.buffer_capacity_2027();
        let posture = self.posture_effect(&r);
        let p = posture.clone().unwrap_or(Value::Null);
        // Same-kind gains take the maximum; posture reductions multiply afterwards.
        let mut cooling = (r.cooling_rate * r.cooling_multiplier).max(r.cooling_rate + r.cooling_bonus);
        if p["cooling_ratio"].is_number() {
            cooling *= n(&p, "cooling_ratio");
        }
        let actor = &mut self.robots[i];
        actor.heat = (actor.heat - cooling * 0.1).max(0.0);
        if p["no_heat"] == true {
            actor.heat = 0.0;
        }
        if actor.overheated && actor.heat <= 1e-8 && !actor.heat_permanent_lock {
            actor.overheated = false;
        }
        if !actor.alive {
            return;
        }
        let limit = actor.power_limit;
        actor.power_w = if active_until(actor.power_cutoff_until, now) { 0.0 } else { actor.power_w };
        actor.buffer_energy_j = (actor.buffer_energy_j - (actor.power_w - limit) * 0.1).clamp(0.0, capacity);
        if actor.buffer_energy_j <= 0.0 && actor.power_w > limit {
            actor.power_cutoff_until = now + 5.0;
            actor.power_w = 0.0;
        }
    }
    /// Effective chassis power limit for physics: base, revive boost, sentry posture.
    fn power_limit_2027(&self, i: usize) -> f64 {
        let r = &self.robots[i];
        let now = self.time();
        let base = match r.role.as_str() {
            "heavy" => n(&self.rules["heavy"]["stages"][self.heavy_stage(i)], "power_w"),
            "infantry" => n(&self.rules["profiles"]["infantry_chassis"][&r.chassis_profile][(r.level.clamp(1, 10) - 1) as usize], "power_w"),
            "sentry" => n(&self.rules["sentry"]["automatic"], "power_w"),
            _ => 0.0,
        };
        let mut multiplier = 1.0_f64;
        if active_until(r.power_boost_until, now) {
            multiplier = multiplier.max(n(&self.rules["respawn"], "instant_power_multiplier"));
        }
        let p = self.posture_effect(r).unwrap_or(Value::Null);
        if p["power_multiplier"].is_number() {
            multiplier = multiplier.max(n(&p, "power_multiplier"));
        }
        // The revive boost doubles the chassis ceiling but never past the absolute 200 W cap.
        let mut limit = (base * multiplier).min(n(&self.rules["power"], "absolute_cap_w"));
        if p["power_w"].is_number() {
            limit = n(&p, "power_w");
        }
        if p["power_ratio"].is_number() {
            limit *= n(&p, "power_ratio");
        }
        limit.round()
    }
    /// Rulebook p.74: bar length = 10 + (420 − remaining)/10 + 20 × paid revives, rounded, then
    /// stretched by the team's logistics factor λ. In a full-length match the elapsed time *is*
    /// 420 − remaining; shortened practice matches keep the elapsed value so the bar never grows
    /// past the match they are running.
    fn revive_bar_2027(&self, i: usize) -> f64 {
        let b = &self.rules["respawn"];
        let elapsed = self.time().max(0.0);
        (n(b, "progress_base")
            + elapsed / n(b, "elapsed_divisor")
            + n(b, "instant_purchase_progress_penalty") * self.robots[i].instant_revives as f64)
            / self.logistics(&self.robots[i].team)
    }
    pub(super) fn respawn_2027(&mut self, i: usize, instant: bool) {
        let now = self.time();
        let capacity = self.buffer_capacity_2027();
        let b = self.rules["respawn"].clone();
        let cost = if instant { (now / 60.0).ceil() * 80.0 + self.xp_level(i) as f64 * 20.0 } else { 0.0 };
        let r = &mut self.robots[i];
        r.alive = true;
        r.hp = (r.max_hp * n(&b, if instant { "instant_hp_ratio" } else { "normal_hp_ratio" })).round();
        r.weak = true;
        r.weak_until = instant.then_some(now + n(&b, "instant_invulnerability_s"));
        r.invulnerable_until = now + if instant { n(&b, "instant_invulnerability_s") } else { n(&b, "normal_invulnerability_s") };
        r.invulnerable = true;
        r.shield = r.invulnerable_until - now;
        r.respawn_progress = 0.0;
        r.respawned_at = now;
        r.heat = 0.0;
        r.overheated = r.heat_permanent_lock;
        r.buffer_energy_j = capacity;
        r.zone_contacts.clear();
        r.crossing_buffs.clear();
        if instant {
            r.instant_revives += 1;
            r.power_boost_until = now + n(&b, "instant_power_duration_s");
        }
        self.event("respawn", json!({"robot":self.robots[i].id,"team":self.robots[i].team,"instant":instant,"weak":true,
            "cost":cost,"hp":self.robots[i].hp,"max_hp":self.robots[i].max_hp}));
    }
    fn rpc_error_2027(&mut self, i: usize, message: &str) {
        self.event("invalid_action", json!({"robot":self.robots[i].id,"reason":message}));
    }
    fn can_local_buy_2027(&self, i: usize) -> bool {
        let r = &self.robots[i];
        !r.weak
            && (self.contacted_zone_2027(i, "base", Some(&r.team))
                || self.contacted_zone_2027(i, "supply", Some(&r.team))
                || self.contacted_zone_2027(i, "high_point", Some(&r.team))
                || ["red", "blue"].iter().any(|owner| self.fully_controls(&format!("{owner}_outpost"), &r.team)
                    && self.contacted_zone_2027(i, "outpost", Some(owner))))
    }
    /// The RFID receiver is mounted under the chassis at its design ground clearance.
    /// Construction manual 3.8.2 gives a 100 mm (+/-5%) effective detection distance.
    /// Explicit tunnel card planes prevent a robot on the roof detecting the card below it.
    fn card_contact_2027(&self, i: usize, zone: &Zone) -> bool {
        let r = &self.robots[i];
        if r.role == "aerial" {
            return false;
        }
        let design = &self.robot_models[&r.design_slot().to_string()];
        let mount = n(design, "ground_clearance_m");
        let [qx, qy, qz, qw] = r.quaternion;
        let receiver = [
            r.x + mount * 2.0 * (qx * qz + qw * qy),
            r.y + mount * 2.0 * (qy * qz - qw * qx),
            r.z + mount * (1.0 - 2.0 * (qx * qx + qy * qy)),
        ];
        let within = if zone.polygon.is_empty() {
            dist(receiver[0], receiver[1], zone.x, zone.y) <= zone.radius
        } else {
            point_in_polygon(receiver[0], receiver[1], &zone.polygon)
        };
        if !within {
            return false;
        }
        let card_z = zone.card_z_m.map(|z| {
            // Cards follow the tilted floor, including across their finite footprint.
            let slope = self.arena.floor_slope_deg.to_radians().tan();
            let substrate = |y: f64| y.min(self.arena.height - y).max(0.0) * slope;
            z + substrate(receiver[1]) - substrate(zone.y)
        }).unwrap_or_else(|| height_at(&self.arena, receiver[0], receiver[1]));
        (receiver[2] - card_z).abs() <= 0.100 * 1.05 + 1e-8
    }
    pub(super) fn raw_zone_2027(&self, i: usize, kind: &str, owner: Option<&str>) -> bool {
        self.arena.zones.iter().any(|z| {
            z.kind == kind
                && owner.map(|o| z.owner.as_deref() == Some(o)).unwrap_or(true)
                && self.card_contact_2027(i, z)
        })
    }
    pub(super) fn contacted_zone_2027(&self, i: usize, kind: &str, owner: Option<&str>) -> bool {
        let r = &self.robots[i];
        let release = n(&self.rules["buffs"], "release_delay_s");
        self.arena.zones.iter().any(|z| {
            z.kind == kind
                && owner.map(|o| z.owner.as_deref() == Some(o)).unwrap_or(true)
                && (self.card_contact_2027(i, z)
                    || r.zone_contacts.get(&z.id).map(|t| self.time() - t <= release + 1e-8).unwrap_or(false))
        })
    }
    fn rulebook_rpc_2027(&mut self, i: usize, rpc: &Value) {
        let now = self.time();
        let r = self.robots[i].clone();
        if r.red_card {
            return;
        }
        let team = r.team.clone();
        if r.role == "aerial" {
            if rpc.get("drones").is_some() && !rpc["drones"].is_object() {
                self.rpc_error_2027(i, "drones must map drone index to a command");
            }
            return;
        }
        if rpc.get("revive").and_then(Value::as_bool) == Some(true) && !r.alive {
            let cost = (now / 60.0).ceil() * 80.0 + self.xp_level(i) as f64 * 20.0;
            if self.teams[&team].resources >= cost {
                self.teams.get_mut(&team).unwrap().resources -= cost;
                self.respawn_2027(i, true);
            }
        }
        if rpc.get("remote_heal").and_then(Value::as_bool) == Some(true) && r.pending_heal.is_none() && self.disengaged_2027(i) {
            let cost = 50.0 + (now / 60.0 * 20.0).ceil();
            if self.teams[&team].resources >= cost {
                self.teams.get_mut(&team).unwrap().resources -= cost;
                self.robots[i].pending_heal = Some(now + 6.0);
            }
        }
        if let Some(order) = rpc.get("buy_ammo") {
            let launches = match r.role.as_str() { "heavy" => Some("42mm"), "infantry" | "sentry" => Some("17mm"), _ => None };
            if let (Some(kind), true) = (launches, r.alive) {
                let remote = order["remote"].as_bool().unwrap_or(false);
                let allowed = if remote { self.disengaged_2027(i) } else { self.can_local_buy_2027(i) };
                let v = self.rules["economy"]["ammo"][kind].clone();
                let batch = n(&v, if remote { "remote_batch" } else { "local_batch" }) as u32;
                let amount = order["amount"].as_u64().unwrap_or(batch as u64).min(1000) as u32;
                let cost = if batch > 0 { amount as f64 / batch as f64 * n(&v, if remote { "remote_cost" } else { "local_cost" }) } else { f64::INFINITY };
                let purchased = if kind == "42mm" { self.teams[&team].purchased_42 } else { self.teams[&team].purchased_17 };
                let cap = n(&v, "team_purchase_cap") as u32;
                // Say why an order was refused: a silent refusal looks like a lost key press.
                let refusal = if !allowed {
                    Some(if remote { "远程补给需脱离交战 6 秒".to_string() } else { "需在本方基地 / 补给区 / 制高点或已完全控制的前哨站内".to_string() })
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
                    if kind == "42mm" { t.purchased_42 += amount } else { t.purchased_17 += amount }
                    if remote {
                        self.robots[i].pending_ammo.push((now + 6.0, amount));
                    } else {
                        self.robots[i].ammo += amount;
                    }
                    self.event("ammo_purchase", json!({"robot":r.id,"amount":amount,"cost":cost,"remote":remote}));
                }
            } else {
                self.event("ammo_refused", json!({"robot":r.id,"reason":"该型号不能购买弹药"}));
            }
        }
        if r.role == "sentry" {
            self.posture_rpc(i, rpc);
        }
        if r.role == "heavy" && rpc.get("install_launcher").and_then(Value::as_bool) == Some(true) {
            self.install_launcher(i);
        } else if r.role == "heavy" && self.robots[i].launcher == "installing" {
            self.robots[i].launcher = self.launcher_idle_state(i);
            self.robots[i].launcher_progress = 0.0;
        }
        self.request_pickup(i, rpc);
        self.assembly_rpc_2027(i, rpc);
        if rpc.get("rune_start").and_then(Value::as_bool) == Some(true)
            && ["infantry", "sentry"].contains(&r.role.as_str())
            && r.alive
            && !r.weak
        {
            self.start_rune_2027(&team);
        }
    }
    fn posture_rpc(&mut self, i: usize, rpc: &Value) {
        let now = self.time();
        let r = self.robots[i].clone();
        let wanted = rpc.get("posture").and_then(Value::as_str).filter(|p| POSTURES.contains(p)).map(String::from);
        let enhanced = rpc.get("posture_enhanced").and_then(Value::as_bool);
        if rpc.get("posture").is_some() && wanted.is_none() {
            self.rpc_error_2027(i, "posture must be attack, defense or move");
            return;
        }
        let next = wanted.clone().unwrap_or(r.posture.clone());
        let k = POSTURES.iter().position(|p| *p == next).unwrap_or(2);
        let budget = n(&self.rules["sentry"]["posture"], "enhanced_budget_s");
        let next_enhanced = enhanced.unwrap_or(r.posture_enhanced && next == r.posture) && r.posture_enhanced_time[k] < budget - 1e-8;
        if (next != r.posture || next_enhanced != r.posture_enhanced) && r.alive {
            if now + 1e-8 < r.posture_ready_at {
                return;
            }
            let actor = &mut self.robots[i];
            actor.posture = next.clone();
            actor.posture_enhanced = next_enhanced;
            actor.posture_ready_at = now + n(&self.rules["sentry"]["posture"], "switch_cooldown_s");
            self.event("posture", json!({"robot":r.id,"team":r.team,"posture":next,"enhanced":next_enhanced}));
        }
    }
    fn workbench_docked(&self, i: usize) -> bool {
        let r = &self.robots[i];
        let tol = &self.sim["launcher_install"];
        self.arena.zones.iter().any(|z| {
            z.kind == "workbench" && z.owner.as_deref() == Some(&r.team) && dist(r.x, r.y, z.x, z.y) <= n(tol, "dock_radius_m").max(z.radius)
        }) && r.vx.hypot(r.vy) <= n(tol, "max_speed_mps").max(0.01) + 1e-9
    }
    /// What an abandoned install falls back to: the team's unlock state, so cancelling early does not
    /// quietly hand out the rules' 42 mm unlock.
    fn launcher_idle_state(&self, i: usize) -> String {
        let team = self.robots[i].team.clone();
        let unlocked = self
            .teams
            .get(&team)
            .map(|t| {
                t.completed_difficulties.contains(&3)
                    || t.launcher_unlock_s.map_or(true, |s| self.time() + 1e-8 >= s)
            })
            .unwrap_or(true);
        if unlocked { "unlocked".into() } else { "locked".into() }
    }
    /// True once the team has earned the 42 mm: difficulty 3, or 300 - M/50 - 2N/3 seconds.
    fn launcher_unlocked(&self, team: &str) -> bool {
        self.teams
            .get(team)
            .map(|t| {
                t.completed_difficulties.contains(&3)
                    || t.launcher_unlock_s.map_or(true, |s| self.time() + 1e-8 >= s)
            })
            .unwrap_or(true)
    }
    fn install_launcher(&mut self, i: usize) {
        let r = self.robots[i].clone();
        // Bolting the module on is allowed at any point in the match; whether it may *shoot* is what
        // the rules' unlock (difficulty 3 or 300 - M/50 - 2N/3 s) decides, in `fire_2027`.
        if !r.alive || r.weak || !["locked", "unlocked", "installing"].contains(&r.launcher.as_str()) {
            return;
        }
        if !self.workbench_docked(i) {
            if r.launcher == "installing" {
                self.robots[i].launcher = self.launcher_idle_state(i);
                self.robots[i].launcher_progress = 0.0;
            }
            return;
        }
        let duration = self.robot_models[&r.design_slot().to_string()]["launcher_install_s"]
            .as_f64().unwrap_or(n(&self.sim["launcher_install"], "stationary_s")).max(0.5);
        let actor = &mut self.robots[i];
        actor.launcher = "installing".into();
        actor.launcher_progress = (actor.launcher_progress + 0.1 / duration).min(1.0);
        if actor.launcher_progress >= 1.0 - 1e-9 {
            actor.launcher = "fitted".into();
            actor.launcher_progress = 1.0;
            self.event("launcher_fitted", json!({"robot":r.id,"team":r.team}));
        }
    }
    /// Unlock path 2 (5.7.1): after 300 - M/50 - 2N/3 seconds, or as soon as difficulty 3 is done. The
    /// unlock only drives the HUD/strategy surface now — the workbench install is allowed regardless.
    fn update_launcher_unlocks(&mut self) {
        let now = self.time();
        for team in ["red", "blue"] {
            let other = opponent(team);
            let gap = self.bases[other].hp - self.bases[team].hp;
            let control_gap = self.teams[other].control.unwrap_or(0.0) - self.teams[team].control.unwrap_or(0.0);
            let t = self.teams.get_mut(team).unwrap();
            let m = t.max_base_gap.unwrap_or(0.0).max(gap);
            let c = t.max_control_gap.unwrap_or(0.0).max(control_gap);
            t.max_base_gap = Some(m);
            t.max_control_gap = Some(c);
            t.launcher_unlock_s = Some(300.0 - m / 50.0 - 2.0 * c / 3.0);
            let low = t.base_lowest_hp.unwrap_or(f64::INFINITY).min(self.bases[team].hp);
            t.base_lowest_hp = Some(low);
            let unlocked = self.teams[team].completed_difficulties.contains(&3) || now + 1e-8 >= self.teams[team].launcher_unlock_s.unwrap();
            if unlocked {
                for k in 0..self.robots.len() {
                    if self.robots[k].team == team && self.robots[k].launcher == "locked" {
                        self.robots[k].launcher = "unlocked".into();
                        self.event("launcher_unlocked", json!({"robot":self.robots[k].id,"team":team}));
                    }
                }
            }
        }
    }

    // ---- Buffs and zones --------------------------------------------------------------
    pub(super) fn update_buffs_2027(&mut self) {
        let now = self.time();
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            if r.role == "aerial" {
                self.robots[i].zone_contacts.clear();
                self.robots[i].last_raw_zones.clear();
                continue;
            }
            // Dead ground robots retain physical RFID contact for supply respawn speed,
            // but are excluded from card occupants and crossing rewards below.
            let cards: Vec<Zone> = self.arena.zones.iter()
                .filter(|z| self.card_contact_2027(i, z)).cloned().collect();
            let raw: Vec<String> = cards.iter().map(|z| z.id.clone()).collect();
            // Detecting any other card interrupts a pending crossing, including a card
            // belonging to another crossing group. Check before awarding this frame.
            let interrupted: Vec<String> = self.robots[i].crossings.keys()
                .filter(|key| !key.contains(':') && cards.iter().any(|z| z.group.as_deref() != Some(key.as_str())))
                .cloned().collect();
            for group in interrupted {
                for key in [group.clone(), format!("{group}:order"), format!("{group}:path")] {
                    self.robots[i].crossings.remove(&key);
                }
            }
            for zone in self.arena.zones.clone() {
                if raw.contains(&zone.id) {
                    self.robots[i].zone_contacts.insert(zone.id.clone(), now);
                    if r.alive && !r.weak && !r.last_raw_zones.contains(&zone.id) {
                        if let (Some(group), Some(order)) = (&zone.group, zone.order) {
                            if cards.iter().all(|z| z.group.as_deref() == Some(group.as_str())) {
                                let kind = zone.crossing.clone().unwrap_or(zone.kind.clone());
                                self.process_crossing_2027(i, group, order, &kind);
                            }
                        }
                        if zone.kind == "high_point" && zone.owner.as_deref() == Some(&r.team)
                            && !self.robots[i].crossing_firsts.contains(&"high_point".to_string())
                        {
                            self.robots[i].crossing_firsts.push("high_point".into());
                            self.gain_xp_2027(i, n(&self.rules["experience"], "terrain_first_xp"));
                        }
                    }
                }
            }
            self.robots[i].last_raw_zones = raw;
            let release = n(&self.rules["buffs"], "release_delay_s");
            self.robots[i].zone_contacts.retain(|_, time| now - *time <= release + 1e-8);
            let clears = self.contacted_zone_2027(i, "base", Some(&r.team))
                || self.contacted_zone_2027(i, "supply", Some(&r.team))
                || ["red", "blue"].iter().any(|o| self.fully_controls(&format!("{o}_outpost"), &r.team) && self.contacted_zone_2027(i, "outpost", Some(o)));
            if r.alive && r.weak && clears && r.weak_until.map(|until| now + 1e-8 >= until).unwrap_or(true) {
                let actor = &mut self.robots[i];
                actor.weak = false;
                actor.weak_until = None;
                // Respawn protection then ends, but not before 10 s after the respawn.
                let floor = (actor.respawned_at + n(&self.rules["respawn"], "weak_clear_invulnerability_min_s")).max(now);
                actor.invulnerable_until = actor.invulnerable_until.min(floor);
            }
        }
        // Card occupancy: central cards are exclusive between teams; fortress cards take one own robot.
        for z in self.arena.zones.clone() {
            let candidates: Vec<usize> = self.robots.iter().enumerate()
                .filter(|(_, r)| r.role != "aerial" && r.alive && !r.weak && r.zone_contacts.contains_key(&z.id))
                .map(|(i, _)| i).collect();
            let mut occupants: Vec<String> = Vec::new();
            let mut owner = z.owner.clone();
            if z.kind == "central" {
                let old = self.zones[&z.id].owner.clone().filter(|t| t != "neutral");
                let current = old.filter(|t| candidates.iter().any(|i| self.robots[*i].team == *t));
                owner = current.or_else(|| candidates.iter().min_by_key(|i| self.robots[**i].id.clone()).map(|i| self.robots[*i].team.clone()));
                for i in &candidates {
                    if Some(&self.robots[*i].team) == owner.as_ref() {
                        occupants.push(self.robots[*i].id.clone());
                    }
                }
                owner = owner.or(Some("neutral".into()));
            } else {
                for i in &candidates {
                    let r = &self.robots[*i];
                    let own = z.owner.as_deref() == Some(&r.team);
                    let permitted = match z.kind.as_str() {
                        "supply" | "high_point" | "resource" | "workbench" => own,
                        "assembly" => own && r.role == "heavy",
                        "base" | "outpost" => true,
                        "fortress" => {
                            let o = z.owner.as_deref().unwrap_or("red");
                            if own { self.fortress_active(o) } else {
                                self.enemy_fortress_allowed(&r.team, o) || self.enemy_fortress_hold_ongoing(*i, o)
                            }
                        }
                        _ => true,
                    };
                    if permitted {
                        occupants.push(r.id.clone());
                    }
                }
                if z.kind == "fortress" {
                    if let Some(o) = &z.owner {
                        let mut own: Vec<String> = occupants.iter().filter(|id| id.starts_with(&format!("{o}-"))).cloned().collect();
                        own.sort();
                        // The first own robot keeps the card while it stays on it.
                        let previous = self.zones[&z.id].occupants.iter().find(|id| own.contains(id)).cloned();
                        let keep = previous.or_else(|| own.first().cloned());
                        occupants.retain(|id| !id.starts_with(&format!("{o}-")) || Some(id) == keep.as_ref());
                    }
                }
            }
            let contested = candidates.iter().any(|i| self.robots[*i].team == "red") && candidates.iter().any(|i| self.robots[*i].team == "blue");
            let state = self.zones.get_mut(&z.id).unwrap();
            state.owner = owner;
            state.progress = if occupants.is_empty() { 0.0 } else { 1.0 };
            state.contested = contested;
            state.occupants = occupants;
        }
        self.update_positions();
        // Compose each robot's buffs: same kinds take the maximum (5.6.4.1).
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            let team = r.team.clone();
            let retention = n(&self.rules["buffs"]["fortress"], "enemy_timer_retention_s");
            if now - r.fortress_last_contact > retention + 1e-8 {
                self.robots[i].fortress_hold = 0.0;
            }
            let rune = self.runes[&team].clone();
            let mut defense = self.teams[&team].tech_defense;
            let (mut attack, mut cooling, mut extra, mut vuln) = (1.0_f64, 1.0_f64, 0.0_f64, 0.0_f64);
            if rune.status == "active" {
                defense = defense.max(rune.defense);
                attack = attack.max(rune.attack);
                cooling = cooling.max(rune.cooling);
            }
            if r.role == "heavy" && now < n(&self.rules["heavy"], "early_defense_until_s") && r.launcher != "installed" {
                defense = defense.max(n(&self.rules["heavy"], "early_defense"));
            }
            self.robots[i].crossing_buffs.retain(|_, until| *until > now + 1e-8);
            let held = self.robots[i].crossing_buffs.clone();
            if active_until(r.crossing_defense_until, now) {
                defense = defense.max(r.crossing_defense);
            }
            if held.contains_key("tunnel_through") {
                cooling = cooling.max(n(&self.rules["buffs"]["crossings"]["tunnel_through"], "cooling_multiplier"));
            }
            if let Some(p) = self.posture_effect(&r) {
                defense = defense.max(n(&p, "defense"));
                vuln = vuln.max(n(&p, "vulnerability"));
                if p["cooling_multiplier"].is_number() {
                    cooling = cooling.max(n(&p, "cooling_multiplier"));
                }
            }
            if r.alive && !r.weak && r.role != "aerial" {
                for zone in self.arena.zones.clone() {
                    if !self.zones[&zone.id].occupants.contains(&r.id) {
                        continue;
                    }
                    let own = zone.owner.as_deref() == Some(&team);
                    match zone.kind.as_str() {
                        "base" if own => defense = defense.max(n(&self.rules["buffs"], "base_defense")),
                        "high_point" if own => defense = defense.max(n(&self.rules["buffs"], "high_point_defense")),
                        "central" => defense = defense.max(n(&self.rules["buffs"], "central_defense")),
                        "assembly" => {
                            // Protection is positional now, so the bay only records how long a heavy
                            // spent working under it; nothing gates on this any more.
                            if r.role == "heavy" {
                                self.robots[i].assembly_invulnerable_spent += DT;
                            }
                        }
                        "fortress" => {
                            let o = zone.owner.clone().unwrap();
                            let f = &self.rules["buffs"]["fortress"];
                            if own {
                                defense = defense.max(n(f, "own_defense"));
                                let drop = (self.bases[&team].max_hp - self.teams[&team].base_lowest_hp.unwrap_or(self.bases[&team].hp)).max(0.0);
                                extra = extra.max((drop / 80.0).floor().min(75.0));
                                let cap = (100.0 + (drop / 15.0).floor()).min(500.0) as u32;
                                let t = self.teams.get_mut(&team).unwrap();
                                if cap > t.fortress_capacity {
                                    t.fortress_ammo += cap - t.fortress_capacity;
                                }
                                t.fortress_capacity = cap;
                                t.fortress_ammo = t.fortress_ammo.min(cap);
                            } else if !self.bases[&o].armor_open {
                                vuln = vuln.max(n(f, "enemy_vulnerability"));
                                let elapsed = (now - self.robots[i].fortress_last_contact).max(0.0);
                                if elapsed <= DT + 1e-8 {
                                    self.robots[i].fortress_hold += elapsed;
                                }
                                self.robots[i].fortress_last_contact = now;
                                if self.robots[i].fortress_hold >= n(f, "enemy_hold_to_open_base_s") - 1e-8 {
                                    self.bases.get_mut(&o).unwrap().armor_open = true;
                                    self.event("base_armor_opened", json!({"team":o,"robot":r.id,"reason":"enemy_fortress"}));
                                }
                            }
                        }
                        _ => {}
                    }
                }
            }
            let inv = self.robot_invulnerable_2027(i);
            let limit = if r.alive { self.power_limit_2027(i) } else { r.power_limit };
            let actor = &mut self.robots[i];
            actor.attack_multiplier = attack;
            actor.defense_buff = defense;
            actor.vulnerability = vuln;
            actor.cooling_multiplier = cooling;
            actor.cooling_bonus = extra;
            actor.invulnerable = inv;
            actor.power_limit = limit;
            actor.shield = if inv { (actor.invulnerable_until - now).max(0.05) } else { 0.0 };
        }
    }
    fn assembly_active_for(&self, team: &str) -> bool {
        ["moving", "inserting"].contains(&self.assemblies[team].stage.as_str())
    }
    pub(super) fn robot_invulnerable_2027(&self, i: usize) -> bool {
        let r = &self.robots[i];
        if !r.alive {
            return false;
        }
        if active_until(r.invulnerable_until, self.time()) {
            return true;
        }
        if r.role != "heavy" || r.weak {
            return false;
        }
        let supply = self.contacted_zone_2027(i, "supply", Some(&r.team));
        // The heavy works the assembly core from a fixed bay, so the bay itself protects it: standing
        // on the team's own assembly card is invulnerable whether or not a core is being worked and
        // whatever protection time has already been used, which is what a pilot expects when it
        // parks there to fit a launcher or a core.
        let assembly = self.contacted_zone_2027(i, "assembly", Some(&r.team));
        supply || assembly
    }
    fn process_crossing_2027(&mut self, i: usize, group: &str, order: u32, kind: &str) {
        let now = self.time();
        let order_key = format!("{group}:order");
        let path_key = format!("{group}:path");
        let window = n(&self.rules["buffs"]["crossings"][if kind == "tunnel" { "tunnel_over" } else { kind }], "window_s");
        let previous = self.robots[i].crossings.get(group).copied();
        let previous_order = self.robots[i].crossings.get(&order_key).copied().map(|o| o as u32);
        let path = self.robots[i].crossings.get(&path_key).copied().unwrap_or(0.0) as u32;
        let fresh = previous.map(|t| now - t <= window + 1e-8).unwrap_or(false);
        let mut completed: Option<String> = None;
        if kind == "tunnel" {
            // Tunnel cards: 0 and 2 are the ends, 1 lies under the roof (table 5-27).
            if order == 1 {
                if fresh && matches!(previous_order, Some(0) | Some(2)) {
                    self.robots[i].crossings.insert(path_key.clone(), 1.0);
                }
                return;
            }
            if fresh && previous_order.is_some() && previous_order != Some(order) && previous_order != Some(1) {
                completed = Some(if path == 1 { "tunnel_through".into() } else { "tunnel_over".into() });
            } else if fresh && previous_order == Some(1) {
                completed = Some("tunnel_through".into());
            }
            if completed.is_none() {
                self.robots[i].crossings.insert(group.into(), now);
                self.robots[i].crossings.insert(order_key, order as f64);
                self.robots[i].crossings.insert(path_key, 0.0);
                return;
            }
        } else {
            let complete = fresh && previous_order == Some(0) && order == 1;
            if !complete {
                if order == 0 {
                    self.robots[i].crossings.insert(group.into(), now);
                    self.robots[i].crossings.insert(order_key, 0.0);
                }
                return;
            }
            if kind == "road" && active_until(self.robots[i].road_cooldown_until, now) {
                return;
            }
            completed = Some(kind.into());
        }
        let buff = completed.unwrap();
        for key in [group.to_string(), order_key, path_key] {
            self.robots[i].crossings.remove(&key);
        }
        let v = self.rules["buffs"]["crossings"][&buff].clone();
        let duration = n(&v, "duration_s");
        // Holding a different crossing buff raises the defense to 50% (5.6.4.5).
        let other = self.robots[i].crossing_buffs.iter().any(|(k, until)| k != &buff && *until > now + 1e-8);
        self.robots[i].crossing_buffs.insert(buff.clone(), now + duration);
        let r = &mut self.robots[i];
        if v["defense"].is_number() || other {
            let remaining = (r.crossing_defense_until - now).max(0.0);
            let value = if other { n(&self.rules["buffs"], "different_crossing_defense") } else { n(&v, "defense").max(if active_until(r.crossing_defense_until, now) { r.crossing_defense } else { 0.0 }) };
            r.crossing_defense = value;
            r.crossing_defense_until = now + duration.max(remaining);
        }
        if buff == "road" {
            r.road_cooldown_until = now + n(&v, "cooldown_s");
        }
        let first_kind = if buff.starts_with("tunnel") { "tunnel".to_string() } else { buff.clone() };
        if !r.crossing_firsts.contains(&first_kind) {
            r.crossing_firsts.push(first_kind);
            self.gain_xp_2027(i, n(&self.rules["experience"], "terrain_first_xp"));
        }
        self.event("terrain_buff", json!({"robot":self.robots[i].id,"kind":buff}));
    }

    // ---- Firing, hits and damage ------------------------------------------------------
    fn heavy_42_enabled(&self, team: &str) -> bool {
        let now = self.time();
        self.robots.iter().filter(|r| r.team == team && r.role == "heavy").any(|h| {
            now - h.last_shot_time < n(&self.rules["damage"], "heavy_idle_42_mask_s") - 1e-9
                && (h.alive || now - h.death_time < n(&self.rules["damage"], "heavy_dead_42_mask_s") - 1e-9)
        })
    }
    pub(super) fn fire_2027(&mut self, actions: &[Action]) {
        let now = self.time();
        for i in 0..self.robots.len() {
            let r = self.robots[i].clone();
            let a = &actions[i];
            // Hard rule: the 42 mm may be bolted on early, but it only shoots once the team has
            // unlocked it (difficulty 3 or the 300 - M/50 - 2N/3 s timer).
            let launcher_ready = r.role != "heavy"
                || (["fitted", "installed"].contains(&r.launcher.as_str()) && self.launcher_unlocked(&r.team));
            let in_assembly = r.role == "heavy" && self.assembly_active_for(&r.team) && self.raw_zone(i, "assembly", Some(&r.team));
            if !a.fire || !r.alive || r.red_card || r.role == "aerial" || r.weak || !launcher_ready || in_assembly
                || r.overspeed_permanent_lock || active_until(r.overspeed_lock_until, now) || r.overheated || r.heat_permanent_lock
                || r.cooldown > 1e-8 || (r.transform_allowed && r.transform_progress > 1e-8 && r.transform_progress < 1.0 - 1e-8)
            {
                continue;
            }
            let heavy = is_42(&r.role);
            let fortress = format!("{}_fortress", r.team);
            let per_shot = if heavy { n(&self.rules["buffs"]["fortress"], "heavy_reserve_per_42mm") as u32 } else { 1 };
            let reserve = self.fortress_active(&r.team)
                && self.zones.get(&fortress).map(|z| z.occupants.contains(&r.id)).unwrap_or(false)
                && self.teams[&r.team].fortress_ammo >= per_shot;
            if r.ammo == 0 && !reserve {
                continue;
            }
            let design = self.robot_models[&r.design_slot().to_string()].clone();
            let spec = self.specs[&r.role].clone();
            let speed = design["muzzle_speed_mps"].as_f64().unwrap_or(spec.projectile_speed);
            let caliber = if heavy { "42mm" } else { "17mm" };
            let limit = n(&self.rules["projectiles"][caliber], "muzzle_limit_mps");
            let install_shot = r.launcher == "fitted";
            let penalty = if install_shot { None } else { self.overspeed_penalty_2027(heavy, speed, limit) };
            if reserve {
                self.teams.get_mut(&r.team).unwrap().fortress_ammo -= per_shot;
            } else {
                self.robots[i].ammo -= 1;
            }
            self.robots[i].cooldown = design["trigger_interval_s"].as_f64().filter(|v| *v > 0.0).unwrap_or(spec.cooldown);
            self.robots[i].last_shot_time = now;
            let no_heat = self.posture_effect(&r).map(|p| p["no_heat"] == true).unwrap_or(false);
            if !install_shot && !no_heat {
                self.robots[i].heat += spec.heat_per_shot;
                let q2 = r.max_heat + if heavy { 200.0 } else { 100.0 };
                if self.robots[i].heat >= q2 {
                    self.robots[i].heat_permanent_lock = true;
                    self.robots[i].overheated = true;
                } else if self.robots[i].heat > r.max_heat {
                    self.robots[i].overheated = true;
                    self.event("overheat", json!({"robot":r.id,"heat":self.robots[i].heat,"limit":r.max_heat}));
                }
            }
            if r.role == "infantry" {
                self.gain_xp_2027(i, n(&self.rules["experience"], "shot_xp_17"));
            }
            if install_shot {
                self.robots[i].launcher = "installed".into();
                self.apply_performance_2027(i, true);
                self.event("launcher_installed", json!({"robot":r.id,"team":r.team}));
            }
            let (origin, direction) = physics::muzzle(&r);
            let direction = self.dispersed_shot_direction_2027(direction, heavy);
            self.projectiles.push(Projectile {
                id: self.next_projectile,
                source: r.id.clone(),
                team: r.team.clone(),
                role: r.role.clone(),
                x: origin[0],
                y: origin[1],
                z: origin[2],
                vx: direction[0] * speed + r.vx,
                vy: direction[1] * speed + r.vy,
                vz: direction[2] * speed + r.vz,
                remaining: 40.0,
                damage: if install_shot { 0.0 } else { spec.damage * r.attack_multiplier },
                radius: if heavy { 0.02125 } else { 0.0084 },
                deployed: false,
            });
            self.next_projectile += 1;
            if let Some(seconds) = penalty {
                self.record_overspeed(i, speed, seconds);
            }
            let mass = if heavy { 0.0445 } else { 0.0032 };
            if let Some(world) = self.physics.as_mut() {
                world.recoil(i, rapier3d::na::Vector3::new(-direction[0] * speed * mass, -direction[1] * speed * mass, -direction[2] * speed * mass),
                             rapier3d::na::Point3::new(origin[0], origin[1], origin[2]));
            }
            let angle = direction[1].atan2(direction[0]);
            let pitch = direction[2].atan2(direction[0].hypot(direction[1]));
            self.event("shot", json!({"robot":r.id,"team":r.team,"role":r.role,"x":origin[0],"y":origin[1],"z":origin[2],"muzzle_origin":true,"angle":angle,"pitch":pitch}));
        }
    }
    fn dispersed_shot_direction_2027(&mut self, direction: [f64; 3], heavy: bool) -> [f64; 3] {
        let role = if heavy { "hero" } else { "infantry" };
        self.dispersed_shot_direction(direction, role)
    }
    /// Table 5-3 (2027): 17 mm by excess; 42 mm locks 15 s up to 18 m/s, beyond that permanently.
    fn overspeed_penalty_2027(&self, heavy: bool, speed: f64, limit: f64) -> Option<f64> {
        if speed <= limit {
            return None;
        }
        if heavy {
            return Some(if speed > 18.0 { f64::INFINITY } else { 15.0 });
        }
        let excess = speed - limit;
        Some(if excess < 5.0 { 15.0 } else if excess < 10.0 { 20.0 } else { f64::INFINITY })
    }
    pub(super) fn robot_hit_2027(&mut self, index: usize, p: &Projectile, x: f64, y: f64, z: f64) {
        let target = &self.robots[index];
        if !target.alive || target.role == "aerial" || self.robot_invulnerable_2027(index) || p.damage <= 0.0 {
            return;
        }
        let heavy = is_42(&p.role);
        if heavy && !self.heavy_42_enabled(&p.team) {
            return;
        }
        let face = physics::face_at_point(target, [x, y, z]);
        let normal = physics::armor_normal(target, face);
        let velocity = self.physics.as_ref().unwrap().velocity_at_point(index, [x, y, z]);
        let normal_speed = -((p.vx - velocity.x) * normal.x + (p.vy - velocity.y) * normal.y + (p.vz - velocity.z) * normal.z);
        let d = &self.rules["damage"];
        if normal_speed <= n(d, if heavy { "normal_speed_min_42_mps" } else { "normal_speed_min_17_mps" }) {
            return;
        }
        let interval = n(d, if heavy { "armor_interval_42_s" } else { "armor_interval_17_s" });
        if self.time() - target.armor_hit_times[face] < interval - 1e-8 {
            return;
        }
        let amount = (p.damage * (1.0 - target.defense_buff + target.vulnerability)).round().max(0.0).min(target.hp);
        if amount <= 0.0 {
            return;
        }
        let source = self.robots.iter().position(|r| r.id == p.source);
        let victim_level = self.xp_level(index);
        let attacker = opponent(&target.team).to_string();
        let caliber = Some(if heavy { "42mm" } else { "17mm" });
        let now = self.time();
        let r = &mut self.robots[index];
        r.hp -= amount;
        r.last_damage_time = now;
        r.armor_hit_times[face] = now;
        self.record_damage_2027(source, &attacker, amount, n(&self.rules["experience"], "robot_damage_xp_per_hp"), caliber);
        // `source` names the shooter so the post-match reel can credit a burst to one chassis;
        // `robot` stays the victim, which is what the broadcast text reads.
        self.event("hit", json!({"robot":self.robots[index].id,"source":p.source,"team":p.team,"x":x,"y":y,"z":z,"damage":amount}));
        if self.robots[index].hp <= 0.0 {
            let capacity = self.buffer_capacity_2027();
            // Rulebook p.74: the bar is 10 + (420 − remaining)/10 + 20 × paid revives, rounded.
            let respawn_required = self.revive_bar_2027(index).round();
            let r = &mut self.robots[index];
            r.alive = false;
            r.weak_until = None;
            r.death_time = now;
            // Death clears the shot heat and hands the chassis a full buffer.
            r.heat = 0.0;
            r.buffer_energy_j = capacity;
            r.vx = 0.0;
            r.vy = 0.0;
            r.crossing_buffs.clear();
            r.crossing_defense_until = 0.0;
            r.pending_heal = None;
            r.respawn_progress = 0.0;
            r.respawn_required = respawn_required;
            if r.unit_pickup.take().is_some() {
                self.event("unit_pickup_cancelled", json!({"robot":self.robots[index].id}));
            }
            if let Some(source) = source.filter(|i| self.robots[*i].team == attacker) {
                self.robots[source].kills += 1;
            }
            self.teams.get_mut(&attacker).unwrap().kills += 1;
            self.award_kill_xp_2027(source, &attacker, victim_level, caliber);
            if self.robots[index].role == "heavy" {
                self.heavy_down(index);
            }
            self.event("destroy", json!({"robot":self.robots[index].id,"killer":p.source,"team":p.team,"x":x,"y":y,"z":z}));
        }
    }
    fn record_damage_2027(&mut self, source: Option<usize>, team: &str, amount: f64, xp_rate: f64, caliber: Option<&str>) {
        if let Some(i) = source.filter(|i| self.robots[*i].team == team) {
            self.robots[i].damage += amount;
        }
        let t = self.teams.get_mut(team).unwrap();
        t.attack_damage += amount;
        t.score = t.attack_damage;
        self.award_damage_xp_2027(source, team, amount * xp_rate, caliber);
    }
    /// Base 100% defense while its outpost position is fully held by its own team; team
    /// assembly and rune defense apply to both structures.
    pub(super) fn structure_defense(&self, team: &str, outpost: bool) -> f64 {
        let holder = if outpost { self.outposts[team].owner.as_deref().unwrap_or(team) } else { team };
        let rune = &self.runes[holder];
        let mut defense = self.teams[holder].tech_defense.max(if rune.status == "active" { rune.defense } else { 0.0 });
        if !outpost && self.fully_controls(&format!("{team}_outpost"), team) {
            defense = defense.max(n(&self.rules["structures"], "base_defense_while_own_outpost_position_fully_controlled"));
        }
        defense
    }
    /// The structure's armor modules in 2027: the base's three upper modules plus, once
    /// opened, the three large ones; the outpost's three rotating middle modules.
    pub(super) fn structure_plates_2027(b: &Base, outpost: bool) -> Vec<structures::Plate> {
        structures::plates(b, outpost).into_iter().filter(|p| !(outpost && p.face >= 3)).collect()
    }
    pub(super) fn structure_hit_2027(&mut self, team: &str, outpost: bool, p: &Projectile, x: f64, y: f64, z: f64) {
        let heavy = is_42(&p.role);
        if p.damage <= 0.0 || (heavy && !self.heavy_42_enabled(&p.team)) {
            return;
        }
        let b = if outpost { &self.outposts[team] } else { &self.bases[team] };
        if b.hp <= 0.0 {
            return;
        }
        // Rebuilt outposts belong to their holder: own shots do not damage it.
        if outpost && b.owner.as_deref() == Some(p.team.as_str()) {
            return;
        }
        let plate = Self::structure_plates_2027(b, outpost).into_iter().find(|plate| {
            let (u, v, depth) = structures::coordinates(plate, [x, y, z]);
            depth.abs() < 0.003 && u.abs() <= plate.width / 2.0 && v.abs() <= plate.height / 2.0
        });
        let Some(plate) = plate else { return };
        let d = &self.rules["damage"];
        let normal_speed = -(p.vx * plate.normal[0] + p.vy * plate.normal[1] + p.vz * plate.normal[2]);
        if normal_speed <= n(d, if heavy { "normal_speed_min_42_mps" } else { "normal_speed_min_17_mps" }) {
            return;
        }
        let interval = n(d, if heavy { "armor_interval_42_s" } else { "armor_interval_17_s" });
        if self.time() - b.armor_hit_times[plate.face] < interval - 1e-8 {
            return;
        }
        let damage = (p.damage * (1.0 - self.structure_defense(team, outpost))).round().max(0.0);
        let source = self.robots.iter().position(|r| r.id == p.source);
        let attacker = p.team.clone();
        let actual = self.damage_structure_2027(team, outpost, plate.face, damage, &attacker);
        if actual <= 0.0 {
            return;
        }
        let rate = n(&self.rules["experience"], if outpost { "outpost_damage_xp_per_hp" } else { "base_damage_xp_per_hp" });
        if let Some(i) = source {
            self.robots[i].damage += actual;
        }
        let xp = if outpost { actual * rate } else { (actual * rate).ceil() };
        self.award_damage_xp_2027(source, &attacker, xp, Some(if heavy { "42mm" } else { "17mm" }));
        self.event(if outpost { "outpost_hit" } else { "base_hit" }, json!({"team":p.team,"robot":p.source,"target_team":team,"x":x,"y":y,"z":z,"damage":actual}));
    }
    /// Applies structure damage and its consequences; returns the HP actually removed.
    fn damage_structure_2027(&mut self, team: &str, outpost: bool, face: usize, damage: f64, attacker: &str) -> f64 {
        let now = self.time();
        let b = if outpost { self.outposts.get_mut(team).unwrap() } else { self.bases.get_mut(team).unwrap() };
        let taken = damage.min(b.hp);
        b.armor_hit_times[face] = now;
        if taken <= 0.0 {
            return 0.0;
        }
        b.hp -= taken;
        let destroyed = b.hp <= 0.0;
        if destroyed {
            b.ever_destroyed = true;
        }
        let t = self.teams.get_mut(attacker).unwrap();
        t.attack_damage += taken;
        t.score = t.attack_damage;
        if outpost && destroyed {
            self.event("outpost_destroyed", json!({"team":team,"by":attacker}));
            self.outpost_destroyed(team);
        }
        taken
    }

    // ---- Aerial system ----------------------------------------------------------------
    /// Drone strikes on enemy base and outpost armor (contact thresholds are simulation choices).
    pub(super) fn drone_strikes(&mut self) {
        let now = self.time();
        let cfg = self.sim["drone_impact"].clone();
        let (min_speed, min_impulse, interval, restitution, recovery) = (
            n(&cfg, "min_normal_speed_mps"), n(&cfg, "min_impulse_n_s"), n(&cfg, "plate_interval_s"), n(&cfg, "restitution"), n(&cfg, "recovery_s"));
        for i in 0..self.robots.len() {
            if self.robots[i].role != "aerial" || self.robots[i].drones.is_empty() {
                continue;
            }
            let team = self.robots[i].team.clone();
            let enemy = opponent(&team).to_string();
            let design = self.robot_models[&self.robots[i].design_slot().to_string()].clone();
            let mass = n(&design["drone"], "mass_kg");
            let radius = n(&design["drone"], "radius_m");
            for k in 0..self.robots[i].drones.len() {
                let dr = self.robots[i].drones[k].clone();
                if dr.state != "flying" {
                    continue;
                }
                let mut targets: Vec<(bool, String)> = vec![(false, enemy.clone())];
                for owner in ["red", "blue"] {
                    let o = &self.outposts[owner];
                    if o.hp > 0.0 && o.owner.as_deref() != Some(team.as_str()) {
                        targets.push((true, owner.to_string()));
                    }
                }
                for (outpost, owner) in targets {
                    let b = if outpost { self.outposts[&owner].clone() } else { self.bases[&owner].clone() };
                    if b.hp <= 0.0 {
                        continue;
                    }
                    for plate in Self::structure_plates_2027(&b, outpost) {
                        let (u, v, depth) = structures::coordinates(&plate, [dr.x, dr.y, dr.z]);
                        if !(depth > -0.02 && depth <= radius + 0.015 && u.abs() <= plate.width / 2.0 + radius * 0.5 && v.abs() <= plate.height / 2.0 + radius * 0.5) {
                            continue;
                        }
                        // The rotating outpost plate moves at omega x r.
                        let spin = if outpost && !b.rotation_stopped { 0.8 * std::f64::consts::PI * (now / 5.0).min(1.0) * self.rune_direction } else { 0.0 };
                        let (px, py) = (plate.center[0] - b.x, plate.center[1] - b.y);
                        let plate_v = [-spin * py, spin * px, 0.0];
                        let rel = [dr.vx - plate_v[0], dr.vy - plate_v[1], dr.vz - plate_v[2]];
                        let approach = -(rel[0] * plate.normal[0] + rel[1] * plate.normal[1] + rel[2] * plate.normal[2]);
                        if approach < min_speed || mass * approach * (1.0 + restitution) < min_impulse {
                            continue;
                        }
                        let fresh = now - b.armor_hit_times[plate.face] >= interval - 1e-8 && now - dr.last_strike >= interval - 1e-8;
                        let attack = if self.runes[&team].status == "active" { self.runes[&team].attack } else { 1.0 };
                        let damage = (n(&self.rules["damage"], "aerial_impact") * attack * (1.0 - self.structure_defense(&owner, outpost))).round();
                        let actual = if fresh { self.damage_structure_2027(&owner, outpost, plate.face, damage, &team) } else { 0.0 };
                        let d = &mut self.robots[i].drones[k];
                        // Bounce off the plate and wear the guard.
                        let vn = rel[0] * plate.normal[0] + rel[1] * plate.normal[1] + rel[2] * plate.normal[2];
                        d.vx -= (1.0 + restitution) * vn * plate.normal[0];
                        d.vy -= (1.0 + restitution) * vn * plate.normal[1];
                        d.vz -= (1.0 + restitution) * vn * plate.normal[2];
                        d.recover_until = now + recovery;
                        d.last_strike = now;
                        if fresh {
                            d.strikes += 1;
                            d.integrity -= 1.0;
                            if d.integrity <= 1e-9 {
                                d.state = "disabled".into();
                            }
                        }
                        if let Some(world) = self.physics.as_mut() {
                            world.set_drone_velocity(i, k, [d.vx, d.vy, d.vz]);
                        }
                        let id = d.id.clone();
                        if actual > 0.0 {
                            self.robots[i].damage += actual;
                            let rate = n(&self.rules["experience"], if outpost { "outpost_damage_xp_per_hp" } else { "base_damage_xp_per_hp" });
                            let xp = if outpost { actual * rate } else { (actual * rate).ceil() };
                            self.award_damage_xp_2027(None, &team, xp, None);
                            let hangar = self.robots[i].id.clone();
                            // A drone strike belongs to the hangar that launched it: `robot` lets the
                            // reel credit the aerial chassis, `drone` lets the camera ride that drone.
                            self.event(if outpost { "outpost_hit" } else { "base_hit" }, json!({"team":team,"robot":hangar,"target_team":owner,"x":dr.x,"y":dr.y,"z":dr.z,"damage":actual,"drone":id}));
                        }
                        self.event("drone_strike", json!({"team":team,"drone":id,"target":if outpost {"outpost"} else {"base"},"target_team":owner,"valid":fresh,"damage":actual}));
                        break;
                    }
                }
            }
        }
    }
    /// A projectile striking a flying drone: momentum transfer and guard wear.
    pub(super) fn drone_hit(&mut self, robot: usize, drone: usize, p: &Projectile) {
        let heavy = is_42(&p.role);
        let mass = if heavy { 0.0445 } else { 0.0032 };
        let d = &mut self.robots[robot].drones[drone];
        if d.state != "flying" && d.state != "down" {
            return;
        }
        let design_mass = self.robot_models[&self.robots[robot].design_slot().to_string()]["drone"]["mass_kg"].as_f64().unwrap_or(0.15);
        let d = &mut self.robots[robot].drones[drone];
        d.vx += p.vx * mass / design_mass;
        d.vy += p.vy * mass / design_mass;
        d.vz += p.vz * mass / design_mass;
        if heavy && self.sim["drone_projectile_hits"]["42mm_knocks_down"] == true {
            d.state = "disabled".into();
            d.integrity = 0.0;
        } else {
            d.integrity -= n(&self.sim["drone_projectile_hits"], "17mm_integrity_wear");
            if d.integrity <= 1e-9 {
                d.state = "disabled".into();
            }
        }
        let (v, id, state) = ([d.vx, d.vy, d.vz], d.id.clone(), d.state.clone());
        if let Some(world) = self.physics.as_mut() {
            world.set_drone_velocity(robot, drone, v);
        }
        self.event("drone_hit", json!({"drone":id,"team":p.team,"by":p.source,"state":state}));
    }

    // ---- Assembly (5.4.3) -------------------------------------------------------------
    fn heavy_down(&mut self, i: usize) {
        let team = self.robots[i].team.clone();
        if self.assembly_active_for(&team) {
            self.fail_assembly_2027(&team, "heavy_destroyed");
        }
    }
    /// One answer per second per (team, difficulty, reason): the cockpit repeats an order it has not
    /// seen acknowledged, so an unthrottled reply would flood the event feed while the reason stands.
    fn refuse_assembly_2027(&mut self, team: &str, difficulty: u32, reason: &str, now: f64) {
        let key = format!("{difficulty}:{reason}");
        if let Some((previous, at)) = self.assembly_refused_at.get(team) {
            if previous == &key && now - at < 1.0 {
                return;
            }
        }
        self.assembly_refused_at.insert(team.to_string(), (key, now));
        self.event(
            "assembly_refused",
            json!({"team":team,"difficulty":difficulty,"reason":reason,"at":now}),
        );
    }

    pub(super) fn assembly_rpc_2027(&mut self, i: usize, rpc: &Value) {
        let r = self.robots[i].clone();
        if r.role != "heavy" {
            return;
        }
        let team = r.team.clone();
        let now = self.time();
        let in_zone = self.raw_zone(i, "assembly", Some(&team));
        if in_zone {
            self.assemblies.get_mut(&team).unwrap().buffer_until = now;
        }
        // A level order is read only on the ticks the rulebook runs, and the cockpit retries it until
        // the engine answers. Every refusal therefore has to answer: a silently dropped order is
        // indistinguishable from a dead button, which is exactly how the panel used to feel.
        let ordered = rpc.get("assembly").and_then(Value::as_u64).filter(|d| (1..=4).contains(d)).map(|d| d as u32);
        if !r.alive || r.weak || !in_zone {
            self.assemblies.get_mut(&team).unwrap().confirm_progress_s = 0.0;
            if let Some(d) = ordered {
                let reason = if !r.alive { "destroyed" } else if r.weak { "penalised" } else { "outside_zone" };
                self.refuse_assembly_2027(&team, d, reason, now);
            }
            return;
        }
        if let Some(d) = ordered {
            let d = d as u32;
            let s = self.assemblies[&team].clone();
            let completed = self.teams[&team].completed_difficulties.clone();
            // Rule 88: a unit already in the socket latches the grade — no switching until the core
            // has assembled or salvaged it. A finished assembly also leaves the core re-initialising.
            let core_free = s.core_units == 0;
            let reinit_ok = now + 1e-8 >= s.repeat_ready_at;
            let eligible = core_free && reinit_ok && ["idle", "complete", "failed"].contains(&s.stage.as_str());
            let unlocks = self.rules["assembly"]["unlock_times_s"].as_array().unwrap();
            let time_ok = now + 1e-8 >= unlocks[(d - 1) as usize].as_f64().unwrap();
            let prev_ok = d == 1 || completed.contains(&(d - 1));
            let once_ok = d != 4 || !completed.contains(&4);
            let stock = r.units_on_board() >= if d == 4 { 2 } else { 1 };
            let other = &self.assemblies[opponent(&team)];
            let other_active = ["moving", "inserting"].contains(&other.stage.as_str());
            // Level 4 needs the other core: never while the other team is assembling, and a
            // level-4 attempt in progress bars the other team from starting at all.
            let priority_ok = !(other_active && (d == 4 || other.difficulty == 4));
            if eligible && time_ok && prev_ok && once_ok && stock && priority_ok {
                let attempt = s.attempt_by_difficulty.get(&d).copied().unwrap_or(0) + 1;
                let generation = s.successes_by_difficulty.get(&d).copied().unwrap_or(0) + 1;
                let mut rng = (self.seed as u64) ^ (generation as u64 * 0x9e3779b9) ^ (d as u64 * 0xbf58476d) ^ 0x2027;
                let mut sample = |low: f64, high: f64| {
                    rng ^= rng << 13;
                    rng ^= rng >> 7;
                    rng ^= rng << 17;
                    low + (high - low) * (rng >> 11) as f64 / (1u64 << 53) as f64
                };
                let ranges = &self.rules["assembly"]["pose_ranges"][if d == 1 { "1" } else { "2-4" }];
                let mut pose = [0.0; 6];
                for (k, key) in ["x", "y", "z", "theta", "phi", "alpha"].iter().enumerate() {
                    let (lo, hi) = (ranges[key][0].as_f64().unwrap(), ranges[key][1].as_f64().unwrap());
                    pose[k] = sample(lo, hi);
                }
                let range = n(&self.rules["assembly"], "rotation_range_deg");
                let target = if d >= 3 { sample(-range, range) } else { 0.0 };
                let target = (target / 5.0).round() * 5.0;
                let second_target = (sample(-range, range) / 5.0).round() * 5.0;
                let state = self.assemblies.get_mut(&team).unwrap();
                state.difficulty = d;
                state.stage = "moving".into();
                state.ready_at = now + n(&self.sim, "core_move_delay_s");
                state.started = state.ready_at;
                state.inserted = 0.0;
                state.translated = 0.0;
                state.rotation = 0.0;
                state.target_rotation = target;
                state.inserted_units = 0;
                state.first_unit_at = 0.0;
                state.attempt = attempt;
                state.attempt_by_difficulty.insert(d, attempt);
                state.pose = pose;
                state.interrupted = false;
                state.confirm_progress_s = 0.0;
                state.retry_at = 0.0;
                state.lifted = 0.0;
                state.flipped = 0.0;
                state.confirmed = false;
                state.second = if d == 4 { Some(CoreProgress::new(second_target)) } else { None };
                state.step_times = Default::default();
                state.buffer_until = now;
                state.fail_reason = None;
                self.event("assembly_start", json!({"team":team,"difficulty":d,"pose":pose}));
            } else {
                let reason = if !core_free {
                    "core_loaded"
                } else if !eligible {
                    "busy"
                } else if !time_ok {
                    "locked"
                } else if !prev_ok {
                    "previous_level"
                } else if !once_ok {
                    "once"
                } else if !stock {
                    "no_unit"
                } else {
                    "priority"
                };
                self.refuse_assembly_2027(&team, d, reason, now);
            }
        }
        // The operator acknowledging a failed attempt is what lets the core salvage the unit now.
        if rpc.get("assembly_cancel").and_then(Value::as_bool) == Some(true) {
            let s = self.assemblies.get_mut(&team).unwrap();
            if s.stage == "failed" && s.core_units > 0 {
                s.recover_at = Some(now);
            }
            return;
        }
        // Rule 89: pulling the unit back out of the socket fails the attempt immediately; the arm
        // has to be on the unit (the 10 N axial pull is far inside what the jaws can take).
        if rpc.get("assembly_extract").and_then(Value::as_bool) == Some(true) {
            self.extract_core_unit_2027(i);
            return;
        }
        let state = self.assemblies[&team].clone();
        if state.stage != "inserting" || now < state.ready_at || self.assembly_motion_blocked(i) {
            if self.assembly_motion_blocked(i) {
                self.assemblies.get_mut(&team).unwrap().confirm_progress_s = 0.0;
            }
            return;
        }
        if self.assembly_pair_expired_2027(&state, now) {
            self.fail_assembly_2027(&team, "pair_window");
            return;
        }
        // Which core the arm works on: 0 = own, 1 = the borrowed core of a level-4 attempt,
        // 2 = both (a dual-arm heavy robot, one arm per core).
        let choice = if state.difficulty == 4 { rpc.get("assembly_core").and_then(Value::as_u64).unwrap_or(0) } else { 0 };
        let cores: Vec<usize> = if choice >= 2 && r.arm_count == 2 { vec![0, 1] } else { vec![choice.min(1) as usize] };
        for core in cores {
            if !self.assembly_core_step(i, core, rpc) {
                return;
            }
        }
    }
    /// One settlement of step commands on one core; false when the attempt ended.
    fn extract_core_unit_2027(&mut self, i: usize) {
        let r = self.robots[i].clone();
        let team = r.team.clone();
        if self.assemblies[&team].core_units == 0 || self.assembly_alignment_error_2027(i, 0).is_some() {
            return;
        }
        self.assemblies.get_mut(&team).unwrap().core_units -= 1;
        if r.carried_units < 2 {
            self.robots[i].carried_units += 1;
        } else if let Some(left) = self.teams.get_mut(&team).unwrap().energy_units_left.as_mut() {
            *left += 1;
        }
        self.fail_assembly_2027(&team, "unit_extracted");
        self.event("assembly_unit_extracted", json!({"team":team,"robot":r.id}));
    }
    fn assembly_core_step(&mut self, i: usize, core: usize, rpc: &Value) -> bool {
        let r = self.robots[i].clone();
        let team = r.team.clone();
        let now = self.time();
        let state = self.assemblies[&team].clone();
        let profile = self.assembly_difficulty(state.difficulty).clone();
        let tolerance = n(&profile, "rotation_tolerance_deg").max(n(&self.rules["assembly"], "rotation_tolerance_deg").min(5.0));
        let depth = n(&self.rules["assembly"], "insert_depth_m");
        let lift = n(&self.rules["assembly"], "lift_m");
        let flip = n(&self.rules["assembly"], "flip_deg");
        let steps = self.rules["assembly"]["steps"][state.difficulty.to_string()].clone();
        let needs = |k: u64| steps.as_array().map(|v| v.iter().any(|x| x.as_u64() == Some(k))).unwrap_or(false);
        let mut c = state.core(core);
        // Each command must be backed by the actual tool at the current unit pose.
        // The IK controller follows progress; commands cannot move a virtual unit ahead of it.
        if self.assembly_alignment_error_2027(i, core).is_some() {
            c.confirm = 0.0;
            self.assemblies.get_mut(&team).unwrap().set_core(core, c);
            if rpc.get("assembly_confirm").and_then(Value::as_bool) == Some(true) {
                self.assemblies.get_mut(&team).unwrap().retry_at = now + n(&self.rules["assembly"], "confirm_retry_lock_s");
            }
            return true;
        }
        let mut done_step: Option<usize> = None;
        if c.inserted < depth - 1e-8 {
            if r.units_on_board() == 0 && c.inserted <= 1e-9 {
                return true;
            }
            if rpc.get("assembly_insert").and_then(Value::as_bool) == Some(true) {
                if c.inserted <= 1e-9 {
                    // The unit leaves the robot's storage for the core's pillar. A unit clamped in
                    // the gripper is already at the core, so it goes in before the stowed bays.
                    if r.gripper_unit() {
                        self.robots[i].unit_pickup = None;
                    } else {
                        self.robots[i].carried_units -= 1;
                    }
                    // The core now holds a unit: the grade is latched until this attempt resolves.
                    let s = self.assemblies.get_mut(&team).unwrap();
                    s.core_units += 1;
                    s.fail_reason = None;
                    s.recover_at = None;
                }
                c.inserted = (c.inserted + n(&self.sim, "arm_insert_speed_mps") * 0.1).min(depth);
                if c.inserted >= depth - 1e-8 {
                    done_step = Some(0);
                }
            }
        } else if needs(3) && c.lifted < lift - 1e-8 {
            if rpc.get("assembly_lift").or(rpc.get("assembly_translate")).and_then(Value::as_bool) == Some(true) {
                c.lifted = (c.lifted + n(&self.sim, "arm_lift_speed_mps") * 0.1).min(lift);
                if c.lifted >= lift - 1e-8 {
                    done_step = Some(1);
                }
            }
        } else if needs(4) && c.flipped < flip - 1e-8 {
            if rpc.get("assembly_flip").and_then(Value::as_bool) == Some(true) {
                c.flipped = (c.flipped + n(&self.sim, "arm_flip_speed_deg_s") * 0.1).min(flip);
            }
        } else if needs(5) && (c.rotation - c.target_rotation).abs() > tolerance + 1e-8 {
            let angle = if core == 1 && r.arm_count == 2 {
                rpc.get("assembly_rotate2").or_else(|| rpc.get("assembly_rotate"))
            } else { rpc.get("assembly_rotate") };
            if let Some(angle) = angle.and_then(Value::as_f64).filter(|a| a.is_finite()) {
                let range = n(&self.rules["assembly"], "rotation_range_deg");
                c.rotation = approach(c.rotation, angle.clamp(-range, range), n(&self.sim, "arm_rotate_speed_deg_s") * 0.1);
            }
        } else if !c.confirmed {
            let aligned = self.assembly_alignment_error_2027(i, core).is_none();
            if aligned {
                c.confirm += 0.1;
                let hold = if needs(5) { n(&profile, "confirm_hold_s") } else { 0.0 };
                if needs(5) && c.confirm >= hold - 1e-8 && c.step_times[3].is_none() {
                    c.step_times[3] = Some(now);
                }
                if rpc.get("assembly_confirm").and_then(Value::as_bool) == Some(true) && now + 1e-8 >= state.retry_at {
                    if c.confirm >= hold - 1e-8 {
                    c.confirmed = true;
                    done_step = Some(4);
                    } else {
                        self.assemblies.get_mut(&team).unwrap().retry_at = now + n(&self.rules["assembly"], "confirm_retry_lock_s");
                    }
                }
            } else {
                c.confirm = 0.0;
            }
        }
        if let Some(step) = done_step {
            c.step_times[step] = Some(now);
        }
        let s = self.assemblies.get_mut(&team).unwrap();
        s.set_core(core, c.clone());
        // Level 4: each paired step must finish on both cores within 5 s (steps 1 and 4 exempt).
        if s.difficulty == 4 {
            let a = s.core(0);
            let b = s.core(1);
            if self.assembly_pair_expired_2027(&self.assemblies[&team], now) {
                self.fail_assembly_2027(&team, "pair_window");
                return false;
            }
            if a.confirmed && b.confirmed {
                self.complete_assembly_2027(&team, i, 4);
                return false;
            }
        } else if c.confirmed {
            self.complete_assembly_2027(&team, i, state.difficulty);
            return false;
        }
        true
    }
    fn assembly_pair_expired_2027(&self, s: &rulebook::AssemblyState, now: f64) -> bool {
        if s.difficulty != 4 { return false; }
        let window = n(&self.rules["assembly"], "difficulty_4_pair_window_s");
        let (a, b) = (s.core(0), s.core(1));
        [0usize, 1, 3, 4].iter().any(|&step| match (a.step_times[step], b.step_times[step]) {
            (Some(x), Some(y)) => (x - y).abs() > window + 1e-8,
            (Some(x), None) | (None, Some(x)) => now - x > window + 1e-8,
            _ => false,
        })
    }
    /// Public assembly state in 2027: the step each core is on, with what blocks it.
    pub(super) fn assembly_snapshot_2027(&self, team: &str, out: &mut Value) {
        let s = &self.assemblies[team];
        let a = &self.rules["assembly"];
        let profile = self.assembly_difficulty(s.difficulty).clone();
        let tolerance = n(&profile, "rotation_tolerance_deg").max(n(a, "rotation_tolerance_deg").min(5.0));
        let steps = a["steps"][s.difficulty.to_string()].clone();
        let needs = |k: u64| steps.as_array().map(|v| v.iter().any(|x| x.as_u64() == Some(k))).unwrap_or(false);
        let active = ["moving", "inserting"].contains(&s.stage.as_str());
        let heavy = self.robots.iter().enumerate().find(|(i, r)| {
            r.team == team && r.role == "heavy" && r.alive && !r.weak && self.raw_zone(*i, "assembly", Some(team))
        });
        let count = if s.difficulty == 4 { 2 } else { 1 };
        let mut cores = Vec::new();
        let mut first_open: Option<(String, f64)> = None;
        let mut done_steps = 0.0;
        let mut total_steps = 0.0;
        for core in 0..count {
            let c = s.core(core);
            let stages = [
                ("inserting", true, c.inserted / n(a, "insert_depth_m")),
                ("lifting", needs(3), c.lifted / n(a, "lift_m")),
                ("flipping", needs(4), c.flipped / n(a, "flip_deg")),
                ("rotating", needs(5), if (c.rotation - c.target_rotation).abs() <= tolerance + 1e-8 { 1.0 } else { 0.0 }),
                ("verifying", true, if c.confirmed { 1.0 } else { c.confirm / n(&profile, "confirm_hold_s").max(1e-9) }),
            ];
            let mut operation = ("complete".to_string(), 1.0);
            for (name, required, value) in stages {
                if !required {
                    continue;
                }
                total_steps += 1.0;
                let value = value.clamp(0.0, 1.0);
                if value >= 1.0 - 1e-8 {
                    done_steps += 1.0;
                } else if operation.0 == "complete" {
                    operation = (name.to_string(), value);
                    done_steps += value;
                }
            }
            if first_open.is_none() && operation.0 != "complete" {
                first_open = Some(operation.clone());
            }
            let alignment = heavy.and_then(|(i, _)| self.assembly_alignment_metrics_2027(i, core))
                .map(|(position, direction, _)| json!({"position_error_m":position,"direction_error_deg":direction}));
            cores.push(json!({"core":core,"operation":operation.0,"operation_progress":operation.1,
                "rotation":c.rotation,"target_rotation":c.target_rotation,"confirmed":c.confirmed,"alignment":alignment}));
        }
        let (operation, progress) = match s.stage.as_str() {
            "moving" => ("positioning".to_string(), (1.0 - (s.ready_at - self.time()) / n(&self.sim, "core_move_delay_s").max(DT)).clamp(0.0, 1.0)),
            _ if active => first_open.clone().unwrap_or(("verifying".into(), 1.0)),
            "complete" => ("complete".into(), 1.0),
            "failed" => ("failed".into(), 0.0),
            _ => ("idle".into(), 0.0),
        };
        let blocked = if active {
            match heavy {
                None => Some("heavy_absent"),
                Some((i, _)) if self.assembly_motion_blocked(i) => Some("chassis_unstable"),
                Some((i, _)) => (0..count).find_map(|core| {
                    let c = s.core(core);
                    if c.confirmed { None } else { self.assembly_alignment_error_2027(i, core) }
                }),
            }
        } else {
            None
        };
        out["cores"] = json!(cores);
        out["operation"] = json!(operation);
        out["operation_progress"] = json!(progress);
        out["progress"] = json!(if s.stage == "complete" { 1.0 } else if active && total_steps > 0.0 { (done_steps / total_steps).clamp(0.0, 1.0) } else { 0.0 });
        out["blocked_reason"] = json!(blocked);
        out["core_units"] = json!(s.core_units);
        out["recover_in_s"] = json!(s.recover_at.map(|at| (at - self.time()).max(0.0)));
        out["difficulty_locked"] = json!(s.core_units > 0);
        out["repeat_cooldown_s"] = json!((s.repeat_ready_at - self.time()).max(0.0));
        out["requirements"] = profile.clone();
        out["steps"] = steps.clone();
        out["alignment"] = cores.first().map(|c| c["alignment"].clone()).unwrap_or(Value::Null);
        out["attempt_remaining_s"] = if s.stage == "inserting" && s.difficulty == 4 {
            let limit = n(a, "difficulty_4_time_limit_s");
            json!((s.started + limit - self.time()).max(0.0))
        } else {
            Value::Null
        };
    }
    /// Whether the tool still holds the core. The tool's *line* is published but not gated: on a
    /// tilted core the wrist saturates while levering the unit up, and gating on it deadlocked grade 3
    /// at the flip — the step that would have moved the goal waited for an alignment the arm could
    /// never reach. Position and the unit's roll still have to match, which is what "still gripping the
    /// unit in the socket" means.
    fn assembly_alignment_error_2027(&self, i: usize, core: usize) -> Option<&'static str> {
        let r = &self.robots[i];
        let profile = self.assembly_difficulty(self.assemblies[&r.team].difficulty);
        let Some((position, _direction, roll)) = self.assembly_alignment_metrics_2027(i, core) else {
            return Some("arm_out_of_reach");
        };
        if position > n(profile, "tip_tolerance_m") {
            return Some("arm_out_of_reach");
        }
        if roll > n(profile, "tool_tolerance_deg") {
            return Some("arm_misaligned");
        }
        None
    }
    /// Expected tool point and direction on a core for its current step progress.
    pub(super) fn core_goal_2027(&self, team: &str, core: usize) -> Option<([f64; 3], [f64; 3], f64)> {
        let (entry, normal, _slide) = self.core_entry_2027(team, core)?;
        let s = &self.assemblies[team];
        let c = s.core(core);
        let depth = if s.stage == "moving" || self.time() < s.ready_at { -0.06 } else { c.inserted };
        let point = [entry[0] - normal[0] * depth, entry[1] - normal[1] * depth, entry[2] - normal[2] * depth + c.lifted];
        // Step 4 turns the unit up about the horizontal P axis: the tool tips from -normal toward +z.
        let a = c.flipped.to_radians();
        let h = normal[0].hypot(normal[1]);
        let frame = &self.arena.assembly_frames[team];
        let (ux, uy) = if h > 1e-9 { (normal[0]/h, normal[1]/h) } else {
            (frame["x_axis"][0].as_f64()?, frame["x_axis"][1].as_f64()?)
        };
        // Rodrigues rotation preserves the tilted core's vertical component too.
        let dir = [-normal[0]*a.cos()-ux*normal[2]*a.sin(),
                   -normal[1]*a.cos()-uy*normal[2]*a.sin(),
                   -normal[2]*a.cos()+h*a.sin()];
        Some((point, dir, (s.current_pose[5] + c.rotation).to_radians()))
    }
    fn assembly_alignment_metrics_2027(&self, i: usize, core: usize) -> Option<(f64, f64, f64)> {
        let r = &self.robots[i];
        let (point, dir, roll) = self.core_goal_2027(&r.team, core)?;
        let secondary = core == 1 && r.arm_count == 2;
        let tip = if secondary { r.arm2_tip } else { r.arm_tip };
        let error = tip.iter().zip(point).map(|(a, b)| (a - b).powi(2)).sum::<f64>().sqrt();
        let direction = physics::arm_direction_for(r, secondary);
        let length = dir.iter().map(|v| v * v).sum::<f64>().sqrt().max(1e-9);
        let cosine = direction.iter().zip(dir).map(|(a, b)| a * b / length).sum::<f64>().clamp(-1.0, 1.0);
        let actual_roll = if secondary { r.arm2_joints[5] } else { r.arm_joints[5] };
        let roll_error = (actual_roll - roll + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI;
        Some((error, cosine.acos().to_degrees(), roll_error.abs().to_degrees()))
    }
    /// Core entry for the own core, or the borrowed core set beside it (y' = y - 0.35 m).
    pub(super) fn core_entry_2027(&self, team: &str, core: usize) -> Option<([f64; 3], [f64; 3], [f64; 3])> {
        let (entry, normal, slide) = self.core_entry(team)?;
        if core == 0 {
            return Some((entry, normal, slide));
        }
        let frame = &self.arena.assembly_frames[team];
        let (yx, yy) = (frame["y_axis"][0].as_f64()?, frame["y_axis"][1].as_f64()?);
        Some(([entry[0] - 0.35 * yx, entry[1] - 0.35 * yy, entry[2]], normal, slide))
    }
    fn complete_assembly_2027(&mut self, team: &str, i: usize, difficulty: u32) {
        let now = self.time();
        let first = !self.teams[team].completed_difficulties.contains(&difficulty);
        let a = self.rules["assembly"].clone();
        let income = if first {
            n(&a["first_rewards"][difficulty as usize - 1], "income")
        } else {
            a["repeat_income_per_period"][difficulty as usize - 1].as_f64().unwrap_or(0.0)
        };
        {
            let t = self.teams.get_mut(team).unwrap();
            t.tech_income += income;
            if t.next_tech_income <= 0.0 {
                t.next_tech_income = now + 10.0;
            }
        }
        if first {
            let reward = a["first_rewards"][difficulty as usize - 1].clone();
            let t = self.teams.get_mut(team).unwrap();
            t.completed_difficulties.push(difficulty);
            if let Some(cap) = reward["level_cap"].as_u64() {
                t.level_cap = t.level_cap.max(cap as u32);
            }
            if let Some(defense) = reward["team_defense"].as_f64() {
                t.tech_defense = t.tech_defense.max(defense);
            }
            if let Some(level) = reward["heavy_level"].as_u64() {
                self.robots[i].level = self.robots[i].level.max(level as u32);
            }
            // A new best level refreshes the 45 s assembly invulnerability budget.
            for k in 0..self.robots.len() {
                if self.robots[k].team == team && self.robots[k].role == "heavy" {
                    self.robots[k].assembly_invulnerable_spent = 0.0;
                    self.apply_performance_2027(k, true);
                }
            }
        }
        let s = self.assemblies.get_mut(team).unwrap();
        s.stage = "complete".into();
        // The core re-initialises for a few seconds before it accepts the next unit.
        s.repeat_ready_at = now + n(&self.rules["assembly"], "core_reinit_s");
        s.core_units = 0;
        s.recover_at = None;
        s.fail_reason = None;
        *s.successes_by_difficulty.entry(difficulty).or_default() += 1;
        self.event("assembly_success", json!({"team":team,"difficulty":difficulty,"first":first,"income_per_10s":self.teams[team].tech_income}));
    }
    fn fail_assembly_2027(&mut self, team: &str, reason: &str) {
        let now = self.time();
        let rules = self.rules["assembly"].clone();
        // How long the core keeps the unit before salvaging it: at once when the operator already
        // lost contact (rule 88/89: 15 s off the card, then the core reclaims the unit), after the
        // operator's 15 s clearing window when the heavy was destroyed, and a short settle otherwise.
        let grace = match reason {
            "left_assembly_area" => 0.0,
            "heavy_destroyed" => n(&rules, "card_absence_fail_s"),
            _ => n(&rules, "salvage_delay_s"),
        };
        let s = self.assemblies.get_mut(team).unwrap();
        if !["moving", "inserting"].contains(&s.stage.as_str()) {
            return;
        }
        s.stage = "failed".into();
        s.second = None;
        s.fail_reason = Some(reason.into());
        s.recover_at = if s.core_units > 0 { Some(now + grace) } else { None };
        let difficulty = s.difficulty;
        self.event("assembly_failed", json!({"team":team,"difficulty":difficulty,"reason":reason}));
    }
    fn assembly_after_step_2027(&mut self) {
        let now = self.time();
        for team in ["red", "blue"] {
            self.salvage_core_units(team);
            let mut s = self.assemblies[team].clone();
            if s.stage == "moving" && now >= s.ready_at
                && s.current_pose.iter().zip(s.pose).all(|(a,b)| (a-b).abs() <= 1e-6) {
                let state = self.assemblies.get_mut(team).unwrap();
                state.stage = "inserting".into();
                state.ready_at = now;
                state.started = now;
                s = state.clone();
            }
            if !["moving", "inserting"].contains(&s.stage.as_str()) {
                continue;
            }
            let limit = n(&self.rules["assembly"], "difficulty_4_time_limit_s");
            if s.stage == "inserting" && s.difficulty == 4 && now - s.started >= limit {
                self.fail_assembly_2027(team, "timeout");
            } else if self.assembly_pair_expired_2027(&s, now) {
                self.fail_assembly_2027(team, "pair_window");
            } else if now - s.buffer_until >= n(&self.rules["assembly"], "card_absence_fail_s") {
                self.fail_assembly_2027(team, "left_assembly_area");
            }
        }
    }

    // ---- Rune -------------------------------------------------------------------------
    fn start_rune_2027(&mut self, team: &str) {
        let was_activating = self.runes[team].status == "activating";
        self.start_rune(team);
        if !was_activating && self.runes[team].status == "activating" && self.runes[team].mode == "large" {
            // Both faces share a shaft: a new large activation resets its target function.
            self.rune_a = 0.780 + self.random() * (1.045 - 0.780);
            self.rune_omega = 1.884 + self.random() * (2.0 - 1.884);
            self.rune_phase[0] = self.time();
        }
    }
    pub(super) fn rune_motor_rates_2027(&self, now: f64) -> (f64, f64) {
        if !self.runes.values().any(|r| r.mode == "large" && r.status == "activating") {
            return (n(&self.rules["rune"], "small_speed_rad_s"), 0.0);
        }
        let phase = self.rune_omega * (now - self.rune_phase[0]);
        (self.rune_a * phase.sin() + 2.090 - self.rune_a,
         self.rune_a * self.rune_omega * phase.cos())
    }
    fn update_rune_motor_2027(&mut self) {
        self.update_rune_motor();
    }
    pub(super) fn rune_hit_2027(&mut self, p: &Projectile, arm: usize, x: f64, y: f64, z: f64) {
        if is_42(&p.role) {
            self.event("invalid_action", json!({"robot":p.source,"reason":"42mm cannot hit the rune"}));
            return;
        }
        let state = self.runes[&p.team].clone();
        // After each large activation only the inner rings keep detecting hits (table 5-24).
        if state.mode == "large" && state.status == "activating" {
            let activations = self.large_activations(&p.team);
            if activations > 0 {
                let rings = &self.rules["rune"]["large_rings_after_activations"][activations.min(2).to_string()];
                let yaw = n(&self.arena.rune, "axis_yaw");
                let (nx, ny) = (yaw.cos(), yaw.sin());
                let [tx, ty, tz] = self.rune_target_points(&p.team)[arm];
                let offset = (-(x - tx) * ny + (y - ty) * nx).hypot(z - tz);
                let ring = (10.0 - (offset / 0.015).floor()).clamp(1.0, 10.0);
                if ring < rings[0].as_f64().unwrap_or(1.0) {
                    return;
                }
            }
        }
        let before = self.runes[&p.team].status.clone();
        self.rulebook_rune_hit(p, arm, x, y, z);
        let after = &self.runes[&p.team];
        if before != "active" && after.status == "active" && after.mode == "large" {
            let team = p.team.clone();
            let count = self.large_activations(&team) + 1;
            self.rune_large_count.insert(team, count);
        }
    }
    fn large_activations(&self, team: &str) -> u32 {
        self.rune_large_count.get(team).copied().unwrap_or(0)
    }

    // ---- After physics ----------------------------------------------------------------
    pub(super) fn after_step_2027(&mut self) {
        self.update_referee_locks();
        let now = self.time();
        self.assembly_after_step_2027();
        self.update_launcher_unlocks();
        for team in ["red", "blue"] {
            let stop = self.outposts[team].ever_destroyed || self.bases[opponent(team)].armor_open || now >= n(&self.rules["structures"], "outpost_stop_after_s");
            let outpost = self.outposts.get_mut(team).unwrap();
            if stop || outpost.rotation_stopped {
                if !outpost.rotation_stopped {
                    outpost.rotation_stopped = true;
                    outpost.rotation_stop_at = now;
                    outpost.rotation_stop_angle = outpost.rotation;
                }
                let fraction = ((now - outpost.rotation_stop_at) / 10.0).clamp(0.0, 1.0);
                let start = (outpost.rotation_stop_angle + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI;
                outpost.rotation = (start * (1.0 - fraction)).rem_euclid(std::f64::consts::TAU);
            } else {
                outpost.rotation = (outpost.rotation + 0.8 * std::f64::consts::PI * (now / 5.0).min(1.0) * DT * self.rune_direction).rem_euclid(std::f64::consts::TAU);
            }
        }
        // Sentry posture timers.
        for r in self.robots.iter_mut() {
            if r.role != "sentry" || !r.alive {
                continue;
            }
            if let Some(k) = POSTURES.iter().position(|p| *p == r.posture) {
                r.posture_time[k] += DT;
                if r.posture_enhanced {
                    r.posture_enhanced_time[k] += DT;
                    if r.posture_enhanced_time[k] >= n(&self.rules["sentry"]["posture"], "enhanced_budget_s") - 1e-8 {
                        r.posture_enhanced = false;
                    }
                }
            }
        }
        for i in 0..self.robots.len() {
            if self.robots[i].launcher == "installing" && !self.workbench_docked(i) {
                self.robots[i].launcher = self.launcher_idle_state(i);
                self.robots[i].launcher_progress = 0.0;
            }
        }
    }
    pub(super) fn check_winner_2027(&mut self) {
        let (red, blue) = (self.bases["red"].hp, self.bases["blue"].hp);
        if red <= 0.0 || blue <= 0.0 {
            let winner = if red == blue { None } else { Some(if red > blue { "red" } else { "blue" }.into()) };
            self.finish(winner, "base_destroyed");
            return;
        }
        if self.time() < self.duration {
            return;
        }
        let metric = |team: &str| {
            (
                (self.teams[team].control.unwrap_or(0.0) * 1000.0).round(),
                self.teams[team].attack_damage,
                self.robots.iter().filter(|r| r.team == team && r.alive && r.role != "aerial").map(|r| r.hp).sum::<f64>(),
            )
        };
        let winner = match metric("red").partial_cmp(&metric("blue")).unwrap() {
            std::cmp::Ordering::Greater => Some("red".into()),
            std::cmp::Ordering::Less => Some("blue".into()),
            _ => None,
        };
        self.finish(winner, "timeout");
    }
    pub(super) fn public_rule_summary_2027(&self) -> Value {
        json!({"season":"2027","source":self.rules["source"],"construction_source":self.rules["construction"]["source"],
            "physical_design_revision":"construction-2027-v1","physics_hz":120,"adaptations":self.rules["adaptations"],
            "match":self.rules["match"],"profile_defaults":self.rules["profiles"]["defaults"],
            "logistics":self.rules["logistics"],"speed_limits":self.rules["speed_limits"],
            "full_rules_available":"sdk.rules(observation) or sdk.RULES_2027, sdk.DESIGNS and protected arena.json"})
    }
    pub(super) fn position_snapshots(&self) -> Value {
        json!(self.positions)
    }
}

/// Step progress of one technology core.
#[derive(Clone, Serialize, Deserialize, Default)]
pub struct CoreProgress {
    pub inserted: f64,
    pub lifted: f64,
    pub flipped: f64,
    pub rotation: f64,
    pub target_rotation: f64,
    pub confirm: f64,
    pub confirmed: bool,
    /// Completion time of steps 2, 3, 4, 5 and 6.
    pub step_times: [Option<f64>; 5],
}
impl CoreProgress {
    pub fn new(target: f64) -> Self {
        Self { target_rotation: target, ..Default::default() }
    }
}

#[cfg(test)]
#[path = "field_2027_regression_tests.rs"]
mod field_2027_regression_tests;

#[cfg(test)]
#[path = "assembly_2027_regression_tests.rs"]
mod assembly_2027_regression_tests;
