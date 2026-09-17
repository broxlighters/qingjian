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

#[test]
fn linux_ui_size_defaults_boundaries_and_invalid_values() {
    let defaults: Config = toml::from_str("[linux_ui]\nrenderer='qingjian'").unwrap();
    assert_eq!(defaults.linux_ui.effective_ui_scale_percent(), 100);
    assert!(defaults.linux_ui.follow_system_text_scale);
    for (requested, effective) in [
        (i64::MIN, 100),
        (i64::MAX, 100),
        (-1, 100),
        (74, 100),
        (75, 75),
        (125, 125),
        (200, 200),
        (201, 100),
    ] {
        let config: Config = toml::from_str(&format!(
            "[linux_ui]\nui_scale_percent={requested}\nfollow_system_text_scale=false"
        ))
        .unwrap();
        assert_eq!(config.linux_ui.effective_ui_scale_percent(), effective);
        assert!(!config.linux_ui.follow_system_text_scale);
    }
}
