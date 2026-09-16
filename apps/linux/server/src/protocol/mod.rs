//! Linux 独立的面板协议扩展；Windows ClientMessage/ServerMessage 和版本不变。
mod display;
mod identity;
pub use display::DisplayAcknowledged;
pub use identity::DisplayIdentity;
pub const LINUX_UI_PROTOCOL: u32 = 1;
