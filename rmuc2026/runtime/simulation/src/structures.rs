//! Shared finite armor geometry. Dimensions without a CAD source are public proxies.
use super::*;
use rapier3d::na::Vector3;

#[derive(Clone, Copy, Serialize)]
pub(crate) struct Plate {
    pub face: usize,
    pub center: [f64; 3],
    pub normal: [f64; 3],
    pub width: f64,
    pub height: f64,
    pub heavy_only: bool,
    pub weakspot: bool,
}

/// Figure 4-32: the outpost's three rotating middle armour modules are staggered,
/// 1116 / 1216 / 1316 mm above its base; face k keeps its own height as it turns.
pub(crate) const OUTPOST_MIDDLE_Z: [f64; 3] = [1.116, 1.216, 1.316];

/// A base faces the field. Red sits on the low-x side, so its faces already point in; blue sits on
/// the high-x side and the whole structure is turned half a turn about its own axis.
pub(crate) fn base_yaw(team: &str) -> f64 {
    if team == "blue" {
        std::f64::consts::PI
    } else {
        0.0
    }
}

pub(crate) fn plates(b: &Base, outpost: bool) -> Vec<Plate> {
    let mut result = Vec::new();
    // Figure 4-11: the large base modules are 287 mm above the pedestal once exposed.
    // Top (dart-detection) modules detect 42 mm even while the base's shells are closed.
    let levels: Vec<(Option<f64>, f64, f64, bool, bool, f64)> = if outpost {
        vec![
            (None, 0.2965, 0.17, false, true, 0.0),
            (Some(1.5), 0.095, 0.135, true, false, 0.0),
        ]
    } else {
        let mut levels = vec![(Some(1.043), 0.095, 0.135, true, false, 0.0)];
        if b.armor_open {
            levels.push((Some(0.287), 0.34, 0.23, false, true, 15.0_f64.to_radians()));
        }
        levels
    };
    for (level, (z, radius, width, heavy_only, weakspot, tilt)) in levels.into_iter().enumerate() {
        for face in 0..3 {
            let a = if outpost {
                if level == 0 {
                    b.rotation
                } else {
                    0.0
                }
            } else {
                b.yaw
            } + face as f64 * std::f64::consts::TAU / 3.0;
            let z = z.unwrap_or(OUTPOST_MIDDLE_Z[face]);
            result.push(Plate {
                face: if outpost && level == 0 {
                    face
                } else if outpost {
                    3 + face
                } else if level == 0 {
                    3 + face
                } else {
                    face
                },
                center: [b.x + radius * a.cos(), b.y + radius * a.sin(), b.z + z],
                normal: [a.cos() * tilt.cos(), a.sin() * tilt.cos(), tilt.sin()],
                width,
                height: 0.125,
                heavy_only,
                weakspot,
            });
        }
    }
    result
}

pub(crate) fn coordinates(plate: &Plate, point: [f64; 3]) -> (f64, f64, f64) {
    let n = Vector3::from(plate.normal);
    let u = Vector3::new(-n.y, n.x, 0.0).normalize();
    let v = n.cross(&u);
    let d = Vector3::from(point) - Vector3::from(plate.center);
    (d.dot(&u), d.dot(&v), d.dot(&n))
}

pub(crate) fn intersection(plate: &Plate, origin: [f64; 3], delta: [f64; 3]) -> Option<f64> {
    let n = Vector3::from(plate.normal);
    let d = Vector3::from(delta).dot(&n);
    if d >= -1e-10 {
        return None;
    }
    let t = (Vector3::from(plate.center) - Vector3::from(origin)).dot(&n) / d;
    if !(0.0..=1.0).contains(&t) {
        return None;
    }
    let point = std::array::from_fn(|k| origin[k] + t * delta[k]);
    let (u, v, _) = coordinates(plate, point);
    (u.abs() <= plate.width / 2.0 && v.abs() <= plate.height / 2.0).then_some(t)
}

/// Figures 4-10/4-11: closed, three corner shells form the pyramid and meet at a
/// narrow slit in the middle of each face. Opening swings each half-shell outward
/// about its corner, uncovering the face's large armour module. The swept boxes
/// stop shots; opening never changes the ground robot forbidden pedestal.
pub(crate) const BASE_SHELL_OPEN_RAD: f64 = 1.15;

pub(crate) fn covers(b: &Base) -> Vec<Wall> {
    cover_geometry(b.x, b.y, b.z, b.armor_open_progress, b.yaw)
}

pub(crate) fn cover_geometry(x: f64, y: f64, z: f64, progress: f64, yaw: f64) -> Vec<Wall> {
    let (radius, reach, length) = (0.43, 0.265, 0.24);
    let mut walls = Vec::new();
    for face in 0..3 {
        // The shells follow the plates: blue's whole structure is turned half a turn.
        let a = yaw + face as f64 * std::f64::consts::TAU / 3.0;
        let (n, t) = ([a.cos(), a.sin()], [-a.sin(), a.cos()]);
        for (k, side) in [-1.0_f64, 1.0].into_iter().enumerate() {
            // Hinge at the corner end; closed, the half-shell runs back toward the slit.
            let hinge = [x + radius * n[0] + side * reach * t[0], y + radius * n[1] + side * reach * t[1]];
            let theta = progress.clamp(0.0, 1.0) * BASE_SHELL_OPEN_RAD;
            let dir = [
                -side * t[0] * theta.cos() + n[0] * theta.sin(),
                -side * t[1] * theta.cos() + n[1] * theta.sin(),
            ];
            let center = [hinge[0] + dir[0] * length / 2.0, hinge[1] + dir[1] * length / 2.0];
            walls.push(Wall {
                x: center[0] - 0.03,
                y: center[1] - length / 2.0,
                w: 0.06,
                h: length,
                height: 0.78,
                bottom: z + 0.08,
                // Local +y runs along the half-shell; `a` already carries the structure's own turn.
                rotation: (-dir[0]).atan2(dir[1]),
                kind: "base_cover".into(),
                id: format!("base_cover_{face}_{k}"),
                source_page: 39,
            });
        }
    }
    walls
}

/// Solid proxies of the outpost that a 2027 micro aerial robot cannot fly into, from the
/// outpost mesh (figure 4-34): the tapered tower, its rotating collar and the top housing
/// as a radius profile about the axis, heights above the outpost datum `b.z`.
fn outpost_column_radius(z: f64) -> Option<f64> {
    let lerp = |z0: f64, z1: f64, r0: f64, r1: f64| r0 + (r1 - r0) * ((z - z0) / (z1 - z0)).clamp(0.0, 1.0);
    let mut r: Option<f64> = None;
    let mut grow = |v: f64| r = Some(r.map_or(v, |old: f64| old.max(v)));
    if (0.0..=0.57).contains(&z) {
        grow(lerp(0.05, 0.57, 0.22, 0.196)); // skirt, hexagon corners
    }
    if (0.57..=1.44).contains(&z) {
        grow(lerp(0.57, 1.44, 0.173, 0.115)); // tower
    }
    if (1.065..=1.365).contains(&z) {
        grow(0.158); // rotating collar and its bands
    }
    if (1.44..=OUTPOST_TOP_M).contains(&z) {
        grow(0.148); // dart-detection housing (210 mm square)
    }
    r
}
const OUTPOST_TOP_M: f64 = 1.608;
/// Rotating armor module with its arm, in the face's frame: radial span (the armor face is
/// the plate centre radius), tangential and vertical half sizes.
const OUTPOST_MODULE: ([f64; 2], f64, f64) = ([0.15, 0.2965], 0.105, 0.08);

/// Where a flat drone (horizontal radius `r`, half height `hz`) at `p` overlaps the outpost:
/// the push-out vector and whether the contact is with rotating armor.
pub(crate) fn outpost_overlap(b: &Base, p: [f64; 3], r: f64, hz: f64) -> Option<([f64; 3], bool)> {
    let (dx, dy, z) = (p[0] - b.x, p[1] - b.y, p[2] - b.z);
    let mut best: Option<([f64; 3], bool)> = None;
    let mut keep = |push: [f64; 3], armor: bool| {
        let size = |v: &[f64; 3]| v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if best.as_ref().map_or(true, |(old, _)| size(&push) < size(old)) {
            best = Some((push, armor));
        }
    };
    // Column: widest section within the drone's height band, or the housing's top face.
    let d = dx.hypot(dy);
    let band = (0..=4)
        .filter_map(|k| outpost_column_radius(z - hz + hz * 0.5 * k as f64))
        .fold(None, |acc: Option<f64>, v| Some(acc.map_or(v, |a| a.max(v))));
    if let Some(radius) = band {
        if d < radius + r {
            let (ux, uy) = if d > 1e-9 { (dx / d, dy / d) } else { (1.0, 0.0) };
            let radial = radius + r - d;
            let up = OUTPOST_TOP_M + hz - z;
            if z > OUTPOST_TOP_M - hz && up < radial {
                keep([0.0, 0.0, up], false);
            } else {
                keep([ux * radial, uy * radial, 0.0], false);
            }
        }
    }
    // Armor modules: the drone's footprint expanded box test in each face's frame.
    let ([inner, outer], half_t, half_z) = OUTPOST_MODULE;
    for face in 0..3 {
        let a = b.rotation + face as f64 * std::f64::consts::TAU / 3.0;
        let (c, s) = (a.cos(), a.sin());
        let (rho, tau, zeta) = (dx * c + dy * s, -dx * s + dy * c, z - OUTPOST_MIDDLE_Z[face]);
        let pen = [outer + r - rho, rho - (inner - r), half_t + r - tau, tau + half_t + r, half_z + hz - zeta, zeta + half_z + hz];
        if pen.iter().any(|v| *v <= 0.0) {
            continue;
        }
        // Leave by the shallowest face; never inward, where the collar is.
        let (k, depth) = pen.iter().enumerate().filter(|(k, _)| *k != 1).fold((0, f64::MAX), |m, (k, v)| if *v < m.1 { (k, *v) } else { m });
        let push = match k {
            0 => [c * depth, s * depth, 0.0],
            2 => [-s * depth, c * depth, 0.0],
            3 => [s * depth, -c * depth, 0.0],
            4 => [0.0, 0.0, depth],
            _ => [0.0, 0.0, -depth],
        };
        keep(push, true);
    }
    best
}

impl Engine {
    /// 2027 aerial robots stay outside the outposts' solid parts: each physics substep after
    /// the strike checks, a drone overlapping the tower, collar, housing or a rotating armor
    /// module is moved back to its surface and loses its approach speed relative to it (the
    /// armor bounces with the strike restitution). Strikes themselves are judged before this.
    pub(super) fn keep_drones_clear_2027(&mut self) {
        let restitution = self.sim["drone_impact"]["restitution"].as_f64().unwrap_or(0.35);
        let now = self.time();
        for i in 0..self.robots.len() {
            if self.robots[i].role != "aerial" || self.robots[i].drones.is_empty() {
                continue;
            }
            let radius = self.robot_models[&self.robots[i].design_slot().to_string()]["drone"]["radius_m"].as_f64().unwrap_or(0.07);
            // The guard ducts' corners reach about 1.23 radii; the frame is 0.4 radii high.
            let (corner, hz) = (radius * 1.23, radius * 0.4);
            for k in 0..self.robots[i].drones.len() {
                if self.robots[i].drones[k].state == "docked" {
                    continue;
                }
                let mut moved = false;
                for _ in 0..3 {
                    let d = &self.robots[i].drones[k];
                    let p = [d.x, d.y, d.z];
                    let mut contact = None;
                    for team in ["red", "blue"] {
                        let b = &self.outposts[team];
                        // Plates use the strike radius so the strike window stays reachable.
                        let hit = outpost_overlap(b, p, radius, hz)
                            .filter(|(_, armor)| *armor)
                            .or_else(|| outpost_overlap(b, p, corner, hz).filter(|(_, armor)| !*armor));
                        if let Some((push, armor)) = hit {
                            let spin = if armor && !b.rotation_stopped { 0.8 * std::f64::consts::PI * (now / 5.0).min(1.0) * self.rune_direction } else { 0.0 };
                            contact = Some((push, armor, spin, b.x, b.y));
                            break;
                        }
                    }
                    let Some((push, armor, spin, cx, cy)) = contact else { break };
                    let length = (push[0] * push[0] + push[1] * push[1] + push[2] * push[2]).sqrt().max(1e-12);
                    let n = [push[0] / length, push[1] / length, push[2] / length];
                    let d = &mut self.robots[i].drones[k];
                    d.x += push[0];
                    d.y += push[1];
                    d.z += push[2];
                    let surface = [-spin * (d.y - cy), spin * (d.x - cx), 0.0];
                    let vn = (d.vx - surface[0]) * n[0] + (d.vy - surface[1]) * n[1] + (d.vz - surface[2]) * n[2];
                    if vn < 0.0 {
                        let e = if armor { restitution } else { 0.0 };
                        d.vx -= (1.0 + e) * vn * n[0];
                        d.vy -= (1.0 + e) * vn * n[1];
                        d.vz -= (1.0 + e) * vn * n[2];
                    }
                    moved = true;
                }
                if moved {
                    let d = &self.robots[i].drones[k];
                    let (at, v) = ([d.x, d.y, d.z], [d.vx, d.vy, d.vz]);
                    if let Some(world) = self.physics.as_mut() {
                        world.place_drone(i, k, at, v);
                    }
                }
            }
        }
    }
}
