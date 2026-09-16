//! 有界面板的结果与配置；业务义项索引由平台适配层从片段位置换算。
mod config;
mod rendered;
pub use config::PanelConfig;
pub use rendered::RenderedPanel;
