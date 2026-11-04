use rmuc_sim::{config, Engine};
use serde_json::{json, Value};
use std::io::{self, BufRead, Write};

/// The team and optional robot slots an observation request is for.
fn viewer(request: &Value) -> Result<(&str, Option<Vec<u32>>), String> {
    let team = request["team"]
        .as_str()
        .filter(|t| ["red", "blue"].contains(t))
        .ok_or("invalid team")?;
    let slots = request.get("slots").and_then(Value::as_array).map(|s| {
        s.iter()
            .filter_map(|n| n.as_u64().map(|n| n as u32))
            .collect()
    });
    Ok((team, slots))
}

/// Encodes `object` with pre-encoded `fields` merged in, byte for byte as
/// `serde_json::to_vec` encodes the combined object (keys in sorted order).
fn encode_with(object: &Value, fields: &[(&str, &[u8])]) -> Result<Vec<u8>, String> {
    let map = object.as_object().ok_or("object required")?;
    let mut keys: Vec<&str> = map
        .keys()
        .map(String::as_str)
        .chain(fields.iter().map(|(key, _)| *key))
        .collect();
    keys.sort();
    let mut out = vec![b'{'];
    for (index, key) in keys.into_iter().enumerate() {
        if index > 0 {
            out.push(b',');
        }
        serde_json::to_writer(&mut out, key).map_err(|e| e.to_string())?;
        out.push(b':');
        match map.get(key) {
            Some(value) => serde_json::to_writer(&mut out, value).map_err(|e| e.to_string())?,
            None => out.extend_from_slice(fields.iter().find(|(k, _)| *k == key).unwrap().1),
        }
    }
    out.push(b'}');
    Ok(out)
}

fn main() {
    let mut engine: Option<Engine> = None;
    // The arena never changes during a match; observations reuse its encoding.
    let mut arena_json: Vec<u8> = Vec::new();
    let stdin = io::stdin();
    let mut stdout = io::BufWriter::new(io::stdout());
    'requests: for line in stdin.lock().lines() {
        let Ok(line) = line else {
            break;
        };
        // Raw observation lines that follow the response, for the batched "observations" op.
        let mut extra: Vec<Vec<u8>> = Vec::new();
        let response = (|| -> Result<Value, String> {
            let request: Value = serde_json::from_str(&line).map_err(|e| e.to_string())?;
            let op = request["op"].as_str().ok_or("missing op")?;
            if op == "config" {
                let c = config();
                return Ok(
                    json!({"specs":c.specs,"maps":c.maps,"dt":rmuc_sim::DT,"engine":"rust","version":"0.2.0"}),
                );
            }
            if op == "init" {
                let mut next = Engine::new_with_settings(
                    request["seed"].as_i64().ok_or("seed must be i64")?,
                    request["duration"].as_f64().ok_or("duration required")?,
                    request["map"].as_str().ok_or("map required")?,
                    request
                        .get("lineups")
                        .filter(|v| !v.is_null())
                        .map(|v| serde_json::from_value(v.clone()))
                        .transpose()
                        .map_err(|e| e.to_string())?,
                    request.get("battle_settings").cloned(),
                )?;
                if let Some(scenario) = request.get("training_scenario").filter(|v| !v.is_null()) {
                    next.apply_training_scenario(scenario)?;
                }
                engine = Some(next);
                // Through `Value`, like the observation itself, so keys come out sorted.
                arena_json = serde_json::to_vec(&json!(engine.as_ref().unwrap().arena))
                    .map_err(|e| e.to_string())?;
                return Ok(engine.as_ref().unwrap().snapshot());
            }
            let e = engine.as_mut().ok_or("engine not initialized")?;
            match op {
                // "static":false omits the match-constant fields; the client keeps them from init.
                "step" if request["static"] == false => {
                    e.advance(&request["commands"]);
                    Ok(e.dynamic_snapshot())
                }
                "step" => Ok(e.step(&request["commands"])),
                "snapshot" => Ok(e.snapshot()),
                "observations" => {
                    // One round trip for every policy: a header with each observation's
                    // controlled ids, then one raw observation line per request.
                    // `"static":false` leaves the arena out of the lines so a transport that
                    // already handed the map over once does not resend 66 KB per decision.
                    let with_arena = request.get("static").map(|v| v != &Value::Bool(false)).unwrap_or(true);
                    let mut controlled = Vec::new();
                    for item in request["requests"].as_array().ok_or("requests required")? {
                        let (team, slots) = viewer(item)?;
                        let observation = e.observation_without_arena(team, slots.as_deref());
                        controlled.push(observation["controlled_ids"].clone());
                        extra.push(if with_arena {
                            encode_with(&observation, &[("arena", &arena_json)])?
                        } else {
                            encode_with(&observation, &[])?
                        });
                    }
                    Ok(json!({"controlled_ids": controlled}))
                }
                "digest" => Ok(json!({"digest":e.digest()})),
                "observation" => {
                    let (team, slots) = viewer(&request)?;
                    Ok(e.observation(team, slots.as_deref()))
                }
                "finish" => {
                    e.finish(
                        request["winner"].as_str().map(str::to_owned),
                        request["reason"].as_str().unwrap_or("stopped"),
                    );
                    Ok(e.snapshot())
                }
                _ => Err("unknown op".into()),
            }
        })();
        let value = response.unwrap_or_else(|error| {
            extra.clear();
            json!({"error":error})
        });
        if serde_json::to_writer(&mut stdout, &value).is_err() {
            break;
        }
        if writeln!(&mut stdout).is_err() {
            break;
        }
        for line in &extra {
            if stdout.write_all(line).is_err() || writeln!(&mut stdout).is_err() {
                break 'requests;
            }
        }
        if stdout.flush().is_err() {
            break;
        }
    }
}
