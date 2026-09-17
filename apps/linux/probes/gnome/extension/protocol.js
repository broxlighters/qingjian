//! 阶段 0 独立协议，正式候选桥接须在真实输入验收后另行冻结。
export const BUS = 'org.qingjian.PanelProbe1';
export const PATH = '/org/qingjian/PanelProbe1';
export const XML = `<node><interface name="${BUS}">
  <method name="Hello"><arg type="u" direction="in"/><arg type="u" direction="out"/></method>
  <method name="Bind">
    <arg type="s" direction="in"/><arg type="s" direction="in"/>
    <arg type="s" direction="in"/><arg type="s" direction="in"/>
    <arg type="s" direction="in"/><arg type="s" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="d" direction="in"/>
    <arg type="d" direction="out"/><arg type="u" direction="out"/><arg type="u" direction="out"/>
  </method>
  <method name="Prepare">
    <arg type="s" direction="in"/><arg type="h" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="out"/>
  </method>
  <method name="Show"><arg type="s" direction="in"/></method>
  <method name="Hide"/>
  <method name="Withdraw"/>
  <method name="Renew"><arg type="s" direction="in"/></method>
  <signal name="Painted"><arg type="s"/></signal>
  <signal name="GeometryChanged"><arg type="s"/></signal>
  <signal name="Hidden"><arg type="s"/></signal>
  <signal name="Pointer"><arg type="s"/><arg type="u"/><arg type="u"/><arg type="u"/></signal>
</interface></node>`;

export function validBuffer(identity, width, height, stride, length) {
    return typeof identity === 'string' && /^[1-9][0-9]{0,19}$/.test(identity)
        && BigInt(identity) <= 18446744073709551615n
        && Number.isInteger(width) && width > 0 && width <= 1600
        && Number.isInteger(height) && height > 0 && height <= 900
        && stride === width * 4 && length === stride * height
        && length <= 5760000;
}
