//! Linux 候选面板选择；实验期默认 Fcitx，修改后重启服务生效。
mod renderer;
pub use renderer::LinuxRenderer;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct LinuxUiConfig {
    /// 默认面板、能力允许时自动选择、优先请求青简自绘。
    pub renderer: LinuxRenderer,

    /// Linux 独立 UI 大小，75–200；无效值使用 100 并记录诊断。
    pub ui_scale_percent: i64,

    /// 跟随系统文字大小，不影响显示器的栅格倍率。
    pub follow_system_text_scale: bool,
}

impl Default for LinuxUiConfig {
    fn default() -> Self {
        Self {
            renderer: LinuxRenderer::default(),
            ui_scale_percent: 100,
            follow_system_text_scale: true,
        }
    }
}

impl LinuxUiConfig {
    pub fn effective_ui_scale_percent(&self) -> u32 {
        if (75..=200).contains(&self.ui_scale_percent) {
            self.ui_scale_percent as u32
        } else {
            tracing::warn!(
                value = self.ui_scale_percent,
                "Linux UI 大小超出 75–200，采用 100"
            );
            100
        }
    }
}
