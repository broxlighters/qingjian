//! Linux 尚未接辅码按键；直接注入既有 Core 辅码态验证显示封装与原样提交边界。
use super::support::{compose, router};
use qingjian_platform::protocol::KeyModifiers;
use serde_json::json;

#[test]
fn client_snapshot_excludes_auxiliary_display_and_preserves_the_full_composition() {
    let mut router = router(9);
    compose(&mut router, "nihao", KeyModifiers::default());
    router.engine.enter_aux();
    assert!(router.engine.push_aux_code('k'));
    router.recompose();
    let update = router
        .handle_linux(json!({"Poll": {"session": 1}}))
        .unwrap();
    assert_eq!(
        update["Update"]["client_preedit"],
        json!({"text": "nihao", "cursor_bytes": 5})
    );
    let marked: String = update["Update"]["frame"]["preedit"]
        .as_array()
        .unwrap()
        .iter()
        .map(|part| part["text"].as_str().unwrap())
        .collect();
    assert!(marked.contains(';') && marked.ends_with('k'));
    let commit = router
        .handle_linux(json!({"Commit": {"session": 1}}))
        .unwrap();
    assert_eq!(commit["Committed"]["text"], "nihao");
}
