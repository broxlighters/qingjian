//! 行内快照在事件后生成，与窗口显示、会话身份和提交责任分别校验。
#[path = "../events/support.rs"]
mod support;
use serde_json::{Value, json};
use support::{caps, compose, event, key, router};

fn raw(response: &Value, text: &str, cursor: usize) {
    assert_eq!(
        response["client_preedit"],
        json!({"text": text, "cursor_bytes": cursor})
    );
    assert!(text.is_char_boundary(cursor));
}

#[test]
fn raw_snapshot_follows_edits_commits_and_poll_without_losing_the_suffix() {
    let mut router = router();
    let first = compose(&mut router, 1, "nihao");
    raw(&first, "nihao", 5);
    let marked: String = first["frame"]["preedit"]
        .as_array()
        .unwrap()
        .iter()
        .map(|s| s["text"].as_str().unwrap())
        .collect();
    assert_eq!(marked, "ni'hao");
    raw(&key(&mut router, 1, 0x24, None, false), "nihao", 0);
    raw(&key(&mut router, 1, 0x27, None, false), "nihao", 1);
    raw(&key(&mut router, 1, 0x27, None, false), "nihao", 2);
    raw(
        &key(&mut router, 1, b'x'.into(), Some('x'), false),
        "nixhao",
        3,
    );
    raw(&key(&mut router, 1, 0x2e, None, false), "nixao", 3);
    raw(&key(&mut router, 1, 0x23, None, false), "nixao", 5);
    let polled = router
        .handle_linux(json!({"Poll": {"session": 1}}))
        .unwrap();
    raw(&polled["Update"], "nixao", 5);
    assert_eq!(polled["Update"]["identity"]["context"], "context-1");
    let committed = key(&mut router, 1, 13, None, false);
    assert_eq!(committed["commit"], "nixao");
    raw(&committed, "", 0);
    compose(&mut router, 1, "nihao");
    let selected = key(&mut router, 1, 32, Some(' '), false);
    assert_eq!(selected["commit"], "你好");
    raw(&selected, "", 0);
}

#[test]
fn other_session_clear_and_focus_responses_cannot_disclose_current_input() {
    let mut router = router();
    let a = compose(&mut router, 1, "ni");
    let b = compose(&mut router, 2, "nihao");
    raw(&a, "ni", 2);
    raw(&b, "nihao", 5);
    assert_ne!(a["identity"]["context"], b["identity"]["context"]);
    raw(&event(&mut router, 1, json!("Reset")), "", 0);
    raw(
        &event(&mut router, 1, json!({"Focus": {"focused": false}})),
        "",
        0,
    );
    raw(&caps(&mut router, 1, true, false, false), "", 0);
    let current = key(&mut router, 2, 0x25, None, false);
    raw(&current, "nihao", 4);
    raw(
        &event(&mut router, 2, json!({"Focus": {"focused": false}})),
        "",
        0,
    );
    raw(
        &event(&mut router, 2, json!({"Focus": {"focused": true}})),
        "nihao",
        4,
    );
    let ignored = router.handle_linux(json!({"LinuxEvent": {"session": 1, "event": {"Candidate": {"identity": a["identity"], "index": 0}}}})).unwrap();
    assert_eq!(ignored, json!({"Ignored": {"session": 1}}));
    raw(&key(&mut router, 2, 0x23, None, false), "nihao", 5);
}

#[test]
fn privacy_and_deactivation_send_explicit_empty_snapshots_without_duplicate_commit() {
    for (sensitive, inline) in [(false, false), (false, true), (true, true)] {
        let mut router = router();
        caps(&mut router, 1, sensitive, false, false);
        raw(&compose(&mut router, 1, "nihao"), "nihao", 5);
        let out = json!({"Deactivate": {"focus_out": true, "client_preedit": inline, "capability_changed": false}});
        let result = event(&mut router, 1, out.clone());
        raw(&result, "", 0);
        assert_eq!(
            result["commit"],
            if inline { json!(null) } else { json!("nihao") }
        );
        let repeated = event(&mut router, 1, out);
        raw(&repeated, "", 0);
        assert!(repeated["commit"].is_null());
    }
    for disabled in [false, true] {
        let mut router = router();
        compose(&mut router, 1, "nihao");
        raw(&caps(&mut router, 1, true, !disabled, disabled), "", 0);
        let blocked = compose(&mut router, 1, "n");
        raw(&blocked, "", 0);
        assert_eq!(blocked["outcome"], "Passthrough");
        assert!(blocked["commit"].is_null());
    }
}
