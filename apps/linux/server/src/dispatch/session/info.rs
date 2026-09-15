//! 单个 Fcitx 输入上下文保存的输入状态。
use crate::dispatch::composed::Composed;
use qingjian_core::EngineSession;

pub(crate) struct SessionInfo {
    /// 应用标识。
    pub(crate) app: Option<String>,
    /// 新会话默认私密，收到能力通知后才允许学习。
    pub(crate) private: bool,
    /// 挂起的 Engine 输入状态。
    pub(crate) engine: EngineSession,
    /// 挂起的候选列表。
    pub(crate) composed: Option<Composed>,
    /// 高亮下标。
    pub(crate) highlight: usize,
    /// 本轮是否移动候选。
    pub(crate) navigated: bool,
}
impl SessionInfo {
    pub(crate) fn new(app: Option<String>) -> Self {
        Self {
            app,
            private: true,
            engine: EngineSession::default(),
            composed: None,
            highlight: 0,
            navigated: false,
        }
    }
}
