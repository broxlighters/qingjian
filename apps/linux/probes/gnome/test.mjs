//! 运行窗口身份及扩展生命周期回归；不替代 GJS/桌面验收。
await import('./tests/identity.mjs');
await import('./tests/extension.mjs');
await import('./tests/notifier.mjs');
await import('./tests/geometry.mjs');
