//! Initial conditions for unranked training, applied before the first simulation tick.
use super::*;

impl Engine {
    pub fn apply_training_scenario(&mut self, scenario: &Value) -> Result<(), String> {
        if self.tick != 0 {
            return Err("Training initial conditions require tick zero".into());
        }
        let object = scenario.as_object().ok_or("Training scenario must be an object")?;
        if object.keys().any(|k| k != "robots") {
            return Err("Unknown training scenario field".into());
        }
        let robots = object.get("robots").and_then(Value::as_object).ok_or("Training robots required")?;
        // Validate the whole request before changing any state.
        let mut updates = Vec::new();
        for (id, pose) in robots {
            let index = self.robots.iter().position(|r| r.id == *id).ok_or("Unknown training robot")?;
            let pose = pose.as_object().ok_or("Training robot must be an object")?;
            if pose.keys().any(|k| !["x", "y", "heading", "aim", "pitch", "ammo"].contains(&k.as_str())) {
                return Err("Unknown training robot field".into());
            }
            let r = &self.robots[index];
            let finite = |key: &str, default: f64| -> Result<f64, String> {
                match pose.get(key) {
                    Some(v) => v.as_f64().filter(|n| n.is_finite()).ok_or_else(|| format!("Invalid training {key}")),
                    None => Ok(default),
                }
            };
            let (x, y) = (finite("x", r.x)?, finite("y", r.y)?);
            if !(r.radius..=self.arena.width-r.radius).contains(&x) || !(r.radius..=self.arena.height-r.radius).contains(&y) {
                return Err("Training robot is outside the field".into());
            }
            let heading = finite("heading", r.heading)?;
            let aim = finite("aim", heading)?;
            let pitch = finite("pitch", r.pitch)?;
            if pitch.abs() > std::f64::consts::FRAC_PI_2 {
                return Err("Invalid training pitch".into());
            }
            let ammo = match pose.get("ammo") {
                Some(v) => v.as_u64().filter(|n| *n <= 5000).ok_or("Invalid training ammunition")? as u32,
                None => r.ammo,
            };
            updates.push((index, x, y, heading, aim, pitch, ammo));
        }
        for (index, x, y, heading, aim, pitch, ammo) in updates {
            let r = &mut self.robots[index];
            r.x=x; r.y=y; r.z=height_at(&self.arena,x,y);
            r.heading=heading; r.aim=aim; r.pitch=pitch; r.ammo=ammo;
            r.quaternion=[0.0,0.0,(heading/2.0).sin(),(heading/2.0).cos()];
        }
        self.physics=Some(physics::PhysicsWorld::new(&self.arena,&self.robots,&config().robot_designs));
        Ok(())
    }
}
