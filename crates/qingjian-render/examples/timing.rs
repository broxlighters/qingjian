//! 固定测试语句的离线基线；不包含 Fcitx、IPC 或窗口贴图，不能代替桌面延迟。
#[path = "preview/samples.rs"]
mod samples;
use qingjian_render::{LayoutMode, RenderConfig, Renderer, ThemeMode};
use std::{error::Error, hint::black_box, time::Instant};
fn percentile(values: &mut [u64], fraction: usize) -> f64 {
    values.sort_unstable();
    values[(values.len() * fraction).div_ceil(100).saturating_sub(1)] as f64 / 1_000_000.0
}
fn main() -> Result<(), Box<dyn Error>> {
    let init = Instant::now();
    let mut renderer = Renderer::new()?;
    eprintln!(
        "系统字体初始化 {:.3} ms（不计入预热样本）",
        init.elapsed().as_secs_f64() * 1000.0
    );
    println!("sample,layout,scale,first_ms,p50_ms,p95_ms,p99_ms,layout_p95_ms,raster_p95_ms");
    for (name, source) in samples::frames() {
        for layout in [LayoutMode::Vertical, LayoutMode::Horizontal] {
            for scale in [1.0, 2.0] {
                let mut frame = source.clone();
                frame.theme = ThemeMode::Light;
                frame.layout = layout;
                let config = RenderConfig {
                    scale,
                    max_width: if name == "narrow" {
                        (180.0 * scale) as u32
                    } else {
                        (1000.0 * scale) as u32
                    },
                    max_height: (700.0 * scale) as u32,
                    ..Default::default()
                };
                let first = Instant::now();
                black_box(renderer.render(&frame, config)?);
                let cold = first.elapsed().as_secs_f64() * 1000.0;
                let (mut total, mut layout_ns, mut raster_ns) =
                    (Vec::new(), Vec::new(), Vec::new());
                for _ in 0..100 {
                    let started = Instant::now();
                    let (output, timing) = renderer.render_timed(&frame, config)?;
                    black_box(output);
                    total.push(started.elapsed().as_nanos() as u64);
                    layout_ns.push(timing.layout_ns);
                    raster_ns.push(timing.raster_ns);
                }
                println!(
                    "{name},{layout:?},{scale},{cold:.3},{:.3},{:.3},{:.3},{:.3},{:.3}",
                    percentile(&mut total, 50),
                    percentile(&mut total, 95),
                    percentile(&mut total, 99),
                    percentile(&mut layout_ns, 95),
                    percentile(&mut raster_ns, 95)
                );
            }
        }
    }
    Ok(())
}
