use super::*;

fn n(v: &Value, key: &str) -> f64 {
    v[key].as_f64().unwrap_or(0.0)
}

impl Engine {
    pub(super) fn update_referee_locks(&mut self) {
        let now = self.time();
        for i in 0..self.robots.len() {
            let r = &self.robots[i];
            if self.season == 2027 {
                let r = &mut self.robots[i];
                r.overspeed_locked = r.overspeed_permanent_lock || r.overspeed_lock_until > now + 1e-8;
                r.launcher_fault = false;
                continue;
            }
            let heavy = r.role == "hero";
            let caliber = if heavy { "42mm" } else { "17mm" };
            let speed = if heavy && r.deployed {
                16.5
            } else {
                self.specs[&r.role].projectile_speed
            };
            let limit = n(
                &self.rules["projectiles"][caliber],
                if heavy && r.deployed {
                    "deployed_muzzle_limit_mps"
                } else {
                    "muzzle_limit_mps"
                },
            );
            let fault =
                r.role != "engineer" && self.overspeed_penalty(heavy, speed, limit).is_err();
            let r = &mut self.robots[i];
            r.overspeed_locked = r.overspeed_permanent_lock || r.overspeed_lock_until > now + 1e-8;
            r.launcher_fault = fault;
        }
    }
    /// A muzzle sensor measures launch speed relative to the launcher, not the
    /// projectile's world velocity (which also includes chassis motion).
    pub(super) fn overspeed_penalty(
        &self,
        heavy: bool,
        speed: f64,
        limit: f64,
    ) -> Result<Option<f64>, ()> {
        if !speed.is_finite() || speed <= 0.0 {
            return Err(());
        }
        if speed <= limit {
            return Ok(None);
        }
        if heavy {
            // The absolute 18 m/s boundary takes priority even when deployed.
            if speed > 18.0 {
                return Ok(Some(f64::INFINITY));
            }
            if speed / limit <= 1.1 {
                return Ok(Some(15.0));
            }
            if speed / limit <= 1.2 {
                return Ok(Some(20.0));
            }
            // Table 5-3 leaves the undeployed 14.4..18 interval undefined.
            // Reject unsupported hardware speeds instead of inventing a sanction.
            return Err(());
        }
        let excess = speed - limit;
        Ok(Some(if excess < 5.0 {
            15.0
        } else if excess < 10.0 {
            20.0
        } else {
            f64::INFINITY
        }))
    }
    pub(super) fn record_overspeed(&mut self, i: usize, speed: f64, seconds: f64) {
        let now = self.time();
        let r = &mut self.robots[i];
        if seconds.is_infinite() {
            r.overspeed_permanent_lock = true;
        } else {
            r.overspeed_lock_until = r.overspeed_lock_until.max(now + seconds);
        }
        r.overspeed_locked = true;
        let payload = json!({"robot":r.id,"team":r.team,"speed":speed,
            "permanent":r.overspeed_permanent_lock,"locked_until":r.overspeed_lock_until});
        self.event("overspeed", payload);
    }
    fn penalty_hp(&mut self, i: usize, fraction: f64, floor: f64) {
        let now = self.time();
        let r = &mut self.robots[i];
        if !r.alive || r.role == "aerial" {
            return;
        }
        let taken = (r.max_hp * fraction).round().min((r.hp - floor).max(0.0));
        if taken <= 0.0 {
            return;
        }
        r.hp -= taken;
        r.last_damage_time = now;
        let team = r.team.clone();
        let robot = r.id.clone();
        let hp = r.hp;
        let victim_level = if r.role == "sentry" { 1 } else { r.level.max(1) };
        let opponent = if team == "red" { "blue" } else { "red" };
        let t = self.teams.get_mut(opponent).unwrap();
        t.attack_damage += taken;
        t.score = t.attack_damage;
        // Referee damage bypasses defence/protection. In 2027, table 5-14
        // attributes its damage/death XP to the opponent's living infantry.
        if self.season == 2027 {
            let xp = taken * n(&self.rules["experience"], "robot_damage_xp_per_hp");
            self.award_damage_xp_2027(None, opponent, xp, None);
            if hp <= 0.0 {
                self.award_kill_xp_2027(None, opponent, victim_level, None);
            }
        }
        self.event(
            "penalty_damage",
            json!({"robot":robot,"team":team,"damage":taken,"hp":hp}),
        );
    }
    fn red_card(&mut self, i: usize, reason: &str) {
        if self.robots[i].red_card {
            return;
        }
        self.penalty_hp(i, 1.0, 0.0);
        let now = self.time();
        let r = &mut self.robots[i];
        r.red_card = true;
        r.alive = false;
        r.hp = 0.0;
        r.powered = false;
        r.death_time = now;
        r.support_active = false;
        r.deployed = false;
        r.deploy_at = None;
        r.pending_heal = None;
        r.weak_until = None;
        r.zone_contacts.clear();
        r.last_raw_zones.clear();
        r.invulnerable_until = 0.0;
        r.invulnerable = false;
        let payload =
            json!({"robot":r.id,"team":r.team,"reason":reason,"card_count":r.yellow_cards});
        self.event("red_card", payload);
    }
    fn yellow_card(&mut self, i: usize, reason: &str) {
        if self.robots[i].red_card {
            return;
        }
        let now = self.time();
        let team = self.robots[i].team.clone();
        let r = &mut self.robots[i];
        let rule = self.rules["penalties"].clone();
        let ratio = if r.yellow_cards > 0
            && now - r.last_yellow_time < n(&rule, "repeat_window_s") - 1e-8
        {
            r.yellow_damage_ratio * 2.0
        } else {
            n(&rule, "offender_hp_ratio")
        };
        r.yellow_cards += 1;
        r.last_yellow_time = now;
        r.yellow_damage_ratio = ratio;
        let count = r.yellow_cards;
        let limit = if rule["yellow_limit"].is_number() {
            n(&rule, "yellow_limit")
        } else if r.role == "engineer" {
            n(&rule, "engineer_yellow_limit")
        } else {
            n(&rule, "other_yellow_limit")
        } as u32;
        let sentry = r.role == "sentry";
        for j in 0..self.robots.len() {
            if self.robots[j].team != team {
                continue;
            }
            let duration = n(
                &rule,
                if i == j {
                    "offender_fpv_s"
                } else {
                    "teammate_fpv_s"
                },
            );
            if i == j && sentry {
                // A sentry has no operator screen: its chassis loses power instead (7.1.2).
                self.robots[j].power_cutoff_until = self.robots[j].power_cutoff_until.max(now) + n(&rule, "sentry_chassis_off_s");
            } else {
                self.robots[j].fpv_blocked_until = self.robots[j].fpv_blocked_until.max(now) + duration;
            }
            self.penalty_hp(
                j,
                if i == j {
                    ratio
                } else {
                    n(&rule, "teammate_hp_ratio")
                },
                1.0,
            );
        }
        self.event("yellow_card", json!({"robot":self.robots[i].id,"team":team,
            "reason":reason,"card_count":count,"hp_ratio":ratio,"fpv_blocked_until":self.robots[i].fpv_blocked_until}));
        if count >= limit {
            self.red_card(i, "yellow_card_limit");
        }
    }
    /// Trusted host commands have their own top-level channel. A contestant's
    /// robot action dictionary cannot issue referee decisions.
    pub(super) fn process_referee_commands(&mut self, raw: Option<&Value>) {
        let Some(raw) = raw else {
            return;
        };
        let valid = |value: &Value| -> bool {
            let Some(obj) = value.as_object() else {
                return false;
            };
            if obj
                .keys()
                .any(|k| !["kind", "robot", "team", "reason"].contains(&k.as_str()))
            {
                return false;
            }
            if let Some(reason) = value.get("reason") {
                if reason
                    .as_str()
                    .map(|s| s.chars().count() > 128)
                    .unwrap_or(true)
                {
                    return false;
                }
            }
            match value["kind"].as_str() {
                Some("yellow" | "red") => {
                    value.get("team").is_none()
                        && value["robot"]
                            .as_str()
                            .map(|id| self.robots.iter().any(|r| r.id == id))
                            .unwrap_or(false)
                }
                Some("yellow_both") => value.get("robot").is_none() && value.get("team").is_none(),
                Some("forfeit") => {
                    value.get("robot").is_none()
                        && matches!(value["team"].as_str(), Some("red" | "blue"))
                }
                _ => false,
            }
        };
        let Some(commands) = raw
            .as_array()
            .filter(|c| c.len() <= 16 && c.iter().all(valid))
        else {
            self.event(
                "referee_error",
                json!({"reason":"invalid referee command batch"}),
            );
            return;
        };
        for c in commands {
            let reason = c["reason"].as_str().unwrap_or("referee_decision");
            match c["kind"].as_str().unwrap() {
                "yellow" | "red" => {
                    let i = self
                        .robots
                        .iter()
                        .position(|r| Some(r.id.as_str()) == c["robot"].as_str())
                        .unwrap();
                    if c["kind"] == "yellow" {
                        self.yellow_card(i, reason);
                    } else {
                        self.red_card(i, reason);
                    }
                }
                "yellow_both" => {
                    let now = self.time();
                    for i in 0..self.robots.len() {
                        self.robots[i].fpv_blocked_until =
                            self.robots[i].fpv_blocked_until.max(now)
                                + n(&self.rules["penalties"], "teammate_fpv_s");
                        self.penalty_hp(i, n(&self.rules["penalties"], "teammate_hp_ratio"), 1.0);
                    }
                    self.event("yellow_both", json!({"reason":reason}));
                }
                "forfeit" => {
                    let team = c["team"].as_str().unwrap();
                    let other = if team == "red" { "blue" } else { "red" };
                    let damage = self.bases[team].hp;
                    self.bases.get_mut(team).unwrap().hp = 0.0;
                    let t = self.teams.get_mut(other).unwrap();
                    t.attack_damage += damage;
                    t.score = t.attack_damage;
                    self.event("forfeit", json!({"team":team,"reason":reason}));
                    self.finish(Some(other.into()), "forfeit");
                    break;
                }
                _ => unreachable!(),
            }
        }
    }
}
