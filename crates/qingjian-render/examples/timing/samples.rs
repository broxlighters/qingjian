//! 固定短句用于新版有界面板离线计时，不含真实输入。
use qingjian_render::{Frame, Preedit, Row, Tone};
pub fn frames() -> Vec<(&'static str, Frame)> {
    ["basic", "mixed", "long", "narrow"]
        .into_iter()
        .map(|name| {
            let mut row = Row::plain(
                0,
                if name == "long" {
                    "很长的候选词".repeat(24)
                } else {
                    "青简".into()
                },
            );
            row.annotation = vec![
                (
                    if name == "long" {
                        "long translation ".repeat(32)
                    } else {
                        "hello".into()
                    },
                    Tone::Fresh,
                ),
                (" · ".into(), Tone::Faint),
                ("日本語(にほんご) 👩‍💻".into(), Tone::Gloss),
            ];
            let rows = if name == "mixed" {
                vec![row.clone(), Row::plain(1, ""), row]
            } else {
                vec![row; 5]
            };
            (
                name,
                Frame {
                    preedit: Some(Preedit::plain("qingjian", 8)),
                    rows,
                    highlighted: Some(0),
                    ..Default::default()
                },
            )
        })
        .collect()
}
