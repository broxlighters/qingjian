//! Linux 行内预编辑快照；不扩充跨平台候选帧。
use qingjian_core::RawPreedit;
use serde::Serialize;

#[derive(Default, Serialize)]
pub struct ClientPreedit {
    pub text: String,

    /// UTF-8 字节偏移，来自同一次 Core 原样文本读取。
    pub cursor_bytes: usize,
}

impl From<RawPreedit> for ClientPreedit {
    fn from(raw: RawPreedit) -> Self {
        Self {
            text: raw.text,
            cursor_bytes: raw.cursor_bytes,
        }
    }
}
