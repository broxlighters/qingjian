//! Linux 候选面板选择；实验期默认 Fcitx，修改后重启服务生效。
mod renderer;
pub use renderer::LinuxRenderer;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct LinuxUiConfig {
    /// 默认面板、能力允许时自动选择、优先请求青简自绘。
    pub renderer: LinuxRenderer,
}
