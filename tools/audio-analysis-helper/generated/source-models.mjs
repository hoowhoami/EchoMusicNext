// Folia a8da4437; AGPL-3.0-only. Generated without algorithm changes; see ../LICENSE.
var __defProp = Object.defineProperty;
var __getOwnPropSymbols = Object.getOwnPropertySymbols;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __propIsEnum = Object.prototype.propertyIsEnumerable;
var __defNormalProp = (obj, key, value) =>
  key in obj
    ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value })
    : (obj[key] = value);
var __spreadValues = (a, b) => {
  for (var prop in b || (b = {})) if (__hasOwnProp.call(b, prop)) __defNormalProp(a, prop, b[prop]);
  if (__getOwnPropSymbols)
    for (var prop of __getOwnPropSymbols(b)) {
      if (__propIsEnum.call(b, prop)) __defNormalProp(a, prop, b[prop]);
    }
  return a;
};
var __async = (__this, __arguments, generator) => {
  return new Promise((resolve, reject) => {
    var fulfilled = (value) => {
      try {
        step(generator.next(value));
      } catch (e) {
        reject(e);
      }
    };
    var rejected = (value) => {
      try {
        step(generator.throw(value));
      } catch (e) {
        reject(e);
      }
    };
    var step = (x) =>
      x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
    step((generator = generator.apply(__this, __arguments)).next());
  });
};

// ../../../../Work/mine/folia-major/src/services/analysis/beatThisWgsl/kernels.ts
var GELU =
  /* wgsl */
  `
fn erf_f32(v: f32) -> f32 {
  let a = abs(v);
  let x = 1.0 / (1.0 + 0.3275911 * a);
  let y = 1.0 - ((((1.061405429 * x - 1.453152027) * x + 1.421413741) * x - 0.284496736) * x + 0.254829592) * x * exp(-a * a);
  return select(-y, y, v >= 0.0);
}
fn gelu(v: f32) -> f32 { return 0.5 * v * (1.0 + erf_f32(v * 0.7071067811865476)); }
`;
var STEM_BN =
  /* wgsl */
  `
struct P { frames: u32, bins: u32 }
@group(0) @binding(0) var<storage, read> mel: array<f32>;      // [T, bins]
@group(0) @binding(1) var<storage, read> scale: array<f32>;
@group(0) @binding(2) var<storage, read> shift: array<f32>;
@group(0) @binding(3) var<storage, read_write> out: array<f32>; // [bins, T]
@group(0) @binding(4) var<uniform> p: P;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let i = id.x;
  if (i >= p.frames * p.bins) { return; }
  let f = i / p.frames;
  let t = i % p.frames;
  out[i] = mel[t * p.bins + f] * scale[f] + shift[f];
}
`;
var CONV_GELU =
  /* wgsl */
  `
${GELU}
struct P { fout: u32, frames: u32, cin: u32, cout: u32, kf: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read> w: array<f32>;   // [kf, 3, cin, cout] = [K, cout]
@group(0) @binding(2) var<storage, read> b: array<f32>;
@group(0) @binding(3) var<storage, read_write> y: array<f32>;
@group(0) @binding(4) var<uniform> p: P;
var<workgroup> as_: array<f32, 1024>; // [64 rows][16 k]
var<workgroup> bs_: array<f32, 1024>; // [16 k][64 cols]

fn gather(row: u32, k: u32) -> f32 {
  let ci = k % p.cin;
  let tap = k / p.cin;
  let dt = tap % 3u;
  let df = tap / 3u;
  let t = row % p.frames;
  let fo = row / p.frames;
  let ti = i32(t) + i32(dt) - 1;
  if (ti < 0 || ti >= i32(p.frames)) { return 0.0; }
  return x[((fo * p.kf + df) * p.frames + u32(ti)) * p.cin + ci];
}

@compute @workgroup_size(16, 16)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {
  let m = p.fout * p.frames;
  let kTotal = p.kf * 3u * p.cin;
  let row0 = wg.y * 64u;
  let col0 = wg.x * 64u;
  var acc: array<array<f32, 4>, 4>;
  for (var k0 = 0u; k0 < kTotal; k0 += 16u) {
    for (var i = 0u; i < 4u; i++) {
      let idx = li + i * 256u;
      let ar = row0 + idx / 16u;
      let ak = k0 + idx % 16u;
      var av = 0.0;
      if (ar < m && ak < kTotal) { av = gather(ar, ak); }
      as_[idx] = av;
      let bk = k0 + idx / 64u;
      let bc = col0 + idx % 64u;
      var bv = 0.0;
      if (bk < kTotal && bc < p.cout) { bv = w[bk * p.cout + bc]; }
      bs_[idx] = bv;
    }
    workgroupBarrier();
    for (var kk = 0u; kk < 16u; kk++) {
      var a: array<f32, 4>;
      var bb: array<f32, 4>;
      for (var i = 0u; i < 4u; i++) { a[i] = as_[(l.y * 4u + i) * 16u + kk]; }
      for (var j = 0u; j < 4u; j++) { bb[j] = bs_[kk * 64u + l.x * 4u + j]; }
      for (var i = 0u; i < 4u; i++) {
        for (var j = 0u; j < 4u; j++) { acc[i][j] += a[i] * bb[j]; }
      }
    }
    workgroupBarrier();
  }
  for (var i = 0u; i < 4u; i++) {
    let r = row0 + l.y * 4u + i;
    if (r >= m) { continue; }
    for (var j = 0u; j < 4u; j++) {
      let c = col0 + l.x * 4u + j;
      if (c >= p.cout) { continue; }
      y[r * p.cout + c] = gelu(acc[i][j] + b[c]);
    }
  }
}
`;
var MATMUL_GELU = 1;
var MATMUL_RESIDUAL = 2;
var MATMUL_NORM = 4;
var MATMUL_ROPE = 8;
var matmulSource = (weights, mode = 'f32') => {
  const f16 = mode !== 'f32';
  return (
    /* wgsl */
    `
${f16 ? 'enable f16;' : ''}
${GELU}
struct P { m: u32, n: u32, k: u32, flags: u32, posDiv: u32, posMod: u32, dmodel: u32, wStride: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
${weights}
@group(0) @binding(2) var<storage, read> bias: array<f32>;
@group(0) @binding(3) var<storage, read_write> y: array<f32>;
@group(0) @binding(4) var<storage, read> table: array<f32>; // [pos][16][cos, sin]
@group(0) @binding(5) var<uniform> p: P;
${
  f16
    ? `var<workgroup> as_: array<vec4h, 256>; // [16 k][16 row quads]
var<workgroup> bs_: array<vec4h, 256>; // [16 k][16 col quads]
var<workgroup> sums_: array<vec4f, 256>; // per-loader row sums of squares
const A_SCALE = 64.0;`
    : `var<workgroup> as_: array<vec4f, 256>; // [16 k][16 row quads]; afterwards the per-loader row sums of squares
var<workgroup> bs_: array<vec4f, 256>; // [16 k][16 col quads]`
}
@compute @workgroup_size(16, 16)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {
  let row0 = wg.y * 64u;
  let col0 = wg.x * 64u;
  var acc: array<vec4f, 4>; // acc[j] = rows 4*l.y..+3 of column 4*l.x + j
  // Loader roles: A - invocation li reads k = li % 16 of row quad li / 16; B - k = li / 16, col quad li % 16.
  let aK = li % 16u;
  let aQuad = li / 16u;
  let bK = li / 16u;
  let bQuad = li % 16u;
  var squares = vec4f(0.0);
  for (var k0 = 0u; k0 < p.k; k0 += 16u) {
    var av = vec4f(0.0);
    let ak = k0 + aK;
    if (ak < p.k) {
      for (var r = 0u; r < 4u; r++) {
        let row = row0 + aQuad * 4u + r;
        if (row < p.m) { av[r] = x[row * p.k + ak]; }
      }
    }
    squares = fma(av, av, squares);
    as_[aK * 16u + aQuad] = ${f16 ? 'vec4h(select(av, av / A_SCALE, (p.flags & 4u) != 0u))' : 'av'};
    var bv = ${f16 ? 'vec4h' : 'vec4f'}(0.0);
    let bk = k0 + bK;
    let bc = col0 + bQuad * 4u;
    if (bk < p.k && bc < p.n) { bv = weightQuad((bk * p.wStride + bc) / 4u); }
    bs_[bK * 16u + bQuad] = bv;
    workgroupBarrier();
${
  f16
    ? `${mode === 'math' ? '    var part: array<vec4h, 4>;\n' : ''}    for (var kk = 0u; kk < 16u; kk++) {
      let a = as_[kk * 16u + l.y];
      let b = bs_[kk * 16u + l.x];
${
  mode === 'math'
    ? `      part[0] = fma(a, vec4h(b.x), part[0]);
      part[1] = fma(a, vec4h(b.y), part[1]);
      part[2] = fma(a, vec4h(b.z), part[2]);
      part[3] = fma(a, vec4h(b.w), part[3]);
    }
    for (var j = 0u; j < 4u; j++) { acc[j] += vec4f(part[j]); }`
    : `      let af = vec4f(a);
      let bf = vec4f(b);
      acc[0] = fma(af, vec4f(bf.x), acc[0]);
      acc[1] = fma(af, vec4f(bf.y), acc[1]);
      acc[2] = fma(af, vec4f(bf.z), acc[2]);
      acc[3] = fma(af, vec4f(bf.w), acc[3]);
    }`
}`
    : `    for (var kk = 0u; kk < 16u; kk++) {
      let a = as_[kk * 16u + l.y];
      let b = bs_[kk * 16u + l.x];
      acc[0] = fma(a, vec4f(b.x), acc[0]);
      acc[1] = fma(a, vec4f(b.y), acc[1]);
      acc[2] = fma(a, vec4f(b.z), acc[2]);
      acc[3] = fma(a, vec4f(b.w), acc[3]);
    }`
}
    workgroupBarrier();
  }
  // The 16 loaders of row quad q are invocations 16q..16q+15; their partial sums meet in ${f16 ? 'sums_' : 'as_'}.
  ${f16 ? 'sums_' : 'as_'}[li] = squares;
  workgroupBarrier();
  var rowScale = vec4f(1.0);
  if ((p.flags & 4u) != 0u) {
    var total = vec4f(0.0);
    for (var t = 0u; t < 16u; t++) { total += ${f16 ? 'sums_' : 'as_'}[l.y * 16u + t]; }
    rowScale = ${f16 ? 'A_SCALE' : '1.0'} / max(sqrt(total), vec4f(1e-12));
  }
  for (var i = 0u; i < 4u; i++) {
    let r = row0 + l.y * 4u + i;
    if (r >= p.m) { continue; }
    var v: array<f32, 4>;
    for (var j = 0u; j < 4u; j++) {
      let c = min(col0 + l.x * 4u + j, p.n - 1u);
      v[j] = acc[j][i] * rowScale[i] + bias[c];
    }
    let c0 = col0 + l.x * 4u;
    if ((p.flags & 8u) != 0u && c0 < 2u * p.dmodel) {
      let pos = (r / p.posDiv) % p.posMod;
      for (var pair = 0u; pair < 2u; pair++) {
        let t = (pos * 16u + ((c0 + 2u * pair) % 32u) / 2u) * 2u;
        let cs = table[t];
        let sn = table[t + 1u];
        let x0 = v[2u * pair];
        let x1 = v[2u * pair + 1u];
        v[2u * pair] = x0 * cs - x1 * sn;
        v[2u * pair + 1u] = x1 * cs + x0 * sn;
      }
    }
    for (var j = 0u; j < 4u; j++) {
      let c = c0 + j;
      if (c >= p.n) { continue; }
      var res = v[j];
      let o = r * p.n + c;
      if ((p.flags & 1u) != 0u) { res = gelu(res); }
      if ((p.flags & 2u) != 0u) { res += y[o]; }
      y[o] = res;
    }
  }
}
`
  );
};
var MATMUL = matmulSource(
  /* wgsl */
  `
@group(0) @binding(1) var<storage, read> w: array<vec4f>;
fn weightQuad(i: u32) -> vec4f { return w[i]; }`,
);
var MATMUL_F16W = matmulSource(
  /* wgsl */
  `
@group(0) @binding(1) var<storage, read> w: array<vec2u>; // four fp16 columns per element
fn weightQuad(i: u32) -> vec4f { let q = w[i]; return vec4f(unpack2x16float(q.x), unpack2x16float(q.y)); }`,
);
var F16_WEIGHTS =
  /* wgsl */
  `
@group(0) @binding(1) var<storage, read> w: array<vec4h>;
fn weightQuad(i: u32) -> vec4h { return w[i]; }`;
var MATMUL_F16 = matmulSource(F16_WEIGHTS, 'storage');
var MATMUL_F16_MATH = matmulSource(F16_WEIGHTS, 'math');
var attentionSource = (f16) =>
  /* wgsl */
  `
${f16 ? 'enable f16;' : ''}
struct P { n: u32, heads: u32, width: u32, dmodel: u32, seqStride: u32, batchStride: u32 }
@group(0) @binding(0) var<storage, read> qkv: array<f32>;
@group(0) @binding(1) var<storage, read_write> o: array<f32>; // [rows, dmodel]
@group(0) @binding(2) var<uniform> p: P;
const TILE = 32u;
var<workgroup> ks: array<${(f16 == null ? void 0 : f16.k) ? 'vec4h' : 'vec4f'}, 256>; // [TILE keys][8 x vec4]
var<workgroup> vs: array<${f16 ? 'vec4h' : 'vec4f'}, 256>;
@compute @workgroup_size(64)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u) {
  let b = wg.z;
  let h = wg.y;
  let qi = wg.x * 64u + l.x;
  let valid = qi < p.n;
  let qrow = b * p.batchStride + min(qi, p.n - 1u) * p.seqStride;
  var q: array<vec4f, 8>;
  var acc: array<vec4f, 8>;
  let qBase = qrow * p.width + h * 32u;
  for (var d = 0u; d < 8u; d++) {
    q[d] = vec4f(qkv[qBase + 4u * d], qkv[qBase + 4u * d + 1u], qkv[qBase + 4u * d + 2u], qkv[qBase + 4u * d + 3u]) * 0.17677669529663687;
    acc[d] = vec4f(0.0);
  }
  var m = -3.0e38;
  var sum = 0.0;
  for (var k0 = 0u; k0 < p.n; k0 += TILE) {
    // 32 keys x 8 vec4 = 256 vec4 each for K and V, four per invocation.
    for (var i = 0u; i < 4u; i++) {
      let idx = l.x + i * 64u;
      let kj = k0 + idx / 8u;
      var kv = vec4f(0.0);
      var vv = vec4f(0.0);
      if (kj < p.n) {
        let base = (b * p.batchStride + kj * p.seqStride) * p.width + h * 32u + (idx % 8u) * 4u;
        let kb = base + p.dmodel;
        let vb = base + 2u * p.dmodel;
        kv = vec4f(qkv[kb], qkv[kb + 1u], qkv[kb + 2u], qkv[kb + 3u]);
        vv = vec4f(qkv[vb], qkv[vb + 1u], qkv[vb + 2u], qkv[vb + 3u]);
      }
      ks[idx] = ${(f16 == null ? void 0 : f16.k) ? 'vec4h(kv)' : 'kv'};
      vs[idx] = ${f16 ? 'vec4h(vv)' : 'vv'};
    }
    workgroupBarrier();
    // Constant trip counts so the compiler unrolls and keeps s[] in registers; keys past the end
    // of the sequence get a score of -3e38, whose exp is exactly 0.
    var s: array<f32, 32>;
    var tileMax = -3.0e38;
    for (var j = 0u; j < TILE; j++) {
      var dotv = vec4f(0.0);
      for (var d = 0u; d < 8u; d++) { dotv = fma(q[d], ${(f16 == null ? void 0 : f16.k) ? 'vec4f(ks[j * 8u + d])' : 'ks[j * 8u + d]'}, dotv); }
      s[j] = select(-3.0e38, dotv.x + dotv.y + dotv.z + dotv.w, k0 + j < p.n);
      tileMax = max(tileMax, s[j]);
    }
    let mNew = max(m, tileMax);
    let r = exp(m - mNew);
    sum *= r;
    for (var d = 0u; d < 8u; d++) { acc[d] *= r; }
    m = mNew;
${
  (f16 == null ? void 0 : f16.pv)
    ? `    var part: array<vec4h, 8>;
    for (var j = 0u; j < TILE; j++) {
      let e = exp(s[j] - m);
      sum += e;
      let eh = vec4h(f16(e));
      for (var d = 0u; d < 8u; d++) { part[d] = fma(eh, vs[j * 8u + d], part[d]); }
    }
    for (var d = 0u; d < 8u; d++) { acc[d] += vec4f(part[d]); }`
    : `    for (var j = 0u; j < TILE; j++) {
      let e = exp(s[j] - m);
      sum += e;
      for (var d = 0u; d < 8u; d++) { acc[d] = fma(vec4f(e), ${f16 ? 'vec4f(vs[j * 8u + d])' : 'vs[j * 8u + d]'}, acc[d]); }
    }`
}
    workgroupBarrier();
  }
  if (!valid) { return; }
  let gate = 1.0 / (1.0 + exp(-qkv[qrow * p.width + 3u * p.dmodel + h]));
  let scale = gate / sum;
  let oBase = qrow * p.dmodel + h * 32u;
  for (var d = 0u; d < 8u; d++) {
    let v = acc[d] * scale;
    o[oBase + 4u * d] = v.x;
    o[oBase + 4u * d + 1u] = v.y;
    o[oBase + 4u * d + 2u] = v.z;
    o[oBase + 4u * d + 3u] = v.w;
  }
}
`;
var ATTENTION = attentionSource(null);
var ATTENTION_F16 = attentionSource({ k: true, pv: false });
var ATTENTION_F16_MATH = attentionSource({ k: true, pv: true });
var FLATTEN =
  /* wgsl */
  `
struct P { freqs: u32, frames: u32, channels: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read_write> y: array<f32>;
@group(0) @binding(2) var<uniform> p: P;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let i = id.x;
  if (i >= p.freqs * p.frames * p.channels) { return; }
  let c = i % p.channels;
  let t = (i / p.channels) % p.frames;
  let f = i / (p.channels * p.frames);
  y[(t * p.freqs + f) * p.channels + c] = x[i];
}
`;
var SUM_HEAD =
  /* wgsl */
  `
struct P { frames: u32 }
@group(0) @binding(0) var<storage, read> l: array<f32>;
@group(0) @binding(1) var<storage, read_write> out: array<f32>;
@group(0) @binding(2) var<uniform> p: P;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let t = id.x;
  if (t >= p.frames) { return; }
  out[t] = l[t * 2u] + l[t * 2u + 1u];
  out[p.frames + t] = l[t * 2u + 1u];
}
`;

// ../../../../Work/mine/folia-major/src/services/analysis/beatThisWgsl/runtime.ts
var BEAT_THIS_WGSL_FRAMES = 1500;
var MEL_BINS = 128;
var HEAD_DIM = 32;
var RELEARN_RUNS = 50;
var product = (shape) => shape.reduce((a, b) => a * b, 1);
var KERNEL_NAMES = /* @__PURE__ */ new Map([
  [STEM_BN, 'stem_bn'],
  [CONV_GELU, 'conv'],
  [MATMUL, 'matmul'],
  [MATMUL_F16W, 'matmul'],
  [MATMUL_F16, 'matmul'],
  [MATMUL_F16_MATH, 'matmul'],
  [ATTENTION, 'attention'],
  [ATTENTION_F16, 'attention'],
  [ATTENTION_F16_MATH, 'attention'],
  [FLATTEN, 'flatten'],
  [SUM_HEAD, 'sum_head'],
]);
var BeatThisWgsl = class _BeatThisWgsl {
  constructor(device, build, melBuffer, outBuffer, owned) {
    this.device = device;
    this.build = build;
    this.melBuffer = melBuffer;
    this.outBuffer = outBuffer;
    this.owned = owned;
    /** Stages (by position among the stage markers) that 'auto' slicing has learnt to cut per dispatch. */
    this.fineStages = /* @__PURE__ */ new Set();
    this.autoRuns = 0;
    /** Graphs by chunk length: the full one, and the most recent shorter one. */
    this.graphs = /* @__PURE__ */ new Map();
  }
  /** GPU memory this model holds (weights and every graph built), bytes: for a residency budget. */
  get bytes() {
    let sum = this.owned.reduce((total, buffer) => total + buffer.size, 0);
    for (const graph of this.graphs.values())
      sum += graph.owned.reduce((total, buffer) => total + buffer.size, 0);
    return sum;
  }
  graph(frames) {
    let found = this.graphs.get(frames);
    if (!found) {
      for (const [length, old] of this.graphs) {
        if (length === BEAT_THIS_WGSL_FRAMES) continue;
        old.owned.forEach((buffer) => buffer.destroy());
        this.graphs.delete(length);
      }
      found = this.build(frames);
      this.graphs.set(frames, found);
    }
    return found.steps;
  }
  /**
   * Uploads the weights (the .bin as written, f32 or --f16) and builds the whole fixed graph for one
   * 1500-frame chunk; shorter chunks get theirs on first use. `f16` (device created with 'shader-f16'): 'storage' keeps matmul and attention
   * tiles in f16 with f32 arithmetic, 'math' multiplies in f16 too (see kernels.ts for what each
   * costs). Without it the fp16 weights are unpacked and everything is computed in f32.
   */
  static create(_0, _1, _2) {
    return __async(this, arguments, function* (device, weights, manifest, options = {}) {
      if (options.f16 && !device.features.has('shader-f16'))
        throw new Error('beat_this f16: device lacks shader-f16');
      const T = BEAT_THIS_WGSL_FRAMES;
      const owned = [];
      const storage = (floats, extra = 0) => {
        const buffer = device.createBuffer({
          size: Math.max(16, floats * 4),
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST | extra,
        });
        owned.push(buffer);
        return buffer;
      };
      const weightBuffer = storage(Math.ceil(weights.byteLength / 4));
      device.queue.writeBuffer(
        weightBuffer,
        0,
        weights.buffer,
        weights.byteOffset,
        weights.byteLength,
      );
      const entry = (name) => {
        const found = manifest.tensors[name];
        if (!found) throw new Error(`beat_this weights: missing tensor ${name}`);
        return found;
      };
      const tensor = (name) => {
        const { byte_offset: offset, shape: shape2, dtype, stride } = entry(name);
        const elements = stride ? shape2[0] * stride : product(shape2);
        return { buffer: weightBuffer, offset, size: elements * (dtype === 'f16' ? 2 : 4) };
      };
      const shape = (name) => manifest.tensors[name].shape;
      const dim = manifest.hyper_parameters.transformer_dim;
      const stem = manifest.hyper_parameters.stem_dim;
      const ffMult = manifest.hyper_parameters.ff_mult;
      const tokens0 = (MEL_BINS / 4) * T;
      const maxWidth = (c) => 3 * c + c / HEAD_DIM;
      const melBuffer = storage(T * MEL_BINS);
      const stemIn = storage(T * MEL_BINS);
      const x = [storage(tokens0 * stem), storage(tokens0 * stem)];
      const qkv = storage(Math.max(tokens0 * maxWidth(stem), T * maxWidth(dim)));
      const o = storage(tokens0 * stem);
      const u = storage(Math.max(tokens0 * stem * ffMult, T * dim * ffMult));
      const flat = storage(T * 4 * stem * 8);
      const logits = storage(T * 2);
      const outBuffer = storage(T * 2);
      const zeros = storage(4096);
      const freqsAt = weights.byteOffset + entry('rope.freqs').byte_offset;
      const freqs = new Float32Array(weights.buffer.slice(freqsAt, freqsAt + (HEAD_DIM / 2) * 4));
      const table = new Float32Array(T * 16 * 2);
      for (let pos = 0; pos < T; pos += 1) {
        for (let i = 0; i < 16; i += 1) {
          const angle = Math.fround(Math.fround(pos) * freqs[i]);
          table[(pos * 16 + i) * 2] = Math.cos(angle);
          table[(pos * 16 + i) * 2 + 1] = Math.sin(angle);
        }
      }
      const ropeTable = storage(table.length);
      device.queue.writeBuffer(ropeTable, 0, table);
      const pipelines2 = /* @__PURE__ */ new Map();
      const pipeline = (code) => {
        let found = pipelines2.get(code);
        if (!found) {
          found = device.createComputePipeline({
            layout: 'auto',
            compute: { module: device.createShaderModule({ code }), entryPoint: 'main' },
          });
          pipelines2.set(code, found);
        }
        return found;
      };
      const build = (T2) => {
        const graphOwned = [];
        const uniform = (values) => {
          const data = new Uint32Array(Math.ceil(values.length / 4) * 4);
          data.set(values);
          const buffer = device.createBuffer({
            size: data.byteLength,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
          });
          device.queue.writeBuffer(buffer, 0, data);
          graphOwned.push(buffer);
          return buffer;
        };
        const steps = [];
        let scope = 'stem';
        const dispatch = (code, bindings, params, count, detail = '') => {
          const compute = pipeline(code);
          const entries = [...bindings, { buffer: uniform(params) }].map((resource, binding) => ({
            binding,
            resource,
          }));
          const label = `${scope} ${KERNEL_NAMES.get(code)}${detail ? ` ${detail}` : ''}`;
          steps.push({
            kind: 'dispatch',
            label,
            pipeline: compute,
            group: device.createBindGroup({ layout: compute.getBindGroupLayout(0), entries }),
            count,
          });
        };
        const groups = (invocations) => [Math.ceil(invocations / 64), 1, 1];
        const stage = (name, buffer, floats) => steps.push({ kind: 'stage', name, buffer, floats });
        const matmul = (
          input,
          weight,
          bias,
          output,
          m,
          flags,
          rope = { posDiv: 1, posMod: 1, dmodel: 0 },
        ) => {
          const [k, n] = shape(weight);
          const { dtype, stride } = entry(weight);
          if (!stride)
            throw new Error(
              `beat_this weights: ${weight} has no row stride (re-export the weights)`,
            );
          const f16Kernel = options.f16 === 'math' ? MATMUL_F16_MATH : MATMUL_F16;
          dispatch(
            dtype !== 'f16' ? MATMUL : options.f16 ? f16Kernel : MATMUL_F16W,
            [
              { buffer: input },
              tensor(weight),
              bias ? tensor(bias) : { buffer: zeros },
              { buffer: output },
              { buffer: ropeTable },
            ],
            [m, n, k, flags, rope.posDiv, rope.posMod, rope.dmodel, stride],
            [Math.ceil(n / 64), Math.ceil(m / 64), 1],
            weight.split('.').slice(-2).join('.'),
          );
        };
        const transformerPair = (stream, prefix, rows, c, axis) => {
          const heads = c / HEAD_DIM;
          const width = 3 * c + heads;
          scope = prefix;
          matmul(
            stream,
            `${prefix}.attn.qkvg.w`,
            `${prefix}.attn.qkvg.b`,
            qkv,
            rows,
            MATMUL_NORM | MATMUL_ROPE,
            { posDiv: axis.posDiv, posMod: axis.n, dmodel: c },
          );
          dispatch(
            !options.f16 ? ATTENTION : options.f16 === 'math' ? ATTENTION_F16_MATH : ATTENTION_F16,
            [{ buffer: qkv }, { buffer: o }],
            [axis.n, heads, width, c, axis.seqStride, axis.batchStride],
            [Math.ceil(axis.n / 64), heads, axis.batches],
          );
          matmul(o, `${prefix}.attn.out.w`, null, stream, rows, MATMUL_RESIDUAL);
          matmul(stream, `${prefix}.ff.w1`, `${prefix}.ff.b1`, u, rows, MATMUL_NORM | MATMUL_GELU);
          matmul(u, `${prefix}.ff.w2`, `${prefix}.ff.b2`, stream, rows, MATMUL_RESIDUAL);
        };
        dispatch(
          STEM_BN,
          [
            { buffer: melBuffer },
            tensor('stem.bn.scale'),
            tensor('stem.bn.shift'),
            { buffer: stemIn },
          ],
          [T2, MEL_BINS],
          groups(T2 * MEL_BINS),
        );
        let freqs_ = MEL_BINS / 4;
        let channels = stem;
        dispatch(
          CONV_GELU,
          [{ buffer: stemIn }, tensor('stem.conv.w'), tensor('stem.conv.b'), { buffer: x[0] }],
          [freqs_, T2, 1, channels, 4],
          [Math.ceil(channels / 64), Math.ceil((freqs_ * T2) / 64), 1],
        );
        let cur = 0;
        stage('stem', x[cur], freqs_ * T2 * channels);
        for (let block = 0; block < 3; block += 1) {
          const rows = freqs_ * T2;
          transformerPair(x[cur], `block${block}.freq`, rows, channels, {
            n: freqs_,
            seqStride: T2,
            batchStride: 1,
            batches: T2,
            posDiv: T2,
          });
          stage(`block${block}_freq`, x[cur], rows * channels);
          transformerPair(x[cur], `block${block}.time`, rows, channels, {
            n: T2,
            seqStride: 1,
            batchStride: T2,
            batches: freqs_,
            posDiv: 1,
          });
          stage(`block${block}_time`, x[cur], rows * channels);
          const next = 1 - cur;
          scope = `block${block}`;
          dispatch(
            CONV_GELU,
            [
              { buffer: x[cur] },
              tensor(`block${block}.conv.w`),
              tensor(`block${block}.conv.b`),
              { buffer: x[next] },
            ],
            [freqs_ / 2, T2, channels, channels * 2, 2],
            [Math.ceil((channels * 2) / 64), Math.ceil(((freqs_ / 2) * T2) / 64), 1],
          );
          cur = next;
          freqs_ /= 2;
          channels *= 2;
          stage(`block${block}_conv`, x[cur], freqs_ * T2 * channels);
        }
        scope = 'frontend';
        dispatch(
          FLATTEN,
          [{ buffer: x[cur] }, { buffer: flat }],
          [freqs_, T2, channels],
          groups(freqs_ * T2 * channels),
        );
        cur = 1 - cur;
        matmul(flat, 'frontend.linear.w', 'frontend.linear.b', x[cur], T2, 0);
        stage('frontend', x[cur], T2 * dim);
        for (let layer = 0; layer < manifest.hyper_parameters.n_layers; layer += 1) {
          transformerPair(x[cur], `layer${layer}`, T2, dim, {
            n: T2,
            seqStride: 1,
            batchStride: T2,
            batches: 1,
            posDiv: 1,
          });
          stage(`layer${layer}`, x[cur], T2 * dim);
        }
        scope = 'head';
        matmul(x[cur], 'head.w', 'head.b', logits, T2, MATMUL_NORM);
        dispatch(SUM_HEAD, [{ buffer: logits }, { buffer: outBuffer }], [T2], groups(T2));
        stage('logits', outBuffer, T2 * 2);
        return { steps, owned: graphOwned };
      };
      const model = new _BeatThisWgsl(device, build, melBuffer, outBuffer, owned);
      model.graph(T);
      yield device.queue.onSubmittedWorkDone();
      return model;
    });
  }
  /**
   * One chunk. `mel` is [T, 128] frame-major, T from 1 to 1500, the layout beatThis.ts produces. `profile` puts every
   * dispatch in its own pass with timestamps (needs a device created with 'timestamp-query'); the
   * extra passes cost a little, so profiled totals run slightly above unprofiled ones.
   *
   * `slice` cuts the chunk into several submits so another user of the GPU (the visualizer) can get
   * frames in between: 'stage' submits at every stage boundary (stem, each frontend half-block and
   * conv, frontend, each transformer layer), 'dispatch' after every dispatch. Each slice waits for
   * the GPU to finish and then for `between`, if given, before the next is submitted - unless
   * `inFlight` allows more than one slice queued at once, in which case the run only waits when
   * that many are outstanding. The GPU only switches work between command buffers, so a slice is
   * the smallest unit anything else can wait behind. Results are identical whatever the slicing:
   * same dispatches, same order.
   *
   * 'auto' is 'stage' that learns: one slice in flight, and any stage whose slice took longer than
   * `budgetMs` (submit to done, so it counts time the GPU spent on other work too; default one
   * 120 Hz frame) is cut per dispatch from the next run on. What it learnt is forgotten every
   * RELEARN_RUNS runs, so a burst of heavy rendering does not keep every later track at the
   * slower cut. A fast GPU next to a light visualizer stays at ~18 submits a chunk; a slow or busy
   * one ends up near one per dispatch. A single dispatch longer than the budget is still one
   * unbroken piece of GPU work - only splitting the kernel itself would get under that.
   */
  run(_0) {
    return __async(this, arguments, function* (mel, options = {}) {
      var _a, _b, _c, _d, _e;
      const T = mel.length / MEL_BINS;
      if (!Number.isInteger(T) || T < 1 || T > BEAT_THIS_WGSL_FRAMES) {
        throw new Error(
          `beat_this chunk must be 1..${BEAT_THIS_WGSL_FRAMES} frames x ${MEL_BINS}, got ${mel.length} values`,
        );
      }
      const steps = this.graph(T);
      const { device } = this;
      device.queue.writeBuffer(this.melBuffer, 0, mel.buffer, mel.byteOffset, mel.byteLength);
      let encoder = device.createCommandEncoder();
      const readbacks = [];
      const profiled =
        options.profile && device.features.has('timestamp-query')
          ? steps.filter((step) => step.kind === 'dispatch')
          : [];
      const querySet = profiled.length
        ? device.createQuerySet({ type: 'timestamp', count: profiled.length * 2 })
        : null;
      let query = 0;
      let pass = null;
      let gpuMs = 0;
      let slices = 0;
      const slice = (_a = options.slice) != null ? _a : 'none';
      const inFlight = slice === 'auto' ? 1 : Math.max(1, (_b = options.inFlight) != null ? _b : 1);
      const outstanding = [];
      const submit = (last = false) =>
        __async(this, null, function* () {
          pass == null ? void 0 : pass.end();
          pass = null;
          const started = performance.now();
          device.queue.submit([encoder.finish()]);
          outstanding.push(device.queue.onSubmittedWorkDone());
          while (outstanding.length > (last ? 0 : inFlight - 1)) yield outstanding.shift();
          const took = performance.now() - started;
          gpuMs += took;
          slices += 1;
          encoder = device.createCommandEncoder();
          return took;
        });
      const budgetMs = (_c = options.budgetMs) != null ? _c : 8;
      if (slice === 'auto' && ++this.autoRuns % RELEARN_RUNS === 0) this.fineStages.clear();
      let stageIndex = 0;
      const wallStarted = performance.now();
      for (const step of steps) {
        const perDispatch =
          slice === 'dispatch' || (slice === 'auto' && this.fineStages.has(stageIndex));
        if (perDispatch && pass) {
          yield submit();
          yield (_d = options.between) == null ? void 0 : _d.call(options);
        }
        if (step.kind === 'dispatch') {
          if (querySet) {
            pass == null ? void 0 : pass.end();
            pass = encoder.beginComputePass({
              timestampWrites: {
                querySet,
                beginningOfPassWriteIndex: query,
                endOfPassWriteIndex: query + 1,
              },
            });
            query += 2;
          }
          pass != null ? pass : (pass = encoder.beginComputePass());
          pass.setPipeline(step.pipeline);
          pass.setBindGroup(0, step.group);
          pass.dispatchWorkgroups(...step.count);
          continue;
        }
        const coarse = slice === 'stage' || (slice === 'auto' && !this.fineStages.has(stageIndex));
        if (coarse && step.name !== 'logits') {
          const took = yield submit();
          if (slice === 'auto' && took > budgetMs) this.fineStages.add(stageIndex);
          yield (_e = options.between) == null ? void 0 : _e.call(options);
        }
        stageIndex += 1;
        if (!options.stages && step.name !== 'logits') continue;
        pass == null ? void 0 : pass.end();
        pass = null;
        const readback = device.createBuffer({
          size: step.floats * 4,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        encoder.copyBufferToBuffer(step.buffer, 0, readback, 0, step.floats * 4);
        readbacks.push({ name: step.name, buffer: readback });
      }
      pass == null ? void 0 : pass.end();
      let timestamps = null;
      if (querySet) {
        const resolved = device.createBuffer({
          size: query * 8,
          usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
        });
        timestamps = device.createBuffer({
          size: query * 8,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        encoder.resolveQuerySet(querySet, 0, query, resolved, 0);
        encoder.copyBufferToBuffer(resolved, 0, timestamps, 0, query * 8);
      }
      yield submit(true);
      const wallMs = performance.now() - wallStarted;
      const stages = {};
      for (const { name, buffer } of readbacks) {
        yield buffer.mapAsync(GPUMapMode.READ);
        stages[name] = new Float32Array(buffer.getMappedRange().slice(0));
        buffer.destroy();
      }
      let profile;
      if (timestamps) {
        yield timestamps.mapAsync(GPUMapMode.READ);
        const ticks = new BigInt64Array(timestamps.getMappedRange());
        profile = profiled.map((step, index) => ({
          label: step.kind === 'dispatch' ? step.label : '',
          ms: Number(ticks[index * 2 + 1] - ticks[index * 2]) / 1e6,
        }));
        timestamps.destroy();
        querySet == null ? void 0 : querySet.destroy();
      }
      const out = stages.logits;
      return __spreadValues(
        __spreadValues(
          {
            beat: out.subarray(0, T),
            downbeat: out.subarray(T, 2 * T),
            gpuMs,
            wallMs,
            slices,
          },
          options.stages ? { stages } : {},
        ),
        profile ? { profile } : {},
      );
    });
  }
  destroy() {
    this.owned.forEach((buffer) => buffer.destroy());
    this.graphs.forEach((graph) => graph.owned.forEach((buffer) => buffer.destroy()));
  }
};

// ../../../../Work/mine/folia-major/src/services/analysis/htdemucsWgsl/kernels.ts
var GELU2 =
  /* wgsl */
  `
fn erf_f32(v: f32) -> f32 {
  let a = abs(v);
  let x = 1.0 / (1.0 + 0.3275911 * a);
  let y = 1.0 - ((((1.061405429 * x - 1.453152027) * x + 1.421413741) * x - 0.284496736) * x + 0.254829592) * x * exp(-a * a);
  return select(-y, y, v >= 0.0);
}
fn gelu(v: f32) -> f32 { return 0.5 * v * (1.0 + erf_f32(v * 0.7071067811865476)); }
`;
var GEMM_GELU = 1;
var GEMM_RESIDUAL = 2;
var weightRead = (f16) =>
  f16
    ? /* wgsl */
      `
@group(0) @binding(1) var<storage, read> w: array<u32>;
fn wload(e: u32) -> f32 {
  let pair = unpack2x16float(w[e >> 1u]);
  return select(pair.x, pair.y, (e & 1u) == 1u);
}`
    : /* wgsl */
      `
@group(0) @binding(1) var<storage, read> w: array<f32>;
fn wload(e: u32) -> f32 { return w[e]; }`;
var tiles = (mode) =>
  /* wgsl */
  `
var<workgroup> as_: array<${mode === 'f16' ? 'vec4h' : 'vec4f'}, 256>;
var<workgroup> bs_: array<${mode === 'f16' ? 'vec4h' : 'vec4f'}, 256>;`;
var tileVector = (mode, value) => `${mode === 'f16' ? 'vec4h' : 'vec4f'}(${value})`;
var kStep = (mode) =>
  mode === 'f16'
    ? /* wgsl */
      `
    var part: array<vec4h, 4>;
    for (var kk = 0u; kk < 16u; kk++) {
      let a = as_[kk * 16u + l.y];
      let bb = bs_[kk * 16u + l.x];
      for (var i = 0u; i < 4u; i++) { part[i] = fma(vec4h(a[i]), bb, part[i]); }
    }
    for (var i = 0u; i < 4u; i++) { acc[i] += vec4f(part[i]); }`
    : /* wgsl */
      `
    for (var kk = 0u; kk < 16u; kk++) {
      let a = as_[kk * 16u + l.y];
      let bb = bs_[kk * 16u + l.x];
      for (var i = 0u; i < 4u; i++) { acc[i] = fma(vec4f(a[i]), bb, acc[i]); }
    }`;
var enableF16 = (mode) => (mode === 'f16' ? 'enable f16;' : '');
var gemmBody = (columns, mode) =>
  /* wgsl */
  `
${tiles(mode)}
@compute @workgroup_size(16, 16)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {
  let n = ${columns};
  let row0 = wg.y * 64u;
  let col0 = wg.x * 64u;
  var acc: array<vec4f, 4>;
  for (var k0 = 0u; k0 < p.k; k0 += 16u) {
    // Invocation li: rows 4 (li / 16).. at k li % 16 of A, columns 4 (li % 16).. at k li / 16 of B.
    let ak = k0 + li % 16u;
    var av = vec4f(0.0);
    let bk = k0 + li / 16u;
    var bv = vec4f(0.0);
    for (var c = 0u; c < 4u; c++) {
      let ar = row0 + (li / 16u) * 4u + c;
      if (ar < p.m && ak < p.k) { av[c] = wload(ar * p.k + ak); }
      let bc = col0 + (li % 16u) * 4u + c;
      if (bk < p.k && bc < n) { bv[c] = gather(bk, bc); }
    }
    as_[(li % 16u) * 16u + li / 16u] = ${tileVector(mode, 'av')};
    bs_[li] = ${tileVector(mode, 'bv')};
    workgroupBarrier();${kStep(mode)}
    workgroupBarrier();
  }
  for (var i = 0u; i < 4u; i++) {
    let r = row0 + l.y * 4u + i;
    if (r >= p.m) { continue; }
    for (var j = 0u; j < 4u; j++) {
      let c = col0 + l.x * 4u + j;
      if (c >= n) { continue; }
      store(r, c, acc[i][j]);
    }
  }
}
`;
var gemmSource = (f16, mode = 'f32') =>
  /* wgsl */
  `
${enableF16(mode)}
${GELU2}
struct P { m: u32, k: u32, cin: u32, kh: u32, kw: u32, sh: u32, sw: u32, ph: u32, pw: u32, dh: u32, dw: u32, hin: u32, win: u32, hout: u32, wout: u32, flags: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
${weightRead(f16)}
@group(0) @binding(2) var<storage, read> b: array<f32>;
@group(0) @binding(3) var<storage, read> side: array<f32>;
@group(0) @binding(4) var<storage, read_write> y: array<f32>;
@group(0) @binding(5) var<uniform> p: P;

fn gather(k: u32, n: u32) -> f32 {
  let taps = p.kh * p.kw;
  let ci = k / taps;
  let r = k - ci * taps;
  let a = r / p.kw;
  let c = r - a * p.kw;
  let ho = n / p.wout;
  let wo = n - ho * p.wout;
  let hi = i32(ho * p.sh + a * p.dh) - i32(p.ph);
  let wi = i32(wo * p.sw + c * p.dw) - i32(p.pw);
  if (hi < 0 || hi >= i32(p.hin) || wi < 0 || wi >= i32(p.win)) { return 0.0; }
  return x[(ci * p.hin + u32(hi)) * p.win + u32(wi)];
}

fn store(r: u32, c: u32, sum: f32) {
  var v = sum + b[r];
  if ((p.flags & ${GEMM_GELU}u) != 0u) { v = gelu(v); }
  let o = r * p.hout * p.wout + c;
  if ((p.flags & ${GEMM_RESIDUAL}u) != 0u) { y[o] = y[o] + side[r] * v; } else { y[o] = v; }
}
${gemmBody('p.hout * p.wout', mode)}
`;
var gemmTrSource = (f16, mode = 'f32') =>
  /* wgsl */
  `
${enableF16(mode)}
${GELU2}
struct P { m: u32, k: u32, cin: u32, hin: u32, win: u32, axis: u32, flags: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
${weightRead(f16)}
@group(0) @binding(2) var<storage, read> b: array<f32>;
@group(0) @binding(3) var<storage, read_write> y: array<f32>;
@group(0) @binding(4) var<uniform> p: P;

fn gather(k: u32, n: u32) -> f32 {
  let ci = k / 2u;
  let t = k - ci * 2u;
  if (p.axis == 0u) {
    let q = n / p.win;
    let w = n - q * p.win;
    if (q < t || q - t >= p.hin) { return 0.0; }
    return x[(ci * p.hin + q - t) * p.win + w];
  }
  let h = n / (p.win + 1u);
  let q = n - h * (p.win + 1u);
  if (q < t || q - t >= p.win) { return 0.0; }
  return x[(ci * p.hin + h) * p.win + q - t];
}

fn store(r: u32, c: u32, sum: f32) {
  let co = r / 4u;
  let ph = r - co * 4u;
  var v = sum + b[co];
  if ((p.flags & ${GEMM_GELU}u) != 0u) { v = gelu(v); }
  if (p.axis == 0u) {
    let q = c / p.win;
    let w = c - q * p.win;
    let o = 4u * q + ph;
    if (o < 2u || o - 2u >= 4u * p.hin) { return; }
    y[(co * 4u * p.hin + o - 2u) * p.win + w] = v;
  } else {
    let h = c / (p.win + 1u);
    let q = c - h * (p.win + 1u);
    let o = 4u * q + ph;
    if (o < 2u || o - 2u >= 4u * p.win) { return; }
    y[(co * p.hin + h) * 4u * p.win + o - 2u] = v;
  }
}
${gemmBody('select(p.hin * (p.win + 1u), (p.hin + 1u) * p.win, p.axis == 0u)', mode)}
`;
var STATS_PARTIAL =
  /* wgsl */
  `
struct P { parts: u32, c: u32, inner: u32, cs: u32, gs: u32, mode: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read> stats: array<f32>;
@group(0) @binding(2) var<storage, read_write> partial: array<f32>;
@group(0) @binding(3) var<uniform> p: P;
var<workgroup> sums: array<f32, 256>;
@compute @workgroup_size(256)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) li: u32) {
  let g = wg.y;
  let n = p.c * p.inner;
  let chunk = (n + p.parts - 1u) / p.parts;
  let lo = wg.x * chunk;
  let hi = min(n, lo + chunk);
  let mean = stats[g * 4u];
  var s = 0.0;
  for (var e = lo + li; e < hi; e += 256u) {
    let ch = e / p.inner;
    let v = x[ch * p.cs + g * p.gs + e - ch * p.inner];
    if (p.mode == 0u) { s += v; } else { let d = v - mean; s = fma(d, d, s); }
  }
  sums[li] = s;
  workgroupBarrier();
  for (var span = 128u; span > 0u; span >>= 1u) {
    if (li < span) { sums[li] += sums[li + span]; }
    workgroupBarrier();
  }
  if (li == 0u) { partial[g * p.parts + wg.x] = sums[0]; }
}
`;
var STATS_FINAL =
  /* wgsl */
  `
struct P { parts: u32, n: u32, mode: u32 }
@group(0) @binding(0) var<storage, read> partial: array<f32>;
@group(0) @binding(1) var<storage, read_write> stats: array<f32>;
@group(0) @binding(2) var<uniform> p: P;
var<workgroup> sums: array<f32, 256>;
@compute @workgroup_size(256)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) li: u32) {
  let g = wg.x;
  var s = 0.0;
  for (var i = li; i < p.parts; i += 256u) { s += partial[g * p.parts + i]; }
  sums[li] = s;
  workgroupBarrier();
  for (var span = 128u; span > 0u; span >>= 1u) {
    if (li < span) { sums[li] += sums[li + span]; }
    workgroupBarrier();
  }
  if (li != 0u) { return; }
  let total = sums[0];
  let n = f32(p.n);
  if (p.mode == 0u) {
    stats[g * 4u] = total / n;
  } else if (p.mode == 1u) {
    stats[g * 4u + 1u] = 1.0 / sqrt(total / n + 1e-5);
  } else {
    let sd = sqrt(total / (n - 1.0));
    stats[g * 4u + 1u] = 1.0 / (sd + 1e-5);
    stats[g * 4u + 2u] = sd;
  }
}
`;
var GN_INPLACE =
  /* wgsl */
  `
${GELU2}
struct P { c: u32, h: u32, w: u32, perH: u32, gelu: u32 }
@group(0) @binding(0) var<storage, read_write> x: array<f32>;
@group(0) @binding(1) var<storage, read> stats: array<f32>;
@group(0) @binding(2) var<storage, read> gamma: array<f32>;
@group(0) @binding(3) var<storage, read> beta: array<f32>;
@group(0) @binding(4) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  let plane = p.h * p.w;
  if (i >= p.c * plane) { return; }
  let ch = i / plane;
  let g = select(0u, (i - ch * plane) / p.w, p.perH == 1u);
  var v = (x[i] - stats[g * 4u]) * stats[g * 4u + 1u] * gamma[ch] + beta[ch];
  if (p.gelu == 1u) { v = gelu(v); }
  x[i] = v;
}
`;
var GN_GLU_RESIDUAL =
  /* wgsl */
  `
struct P { c: u32, h: u32, w: u32, perH: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read> stats: array<f32>;
@group(0) @binding(2) var<storage, read> gamma: array<f32>;
@group(0) @binding(3) var<storage, read> beta: array<f32>;
@group(0) @binding(4) var<storage, read> scale: array<f32>;
@group(0) @binding(5) var<storage, read_write> res: array<f32>;
@group(0) @binding(6) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  let plane = p.h * p.w;
  if (i >= p.c * plane) { return; }
  let ch = i / plane;
  let at = i - ch * plane;
  let g = select(0u, at / p.w, p.perH == 1u);
  let mean = stats[g * 4u];
  let rstd = stats[g * 4u + 1u];
  let a = (x[i] - mean) * rstd * gamma[ch] + beta[ch];
  let gch = ch + p.c;
  let gate = (x[gch * plane + at] - mean) * rstd * gamma[gch] + beta[gch];
  res[i] = res[i] + scale[ch] * (a / (1.0 + exp(-gate)));
}
`;
var GLU =
  /* wgsl */
  `
struct P { c: u32, h: u32, w: u32, emb: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read> emb: array<f32>;
@group(0) @binding(2) var<storage, read_write> y: array<f32>;
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  let plane = p.h * p.w;
  if (i >= p.c * plane) { return; }
  var v = x[i] / (1.0 + exp(-x[i + p.c * plane]));
  if (p.emb == 1u) {
    let ch = i / plane;
    v += emb[ch * p.h + (i - ch * plane) / p.w];
  }
  y[i] = v;
}
`;
var ADD =
  /* wgsl */
  `
struct P { n: u32 }
@group(0) @binding(0) var<storage, read_write> x: array<f32>;
@group(0) @binding(1) var<storage, read> y: array<f32>;
@group(0) @binding(2) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  if (i < p.n) { x[i] = x[i] + y[i]; }
}
`;
var NORMALIZE =
  /* wgsl */
  `
struct P { n: u32 }
@group(0) @binding(0) var<storage, read_write> x: array<f32>;
@group(0) @binding(1) var<storage, read> stats: array<f32>;
@group(0) @binding(2) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  if (i < p.n) { x[i] = (x[i] - stats[0]) * stats[1]; }
}
`;
var LAYER_NORM =
  /* wgsl */
  `
struct P { c: u32, n: u32, pos: u32 }
@group(0) @binding(0) var<storage, read> x: array<f32>;
@group(0) @binding(1) var<storage, read> gamma: array<f32>;
@group(0) @binding(2) var<storage, read> beta: array<f32>;
@group(0) @binding(3) var<storage, read> pos: array<f32>;
@group(0) @binding(4) var<storage, read_write> y: array<f32>;
@group(0) @binding(5) var<uniform> p: P;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let t = id.x;
  if (t >= p.n) { return; }
  var s = 0.0;
  for (var ch = 0u; ch < p.c; ch++) { s += x[ch * p.n + t]; }
  let mean = s / f32(p.c);
  var q = 0.0;
  for (var ch = 0u; ch < p.c; ch++) { let d = x[ch * p.n + t] - mean; q = fma(d, d, q); }
  let rstd = 1.0 / sqrt(q / f32(p.c) + 1e-5);
  for (var ch = 0u; ch < p.c; ch++) {
    var v = (x[ch * p.n + t] - mean) * rstd * gamma[ch] + beta[ch];
    if (p.pos == 1u) { v += pos[ch * p.n + t]; }
    y[ch * p.n + t] = v;
  }
}
`;
var attnScoresSource = (mode = 'f32') =>
  /* wgsl */
  `
${enableF16(mode)}
struct P { nq: u32, nk: u32, qr0: u32, kr0: u32, head0: u32 }
@group(0) @binding(0) var<storage, read> q: array<f32>;
@group(0) @binding(1) var<storage, read> kv: array<f32>;
@group(0) @binding(2) var<storage, read_write> s: array<f32>;
@group(0) @binding(3) var<uniform> p: P;
${tiles(mode)} // [16 d][16 query quads], [16 d][16 key quads]
@compute @workgroup_size(16, 16)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {
  let h = p.head0 + wg.z;
  let row0 = wg.y * 64u;
  let col0 = wg.x * 64u;
  var acc: array<vec4f, 4>;
  for (var d0 = 0u; d0 < 64u; d0 += 16u) {
    // Invocation li: queries and keys 4 (li % 16).. at dimension li / 16.
    let d = d0 + li / 16u;
    var av = vec4f(0.0);
    var bv = vec4f(0.0);
    for (var c = 0u; c < 4u; c++) {
      let e = (li % 16u) * 4u + c;
      let i = row0 + e;
      let j = col0 + e;
      av[c] = select(0.0, q[(p.qr0 + h * 64u + d) * p.nq + min(i, p.nq - 1u)], i < p.nq);
      bv[c] = select(0.0, kv[(p.kr0 + h * 64u + d) * p.nk + min(j, p.nk - 1u)], j < p.nk);
    }
    as_[li] = ${tileVector(mode, 'av')};
    bs_[li] = ${tileVector(mode, 'bv')};
    workgroupBarrier();${kStep(mode)}
    workgroupBarrier();
  }
  for (var i = 0u; i < 4u; i++) {
    let r = row0 + l.y * 4u + i;
    if (r >= p.nq) { continue; }
    for (var j = 0u; j < 4u; j++) {
      let c = col0 + l.x * 4u + j;
      if (c < p.nk) { s[(wg.z * p.nq + r) * p.nk + c] = acc[i][j]; }
    }
  }
}
`;
var ATTN_SOFTMAX =
  /* wgsl */
  `
struct P { nk: u32 }
@group(0) @binding(0) var<storage, read_write> s: array<f32>;
@group(0) @binding(1) var<uniform> p: P;
var<workgroup> red: array<f32, 256>;
@compute @workgroup_size(256)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(num_workgroups) groups: vec3u, @builtin(local_invocation_index) li: u32) {
  let base = (wg.x + wg.y * groups.x) * p.nk;
  var m = -3.0e38;
  for (var j = li; j < p.nk; j += 256u) { m = max(m, s[base + j]); }
  red[li] = m;
  workgroupBarrier();
  for (var span = 128u; span > 0u; span >>= 1u) {
    if (li < span) { red[li] = max(red[li], red[li + span]); }
    workgroupBarrier();
  }
  m = red[0];
  workgroupBarrier();
  var sum = 0.0;
  for (var j = li; j < p.nk; j += 256u) {
    let e = exp(s[base + j] - m);
    s[base + j] = e;
    sum += e;
  }
  red[li] = sum;
  workgroupBarrier();
  for (var span = 128u; span > 0u; span >>= 1u) {
    if (li < span) { red[li] += red[li + span]; }
    workgroupBarrier();
  }
  let inv = 1.0 / red[0];
  for (var j = li; j < p.nk; j += 256u) { s[base + j] *= inv; }
}
`;
var attnOutSource = (mode = 'f32') =>
  /* wgsl */
  `
${enableF16(mode)}
struct P { nq: u32, nk: u32, vr0: u32, head0: u32 }
@group(0) @binding(0) var<storage, read> s: array<f32>;
@group(0) @binding(1) var<storage, read> kv: array<f32>;
@group(0) @binding(2) var<storage, read_write> o: array<f32>;
@group(0) @binding(3) var<uniform> p: P;
${tiles(mode)} // [16 keys][16 query quads], [16 keys][16 d quads]
@compute @workgroup_size(16, 16)
fn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {
  let h = p.head0 + wg.z;
  let row0 = wg.y * 64u;
  var acc: array<vec4f, 4>;
  for (var j0 = 0u; j0 < p.nk; j0 += 16u) {
    // Invocation li: queries and dimensions 4 (li / 16).. at key li % 16.
    let jj = li % 16u;
    let j = j0 + jj;
    var a = vec4f(0.0);
    var b = vec4f(0.0);
    for (var c = 0u; c < 4u; c++) {
      let r = (li / 16u) * 4u + c;
      let i = row0 + r;
      if (j < p.nk) {
        if (i < p.nq) { a[c] = s[(wg.z * p.nq + i) * p.nk + j]; }
        b[c] = kv[(p.vr0 + h * 64u + r) * p.nk + j];
      }
    }
    as_[jj * 16u + li / 16u] = ${tileVector(mode, 'a')};
    bs_[jj * 16u + li / 16u] = ${tileVector(mode, 'b')};
    workgroupBarrier();${kStep(mode)}
    workgroupBarrier();
  }
  for (var i = 0u; i < 4u; i++) {
    let r = row0 + l.y * 4u + i;
    if (r >= p.nq) { continue; }
    for (var j = 0u; j < 4u; j++) {
      let d = l.x * 4u + j;
      o[(h * 64u + d) * p.nq + r] = acc[i][j];
    }
  }
}
`;
var TWIDDLE_N = 4096;
var STFT_A =
  /* wgsl */
  `
struct P { frames: u32, len: u32 }
@group(0) @binding(0) var<storage, read> mix: array<f32>;      // [2, len]
@group(0) @binding(1) var<storage, read> win: array<f32>;
@group(0) @binding(2) var<storage, read> tw: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> y: array<vec2f>;  // [2 frames, 64 k1, 64 n2]
@group(0) @binding(4) var<uniform> p: P;
fn reflect(i: i32, n: i32) -> i32 {
  var j = select(i, -i, i < 0);
  return select(j, 2 * (n - 1) - j, j >= n);
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let n2 = id.x;
  let k1 = id.y;
  let s = id.z;
  if (n2 >= 64u || k1 >= 64u || s >= 2u * p.frames) { return; }
  let ch = s / p.frames;
  let f = s - ch * p.frames;
  let padded = i32(p.len) + 3072;
  var acc = vec2f(0.0);
  for (var n1 = 0u; n1 < 64u; n1++) {
    let n = n1 * 64u + n2;
    let at = reflect(reflect(i32((f + 2u) * 1024u + n) - 2048, padded) - 1536, i32(p.len));
    let v = mix[ch * p.len + u32(at)] * win[n];
    let t = tw[((k1 * n1) % 64u) * 64u];
    acc += v * vec2f(t.x, -t.y);
  }
  let t = tw[k1 * n2];
  y[(s * 64u + k1) * 64u + n2] = vec2f(acc.x * t.x + acc.y * t.y, acc.y * t.x - acc.x * t.y);
}
`;
var STFT_B =
  /* wgsl */
  `
struct P { frames: u32 }
@group(0) @binding(0) var<storage, read> y: array<vec2f>;
@group(0) @binding(1) var<storage, read> tw: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> spec: array<f32>;
@group(0) @binding(3) var<uniform> p: P;
@compute @workgroup_size(32)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let k2 = id.x;
  let k1 = id.y;
  let s = id.z;
  if (k2 >= 32u || k1 >= 64u || s >= 2u * p.frames) { return; }
  var acc = vec2f(0.0);
  for (var n2 = 0u; n2 < 64u; n2++) {
    let v = y[(s * 64u + k1) * 64u + n2];
    let t = tw[((k2 * n2) % 64u) * 64u];
    acc += vec2f(v.x * t.x + v.y * t.y, v.y * t.x - v.x * t.y);
  }
  let ch = s / p.frames;
  let f = s - ch * p.frames;
  let k = k1 + 64u * k2;
  spec[((ch * 2u) * 2048u + k) * p.frames + f] = acc.x;
  spec[((ch * 2u + 1u) * 2048u + k) * p.frames + f] = acc.y;
}
`;
var ISTFT_A =
  /* wgsl */
  `
struct P { frames: u32, source: u32 }
@group(0) @binding(0) var<storage, read> z: array<f32>;     // [16, 2048, frames]
@group(0) @binding(1) var<storage, read> stats: array<f32>;
@group(0) @binding(2) var<storage, read> tw: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> y: array<vec2f>;
@group(0) @binding(4) var<uniform> p: P;
fn bin(ch: u32, f: u32, k: u32) -> vec2f {
  let kk = select(k, 4096u - k, k > 2048u);
  if (kk == 2048u) { return vec2f(0.0); }
  let base = (p.source * 4u + ch * 2u) * 2048u + kk;
  let re = z[base * p.frames + f] * stats[2] + stats[0];
  let im = z[(base + 2048u) * p.frames + f] * stats[2] + stats[0];
  return select(vec2f(re, im), vec2f(re, -im), k > 2048u);
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let n2 = id.x;
  let k1 = id.y;
  let s = id.z;
  if (n2 >= 64u || k1 >= 64u || s >= 2u * p.frames) { return; }
  let ch = s / p.frames;
  let f = s - ch * p.frames;
  var acc = vec2f(0.0);
  for (var k2 = 0u; k2 < 64u; k2++) {
    let v = bin(ch, f, k1 + 64u * k2);
    let t = tw[((k2 * n2) % 64u) * 64u];
    acc += vec2f(v.x * t.x - v.y * t.y, v.x * t.y + v.y * t.x);
  }
  let t = tw[k1 * n2];
  y[(s * 64u + k1) * 64u + n2] = vec2f(acc.x * t.x - acc.y * t.y, acc.x * t.y + acc.y * t.x);
}
`;
var ISTFT_B =
  /* wgsl */
  `
struct P { frames: u32 }
@group(0) @binding(0) var<storage, read> y: array<vec2f>;
@group(0) @binding(1) var<storage, read> tw: array<vec2f>;
@group(0) @binding(2) var<storage, read> win: array<f32>;
@group(0) @binding(3) var<storage, read_write> out: array<f32>;  // [2 frames, 4096]
@group(0) @binding(4) var<uniform> p: P;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let n2 = id.x;
  let n1 = id.y;
  let s = id.z;
  if (n2 >= 64u || n1 >= 64u || s >= 2u * p.frames) { return; }
  var acc = 0.0;
  for (var k1 = 0u; k1 < 64u; k1++) {
    let v = y[(s * 64u + k1) * 64u + n2];
    let t = tw[((k1 * n1) % 64u) * 64u];
    acc += v.x * t.x - v.y * t.y;
  }
  let n = n1 * 64u + n2;
  out[s * 4096u + n] = acc / 4096.0 * win[n];
}
`;
var OVERLAP_ADD =
  /* wgsl */
  `
struct P { frames: u32, len: u32, source: u32, out_at: u32 }
@group(0) @binding(0) var<storage, read> fr: array<f32>;
@group(0) @binding(1) var<storage, read> env: array<f32>;
@group(0) @binding(2) var<storage, read> xt: array<f32>;      // [8, len]
@group(0) @binding(3) var<storage, read> tstats: array<f32>;
@group(0) @binding(4) var<storage, read_write> out: array<f32>; // [2, len] at out_at (one per source asked for)
@group(0) @binding(5) var<uniform> p: P;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
  let i = id.x + id.y * groups.x * 256u;
  if (i >= 2u * p.len) { return; }
  let ch = i / p.len;
  let j = i - ch * p.len;
  let pos = 3584u + j;
  var acc = 0.0;
  let last = pos / 1024u;
  for (var jj = select(0u, last - 3u, last >= 3u); jj <= last; jj++) {
    if (jj < 2u || jj >= p.frames + 2u) { continue; }
    let n = pos - jj * 1024u;
    if (n < 4096u) { acc += fr[(ch * p.frames + jj - 2u) * 4096u + n]; }
  }
  let t = xt[(p.source * 2u + ch) * p.len + j] * tstats[2] + tstats[0];
  out[p.out_at + i] = acc / env[1536u + j] + t;
}
`;

// ../../../../Work/mine/folia-major/src/services/analysis/htdemucsWgsl/runtime.ts
var HTDEMUCS_SEGMENT = 172032;
var HTDEMUCS_SOURCES = ['drums', 'bass', 'other', 'vocals'];
var FRAMES = 168;
var FREQ_TOKENS = 8;
var BINS = 2048;
var DIM = 512;
var HEADS = 8;
var HEADS_PER_DISPATCH = 4;
var RELEARN_RUNS2 = 50;
var product2 = (shape) => shape.reduce((a, b) => a * b, 1);
var HtdemucsWgsl = class _HtdemucsWgsl {
  constructor(device, steps, mixBuffer, outBuffer, owned) {
    this.device = device;
    this.steps = steps;
    this.mixBuffer = mixBuffer;
    this.outBuffer = outBuffer;
    this.owned = owned;
    this.fineStages = /* @__PURE__ */ new Set();
    this.autoRuns = 0;
  }
  /** GPU memory this model holds (weights, activations, uniforms), bytes: for a residency budget. */
  get bytes() {
    return this.owned.reduce((sum, buffer) => sum + buffer.size, 0);
  }
  /**
   * Uploads the weights (the .bin as written, f32 or --f16) and builds the whole graph. `source` is
   * which stem a run returns (an index into HTDEMUCS_SOURCES); `sources`, which several (all four
   * for automix), the first returned as left/right.
   */
  static create(_0, _1, _2) {
    return __async(this, arguments, function* (device, weights, manifest, options = {}) {
      var _a, _b;
      if (manifest.segment !== HTDEMUCS_SEGMENT)
        throw new Error(
          `htdemucs weights are for a ${manifest.segment}-sample segment, not ${HTDEMUCS_SEGMENT}`,
        );
      if (options.f16 && !device.features.has('shader-f16'))
        throw new Error('htdemucs f16 needs a device opened with shader-f16');
      const sources =
        (_b = options.sources) != null ? _b : [(_a = options.source) != null ? _a : 0];
      if (
        sources.length === 0 ||
        sources.some((source) => !(source >= 0 && source < HTDEMUCS_SOURCES.length))
      )
        throw new Error(
          `htdemucs sources must be indices into ${HTDEMUCS_SOURCES.join(', ')}, got ${sources.join(', ')}`,
        );
      const mode = options.f16 ? 'f16' : 'f32';
      const ATTN_SCORES = attnScoresSource(mode);
      const ATTN_OUT = attnOutSource(mode);
      device.pushErrorScope('validation');
      const L = HTDEMUCS_SEGMENT;
      const owned = [];
      const storage = (floats) => {
        const buffer = device.createBuffer({
          size: Math.max(16, Math.ceil(floats) * 4),
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
        });
        owned.push(buffer);
        return buffer;
      };
      const weightBuffer = storage(Math.ceil(weights.byteLength / 4));
      device.queue.writeBuffer(
        weightBuffer,
        0,
        weights.buffer,
        weights.byteOffset,
        weights.byteLength,
      );
      const entry = (name) => {
        const found = manifest.tensors[name];
        if (!found) throw new Error(`htdemucs weights: missing tensor ${name}`);
        return found;
      };
      const tensor = (name) => {
        const { byte_offset: offset, shape, dtype } = entry(name);
        const bytes = product2(shape) * (dtype === 'f16' ? 2 : 4);
        return { buffer: weightBuffer, offset, size: Math.ceil(bytes / 4) * 4 };
      };
      const isHalf = (name) => entry(name).dtype === 'f16';
      const rows = (name) => entry(name).shape[0];
      const twiddles = new Float32Array(TWIDDLE_N * 2);
      for (let j = 0; j < TWIDDLE_N; j += 1) {
        twiddles[j * 2] = Math.cos((2 * Math.PI * j) / TWIDDLE_N);
        twiddles[j * 2 + 1] = Math.sin((2 * Math.PI * j) / TWIDDLE_N);
      }
      const tw = storage(twiddles.length);
      device.queue.writeBuffer(tw, 0, twiddles);
      const top = 48 * (BINS / 4) * FRAMES;
      const mixBuffer = storage(2 * L);
      const outBuffer = storage(2 * L * sources.length);
      const mixStats = storage(4);
      const specStats = storage(4);
      const scratchStats = storage(4 * BINS);
      const partial = storage(4096);
      const zeros = storage(64);
      const dft = storage(2 * FRAMES * 4096 * 2);
      const xin = storage(4 * BINS * FRAMES);
      const stream = storage(top);
      const hidden = storage(top / 8);
      const wide = storage(2 * top);
      const ping = [storage(16 * BINS * FRAMES), storage(16 * BINS * FRAMES)];
      const freqOut = storage(16 * BINS * FRAMES);
      const timeOut = storage(8 * L);
      const frames = storage(2 * FRAMES * 4096);
      const skips = [];
      const tskips = [];
      for (let i = 0; i < 4; i += 1) {
        skips.push(storage(48 * 2 ** i * (BINS / 4 ** (i + 1)) * FRAMES));
        tskips.push(storage(48 * 2 ** i * (L / 4 ** (i + 1))));
      }
      const nf = FREQ_TOKENS * FRAMES;
      const nt = L / 256;
      const xf = storage(DIM * nf);
      const xt = storage(DIM * nt);
      const normF = storage(DIM * nf);
      const normT = storage(DIM * nt);
      const otherF = storage(DIM * nf);
      const otherT = storage(DIM * nt);
      const qkvF = storage(3 * DIM * nf);
      const qkvT = storage(3 * DIM * nt);
      const kvF = storage(2 * DIM * nt);
      const kvT = storage(2 * DIM * nf);
      const attF = storage(DIM * nf);
      const attT = storage(DIM * nt);
      const ff = storage(4 * DIM * nf);
      const scores = storage(HEADS_PER_DISPATCH * nf * nf);
      const pipelines2 = /* @__PURE__ */ new Map();
      const pipeline = (code) => {
        let found = pipelines2.get(code);
        if (!found) {
          found = device.createComputePipeline({
            layout: 'auto',
            compute: { module: device.createShaderModule({ code }), entryPoint: 'main' },
          });
          pipelines2.set(code, found);
        }
        return found;
      };
      const uniform = (values) => {
        const data = new Uint32Array(Math.ceil(values.length / 4) * 4);
        data.set(values);
        const buffer = device.createBuffer({
          size: data.byteLength,
          usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(buffer, 0, data);
        owned.push(buffer);
        return buffer;
      };
      const steps = [];
      let scope = 'input';
      const kernelNames = /* @__PURE__ */ new Map([
        [ADD, 'add'],
        [ATTN_OUT, 'attn_out'],
        [ATTN_SCORES, 'attn_scores'],
        [ATTN_SOFTMAX, 'softmax'],
        [GLU, 'glu'],
        [GN_GLU_RESIDUAL, 'gn_glu'],
        [GN_INPLACE, 'gn'],
        [ISTFT_A, 'istft_a'],
        [ISTFT_B, 'istft_b'],
        [LAYER_NORM, 'layer_norm'],
        [NORMALIZE, 'normalize'],
        [OVERLAP_ADD, 'overlap_add'],
        [STATS_FINAL, 'stats_final'],
        [STATS_PARTIAL, 'stats'],
        [STFT_A, 'stft_a'],
        [STFT_B, 'stft_b'],
      ]);
      const dispatch = (code, bindings, params, count, detail = '') => {
        var _a2;
        const compute = pipeline(code);
        const entries = [...bindings, uniform(params)].map((resource, binding) => ({
          binding,
          resource: resource instanceof GPUBuffer ? { buffer: resource } : resource,
        }));
        const label = `${scope} ${(_a2 = kernelNames.get(code)) != null ? _a2 : 'gemm'}${detail ? ` ${detail}` : ''}`;
        steps.push({
          kind: 'dispatch',
          label,
          pipeline: compute,
          group: device.createBindGroup({ layout: compute.getBindGroupLayout(0), entries }),
          count,
        });
      };
      const flat = (n) => {
        const groups = Math.ceil(n / 256);
        const x = Math.min(groups, 32768);
        return [x, Math.ceil(groups / x), 1];
      };
      const stage = (name, buffer, floats) => steps.push({ kind: 'stage', name, buffer, floats });
      const statsOf = (input, target, groups, c, inner, cs, gs, final) => {
        const n = c * inner;
        const parts = Math.max(1, Math.min(256, Math.floor(4096 / groups), Math.ceil(n / 16384)));
        dispatch(
          STATS_PARTIAL,
          [input, target, partial],
          [parts, c, inner, cs, gs, 0],
          [parts, groups, 1],
        );
        dispatch(STATS_FINAL, [partial, target], [parts, n, 0], [groups, 1, 1]);
        dispatch(
          STATS_PARTIAL,
          [input, target, partial],
          [parts, c, inner, cs, gs, 1],
          [parts, groups, 1],
        );
        dispatch(STATS_FINAL, [partial, target], [parts, n, final], [groups, 1, 1]);
      };
      const groupStats = (input, c, h, w, perH) => {
        if (perH) statsOf(input, scratchStats, h, c, w, h * w, w, 1);
        else statsOf(input, scratchStats, 1, 1, c * h * w, 0, 0, 1);
      };
      const conv = (input, name, output, g, flags = 0, side = zeros) => {
        const { sh = 1, sw = 1, ph = 0, pw = 0, dh = 1, dw = 1 } = g;
        const m = rows(`${name}.w`);
        const k = entry(`${name}.w`).shape[1];
        const cin = k / (g.kh * g.kw);
        const hout = Math.floor((g.hin + 2 * ph - dh * (g.kh - 1) - 1) / sh) + 1;
        const wout = Math.floor((g.win + 2 * pw - dw * (g.kw - 1) - 1) / sw) + 1;
        dispatch(
          gemmSource(isHalf(`${name}.w`), mode),
          [input, tensor(`${name}.w`), tensor(`${name}.b`), side, output],
          [m, k, cin, g.kh, g.kw, sh, sw, ph, pw, dh, dw, g.hin, g.win, hout, wout, flags],
          [Math.ceil((hout * wout) / 64), Math.ceil(m / 64), 1],
          name.split('.').slice(1).join('.'),
        );
        return [hout, wout];
      };
      const convTr = (input, name, output, hin, win, axis, flags) => {
        const m = rows(`${name}.w`);
        const k = entry(`${name}.w`).shape[1];
        const n = axis === 0 ? (hin + 1) * win : hin * (win + 1);
        dispatch(
          gemmTrSource(isHalf(`${name}.w`), mode),
          [input, tensor(`${name}.w`), tensor(`${name}.b`), output],
          [m, k, k / 2, hin, win, axis, flags],
          [Math.ceil(n / 64), Math.ceil(m / 64), 1],
          'tr',
        );
      };
      const dconv = (prefix, c, h, w, perH) => {
        for (let j = 0; j < 2; j += 1) {
          const p = `${prefix}.dc${j}`;
          const d = 2 ** j;
          const hid = rows(`${p}.c1.w`);
          conv(stream, `${p}.c1`, hidden, { hin: h, win: w, kh: 1, kw: 3, pw: d, dw: d });
          groupStats(hidden, hid, h, w, perH);
          dispatch(
            GN_INPLACE,
            [hidden, scratchStats, tensor(`${p}.n1.g`), tensor(`${p}.n1.b`)],
            [hid, h, w, perH ? 1 : 0, 1],
            flat(hid * h * w),
          );
          conv(hidden, `${p}.c2`, wide, { hin: h, win: w, kh: 1, kw: 1 });
          groupStats(wide, 2 * c, h, w, perH);
          dispatch(
            GN_GLU_RESIDUAL,
            [
              wide,
              scratchStats,
              tensor(`${p}.n2.g`),
              tensor(`${p}.n2.b`),
              tensor(`${p}.ls`),
              stream,
            ],
            [c, h, w, perH ? 1 : 0],
            flat(c * h * w),
          );
        }
      };
      const tIn = timeOut;
      statsOf(mixBuffer, mixStats, 1, 1, 2 * L, 0, 0, 2);
      steps.push({ kind: 'copy', from: mixBuffer, to: tIn, bytes: 2 * L * 4 });
      dispatch(NORMALIZE, [tIn, mixStats], [2 * L], flat(2 * L));
      dispatch(
        STFT_A,
        [mixBuffer, tensor('stft.window'), tw, dft],
        [FRAMES, L],
        [1, 64, 2 * FRAMES],
      );
      dispatch(STFT_B, [dft, tw, xin], [FRAMES], [1, 64, 2 * FRAMES]);
      statsOf(xin, specStats, 1, 1, 4 * BINS * FRAMES, 0, 0, 2);
      dispatch(NORMALIZE, [xin, specStats], [4 * BINS * FRAMES], flat(4 * BINS * FRAMES));
      stage('norm_spec', xin, 4 * BINS * FRAMES);
      let tCur = tIn;
      let fCur = xin;
      let tLen = L;
      let fBins = BINS;
      for (let i = 0; i < 4; i += 1) {
        const c = 48 * 2 ** i;
        scope = `tenc${i}`;
        const [, tl] = conv(
          tCur,
          `tenc${i}.conv`,
          stream,
          { hin: 1, win: tLen, kh: 1, kw: 8, sw: 4, pw: 2 },
          GEMM_GELU,
        );
        tLen = tl;
        dconv(`tenc${i}`, c, 1, tLen, false);
        conv(stream, `tenc${i}.rw`, wide, { hin: 1, win: tLen, kh: 1, kw: 1 });
        dispatch(GLU, [wide, zeros, tskips[i]], [c, 1, tLen, 0], flat(c * tLen));
        stage(`tenc${i}`, tskips[i], c * tLen);
        tCur = tskips[i];
        scope = `enc${i}`;
        const [fb] = conv(
          fCur,
          `enc${i}.conv`,
          stream,
          { hin: fBins, win: FRAMES, kh: 8, kw: 1, sh: 4, ph: 2 },
          GEMM_GELU,
        );
        fBins = fb;
        dconv(`enc${i}`, c, fBins, FRAMES, true);
        conv(stream, `enc${i}.rw`, wide, { hin: fBins, win: FRAMES, kh: 1, kw: 1 });
        dispatch(
          GLU,
          [wide, i === 0 ? tensor('freq_emb') : zeros, skips[i]],
          [c, fBins, FRAMES, i === 0 ? 1 : 0],
          flat(c * fBins * FRAMES),
        );
        stage(`enc${i}`, skips[i], c * fBins * FRAMES);
        fCur = skips[i];
      }
      scope = 'tf_in';
      conv(skips[3], 'up', normF, { hin: 1, win: nf, kh: 1, kw: 1 });
      conv(tskips[3], 'up_t', normT, { hin: 1, win: nt, kh: 1, kw: 1 });
      dispatch(
        LAYER_NORM,
        [normF, tensor('norm_in.g'), tensor('norm_in.b'), tensor('pos'), xf],
        [DIM, nf, 1],
        [Math.ceil(nf / 64), 1, 1],
      );
      dispatch(
        LAYER_NORM,
        [normT, tensor('norm_in_t.g'), tensor('norm_in_t.b'), tensor('pos_t'), xt],
        [DIM, nt, 1],
        [Math.ceil(nt / 64), 1, 1],
      );
      stage('tf_in', xf, DIM * nf);
      stage('tf_in_t', xt, DIM * nt);
      const layerNorm = (input, name, output, n) =>
        dispatch(
          LAYER_NORM,
          [input, tensor(`${name}.g`), tensor(`${name}.b`), zeros, output],
          [DIM, n, 0],
          [Math.ceil(n / 64), 1, 1],
        );
      const linear = (input, name, output, n, flags = 0, side = zeros) =>
        conv(input, name, output, { hin: 1, win: n, kh: 1, kw: 1 }, flags, side);
      const attention = (q, kv, out, nq, nk, rowsQ, rowsK, rowsV) => {
        for (let head = 0; head < HEADS; head += HEADS_PER_DISPATCH) {
          const heads = `heads ${head}-${head + HEADS_PER_DISPATCH - 1}`;
          dispatch(
            ATTN_SCORES,
            [q, kv, scores],
            [nq, nk, rowsQ, rowsK, head],
            [Math.ceil(nk / 64), Math.ceil(nq / 64), HEADS_PER_DISPATCH],
            heads,
          );
          const rowCount = HEADS_PER_DISPATCH * nq;
          const x = Math.min(rowCount, 32768);
          dispatch(ATTN_SOFTMAX, [scores], [nk], [x, Math.ceil(rowCount / x), 1], heads);
          dispatch(
            ATTN_OUT,
            [scores, kv, out],
            [nq, nk, rowsV, head],
            [1, Math.ceil(nq / 64), HEADS_PER_DISPATCH],
            heads,
          );
        }
      };
      const feedForward = (x, p, n, norm) => {
        layerNorm(x, `${p}.nf`, norm, n);
        linear(norm, `${p}.ff1`, ff, n, GEMM_GELU);
        linear(ff, `${p}.ff2`, x, n, GEMM_RESIDUAL, tensor(`${p}.g2`));
        statsOf(x, scratchStats, 1, 1, DIM * n, 0, 0, 1);
        dispatch(
          GN_INPLACE,
          [x, scratchStats, tensor(`${p}.no.g`), tensor(`${p}.no.b`)],
          [DIM, 1, n, 0, 0],
          flat(DIM * n),
        );
      };
      for (let layer = 0; layer < 5; layer += 1) {
        const pf = `tf${layer}`;
        const pt = `tft${layer}`;
        scope = pf;
        if (layer % 2 === 0) {
          for (const [x, p, n, norm, qkv, att] of [
            [xf, pf, nf, normF, qkvF, attF],
            [xt, pt, nt, normT, qkvT, attT],
          ]) {
            layerNorm(x, `${p}.n1`, norm, n);
            linear(norm, `${p}.qkv`, qkv, n);
            attention(qkv, qkv, att, n, n, 0, DIM, 2 * DIM);
            linear(att, `${p}.out`, x, n, GEMM_RESIDUAL, tensor(`${p}.g1`));
            feedForward(x, p, n, norm);
          }
        } else {
          layerNorm(xf, `${pf}.n1`, normF, nf);
          linear(normF, `${pf}.q`, qkvF, nf);
          layerNorm(xt, `${pf}.nkv`, otherT, nt);
          linear(otherT, `${pf}.kv`, kvF, nt);
          layerNorm(xt, `${pt}.n1`, normT, nt);
          linear(normT, `${pt}.q`, qkvT, nt);
          layerNorm(xf, `${pt}.nkv`, otherF, nf);
          linear(otherF, `${pt}.kv`, kvT, nf);
          attention(qkvF, kvF, attF, nf, nt, 0, 0, DIM);
          attention(qkvT, kvT, attT, nt, nf, 0, 0, DIM);
          linear(attF, `${pf}.out`, xf, nf, GEMM_RESIDUAL, tensor(`${pf}.g1`));
          linear(attT, `${pt}.out`, xt, nt, GEMM_RESIDUAL, tensor(`${pt}.g1`));
          feedForward(xf, pf, nf, normF);
          feedForward(xt, pt, nt, normT);
        }
        stage(`tf${layer}`, xf, DIM * nf);
        stage(`tft${layer}`, xt, DIM * nt);
      }
      let cur = ping[0];
      scope = 'dec0';
      linear(xf, 'down', cur, nf);
      for (let i = 0; i < 4; i += 1) {
        const c = 48 * 2 ** (3 - i);
        scope = `dec${i}`;
        const plane = fBins * FRAMES;
        dispatch(ADD, [cur, skips[3 - i]], [c * plane], flat(c * plane));
        conv(cur, `dec${i}.rw`, wide, { hin: fBins, win: FRAMES, kh: 3, kw: 3, ph: 1, pw: 1 });
        dispatch(GLU, [wide, zeros, stream], [c, fBins, FRAMES, 0], flat(c * plane));
        dconv(`dec${i}`, c, fBins, FRAMES, true);
        const next = i === 3 ? freqOut : ping[(i + 1) % 2];
        convTr(stream, `dec${i}.tr`, next, fBins, FRAMES, 0, i === 3 ? 0 : GEMM_GELU);
        fBins *= 4;
        stage(`dec${i}`, next, (rows(`dec${i}.tr.w`) / 4) * fBins * FRAMES);
        cur = next;
      }
      cur = ping[0];
      scope = 'tdec0';
      linear(xt, 'down_t', cur, nt);
      for (let i = 0; i < 4; i += 1) {
        const c = 48 * 2 ** (3 - i);
        scope = `tdec${i}`;
        dispatch(ADD, [cur, tskips[3 - i]], [c * tLen], flat(c * tLen));
        conv(cur, `tdec${i}.rw`, wide, { hin: 1, win: tLen, kh: 1, kw: 3, pw: 1 });
        dispatch(GLU, [wide, zeros, stream], [c, 1, tLen, 0], flat(c * tLen));
        dconv(`tdec${i}`, c, 1, tLen, false);
        const next = i === 3 ? timeOut : ping[(i + 1) % 2];
        convTr(stream, `tdec${i}.tr`, next, 1, tLen, 1, i === 3 ? 0 : GEMM_GELU);
        tLen *= 4;
        stage(`tdec${i}`, next, (rows(`tdec${i}.tr.w`) / 4) * tLen);
        cur = next;
      }
      scope = 'output';
      sources.forEach((source, index) => {
        dispatch(
          ISTFT_A,
          [freqOut, specStats, tw, dft],
          [FRAMES, source],
          [1, 64, 2 * FRAMES],
          HTDEMUCS_SOURCES[source],
        );
        dispatch(
          ISTFT_B,
          [dft, tw, tensor('istft.window'), frames],
          [FRAMES],
          [1, 64, 2 * FRAMES],
          HTDEMUCS_SOURCES[source],
        );
        dispatch(
          OVERLAP_ADD,
          [frames, tensor('istft.env'), timeOut, mixStats, outBuffer],
          [FRAMES, L, source, index * 2 * L],
          flat(2 * L),
          HTDEMUCS_SOURCES[source],
        );
        if (index < sources.length - 1)
          stage(`out_${HTDEMUCS_SOURCES[source]}`, outBuffer, 2 * L * (index + 1));
      });
      stage('out', outBuffer, 2 * L * sources.length);
      const invalid = yield device.popErrorScope();
      if (invalid) {
        owned.forEach((buffer) => buffer.destroy());
        throw new Error(`htdemucs graph rejected: ${invalid.message}`);
      }
      const model = new _HtdemucsWgsl(device, steps, mixBuffer, outBuffer, owned);
      yield device.queue.onSubmittedWorkDone();
      return model;
    });
  }
  /**
   * One segment: `left` and `right` of exactly HTDEMUCS_SEGMENT samples at 44.1 kHz. `slice` as in
   * BeatThisWgsl.run: 'stage' submits at every stage boundary, 'dispatch' after every dispatch,
   * 'auto' at stage boundaries but per dispatch inside any stage that outlasted `budgetMs` (one
   * 120 Hz frame by default), relearnt every RELEARN_RUNS runs. Results are identical whatever the
   * slicing.
   */
  run(_0, _1) {
    return __async(this, arguments, function* (left, right, options = {}) {
      var _a, _b;
      const L = HTDEMUCS_SEGMENT;
      if (left.length !== L || right.length !== L)
        throw new Error(
          `htdemucs segment must be ${L} samples per channel, got ${left.length} and ${right.length}`,
        );
      const { device } = this;
      device.queue.writeBuffer(this.mixBuffer, 0, left.buffer, left.byteOffset, left.byteLength);
      device.queue.writeBuffer(
        this.mixBuffer,
        L * 4,
        right.buffer,
        right.byteOffset,
        right.byteLength,
      );
      let encoder = device.createCommandEncoder();
      let pass = null;
      const readbacks = [];
      let gpuMs = 0;
      let slices = 0;
      const slice = (_a = options.slice) != null ? _a : 'none';
      const submit = () =>
        __async(this, null, function* () {
          pass == null ? void 0 : pass.end();
          pass = null;
          const started = performance.now();
          device.queue.submit([encoder.finish()]);
          yield device.queue.onSubmittedWorkDone();
          const took = performance.now() - started;
          gpuMs += took;
          slices += 1;
          encoder = device.createCommandEncoder();
          return took;
        });
      const budgetMs = (_b = options.budgetMs) != null ? _b : 8;
      const timings = [];
      let lastLabel = '';
      if (slice === 'auto' && ++this.autoRuns % RELEARN_RUNS2 === 0) this.fineStages.clear();
      let stageIndex = 0;
      const wallStarted = performance.now();
      for (const step of this.steps) {
        const perDispatch =
          slice === 'dispatch' || (slice === 'auto' && this.fineStages.has(stageIndex));
        if (perDispatch && pass) {
          const took = yield submit();
          if (options.timings) timings.push({ label: lastLabel, ms: took });
        }
        if (step.kind === 'dispatch') {
          lastLabel = step.label;
          pass != null ? pass : (pass = encoder.beginComputePass());
          pass.setPipeline(step.pipeline);
          pass.setBindGroup(0, step.group);
          pass.dispatchWorkgroups(...step.count);
          continue;
        }
        if (step.kind === 'copy') {
          pass == null ? void 0 : pass.end();
          pass = null;
          encoder.copyBufferToBuffer(step.from, 0, step.to, 0, step.bytes);
          continue;
        }
        const coarse = slice === 'stage' || (slice === 'auto' && !this.fineStages.has(stageIndex));
        if (coarse && step.name !== 'out') {
          const took = yield submit();
          if (slice === 'auto' && took > budgetMs) this.fineStages.add(stageIndex);
        }
        stageIndex += 1;
        if (!options.stages && step.name !== 'out') continue;
        pass == null ? void 0 : pass.end();
        pass = null;
        const readback = device.createBuffer({
          size: step.floats * 4,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        encoder.copyBufferToBuffer(step.buffer, 0, readback, 0, step.floats * 4);
        readbacks.push({ name: step.name, buffer: readback });
      }
      yield submit();
      const wallMs = performance.now() - wallStarted;
      const stages = {};
      for (const { name, buffer } of readbacks) {
        yield buffer.mapAsync(GPUMapMode.READ);
        stages[name] = new Float32Array(buffer.getMappedRange().slice(0));
        buffer.destroy();
      }
      const out = stages.out;
      if (!options.stages) delete stages.out;
      const stems = Array.from({ length: out.length / (2 * L) }, (_, index) => ({
        left: out.subarray(index * 2 * L, index * 2 * L + L),
        right: out.subarray(index * 2 * L + L, (index + 1) * 2 * L),
      }));
      return __spreadValues(
        __spreadValues(
          {
            left: stems[0].left,
            right: stems[0].right,
            stems,
            gpuMs,
            wallMs,
            slices,
          },
          options.stages ? { stages } : {},
        ),
        options.timings ? { timings } : {},
      );
    });
  }
  destroy() {
    this.owned.forEach((buffer) => buffer.destroy());
  }
};

// ../../../../Work/mine/folia-major/src/services/analysis/beatThisWgsl/percussive.ts
var MEL_BINS2 = 128;
var WORKGROUP = 64;
var SLICE_FRAMES = 1024;
var PERCUSSIVE_WGSL =
  /* wgsl */
  `
struct Params { frames: u32, first: u32, count: u32 }
@group(0) @binding(0) var<storage, read> mel: array<f32>;
@group(0) @binding(1) var<storage, read_write> energy: array<f32>;
@group(0) @binding(2) var<uniform> p: Params;

const BANDS: i32 = ${MEL_BINS2};
const HALF: i32 = 8;
const TAPS: i32 = 17;

fn magnitude(frame: i32, band: i32) -> f32 {
  if (frame < 0 || frame >= i32(p.frames) || band < 0 || band >= BANDS) { return 0.0; }
  let v = mel[u32(frame * BANDS + band)];
  // expm1 without its cancellation near 0.
  if (v < 1e-3) { return max(0.0, v + 0.5 * v * v); }
  return exp(v) - 1.0;
}

fn median(values: ptr<function, array<f32, 17>>) -> f32 {
  for (var i = 1; i < TAPS; i++) {
    let value = (*values)[i];
    var j = i - 1;
    while (j >= 0 && (*values)[j] > value) {
      (*values)[j + 1] = (*values)[j];
      j--;
    }
    (*values)[j + 1] = value;
  }
  return (*values)[HALF];
}

@compute @workgroup_size(${WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (id.x >= p.count) { return; }
  let at = p.first + id.x;
  let frame = i32(at);
  var percussive = 0.0;
  var total = 0.0;
  var taps: array<f32, 17>;
  for (var band = 0; band < BANDS; band++) {
    for (var k = 0; k < TAPS; k++) { taps[k] = magnitude(frame + k - HALF, band); }
    let harmonic = median(&taps);
    for (var k = 0; k < TAPS; k++) { taps[k] = magnitude(frame, band + k - HALF); }
    let hit = median(&taps);
    let value = magnitude(frame, band);
    let e = value * value;
    let hh = harmonic * harmonic;
    let pp = hit * hit;
    if (hh + pp > 0.0) { percussive += e * pp / (hh + pp); }
    total += e;
  }
  energy[2u * at] = percussive;
  energy[2u * at + 1u] = total;
}
`;
var pipelines = /* @__PURE__ */ new WeakMap();
var runPercussiveEnergy = (device, mel, frames) =>
  __async(null, null, function* () {
    if (frames < 1 || mel.length < frames * MEL_BINS2)
      throw new Error(`percussive: ${mel.length} floats for ${frames} frames`);
    let pipeline = pipelines.get(device);
    if (!pipeline) {
      pipeline = yield device.createComputePipelineAsync({
        layout: 'auto',
        compute: {
          module: device.createShaderModule({ code: PERCUSSIVE_WGSL }),
          entryPoint: 'main',
        },
      });
      pipelines.set(device, pipeline);
    }
    const input = device.createBuffer({
      size: frames * MEL_BINS2 * 4,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    const output = device.createBuffer({
      size: frames * 2 * 4,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    });
    const slices = Math.ceil(frames / SLICE_FRAMES);
    const params = device.createBuffer({
      size: slices * 256,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const readback = device.createBuffer({
      size: frames * 2 * 4,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });
    try {
      device.queue.writeBuffer(input, 0, mel.buffer, mel.byteOffset, frames * MEL_BINS2 * 4);
      for (let slice = 0; slice < slices; slice += 1) {
        const first = slice * SLICE_FRAMES;
        const count = Math.min(SLICE_FRAMES, frames - first);
        device.queue.writeBuffer(params, slice * 256, new Uint32Array([frames, first, count, 0]));
        const group = device.createBindGroup({
          layout: pipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: { buffer: input } },
            { binding: 1, resource: { buffer: output } },
            { binding: 2, resource: { buffer: params, offset: slice * 256, size: 16 } },
          ],
        });
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, group);
        pass.dispatchWorkgroups(Math.ceil(count / WORKGROUP));
        pass.end();
        if (slice === slices - 1)
          encoder.copyBufferToBuffer(output, 0, readback, 0, frames * 2 * 4);
        device.queue.submit([encoder.finish()]);
        yield device.queue.onSubmittedWorkDone();
      }
      yield readback.mapAsync(GPUMapMode.READ);
      const interleaved = new Float32Array(readback.getMappedRange().slice(0));
      readback.unmap();
      const percussive = new Float32Array(frames);
      const total = new Float32Array(frames);
      for (let frame = 0; frame < frames; frame += 1) {
        percussive[frame] = interleaved[2 * frame];
        total[frame] = interleaved[2 * frame + 1];
      }
      return { percussive, total };
    } finally {
      input.destroy();
      output.destroy();
      params.destroy();
      readback.destroy();
    }
  });
export { BeatThisWgsl, HtdemucsWgsl, runPercussiveEnergy };
