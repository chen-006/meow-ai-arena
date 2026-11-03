use crate::{Deployment, Engine, SLOT_IDS};
use serde_json::json;

#[test]
fn deck_models_are_independent_from_deployment_ids() {
    let lineups = [
        (
            "red".into(),
            vec![
                Deployment { slot: 1, model: 7 },
                Deployment { slot: 8, model: 3 },
                Deployment { slot: 9, model: 3 },
            ],
        ),
        ("blue".into(), vec![Deployment { slot: 1, model: 6 }]),
    ]
    .into();
    let mut engine = Engine::new_with_lineups(42, 1.0, "rmuc2026", Some(lineups)).unwrap();
    assert_eq!(engine.robots.len(), 4);
    assert_eq!(engine.robots[0].design_slot(), 7);
    assert_eq!(engine.robots[1].design_slot(), 3);
    assert!(engine.robots[1].legs[0] > 0.0);
    assert_ne!(
        (engine.robots[1].x, engine.robots[1].y),
        (engine.robots[2].x, engine.robots[2].y)
    );
    let obs = engine.observation("red", Some(&[8]));
    assert_eq!(obs["controlled_ids"], json!(["8"]));
    assert_eq!(obs["robots"].as_array().unwrap().len(), 3);
    assert_eq!(obs["roster"]["red"], json!([1, 8, 9]));
    engine.step(&json!({"red":{"1":{"crouch":true}},"blue":{}}));
    assert!(engine.robots[0].crouched);
}

#[test]
fn maximum_deck_spawns_distinct_instances_for_repeated_models() {
    let list: Vec<_> = (1..=12).map(|slot| Deployment { slot, model: 1 }).collect();
    let engine = Engine::new_with_lineups(
        1,
        1.0,
        "rmuc2026",
        Some([("red".into(), list.clone()), ("blue".into(), list)].into()),
    )
    .unwrap();
    assert_eq!(engine.robots.len(), 24);
    for team in ["red", "blue"] {
        let robots: Vec<_> = engine.robots.iter().filter(|r| r.team == team).collect();
        for (i, a) in robots.iter().enumerate() {
            for b in &robots[i + 1..] {
                assert!((a.x - b.x).hypot(a.y - b.y) > a.radius + b.radius);
            }
        }
    }
}

#[test]
fn standard_lineup_and_observation_shape_are_preserved() {
    let engine = Engine::new(42, 1.0, "rmuc2026").unwrap();
    assert_eq!(engine.robots.len(), 12);
    assert_eq!(engine.snapshot()["roster"], json!(SLOT_IDS));
    assert_eq!(engine.observation("red", None)["roster"], json!(SLOT_IDS));
    assert!(engine.snapshot()["robots"][0].get("model_slot").is_none());
}
