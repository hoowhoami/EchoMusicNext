// SPDX-License-Identifier: AGPL-3.0-only
// Qt6.12 QV4 omits %TypedArray%.prototype.sort; ECMAScript numeric in-place semantics.
for (const Constructor of [Float32Array, Float64Array]) {
  if (typeof Constructor.prototype.sort === 'function') continue;
  Object.defineProperty(Constructor.prototype, 'sort', {
    configurable: true,
    writable: true,
    value(compare) {
      const values = Array.from(this);
      const numeric = (a, b) => {
        if (Number.isNaN(a)) return Number.isNaN(b) ? 0 : 1;
        if (Number.isNaN(b)) return -1;
        if (a === 0 && b === 0) return 1 / a < 1 / b ? -1 : 1 / a > 1 / b ? 1 : 0;
        return a - b;
      };
      values.sort(compare === undefined ? numeric : compare);
      this.set(values);
      return this;
    },
  });
}
