//! 渲染器自身工作耗时，不包括字体初始化、IPC 与窗口提交。
#[derive(Debug, Clone, Copy, Default)]
pub struct RenderTiming {
    /// 校验、测量、整形与排版纳秒。
    pub layout_ns: u64,

    /// 背景、字形绘制与合成纳秒。
    pub raster_ns: u64,
}
