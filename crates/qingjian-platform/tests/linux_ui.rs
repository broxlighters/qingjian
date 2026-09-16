//! Linux UI 开关的默认值和严格枚举，不影响其他平台的通用协议版本。
use qingjian_platform::{Config, LinuxRenderer};
#[test]
fn linux_ui_defaults_to_fcitx_and_rejects_typos() {
    assert_eq!(Config::default().linux_ui.renderer, LinuxRenderer::Fcitx);
    for (name, mode) in [
        ("fcitx", LinuxRenderer::Fcitx),
        ("auto", LinuxRenderer::Auto),
        ("qingjian", LinuxRenderer::Qingjian),
    ] {
        let config: Config = toml::from_str(&format!("[linux_ui]\nrenderer = '{name}'\n")).unwrap();
        assert_eq!(config.linux_ui.renderer, mode);
    }
    assert!(toml::from_str::<Config>("[linux_ui]\nrenderer = 'typo'\n").is_err());
}
