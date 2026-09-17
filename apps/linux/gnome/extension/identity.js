//! 短命按键证据签发可撤销窗口凭证；单调时间负责期限，uint32 时间只作匹配。
export class FocusProofs {
    constructor(now, focused) {
        this._now = now;
        this._focused = focused;
        this._epoch = 0;
        this._sequence = 0;
        this._history = [];
        this._current = null;
        this._proof = null;
    }

    invalidate() {
        ++this._epoch;
        ++this._sequence;
        this._current = null;
        this._proof = null;
        // 消费墓碑保留到 TTL，不能因 Hide/Owner 更换而再次签发。
        this.prune();
    }

    revoke() {
        this._proof = null;
        if (this._current?.claim) {
            this._current.revoked = true;
            this._current.claim = null;
        }
    }

    prune() {
        const now = this._now();
        this._history = this._history.filter(item => now - item.received <= 500000);
        if (this._current && now - this._current.received > 500000) this._current = null;
        if (this._proof && now >= this._proof.expires) this._proof = null;
    }

    record(window, time, code) {
        this.prune();
        if (window !== this._focused() || !Number.isInteger(time) || time < 0 || time > 0xffffffff ||
            !Number.isInteger(code) || code <= 0 || code > 0xffff) return false;
        const duplicates = this._history.filter(item => item.time === time && item.code === code);
        for (const item of duplicates) item.ambiguous = true;
        const entry = {window, time, code, epoch: this._epoch, sequence: ++this._sequence,
            received: this._now(), consumed: false, ambiguous: duplicates.length > 0};
        this._history.push(entry);
        this._history = this._history.slice(-32);
        this._current = entry;
        this._proof = null;
        return true;
    }

    _source(source) {
        const {bus, sender, path, context, epoch} = source;
        if (!/^[a-f0-9]{32}$/.test(bus) || !/^:[0-9]+\.[0-9]+$/.test(sender) ||
            !/^\/org\/freedesktop\/portal\/inputcontext\/[0-9]+$/.test(path) ||
            !/^[a-f0-9]{32}$/.test(context) || !/^[1-9][0-9]{0,19}$/.test(epoch) ||
            BigInt(epoch) > 18446744073709551615n) return null;
        return JSON.stringify([bus, sender, path, context, epoch]);
    }

    reserve(source, owner) {
        this.prune();
        const key = this._source(source);
        if (!key) return null;
        if (this.valid(this._proof) && this._proof.source === key && this._proof.owner === owner)
            return {proof: this._proof, owner, source: key};
        const entry = this._current;
        if (!entry || entry.issued || entry.revoked || entry.ambiguous || entry.window !== this._focused() ||
            entry.epoch !== this._epoch || entry.time !== source.time || entry.code !== source.code) return null;
        if (entry.consumed) return entry.claim?.source === key && entry.claim.owner === owner ? entry.claim : null;
        entry.consumed = true;
        entry.claim = {entry, owner, source: key};
        return entry.claim;
    }

    commit(claim, pid) {
        if (claim?.proof) return this.valid(claim.proof) && pid > 0 &&
            pid === claim.proof.window.get_pid() ? claim.proof : null;
        const entry = claim?.entry;
        if (!entry || entry.issued || entry.revoked || entry !== this._current || entry.ambiguous || entry.epoch !== this._epoch ||
            entry.sequence !== this._sequence || entry.window !== this._focused() ||
            this._now() - entry.received > 500000 || !Number.isInteger(pid) || pid <= 0 ||
            pid !== entry.window.get_pid()) return null;
        // 凭证不保留键码和时间；只要代次仍有效，cursor-only 更新可续用窗口身份。
        this._proof = {window: entry.window, epoch: entry.epoch, sequence: entry.sequence,
            source: claim.source, owner: claim.owner, expires: this._now() + 500000};
        entry.issued = true;
        entry.claim = null;
        return this._proof;
    }

    valid(proof) {
        return Boolean(proof && proof === this._proof && proof.window === this._focused() &&
            proof.epoch === this._epoch && proof.sequence === this._sequence && this._now() < proof.expires);
    }

    renew(proof) {
        if (!this.valid(proof)) return false;
        proof.expires = this._now() + 500000;
        return true;
    }
}
