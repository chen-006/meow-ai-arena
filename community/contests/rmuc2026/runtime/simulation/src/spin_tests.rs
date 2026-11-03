use super::*;

/// Seconds the pilot keeps a spin command before the travelled angle is measured.
const SPIN_SECONDS: u64 = 80;

/// Ticks to settle the chassis on its wheels before the spin starts.
const SETTLE_TICKS: u64 = 40;

/// Body yaw rate a piloted chassis reaches under `rate` with `boost` burst authority.
fn spin_rate(slot: u32, rate: f64, boost: f64) -> f64 {
    let mut m = Engine::new(7, 60.0, "rmuc2027").unwrap();
    let settle = json!({"red":{slot.to_string():{"move":[0,0],"aim":0.0,"pitch":0.0,"fire":false,"brake":true}}});
    for _ in 0..SETTLE_TICKS {
        m.advance(&settle);
    }
    let mut last = m.robots.iter().find(|r| r.slot == slot).unwrap().heading;
    let turn = json!({"red":{slot.to_string():{"move":[0,0],"aim":0.0,"pitch":0.0,"fire":false,"brake":true,"yaw_rate":rate,"yaw_boost":boost}}});
    let mut travelled = 0.0;
    for _ in 0..SPIN_SECONDS {
        m.advance(&turn);
        let now = m.robots.iter().find(|r| r.slot == slot).unwrap().heading;
        let mut d = now - last;
        while d > std::f64::consts::PI {
            d -= std::f64::consts::TAU;
        }
        while d < -std::f64::consts::PI {
            d += std::f64::consts::TAU;
        }
        travelled += d;
        last = now;
    }
    travelled / (SPIN_SECONDS as f64 * DT)
}

#[test]
fn a_pilot_spins_the_hull_twice_as_fast_with_burst_authority() {
    // 小陀螺: the rated yaw motors top out near 5 rad/s; a pilot's boosted spin reaches roughly twice
    // that, which is the whole point of the `yaw_boost` channel.
    for slot in [1u32, 3, 4, 7] {
        let rated = spin_rate(slot, 5.9, 0.0);
        let boosted = spin_rate(slot, 12.0, 1.0);
        assert!(
            rated > 3.0,
            "slot {slot} barely spun at its rated rate: {rated:.2} rad/s"
        );
        assert!(
            boosted > rated * 1.8,
            "slot {slot} boosted spin {boosted:.2} rad/s is not twice the rated {rated:.2} rad/s"
        );
        assert!(
            boosted < 12.5,
            "slot {slot} spun past the engine's yaw cap: {boosted:.2} rad/s"
        );
    }
}

#[test]
fn a_strategy_spin_is_unchanged_without_the_burst_channel() {
    // Strategies never send `yaw_boost`, so their spin stays exactly on the rated motors.
    let plain = spin_rate(4, 12.0, 0.0);
    assert!(
        plain < 6.0,
        "an unboosted spin should stay at the rated motors, got {plain:.2} rad/s"
    );
}
