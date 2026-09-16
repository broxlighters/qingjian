//! Linux 面板选择，无法可靠定位时所有模式都允许回退。
use serde::{Deserialize, Serialize};
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum LinuxRenderer {
    /// 强制默认面板。
    #[default]
    Fcitx,
    /// 仅已验收的后端使用自绘。
    Auto,
    /// 请求自绘，无法可靠显示时回退。
    Qingjian,
}
