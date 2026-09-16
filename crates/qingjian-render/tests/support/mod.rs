//! 固定许可字体，测试不依赖宿主系统的字体版本。
use qingjian_render::{FontLibrary, Renderer};
pub fn renderer() -> Renderer {
    Renderer::new(
        FontLibrary::from_fonts(
            [
                include_bytes!("../fonts/NotoSans-Regular.ttf").to_vec(),
                include_bytes!("../fonts/QingjianFixtureCJK.otf").to_vec(),
                include_bytes!("../fonts/QingjianFixtureEmoji.ttf").to_vec(),
            ],
            "Noto Sans",
            "zh-CN",
        )
        .unwrap(),
    )
}
