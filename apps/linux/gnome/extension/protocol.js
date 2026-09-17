//! Panel1 v1：所有64位身份使用十进制字符串，位图和交互均绑定完整提交身份。
export const BUS = 'org.qingjian.Panel1';
export const PATH = '/org/qingjian/Panel1';
export const MAX_BYTES = 5760000;
export const XML = `<node><interface name="${BUS}">
  <method name="Hello"><arg type="u" direction="in"/><arg type="s" direction="out"/></method>
  <method name="BindContext"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <method name="PrepareFrame"><arg type="s" direction="in"/><arg type="h" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="s" direction="in"/>
  </method>
  <method name="Show"><arg type="s" direction="in"/></method>
  <method name="Hide"><arg type="s" direction="in"/></method>
  <method name="Renew"><arg type="s" direction="in"/></method>
  <method name="Status"><arg type="s" direction="out"/></method>
  <signal name="Prepared"><arg type="s"/></signal>
  <signal name="Painted"><arg type="s"/></signal>
  <signal name="Hidden"><arg type="s"/></signal>
  <signal name="Released"><arg type="s"/></signal>
  <signal name="GeometryChanged"><arg type="s"/></signal>
  <signal name="OutputsChanged"><arg type="s"/></signal>
  <signal name="Lost"><arg type="s"/><arg type="s"/></signal>
  <signal name="Pointer"><arg type="s"/><arg type="u"/><arg type="u"/><arg type="u"/></signal>
</interface></node>`;

const REASONS = new Set(['extension_missing', 'protocol_mismatch', 'unsupported_frontend', 'anchor_unavailable',
    'focus_mismatch', 'scale_unresolved', 'buffer_invalid', 'paint_timeout', 'transport_lost', 'renderer_unavailable', 'xcb_failure']);

export function reasonCode(value, fallback = 'protocol_mismatch') {
    return REASONS.has(value) ? value : fallback;
}

export function uint64(value, zero = false) {
    return typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
        (zero || value !== '0') && BigInt(value) <= 18446744073709551615n;
}

export function identity(value) {
    if (typeof value !== 'string' || value.length > 1024) return null;
    try {
        const data = JSON.parse(value);
        if (!/^[a-f0-9]{32}$/.test(data.context)) return null;
        for (const field of ['generation', 'revision', 'transport_epoch', 'focus_epoch', 'submission', 'geometry_revision'])
            if (!uint64(data[field], field === 'geometry_revision')) return null;
        return data;
    } catch (_) {
        return null;
    }
}

export function validBuffer(width, height, stride, length, logicalWidth, logicalHeight, raster) {
    return Number.isInteger(width) && width > 1 && width <= 1600 &&
        Number.isInteger(height) && height > 1 && height <= 900 &&
        stride === width * 4 && length === stride * height && length <= MAX_BYTES &&
        Number.isFinite(logicalWidth) && Number.isFinite(logicalHeight) &&
        Math.abs(logicalWidth * raster - width) < 0.001 &&
        Math.abs(logicalHeight * raster - height) < 0.001;
}

export class Submissions {
    constructor(epoch) {
        this.epoch = epoch;
        this.latest = 0n;
        this.cancelled = 0n;
        this.current = null;
    }

    accept(token) {
        const data = identity(token);
        if (!data || data.transport_epoch !== this.epoch) return false;
        const serial = BigInt(data.submission);
        if (serial <= this.cancelled || serial < this.latest) return false;
        this.latest = serial;
        this.current = token;
        return true;
    }

    hide(token) {
        const data = identity(token);
        if (!data || data.transport_epoch !== this.epoch) return false;
        const serial = BigInt(data.submission);
        this.cancelled = serial > this.cancelled ? serial : this.cancelled;
        if (this.current && BigInt(identity(this.current).submission) <= this.cancelled) this.current = null;
        return true;
    }

    valid(token) {
        return token === this.current && identity(token) !== null;
    }
}
