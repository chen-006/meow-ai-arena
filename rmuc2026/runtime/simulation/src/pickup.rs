use super::*;

#[derive(Clone, Serialize, Deserialize)]
pub struct PickupState {
    pub stage: String,
    pub elapsed: f64,
    pub phase_elapsed: f64,
    pub progress: f64,
    pub point: [f64; 3],
    pub direction: [f64; 3],
    pub held: bool,
    #[serde(default)]
    pub arm: u32,
    /// Seconds since the current stage last made progress. The arm clamps every joint to its
    /// travel limit, so a unit outside the workspace would otherwise sit at "approaching" forever.
    #[serde(default)]
    pub stall: f64,
}

/// A grab that cannot reach its target must fail instead of hanging: the pilot would hold the key
/// and watch 0 % until they leave the resource zone. The arm now runs at half rate, so a legitimate
/// grab that starts with the arm stowed and the chassis turned away needs about twice the old
/// window; eight seconds still fails an unreachable unit long before a pilot would give up.
const PICKUP_STALL_S: f64 = 8.0;

impl Engine {
    pub(super) fn request_pickup(&mut self, i: usize, rpc: &Value) {
        let r = &self.robots[i];
        if r.arm_count == 0 {
            return;
        }
        let arm=rpc["pickup_arm"].as_u64().unwrap_or(if r.arm_count==2 {r.carried_units as u64%2} else {0}) as u32;
        let requested = rpc["pick_unit"].as_bool() == Some(true)
            && arm<r.arm_count
            && !rpc[if arm==0 {"arm"}else{"arm2"}].is_object()
            && (arm==1 || rpc.get("rescue").is_none());
        let eligible = requested
            && r.alive
            && !r.weak
            && !r.red_card
            && r.unit_pickup.as_ref().map(|s|s.arm==arm).unwrap_or(true)
            && r.units_on_board() < r.unit_capacity()
            && self.teams[&r.team].energy_units_left.map(|left| left > 0).unwrap_or(true)
            && self.raw_zone(i, "resource", Some(&r.team));
        if !eligible {
            // A unit already clamped in the gripper is cargo, not a grab in progress: releasing the
            // key (or backing away) must not throw the third unit away.
            let carrying = r.gripper_unit();
            if !carrying && self.robots[i].unit_pickup.take().is_some() {
                self.event("unit_pickup_cancelled", json!({"robot":self.robots[i].id}));
            }
            return;
        }
        if r.unit_pickup.is_some() {
            // A unit already clamped in the gripper rides there until it is inserted or the robot
            // dies; asking again must not restart the sequence.
            return;
        }
        // A card lineup may contain multiple engineers. The physical dispenser
        // presents one unit at a time, so only one arm can claim that unit.
        if self.robots.iter().enumerate().any(|(j, other)| {
            j != i && other.team == r.team && other.unit_pickup.is_some()
        }) {
            return;
        }
        let station = self.arena.field_geometry["fixtures"]
            .as_array()
            .and_then(|f| {
                f.iter()
                    .find(|f| f["kind"] == "resource" && f["team"].as_str() == Some(&r.team))
            });
        let Some(station) = station else {
            return;
        };
        let point = &station["pick_point"];
        let (Some(x), Some(y)) = (point[0].as_f64(), point[1].as_f64()) else {
            return;
        };
        self.robots[i].unit_pickup = Some(PickupState {
            stage: "approaching".into(),
            elapsed: 0.0,
            phase_elapsed: 0.0,
            progress: 0.0,
            point: [
                x,
                y,
                height_at(&self.arena, x, y) + point[2].as_f64().unwrap_or(0.6),
            ],
            direction: std::array::from_fn(|k| station["pick_dir"][k].as_f64().unwrap_or(0.0)),
            held: false,
            arm,
            stall: 0.0,
        });
        self.event("unit_pickup_started", json!({"robot":self.robots[i].id}));
    }

    pub(super) fn advance_pickups(&mut self) {
        for i in 0..self.robots.len() {
            let r = &self.robots[i];
            if r.unit_pickup.is_none() {
                continue;
            }
            // A unit in the gripper is carried across the field to the assembly plinth, so leaving
            // the resource zone must not cancel it; losing the chassis still does.
            let carrying = r.gripper_unit();
            if !r.alive || r.weak || r.red_card || (!carrying && !self.raw_zone(i, "resource", Some(&r.team))) {
                self.robots[i].unit_pickup = None;
                self.event("unit_pickup_cancelled", json!({"robot":self.robots[i].id}));
                continue;
            }
            if carrying {
                continue;
            }
            let selected=r.unit_pickup.as_ref().unwrap().arm;
            let arm = self.physics.as_ref().unwrap().design(i)[if selected==0 {"arm"}else{"arm2"}].clone();
            let stowed = physics::arm_stowed(&arm);
            let r = &mut self.robots[i];
            let state = r.unit_pickup.as_mut().unwrap();
            state.elapsed += DT;
            let target = if state.stage == "lifting" {
                [state.point[0], state.point[1], state.point[2] + 0.18]
            } else {
                state.point
            };
            let tip=if selected==0 {r.arm_tip}else{r.arm2_tip};
            let joints=if selected==0 {r.arm_joints}else{r.arm2_joints};
            let error = tip
                .iter()
                .zip(target)
                .map(|(a, b)| (a - b).powi(2))
                .sum::<f64>()
                .sqrt();
            let ready = match state.stage.as_str() {
                "approaching" => error < 0.045 && joints[6] > 0.8,
                "gripping" => error < 0.045 && joints[6] < 0.2,
                "lifting" => error < 0.05,
                "stowing" => joints
                    .iter()
                    .zip(stowed)
                    .enumerate()
                    .all(|(k, (a, b))| (a - b).abs() < if k == 1 || k == 2 { 0.025 } else { 0.08 }),
                _ => false,
            };
            state.phase_elapsed = if ready { state.phase_elapsed + DT } else { 0.0 };
            state.stall = if ready { 0.0 } else { state.stall + DT };
            let stalled = state.stall > PICKUP_STALL_S;
            // A stage is held for the longer of its own dwell and the time the joints need, and the
            // joints now run at half rate for a grab: these dwells are doubled with them, so a stage
            // that used to be gated by the dwell is twice as long as well instead of only its
            // joint-limited part.
            let (index, duration, next) = match state.stage.as_str() {
                "approaching" => (0, 0.4, "gripping"),
                "gripping" => (1, 0.7, "lifting"),
                "lifting" => (2, 0.6, "stowing"),
                _ => (3, 0.8, "complete"),
            };
            state.progress = (index as f64 + (state.phase_elapsed / duration).min(1.0)) / 4.0;
            if stalled {
                // The unit is outside the arm's workspace (too far, wrong side, or the chassis is
                // misaligned): stop pretending a grab is in progress and let the cockpit say why.
                let id = r.id.clone();
                r.unit_pickup = None;
                self.event("unit_pickup_cancelled", json!({"robot":id,"reason":"unreachable"}));
                continue;
            }
            if state.phase_elapsed + 1e-8 < duration {
                continue;
            }
            // Both storage bays fill first; the third unit the heavy robot is allowed to take stays
            // clamped in the gripper, ready to be pushed into a core or carried to another one.
            let next = if next == "stowing" && r.carried_units >= 2 {
                "carrying"
            } else {
                next
            };
            if next == "complete" {
                r.carried_units += 1;
                r.unit_pickup = None;
                let team = r.team.clone();
                if let Some(left) = self.teams.get_mut(&team).unwrap().energy_units_left.as_mut() {
                    *left = left.saturating_sub(1);
                }
                self.event("unit_pickup", json!({"robot":self.robots[i].id,"amount":1}));
            } else if next == "carrying" {
                let team = r.team.clone();
                let state = r.unit_pickup.as_mut().unwrap();
                state.stage = "carrying".into();
                state.held = true;
                state.phase_elapsed = 0.0;
                state.progress = 1.0;
                if let Some(left) = self.teams.get_mut(&team).unwrap().energy_units_left.as_mut() {
                    *left = left.saturating_sub(1);
                }
                self.event("unit_pickup", json!({"robot":self.robots[i].id,"amount":1,"gripper":true}));
            } else {
                state.stage = next.into();
                state.phase_elapsed = 0.0;
                if next == "lifting" {
                    state.held = true;
                }
            }
        }
    }
}
