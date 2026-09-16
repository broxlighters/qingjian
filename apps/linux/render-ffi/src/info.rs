//! 固定宽度 C ABI 的只读位图描述。
#[repr(C)]
#[derive(Default)]
pub struct ImageInfo {
    /// 物理像素宽。
    pub width: u32,

    /// 物理像素高。
    pub height: u32,

    /// width * 4。
    pub stride: u32,

    /// 位图字节数。
    pub length: u64,

    /// 预乘 RGBA；借用到 result 销毁为止。
    pub pixels: *const u8,

    /// 存在省略的内容。
    pub truncated: u32,

    /// 排版与整形耗时（纳秒）。
    pub layout_ns: u64,

    /// 位图绘制耗时（纳秒）。
    pub raster_ns: u64,
}
