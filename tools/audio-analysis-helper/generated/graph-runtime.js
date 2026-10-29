var __generator =
  (this && this.__generator) ||
  function (thisArg, body) {
    var f, y, t, g;
    var _ = {
      label: 0,
      sent: function () {
        if (t[0] & 1) throw t[1];
        return t[1];
      },
      trys: [],
      ops: [],
    };
    g = Object.create((typeof Iterator === 'function' ? Iterator : Object).prototype);
    return (
      (g.next = verb(0)),
      (g['throw'] = verb(1)),
      (g['return'] = verb(2)),
      typeof Symbol === 'function' &&
        (g[Symbol.iterator] = function () {
          return this;
        }),
      g
    );
    function verb(n) {
      return function (v) {
        return step([n, v]);
      };
    }
    function step(op) {
      if (f) throw new TypeError('Generator is already executing.');
      while ((g && ((g = 0), op[0] && (_ = 0)), _))
        try {
          if (
            ((f = 1),
            y &&
              (t =
                op[0] & 2
                  ? y['return']
                  : op[0]
                    ? y['throw'] || ((t = y['return']) && t.call(y), 0)
                    : y.next) &&
              !(t = t.call(y, op[1])).done)
          )
            return t;
          if (((y = 0), t)) op = [op[0] & 2, t.value];
          switch (op[0]) {
            case 0:
            case 1:
              t = op;
              break;
            case 4:
              _.label++;
              return { value: op[1], done: false };
            case 5:
              _.label++;
              y = op[1];
              op = [0];
              continue;
            case 7:
              op = _.ops.pop();
              _.trys.pop();
              continue;
            default:
              if (
                !((t = _.trys), (t = t.length > 0 && t[t.length - 1])) &&
                (op[0] === 6 || op[0] === 2)
              ) {
                _ = 0;
                continue;
              }
              if (op[0] === 3 && (!t || (op[1] > t[0] && op[1] < t[3]))) {
                _.label = op[1];
                break;
              }
              if (op[0] === 6 && _.label < t[1]) {
                _.label = t[1];
                t = op;
                break;
              }
              if (t && _.label < t[2]) {
                _.label = t[2];
                _.ops.push(op);
                break;
              }
              if (t[2]) _.ops.pop();
              _.trys.pop();
              continue;
          }
          op = body.call(thisArg, _);
        } catch (e) {
          op = [6, e];
          y = 0;
        } finally {
          f = t = 0;
        }
      if (op[0] & 5) throw op[1];
      return { value: op[0] ? op[1] : void 0, done: true };
    }
  };
var __values =
  (this && this.__values) ||
  function (o) {
    var s = typeof Symbol === 'function' && Symbol.iterator,
      m = s && o[s],
      i = 0;
    if (m) return m.call(o);
    if (o && typeof o.length === 'number')
      return {
        next: function () {
          if (o && i >= o.length) o = void 0;
          return { value: o && o[i++], done: !o };
        },
      };
    throw new TypeError(s ? 'Object is not iterable.' : 'Symbol.iterator is not defined.');
  };
var __read =
  (this && this.__read) ||
  function (o, n) {
    var m = typeof Symbol === 'function' && o[Symbol.iterator];
    if (!m) return o;
    var i = m.call(o),
      r,
      ar = [],
      e;
    try {
      while ((n === void 0 || n-- > 0) && !(r = i.next()).done) ar.push(r.value);
    } catch (error) {
      e = { error: error };
    } finally {
      try {
        if (r && !r.done && (m = i['return'])) m.call(i);
      } finally {
        if (e) throw e.error;
      }
    }
    return ar;
  };
var __spreadArray =
  (this && this.__spreadArray) ||
  function (to, from, pack) {
    if (pack || arguments.length === 2)
      for (var i = 0, l = from.length, ar; i < l; i++) {
        if (ar || !(i in from)) {
          if (!ar) ar = Array.prototype.slice.call(from, 0, i);
          ar[i] = from[i];
        }
      }
    return to.concat(ar || Array.prototype.slice.call(from));
  };
var AnalysisGraph = (function () {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = function (target, all) {
    for (var name in all) __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = function (to, from, except, desc) {
    var e_1, _f;
    if ((from && typeof from === 'object') || typeof from === 'function') {
      var _loop_1 = function (key) {
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, {
            get: function () {
              return from[key];
            },
            enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable,
          });
      };
      try {
        for (var _g = __values(__getOwnPropNames(from)), _h = _g.next(); !_h.done; _h = _g.next()) {
          var key = _h.value;
          _loop_1(key);
        }
      } catch (e_1_1) {
        e_1 = { error: e_1_1 };
      } finally {
        try {
          if (_h && !_h.done && (_f = _g.return)) _f.call(_g);
        } finally {
          if (e_1) throw e_1.error;
        }
      }
    }
    return to;
  };
  var __toCommonJS = function (mod) {
    return __copyProps(__defProp({}, '__esModule', { value: true }), mod);
  };
  var __async = function (__this, __arguments, generator) {
    return new Promise(function (resolve, reject) {
      var step;
      var fulfilled = function (value) {
        try {
          step(generator.next(value));
        } catch (e) {
          reject(e);
        }
      };
      var rejected = function (value) {
        try {
          step(generator.throw(value));
        } catch (e) {
          reject(e);
        }
      };
      step = function (x) {
        return x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
      };
      step((generator = generator.apply(__this, __arguments)).next());
    });
  };
  // tools/audio-analysis-helper/graph-recorder.mjs
  var graph_recorder_exports = {};
  __export(graph_recorder_exports, {
    recordModelGraph: function () {
      return recordModelGraph;
    },
  });
  // tools/audio-analysis-helper/generated/source-models.mjs
  var __defProp2 = Object.defineProperty;
  var __getOwnPropSymbols = Object.getOwnPropertySymbols;
  var __hasOwnProp2 = Object.prototype.hasOwnProperty;
  var __propIsEnum = Object.prototype.propertyIsEnumerable;
  var __defNormalProp = function (obj, key, value) {
    return key in obj
      ? __defProp2(obj, key, { enumerable: true, configurable: true, writable: true, value: value })
      : (obj[key] = value);
  };
  var __spreadValues = function (a, b) {
    var e_2, _f;
    for (var prop in b || (b = {}))
      if (__hasOwnProp2.call(b, prop)) __defNormalProp(a, prop, b[prop]);
    if (__getOwnPropSymbols)
      try {
        for (var _g = __values(__getOwnPropSymbols(b)), _h = _g.next(); !_h.done; _h = _g.next()) {
          var prop = _h.value;
          if (__propIsEnum.call(b, prop)) __defNormalProp(a, prop, b[prop]);
        }
      } catch (e_2_1) {
        e_2 = { error: e_2_1 };
      } finally {
        try {
          if (_h && !_h.done && (_f = _g.return)) _f.call(_g);
        } finally {
          if (e_2) throw e_2.error;
        }
      }
    return a;
  };
  var __async2 = function (__this, __arguments, generator) {
    return new Promise(function (resolve, reject) {
      var step;
      var fulfilled = function (value) {
        try {
          step(generator.next(value));
        } catch (e) {
          reject(e);
        }
      };
      var rejected = function (value) {
        try {
          step(generator.throw(value));
        } catch (e) {
          reject(e);
        }
      };
      step = function (x) {
        return x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
      };
      step((generator = generator.apply(__this, __arguments)).next());
    });
  };
  var GELU =
    /* wgsl */
    '\nfn erf_f32(v: f32) -> f32 {\n  let a = abs(v);\n  let x = 1.0 / (1.0 + 0.3275911 * a);\n  let y = 1.0 - ((((1.061405429 * x - 1.453152027) * x + 1.421413741) * x - 0.284496736) * x + 0.254829592) * x * exp(-a * a);\n  return select(-y, y, v >= 0.0);\n}\nfn gelu(v: f32) -> f32 { return 0.5 * v * (1.0 + erf_f32(v * 0.7071067811865476)); }\n';
  var STEM_BN =
    /* wgsl */
    '\nstruct P { frames: u32, bins: u32 }\n@group(0) @binding(0) var<storage, read> mel: array<f32>;      // [T, bins]\n@group(0) @binding(1) var<storage, read> scale: array<f32>;\n@group(0) @binding(2) var<storage, read> shift: array<f32>;\n@group(0) @binding(3) var<storage, read_write> out: array<f32>; // [bins, T]\n@group(0) @binding(4) var<uniform> p: P;\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let i = id.x;\n  if (i >= p.frames * p.bins) { return; }\n  let f = i / p.frames;\n  let t = i % p.frames;\n  out[i] = mel[t * p.bins + f] * scale[f] + shift[f];\n}\n';
  var CONV_GELU =
    /* wgsl */
    '\n'.concat(
      GELU,
      '\nstruct P { fout: u32, frames: u32, cin: u32, cout: u32, kf: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read> w: array<f32>;   // [kf, 3, cin, cout] = [K, cout]\n@group(0) @binding(2) var<storage, read> b: array<f32>;\n@group(0) @binding(3) var<storage, read_write> y: array<f32>;\n@group(0) @binding(4) var<uniform> p: P;\nvar<workgroup> as_: array<f32, 1024>; // [64 rows][16 k]\nvar<workgroup> bs_: array<f32, 1024>; // [16 k][64 cols]\n\nfn gather(row: u32, k: u32) -> f32 {\n  let ci = k % p.cin;\n  let tap = k / p.cin;\n  let dt = tap % 3u;\n  let df = tap / 3u;\n  let t = row % p.frames;\n  let fo = row / p.frames;\n  let ti = i32(t) + i32(dt) - 1;\n  if (ti < 0 || ti >= i32(p.frames)) { return 0.0; }\n  return x[((fo * p.kf + df) * p.frames + u32(ti)) * p.cin + ci];\n}\n\n@compute @workgroup_size(16, 16)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {\n  let m = p.fout * p.frames;\n  let kTotal = p.kf * 3u * p.cin;\n  let row0 = wg.y * 64u;\n  let col0 = wg.x * 64u;\n  var acc: array<array<f32, 4>, 4>;\n  for (var k0 = 0u; k0 < kTotal; k0 += 16u) {\n    for (var i = 0u; i < 4u; i++) {\n      let idx = li + i * 256u;\n      let ar = row0 + idx / 16u;\n      let ak = k0 + idx % 16u;\n      var av = 0.0;\n      if (ar < m && ak < kTotal) { av = gather(ar, ak); }\n      as_[idx] = av;\n      let bk = k0 + idx / 64u;\n      let bc = col0 + idx % 64u;\n      var bv = 0.0;\n      if (bk < kTotal && bc < p.cout) { bv = w[bk * p.cout + bc]; }\n      bs_[idx] = bv;\n    }\n    workgroupBarrier();\n    for (var kk = 0u; kk < 16u; kk++) {\n      var a: array<f32, 4>;\n      var bb: array<f32, 4>;\n      for (var i = 0u; i < 4u; i++) { a[i] = as_[(l.y * 4u + i) * 16u + kk]; }\n      for (var j = 0u; j < 4u; j++) { bb[j] = bs_[kk * 64u + l.x * 4u + j]; }\n      for (var i = 0u; i < 4u; i++) {\n        for (var j = 0u; j < 4u; j++) { acc[i][j] += a[i] * bb[j]; }\n      }\n    }\n    workgroupBarrier();\n  }\n  for (var i = 0u; i < 4u; i++) {\n    let r = row0 + l.y * 4u + i;\n    if (r >= m) { continue; }\n    for (var j = 0u; j < 4u; j++) {\n      let c = col0 + l.x * 4u + j;\n      if (c >= p.cout) { continue; }\n      y[r * p.cout + c] = gelu(acc[i][j] + b[c]);\n    }\n  }\n}\n',
    );
  var MATMUL_GELU = 1;
  var MATMUL_RESIDUAL = 2;
  var MATMUL_NORM = 4;
  var MATMUL_ROPE = 8;
  var matmulSource = function (weights, mode) {
    if (mode === void 0) {
      mode = 'f32';
    }
    var f16 = mode !== 'f32';
    return (
      /* wgsl */
      '\n'
        .concat(f16 ? 'enable f16;' : '', '\n')
        .concat(
          GELU,
          '\nstruct P { m: u32, n: u32, k: u32, flags: u32, posDiv: u32, posMod: u32, dmodel: u32, wStride: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n',
        )
        .concat(
          weights,
          '\n@group(0) @binding(2) var<storage, read> bias: array<f32>;\n@group(0) @binding(3) var<storage, read_write> y: array<f32>;\n@group(0) @binding(4) var<storage, read> table: array<f32>; // [pos][16][cos, sin]\n@group(0) @binding(5) var<uniform> p: P;\n',
        )
        .concat(
          f16
            ? 'var<workgroup> as_: array<vec4h, 256>; // [16 k][16 row quads]\nvar<workgroup> bs_: array<vec4h, 256>; // [16 k][16 col quads]\nvar<workgroup> sums_: array<vec4f, 256>; // per-loader row sums of squares\nconst A_SCALE = 64.0;'
            : 'var<workgroup> as_: array<vec4f, 256>; // [16 k][16 row quads]; afterwards the per-loader row sums of squares\nvar<workgroup> bs_: array<vec4f, 256>; // [16 k][16 col quads]',
          '\n@compute @workgroup_size(16, 16)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {\n  let row0 = wg.y * 64u;\n  let col0 = wg.x * 64u;\n  var acc: array<vec4f, 4>; // acc[j] = rows 4*l.y..+3 of column 4*l.x + j\n  // Loader roles: A - invocation li reads k = li % 16 of row quad li / 16; B - k = li / 16, col quad li % 16.\n  let aK = li % 16u;\n  let aQuad = li / 16u;\n  let bK = li / 16u;\n  let bQuad = li % 16u;\n  var squares = vec4f(0.0);\n  for (var k0 = 0u; k0 < p.k; k0 += 16u) {\n    var av = vec4f(0.0);\n    let ak = k0 + aK;\n    if (ak < p.k) {\n      for (var r = 0u; r < 4u; r++) {\n        let row = row0 + aQuad * 4u + r;\n        if (row < p.m) { av[r] = x[row * p.k + ak]; }\n      }\n    }\n    squares = fma(av, av, squares);\n    as_[aK * 16u + aQuad] = ',
        )
        .concat(
          f16 ? 'vec4h(select(av, av / A_SCALE, (p.flags & 4u) != 0u))' : 'av',
          ';\n    var bv = ',
        )
        .concat(
          f16 ? 'vec4h' : 'vec4f',
          '(0.0);\n    let bk = k0 + bK;\n    let bc = col0 + bQuad * 4u;\n    if (bk < p.k && bc < p.n) { bv = weightQuad((bk * p.wStride + bc) / 4u); }\n    bs_[bK * 16u + bQuad] = bv;\n    workgroupBarrier();\n',
        )
        .concat(
          f16
            ? ''
                .concat(
                  mode === 'math' ? '    var part: array<vec4h, 4>;\n' : '',
                  '    for (var kk = 0u; kk < 16u; kk++) {\n      let a = as_[kk * 16u + l.y];\n      let b = bs_[kk * 16u + l.x];\n',
                )
                .concat(
                  mode === 'math'
                    ? '      part[0] = fma(a, vec4h(b.x), part[0]);\n      part[1] = fma(a, vec4h(b.y), part[1]);\n      part[2] = fma(a, vec4h(b.z), part[2]);\n      part[3] = fma(a, vec4h(b.w), part[3]);\n    }\n    for (var j = 0u; j < 4u; j++) { acc[j] += vec4f(part[j]); }'
                    : '      let af = vec4f(a);\n      let bf = vec4f(b);\n      acc[0] = fma(af, vec4f(bf.x), acc[0]);\n      acc[1] = fma(af, vec4f(bf.y), acc[1]);\n      acc[2] = fma(af, vec4f(bf.z), acc[2]);\n      acc[3] = fma(af, vec4f(bf.w), acc[3]);\n    }',
                )
            : '    for (var kk = 0u; kk < 16u; kk++) {\n      let a = as_[kk * 16u + l.y];\n      let b = bs_[kk * 16u + l.x];\n      acc[0] = fma(a, vec4f(b.x), acc[0]);\n      acc[1] = fma(a, vec4f(b.y), acc[1]);\n      acc[2] = fma(a, vec4f(b.z), acc[2]);\n      acc[3] = fma(a, vec4f(b.w), acc[3]);\n    }',
          '\n    workgroupBarrier();\n  }\n  // The 16 loaders of row quad q are invocations 16q..16q+15; their partial sums meet in ',
        )
        .concat(f16 ? 'sums_' : 'as_', '.\n  ')
        .concat(
          f16 ? 'sums_' : 'as_',
          '[li] = squares;\n  workgroupBarrier();\n  var rowScale = vec4f(1.0);\n  if ((p.flags & 4u) != 0u) {\n    var total = vec4f(0.0);\n    for (var t = 0u; t < 16u; t++) { total += ',
        )
        .concat(f16 ? 'sums_' : 'as_', '[l.y * 16u + t]; }\n    rowScale = ')
        .concat(
          f16 ? 'A_SCALE' : '1.0',
          ' / max(sqrt(total), vec4f(1e-12));\n  }\n  for (var i = 0u; i < 4u; i++) {\n    let r = row0 + l.y * 4u + i;\n    if (r >= p.m) { continue; }\n    var v: array<f32, 4>;\n    for (var j = 0u; j < 4u; j++) {\n      let c = min(col0 + l.x * 4u + j, p.n - 1u);\n      v[j] = acc[j][i] * rowScale[i] + bias[c];\n    }\n    let c0 = col0 + l.x * 4u;\n    if ((p.flags & 8u) != 0u && c0 < 2u * p.dmodel) {\n      let pos = (r / p.posDiv) % p.posMod;\n      for (var pair = 0u; pair < 2u; pair++) {\n        let t = (pos * 16u + ((c0 + 2u * pair) % 32u) / 2u) * 2u;\n        let cs = table[t];\n        let sn = table[t + 1u];\n        let x0 = v[2u * pair];\n        let x1 = v[2u * pair + 1u];\n        v[2u * pair] = x0 * cs - x1 * sn;\n        v[2u * pair + 1u] = x1 * cs + x0 * sn;\n      }\n    }\n    for (var j = 0u; j < 4u; j++) {\n      let c = c0 + j;\n      if (c >= p.n) { continue; }\n      var res = v[j];\n      let o = r * p.n + c;\n      if ((p.flags & 1u) != 0u) { res = gelu(res); }\n      if ((p.flags & 2u) != 0u) { res += y[o]; }\n      y[o] = res;\n    }\n  }\n}\n',
        )
    );
  };
  var MATMUL = matmulSource(
    /* wgsl */
    '\n@group(0) @binding(1) var<storage, read> w: array<vec4f>;\nfn weightQuad(i: u32) -> vec4f { return w[i]; }',
  );
  var MATMUL_F16W = matmulSource(
    /* wgsl */
    '\n@group(0) @binding(1) var<storage, read> w: array<vec2u>; // four fp16 columns per element\nfn weightQuad(i: u32) -> vec4f { let q = w[i]; return vec4f(unpack2x16float(q.x), unpack2x16float(q.y)); }',
  );
  var F16_WEIGHTS =
    /* wgsl */
    '\n@group(0) @binding(1) var<storage, read> w: array<vec4h>;\nfn weightQuad(i: u32) -> vec4h { return w[i]; }';
  var MATMUL_F16 = matmulSource(F16_WEIGHTS, 'storage');
  var MATMUL_F16_MATH = matmulSource(F16_WEIGHTS, 'math');
  var attentionSource = function (f16) {
    return (
      /* wgsl */
      '\n'
        .concat(
          f16 ? 'enable f16;' : '',
          '\nstruct P { n: u32, heads: u32, width: u32, dmodel: u32, seqStride: u32, batchStride: u32 }\n@group(0) @binding(0) var<storage, read> qkv: array<f32>;\n@group(0) @binding(1) var<storage, read_write> o: array<f32>; // [rows, dmodel]\n@group(0) @binding(2) var<uniform> p: P;\nconst TILE = 32u;\nvar<workgroup> ks: array<',
        )
        .concat(
          (f16 == null ? void 0 : f16.k) ? 'vec4h' : 'vec4f',
          ', 256>; // [TILE keys][8 x vec4]\nvar<workgroup> vs: array<',
        )
        .concat(
          f16 ? 'vec4h' : 'vec4f',
          ', 256>;\n@compute @workgroup_size(64)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u) {\n  let b = wg.z;\n  let h = wg.y;\n  let qi = wg.x * 64u + l.x;\n  let valid = qi < p.n;\n  let qrow = b * p.batchStride + min(qi, p.n - 1u) * p.seqStride;\n  var q: array<vec4f, 8>;\n  var acc: array<vec4f, 8>;\n  let qBase = qrow * p.width + h * 32u;\n  for (var d = 0u; d < 8u; d++) {\n    q[d] = vec4f(qkv[qBase + 4u * d], qkv[qBase + 4u * d + 1u], qkv[qBase + 4u * d + 2u], qkv[qBase + 4u * d + 3u]) * 0.17677669529663687;\n    acc[d] = vec4f(0.0);\n  }\n  var m = -3.0e38;\n  var sum = 0.0;\n  for (var k0 = 0u; k0 < p.n; k0 += TILE) {\n    // 32 keys x 8 vec4 = 256 vec4 each for K and V, four per invocation.\n    for (var i = 0u; i < 4u; i++) {\n      let idx = l.x + i * 64u;\n      let kj = k0 + idx / 8u;\n      var kv = vec4f(0.0);\n      var vv = vec4f(0.0);\n      if (kj < p.n) {\n        let base = (b * p.batchStride + kj * p.seqStride) * p.width + h * 32u + (idx % 8u) * 4u;\n        let kb = base + p.dmodel;\n        let vb = base + 2u * p.dmodel;\n        kv = vec4f(qkv[kb], qkv[kb + 1u], qkv[kb + 2u], qkv[kb + 3u]);\n        vv = vec4f(qkv[vb], qkv[vb + 1u], qkv[vb + 2u], qkv[vb + 3u]);\n      }\n      ks[idx] = ',
        )
        .concat((f16 == null ? void 0 : f16.k) ? 'vec4h(kv)' : 'kv', ';\n      vs[idx] = ')
        .concat(
          f16 ? 'vec4h(vv)' : 'vv',
          ';\n    }\n    workgroupBarrier();\n    // Constant trip counts so the compiler unrolls and keeps s[] in registers; keys past the end\n    // of the sequence get a score of -3e38, whose exp is exactly 0.\n    var s: array<f32, 32>;\n    var tileMax = -3.0e38;\n    for (var j = 0u; j < TILE; j++) {\n      var dotv = vec4f(0.0);\n      for (var d = 0u; d < 8u; d++) { dotv = fma(q[d], ',
        )
        .concat(
          (f16 == null ? void 0 : f16.k) ? 'vec4f(ks[j * 8u + d])' : 'ks[j * 8u + d]',
          ', dotv); }\n      s[j] = select(-3.0e38, dotv.x + dotv.y + dotv.z + dotv.w, k0 + j < p.n);\n      tileMax = max(tileMax, s[j]);\n    }\n    let mNew = max(m, tileMax);\n    let r = exp(m - mNew);\n    sum *= r;\n    for (var d = 0u; d < 8u; d++) { acc[d] *= r; }\n    m = mNew;\n',
        )
        .concat(
          (f16 == null ? void 0 : f16.pv)
            ? '    var part: array<vec4h, 8>;\n    for (var j = 0u; j < TILE; j++) {\n      let e = exp(s[j] - m);\n      sum += e;\n      let eh = vec4h(f16(e));\n      for (var d = 0u; d < 8u; d++) { part[d] = fma(eh, vs[j * 8u + d], part[d]); }\n    }\n    for (var d = 0u; d < 8u; d++) { acc[d] += vec4f(part[d]); }'
            : '    for (var j = 0u; j < TILE; j++) {\n      let e = exp(s[j] - m);\n      sum += e;\n      for (var d = 0u; d < 8u; d++) { acc[d] = fma(vec4f(e), '.concat(
                f16 ? 'vec4f(vs[j * 8u + d])' : 'vs[j * 8u + d]',
                ', acc[d]); }\n    }',
              ),
          '\n    workgroupBarrier();\n  }\n  if (!valid) { return; }\n  let gate = 1.0 / (1.0 + exp(-qkv[qrow * p.width + 3u * p.dmodel + h]));\n  let scale = gate / sum;\n  let oBase = qrow * p.dmodel + h * 32u;\n  for (var d = 0u; d < 8u; d++) {\n    let v = acc[d] * scale;\n    o[oBase + 4u * d] = v.x;\n    o[oBase + 4u * d + 1u] = v.y;\n    o[oBase + 4u * d + 2u] = v.z;\n    o[oBase + 4u * d + 3u] = v.w;\n  }\n}\n',
        )
    );
  };
  var ATTENTION = attentionSource(null);
  var ATTENTION_F16 = attentionSource({ k: true, pv: false });
  var ATTENTION_F16_MATH = attentionSource({ k: true, pv: true });
  var FLATTEN =
    /* wgsl */
    '\nstruct P { freqs: u32, frames: u32, channels: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read_write> y: array<f32>;\n@group(0) @binding(2) var<uniform> p: P;\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let i = id.x;\n  if (i >= p.freqs * p.frames * p.channels) { return; }\n  let c = i % p.channels;\n  let t = (i / p.channels) % p.frames;\n  let f = i / (p.channels * p.frames);\n  y[(t * p.freqs + f) * p.channels + c] = x[i];\n}\n';
  var SUM_HEAD =
    /* wgsl */
    '\nstruct P { frames: u32 }\n@group(0) @binding(0) var<storage, read> l: array<f32>;\n@group(0) @binding(1) var<storage, read_write> out: array<f32>;\n@group(0) @binding(2) var<uniform> p: P;\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let t = id.x;\n  if (t >= p.frames) { return; }\n  out[t] = l[t * 2u] + l[t * 2u + 1u];\n  out[p.frames + t] = l[t * 2u + 1u];\n}\n';
  var BEAT_THIS_WGSL_FRAMES = 1500;
  var MEL_BINS = 128;
  var HEAD_DIM = 32;
  var RELEARN_RUNS = 50;
  var product = function (shape) {
    return shape.reduce(function (a, b) {
      return a * b;
    }, 1);
  };
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
  var BeatThisWgsl = /** @class */ (function () {
    function _BeatThisWgsl(device, build, melBuffer, outBuffer, owned) {
      this.device = device;
      this.build = build;
      this.melBuffer = melBuffer;
      this.outBuffer = outBuffer;
      this.owned = owned;
      this.fineStages = /* @__PURE__ */ new Set();
      this.autoRuns = 0;
      this.graphs = /* @__PURE__ */ new Map();
    }
    Object.defineProperty(_BeatThisWgsl.prototype, 'bytes', {
      /** GPU memory this model holds (weights and every graph built), bytes: for a residency budget. */
      get: function () {
        var e_3, _f;
        var sum = this.owned.reduce(function (total, buffer) {
          return total + buffer.size;
        }, 0);
        try {
          for (var _g = __values(this.graphs.values()), _h = _g.next(); !_h.done; _h = _g.next()) {
            var graph = _h.value;
            sum += graph.owned.reduce(function (total, buffer) {
              return total + buffer.size;
            }, 0);
          }
        } catch (e_3_1) {
          e_3 = { error: e_3_1 };
        } finally {
          try {
            if (_h && !_h.done && (_f = _g.return)) _f.call(_g);
          } finally {
            if (e_3) throw e_3.error;
          }
        }
        return sum;
      },
      enumerable: false,
      configurable: true,
    });
    _BeatThisWgsl.prototype.graph = function (frames) {
      var e_4, _f;
      var found = this.graphs.get(frames);
      if (!found) {
        try {
          for (var _g = __values(this.graphs), _h = _g.next(); !_h.done; _h = _g.next()) {
            var _j = __read(_h.value, 2),
              length = _j[0],
              old = _j[1];
            if (length === BEAT_THIS_WGSL_FRAMES) continue;
            old.owned.forEach(function (buffer) {
              return buffer.destroy();
            });
            this.graphs.delete(length);
          }
        } catch (e_4_1) {
          e_4 = { error: e_4_1 };
        } finally {
          try {
            if (_h && !_h.done && (_f = _g.return)) _f.call(_g);
          } finally {
            if (e_4) throw e_4.error;
          }
        }
        found = this.build(frames);
        this.graphs.set(frames, found);
      }
      return found.steps;
    };
    /**
     * Uploads the weights (the .bin as written, f32 or --f16) and builds the whole fixed graph for one
     * 1500-frame chunk; shorter chunks get theirs on first use. `f16` (device created with 'shader-f16'): 'storage' keeps matmul and attention
     * tiles in f16 with f32 arithmetic, 'math' multiplies in f16 too (see kernels.ts for what each
     * costs). Without it the fp16 weights are unpacked and everything is computed in f32.
     */
    _BeatThisWgsl.create = function (_0, _1, _2) {
      return __async2(this, arguments, function (device, weights, manifest, options) {
        var T,
          owned,
          storage,
          weightBuffer,
          entry,
          tensor,
          shape,
          dim,
          stem,
          ffMult,
          tokens0,
          maxWidth,
          melBuffer,
          stemIn,
          x,
          qkv,
          o,
          u,
          flat,
          logits,
          outBuffer,
          zeros,
          freqsAt,
          freqs,
          table,
          pos,
          i,
          angle,
          ropeTable,
          pipelines2,
          pipeline,
          build,
          model;
        if (options === void 0) {
          options = {};
        }
        return __generator(this, function (_f) {
          switch (_f.label) {
            case 0:
              if (options.f16 && !device.features.has('shader-f16'))
                throw new Error('beat_this f16: device lacks shader-f16');
              T = BEAT_THIS_WGSL_FRAMES;
              owned = [];
              storage = function (floats, extra) {
                if (extra === void 0) {
                  extra = 0;
                }
                var buffer = device.createBuffer({
                  size: Math.max(16, floats * 4),
                  usage:
                    GPUBufferUsage.STORAGE |
                    GPUBufferUsage.COPY_SRC |
                    GPUBufferUsage.COPY_DST |
                    extra,
                });
                owned.push(buffer);
                return buffer;
              };
              weightBuffer = storage(Math.ceil(weights.byteLength / 4));
              device.queue.writeBuffer(
                weightBuffer,
                0,
                weights.buffer,
                weights.byteOffset,
                weights.byteLength,
              );
              entry = function (name) {
                var found = manifest.tensors[name];
                if (!found) throw new Error('beat_this weights: missing tensor '.concat(name));
                return found;
              };
              tensor = function (name) {
                var _f = entry(name),
                  offset = _f.byte_offset,
                  shape2 = _f.shape,
                  dtype = _f.dtype,
                  stride = _f.stride;
                var elements = stride ? shape2[0] * stride : product(shape2);
                return {
                  buffer: weightBuffer,
                  offset: offset,
                  size: elements * (dtype === 'f16' ? 2 : 4),
                };
              };
              shape = function (name) {
                return manifest.tensors[name].shape;
              };
              dim = manifest.hyper_parameters.transformer_dim;
              stem = manifest.hyper_parameters.stem_dim;
              ffMult = manifest.hyper_parameters.ff_mult;
              tokens0 = (MEL_BINS / 4) * T;
              maxWidth = function (c) {
                return 3 * c + c / HEAD_DIM;
              };
              melBuffer = storage(T * MEL_BINS);
              stemIn = storage(T * MEL_BINS);
              x = [storage(tokens0 * stem), storage(tokens0 * stem)];
              qkv = storage(Math.max(tokens0 * maxWidth(stem), T * maxWidth(dim)));
              o = storage(tokens0 * stem);
              u = storage(Math.max(tokens0 * stem * ffMult, T * dim * ffMult));
              flat = storage(T * 4 * stem * 8);
              logits = storage(T * 2);
              outBuffer = storage(T * 2);
              zeros = storage(4096);
              freqsAt = weights.byteOffset + entry('rope.freqs').byte_offset;
              freqs = new Float32Array(weights.buffer.slice(freqsAt, freqsAt + (HEAD_DIM / 2) * 4));
              table = new Float32Array(T * 16 * 2);
              for (pos = 0; pos < T; pos += 1) {
                for (i = 0; i < 16; i += 1) {
                  angle = Math.fround(Math.fround(pos) * freqs[i]);
                  table[(pos * 16 + i) * 2] = Math.cos(angle);
                  table[(pos * 16 + i) * 2 + 1] = Math.sin(angle);
                }
              }
              ropeTable = storage(table.length);
              device.queue.writeBuffer(ropeTable, 0, table);
              pipelines2 = new Map();
              pipeline = function (code) {
                var found = pipelines2.get(code);
                if (!found) {
                  found = device.createComputePipeline({
                    layout: 'auto',
                    compute: {
                      module: device.createShaderModule({ code: code }),
                      entryPoint: 'main',
                    },
                  });
                  pipelines2.set(code, found);
                }
                return found;
              };
              build = function (T2) {
                var graphOwned = [];
                var uniform = function (values) {
                  var data = new Uint32Array(Math.ceil(values.length / 4) * 4);
                  data.set(values);
                  var buffer = device.createBuffer({
                    size: data.byteLength,
                    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
                  });
                  device.queue.writeBuffer(buffer, 0, data);
                  graphOwned.push(buffer);
                  return buffer;
                };
                var steps = [];
                var scope = 'stem';
                var dispatch = function (code, bindings, params, count, detail) {
                  if (detail === void 0) {
                    detail = '';
                  }
                  var compute = pipeline(code);
                  var entries = __spreadArray(
                    __spreadArray([], __read(bindings), false),
                    [{ buffer: uniform(params) }],
                    false,
                  ).map(function (resource, binding) {
                    return { binding: binding, resource: resource };
                  });
                  var label = ''
                    .concat(scope, ' ')
                    .concat(KERNEL_NAMES.get(code))
                    .concat(detail ? ' '.concat(detail) : '');
                  steps.push({
                    kind: 'dispatch',
                    label: label,
                    pipeline: compute,
                    group: device.createBindGroup({
                      layout: compute.getBindGroupLayout(0),
                      entries: entries,
                    }),
                    count: count,
                  });
                };
                var groups = function (invocations) {
                  return [Math.ceil(invocations / 64), 1, 1];
                };
                var stage = function (name, buffer, floats) {
                  return steps.push({ kind: 'stage', name: name, buffer: buffer, floats: floats });
                };
                var matmul = function (input, weight, bias, output, m, flags, rope) {
                  if (rope === void 0) {
                    rope = { posDiv: 1, posMod: 1, dmodel: 0 };
                  }
                  var _f = __read(shape(weight), 2),
                    k = _f[0],
                    n = _f[1];
                  var _g = entry(weight),
                    dtype = _g.dtype,
                    stride = _g.stride;
                  if (!stride)
                    throw new Error(
                      'beat_this weights: '.concat(
                        weight,
                        ' has no row stride (re-export the weights)',
                      ),
                    );
                  var f16Kernel = options.f16 === 'math' ? MATMUL_F16_MATH : MATMUL_F16;
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
                var transformerPair = function (stream, prefix, rows, c, axis) {
                  var heads = c / HEAD_DIM;
                  var width = 3 * c + heads;
                  scope = prefix;
                  matmul(
                    stream,
                    ''.concat(prefix, '.attn.qkvg.w'),
                    ''.concat(prefix, '.attn.qkvg.b'),
                    qkv,
                    rows,
                    MATMUL_NORM | MATMUL_ROPE,
                    { posDiv: axis.posDiv, posMod: axis.n, dmodel: c },
                  );
                  dispatch(
                    !options.f16
                      ? ATTENTION
                      : options.f16 === 'math'
                        ? ATTENTION_F16_MATH
                        : ATTENTION_F16,
                    [{ buffer: qkv }, { buffer: o }],
                    [axis.n, heads, width, c, axis.seqStride, axis.batchStride],
                    [Math.ceil(axis.n / 64), heads, axis.batches],
                  );
                  matmul(o, ''.concat(prefix, '.attn.out.w'), null, stream, rows, MATMUL_RESIDUAL);
                  matmul(
                    stream,
                    ''.concat(prefix, '.ff.w1'),
                    ''.concat(prefix, '.ff.b1'),
                    u,
                    rows,
                    MATMUL_NORM | MATMUL_GELU,
                  );
                  matmul(
                    u,
                    ''.concat(prefix, '.ff.w2'),
                    ''.concat(prefix, '.ff.b2'),
                    stream,
                    rows,
                    MATMUL_RESIDUAL,
                  );
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
                var freqs_ = MEL_BINS / 4;
                var channels = stem;
                dispatch(
                  CONV_GELU,
                  [
                    { buffer: stemIn },
                    tensor('stem.conv.w'),
                    tensor('stem.conv.b'),
                    { buffer: x[0] },
                  ],
                  [freqs_, T2, 1, channels, 4],
                  [Math.ceil(channels / 64), Math.ceil((freqs_ * T2) / 64), 1],
                );
                var cur = 0;
                stage('stem', x[cur], freqs_ * T2 * channels);
                for (var block = 0; block < 3; block += 1) {
                  var rows = freqs_ * T2;
                  transformerPair(x[cur], 'block'.concat(block, '.freq'), rows, channels, {
                    n: freqs_,
                    seqStride: T2,
                    batchStride: 1,
                    batches: T2,
                    posDiv: T2,
                  });
                  stage('block'.concat(block, '_freq'), x[cur], rows * channels);
                  transformerPair(x[cur], 'block'.concat(block, '.time'), rows, channels, {
                    n: T2,
                    seqStride: 1,
                    batchStride: T2,
                    batches: freqs_,
                    posDiv: 1,
                  });
                  stage('block'.concat(block, '_time'), x[cur], rows * channels);
                  var next = 1 - cur;
                  scope = 'block'.concat(block);
                  dispatch(
                    CONV_GELU,
                    [
                      { buffer: x[cur] },
                      tensor('block'.concat(block, '.conv.w')),
                      tensor('block'.concat(block, '.conv.b')),
                      { buffer: x[next] },
                    ],
                    [freqs_ / 2, T2, channels, channels * 2, 2],
                    [Math.ceil((channels * 2) / 64), Math.ceil(((freqs_ / 2) * T2) / 64), 1],
                  );
                  cur = next;
                  freqs_ /= 2;
                  channels *= 2;
                  stage('block'.concat(block, '_conv'), x[cur], freqs_ * T2 * channels);
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
                for (var layer = 0; layer < manifest.hyper_parameters.n_layers; layer += 1) {
                  transformerPair(x[cur], 'layer'.concat(layer), T2, dim, {
                    n: T2,
                    seqStride: 1,
                    batchStride: T2,
                    batches: 1,
                    posDiv: 1,
                  });
                  stage('layer'.concat(layer), x[cur], T2 * dim);
                }
                scope = 'head';
                matmul(x[cur], 'head.w', 'head.b', logits, T2, MATMUL_NORM);
                dispatch(SUM_HEAD, [{ buffer: logits }, { buffer: outBuffer }], [T2], groups(T2));
                stage('logits', outBuffer, T2 * 2);
                return { steps: steps, owned: graphOwned };
              };
              model = new _BeatThisWgsl(device, build, melBuffer, outBuffer, owned);
              model.graph(T);
              return [4 /*yield*/, device.queue.onSubmittedWorkDone()];
            case 1:
              _f.sent();
              return [2 /*return*/, model];
          }
        });
      });
    };
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
    _BeatThisWgsl.prototype.run = function (_0) {
      return __async2(this, arguments, function (mel, options) {
        var _a,
          _b,
          _c,
          _d,
          _e,
          T,
          steps,
          device,
          encoder,
          readbacks,
          profiled,
          querySet,
          query,
          pass,
          gpuMs,
          slices,
          slice,
          inFlight,
          outstanding,
          submit,
          budgetMs,
          stageIndex,
          wallStarted,
          steps_1,
          steps_1_1,
          step,
          perDispatch,
          coarse,
          took,
          readback,
          e_5_1,
          timestamps,
          resolved,
          wallMs,
          stages,
          readbacks_1,
          readbacks_1_1,
          _f,
          name,
          buffer,
          e_6_1,
          profile,
          ticks_1,
          out;
        var e_5, _g, e_6, _h;
        var _this = this;
        if (options === void 0) {
          options = {};
        }
        return __generator(this, function (_j) {
          switch (_j.label) {
            case 0:
              T = mel.length / MEL_BINS;
              if (!Number.isInteger(T) || T < 1 || T > BEAT_THIS_WGSL_FRAMES) {
                throw new Error(
                  'beat_this chunk must be 1..'
                    .concat(BEAT_THIS_WGSL_FRAMES, ' frames x ')
                    .concat(MEL_BINS, ', got ')
                    .concat(mel.length, ' values'),
                );
              }
              steps = this.graph(T);
              device = this.device;
              device.queue.writeBuffer(
                this.melBuffer,
                0,
                mel.buffer,
                mel.byteOffset,
                mel.byteLength,
              );
              encoder = device.createCommandEncoder();
              readbacks = [];
              profiled =
                options.profile && device.features.has('timestamp-query')
                  ? steps.filter(function (step) {
                      return step.kind === 'dispatch';
                    })
                  : [];
              querySet = profiled.length
                ? device.createQuerySet({ type: 'timestamp', count: profiled.length * 2 })
                : null;
              query = 0;
              pass = null;
              gpuMs = 0;
              slices = 0;
              slice = (_a = options.slice) != null ? _a : 'none';
              inFlight =
                slice === 'auto' ? 1 : Math.max(1, (_b = options.inFlight) != null ? _b : 1);
              outstanding = [];
              submit = function (last) {
                if (last === void 0) {
                  last = false;
                }
                return __async2(_this, null, function () {
                  var started, took;
                  return __generator(this, function (_f) {
                    switch (_f.label) {
                      case 0:
                        pass == null ? void 0 : pass.end();
                        pass = null;
                        started = performance.now();
                        device.queue.submit([encoder.finish()]);
                        outstanding.push(device.queue.onSubmittedWorkDone());
                        _f.label = 1;
                      case 1:
                        if (!(outstanding.length > (last ? 0 : inFlight - 1)))
                          return [3 /*break*/, 3];
                        return [4 /*yield*/, outstanding.shift()];
                      case 2:
                        _f.sent();
                        return [3 /*break*/, 1];
                      case 3:
                        took = performance.now() - started;
                        gpuMs += took;
                        slices += 1;
                        encoder = device.createCommandEncoder();
                        return [2 /*return*/, took];
                    }
                  });
                });
              };
              budgetMs = (_c = options.budgetMs) != null ? _c : 8;
              if (slice === 'auto' && ++this.autoRuns % RELEARN_RUNS === 0) this.fineStages.clear();
              stageIndex = 0;
              wallStarted = performance.now();
              _j.label = 1;
            case 1:
              _j.trys.push([1, 11, 12, 13]);
              ((steps_1 = __values(steps)), (steps_1_1 = steps_1.next()));
              _j.label = 2;
            case 2:
              if (!!steps_1_1.done) return [3 /*break*/, 10];
              step = steps_1_1.value;
              perDispatch =
                slice === 'dispatch' || (slice === 'auto' && this.fineStages.has(stageIndex));
              if (!(perDispatch && pass)) return [3 /*break*/, 5];
              return [4 /*yield*/, submit()];
            case 3:
              _j.sent();
              return [4 /*yield*/, (_d = options.between) == null ? void 0 : _d.call(options)];
            case 4:
              _j.sent();
              _j.label = 5;
            case 5:
              if (step.kind === 'dispatch') {
                if (querySet) {
                  pass == null ? void 0 : pass.end();
                  pass = encoder.beginComputePass({
                    timestampWrites: {
                      querySet: querySet,
                      beginningOfPassWriteIndex: query,
                      endOfPassWriteIndex: query + 1,
                    },
                  });
                  query += 2;
                }
                pass != null ? pass : (pass = encoder.beginComputePass());
                pass.setPipeline(step.pipeline);
                pass.setBindGroup(0, step.group);
                pass.dispatchWorkgroups.apply(pass, __spreadArray([], __read(step.count), false));
                return [3 /*break*/, 9];
              }
              coarse = slice === 'stage' || (slice === 'auto' && !this.fineStages.has(stageIndex));
              if (!(coarse && step.name !== 'logits')) return [3 /*break*/, 8];
              return [4 /*yield*/, submit()];
            case 6:
              took = _j.sent();
              if (slice === 'auto' && took > budgetMs) this.fineStages.add(stageIndex);
              return [4 /*yield*/, (_e = options.between) == null ? void 0 : _e.call(options)];
            case 7:
              _j.sent();
              _j.label = 8;
            case 8:
              stageIndex += 1;
              if (!options.stages && step.name !== 'logits') return [3 /*break*/, 9];
              pass == null ? void 0 : pass.end();
              pass = null;
              readback = device.createBuffer({
                size: step.floats * 4,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
              });
              encoder.copyBufferToBuffer(step.buffer, 0, readback, 0, step.floats * 4);
              readbacks.push({ name: step.name, buffer: readback });
              _j.label = 9;
            case 9:
              steps_1_1 = steps_1.next();
              return [3 /*break*/, 2];
            case 10:
              return [3 /*break*/, 13];
            case 11:
              e_5_1 = _j.sent();
              e_5 = { error: e_5_1 };
              return [3 /*break*/, 13];
            case 12:
              try {
                if (steps_1_1 && !steps_1_1.done && (_g = steps_1.return)) _g.call(steps_1);
              } finally {
                if (e_5) throw e_5.error;
              }
              return [7 /*endfinally*/];
            case 13:
              pass == null ? void 0 : pass.end();
              timestamps = null;
              if (querySet) {
                resolved = device.createBuffer({
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
              return [4 /*yield*/, submit(true)];
            case 14:
              _j.sent();
              wallMs = performance.now() - wallStarted;
              stages = {};
              _j.label = 15;
            case 15:
              _j.trys.push([15, 20, 21, 22]);
              ((readbacks_1 = __values(readbacks)), (readbacks_1_1 = readbacks_1.next()));
              _j.label = 16;
            case 16:
              if (!!readbacks_1_1.done) return [3 /*break*/, 19];
              ((_f = readbacks_1_1.value), (name = _f.name), (buffer = _f.buffer));
              return [4 /*yield*/, buffer.mapAsync(GPUMapMode.READ)];
            case 17:
              _j.sent();
              stages[name] = new Float32Array(buffer.getMappedRange().slice(0));
              buffer.destroy();
              _j.label = 18;
            case 18:
              readbacks_1_1 = readbacks_1.next();
              return [3 /*break*/, 16];
            case 19:
              return [3 /*break*/, 22];
            case 20:
              e_6_1 = _j.sent();
              e_6 = { error: e_6_1 };
              return [3 /*break*/, 22];
            case 21:
              try {
                if (readbacks_1_1 && !readbacks_1_1.done && (_h = readbacks_1.return))
                  _h.call(readbacks_1);
              } finally {
                if (e_6) throw e_6.error;
              }
              return [7 /*endfinally*/];
            case 22:
              if (!timestamps) return [3 /*break*/, 24];
              return [4 /*yield*/, timestamps.mapAsync(GPUMapMode.READ)];
            case 23:
              _j.sent();
              ticks_1 = new BigInt64Array(timestamps.getMappedRange());
              profile = profiled.map(function (step, index) {
                return {
                  label: step.kind === 'dispatch' ? step.label : '',
                  ms: Number(ticks_1[index * 2 + 1] - ticks_1[index * 2]) / 1e6,
                };
              });
              timestamps.destroy();
              querySet == null ? void 0 : querySet.destroy();
              _j.label = 24;
            case 24:
              out = stages.logits;
              return [
                2 /*return*/,
                __spreadValues(
                  __spreadValues(
                    {
                      beat: out.subarray(0, T),
                      downbeat: out.subarray(T, 2 * T),
                      gpuMs: gpuMs,
                      wallMs: wallMs,
                      slices: slices,
                    },
                    options.stages ? { stages: stages } : {},
                  ),
                  profile ? { profile: profile } : {},
                ),
              ];
          }
        });
      });
    };
    _BeatThisWgsl.prototype.destroy = function () {
      this.owned.forEach(function (buffer) {
        return buffer.destroy();
      });
      this.graphs.forEach(function (graph) {
        return graph.owned.forEach(function (buffer) {
          return buffer.destroy();
        });
      });
    };
    return _BeatThisWgsl;
  })();
  var GELU2 =
    /* wgsl */
    '\nfn erf_f32(v: f32) -> f32 {\n  let a = abs(v);\n  let x = 1.0 / (1.0 + 0.3275911 * a);\n  let y = 1.0 - ((((1.061405429 * x - 1.453152027) * x + 1.421413741) * x - 0.284496736) * x + 0.254829592) * x * exp(-a * a);\n  return select(-y, y, v >= 0.0);\n}\nfn gelu(v: f32) -> f32 { return 0.5 * v * (1.0 + erf_f32(v * 0.7071067811865476)); }\n';
  var GEMM_GELU = 1;
  var GEMM_RESIDUAL = 2;
  var weightRead = function (f16) {
    return f16
      ? /* wgsl */
        '\n@group(0) @binding(1) var<storage, read> w: array<u32>;\nfn wload(e: u32) -> f32 {\n  let pair = unpack2x16float(w[e >> 1u]);\n  return select(pair.x, pair.y, (e & 1u) == 1u);\n}'
      : /* wgsl */
        '\n@group(0) @binding(1) var<storage, read> w: array<f32>;\nfn wload(e: u32) -> f32 { return w[e]; }';
  };
  var tiles = function (mode) {
    return (
      /* wgsl */
      '\nvar<workgroup> as_: array<'
        .concat(mode === 'f16' ? 'vec4h' : 'vec4f', ', 256>;\nvar<workgroup> bs_: array<')
        .concat(mode === 'f16' ? 'vec4h' : 'vec4f', ', 256>;')
    );
  };
  var tileVector = function (mode, value) {
    return ''.concat(mode === 'f16' ? 'vec4h' : 'vec4f', '(').concat(value, ')');
  };
  var kStep = function (mode) {
    return mode === 'f16'
      ? /* wgsl */
        '\n    var part: array<vec4h, 4>;\n    for (var kk = 0u; kk < 16u; kk++) {\n      let a = as_[kk * 16u + l.y];\n      let bb = bs_[kk * 16u + l.x];\n      for (var i = 0u; i < 4u; i++) { part[i] = fma(vec4h(a[i]), bb, part[i]); }\n    }\n    for (var i = 0u; i < 4u; i++) { acc[i] += vec4f(part[i]); }'
      : /* wgsl */
        '\n    for (var kk = 0u; kk < 16u; kk++) {\n      let a = as_[kk * 16u + l.y];\n      let bb = bs_[kk * 16u + l.x];\n      for (var i = 0u; i < 4u; i++) { acc[i] = fma(vec4f(a[i]), bb, acc[i]); }\n    }';
  };
  var enableF16 = function (mode) {
    return mode === 'f16' ? 'enable f16;' : '';
  };
  var gemmBody = function (columns, mode) {
    return (
      /* wgsl */
      '\n'
        .concat(
          tiles(mode),
          '\n@compute @workgroup_size(16, 16)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {\n  let n = ',
        )
        .concat(
          columns,
          ';\n  let row0 = wg.y * 64u;\n  let col0 = wg.x * 64u;\n  var acc: array<vec4f, 4>;\n  for (var k0 = 0u; k0 < p.k; k0 += 16u) {\n    // Invocation li: rows 4 (li / 16).. at k li % 16 of A, columns 4 (li % 16).. at k li / 16 of B.\n    let ak = k0 + li % 16u;\n    var av = vec4f(0.0);\n    let bk = k0 + li / 16u;\n    var bv = vec4f(0.0);\n    for (var c = 0u; c < 4u; c++) {\n      let ar = row0 + (li / 16u) * 4u + c;\n      if (ar < p.m && ak < p.k) { av[c] = wload(ar * p.k + ak); }\n      let bc = col0 + (li % 16u) * 4u + c;\n      if (bk < p.k && bc < n) { bv[c] = gather(bk, bc); }\n    }\n    as_[(li % 16u) * 16u + li / 16u] = ',
        )
        .concat(tileVector(mode, 'av'), ';\n    bs_[li] = ')
        .concat(tileVector(mode, 'bv'), ';\n    workgroupBarrier();')
        .concat(
          kStep(mode),
          '\n    workgroupBarrier();\n  }\n  for (var i = 0u; i < 4u; i++) {\n    let r = row0 + l.y * 4u + i;\n    if (r >= p.m) { continue; }\n    for (var j = 0u; j < 4u; j++) {\n      let c = col0 + l.x * 4u + j;\n      if (c >= n) { continue; }\n      store(r, c, acc[i][j]);\n    }\n  }\n}\n',
        )
    );
  };
  var gemmSource = function (f16, mode) {
    if (mode === void 0) {
      mode = 'f32';
    }
    return (
      /* wgsl */
      '\n'
        .concat(enableF16(mode), '\n')
        .concat(
          GELU2,
          '\nstruct P { m: u32, k: u32, cin: u32, kh: u32, kw: u32, sh: u32, sw: u32, ph: u32, pw: u32, dh: u32, dw: u32, hin: u32, win: u32, hout: u32, wout: u32, flags: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n',
        )
        .concat(
          weightRead(f16),
          '\n@group(0) @binding(2) var<storage, read> b: array<f32>;\n@group(0) @binding(3) var<storage, read> side: array<f32>;\n@group(0) @binding(4) var<storage, read_write> y: array<f32>;\n@group(0) @binding(5) var<uniform> p: P;\n\nfn gather(k: u32, n: u32) -> f32 {\n  let taps = p.kh * p.kw;\n  let ci = k / taps;\n  let r = k - ci * taps;\n  let a = r / p.kw;\n  let c = r - a * p.kw;\n  let ho = n / p.wout;\n  let wo = n - ho * p.wout;\n  let hi = i32(ho * p.sh + a * p.dh) - i32(p.ph);\n  let wi = i32(wo * p.sw + c * p.dw) - i32(p.pw);\n  if (hi < 0 || hi >= i32(p.hin) || wi < 0 || wi >= i32(p.win)) { return 0.0; }\n  return x[(ci * p.hin + u32(hi)) * p.win + u32(wi)];\n}\n\nfn store(r: u32, c: u32, sum: f32) {\n  var v = sum + b[r];\n  if ((p.flags & ',
        )
        .concat(
          GEMM_GELU,
          'u) != 0u) { v = gelu(v); }\n  let o = r * p.hout * p.wout + c;\n  if ((p.flags & ',
        )
        .concat(GEMM_RESIDUAL, 'u) != 0u) { y[o] = y[o] + side[r] * v; } else { y[o] = v; }\n}\n')
        .concat(gemmBody('p.hout * p.wout', mode), '\n')
    );
  };
  var gemmTrSource = function (f16, mode) {
    if (mode === void 0) {
      mode = 'f32';
    }
    return (
      /* wgsl */
      '\n'
        .concat(enableF16(mode), '\n')
        .concat(
          GELU2,
          '\nstruct P { m: u32, k: u32, cin: u32, hin: u32, win: u32, axis: u32, flags: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n',
        )
        .concat(
          weightRead(f16),
          '\n@group(0) @binding(2) var<storage, read> b: array<f32>;\n@group(0) @binding(3) var<storage, read_write> y: array<f32>;\n@group(0) @binding(4) var<uniform> p: P;\n\nfn gather(k: u32, n: u32) -> f32 {\n  let ci = k / 2u;\n  let t = k - ci * 2u;\n  if (p.axis == 0u) {\n    let q = n / p.win;\n    let w = n - q * p.win;\n    if (q < t || q - t >= p.hin) { return 0.0; }\n    return x[(ci * p.hin + q - t) * p.win + w];\n  }\n  let h = n / (p.win + 1u);\n  let q = n - h * (p.win + 1u);\n  if (q < t || q - t >= p.win) { return 0.0; }\n  return x[(ci * p.hin + h) * p.win + q - t];\n}\n\nfn store(r: u32, c: u32, sum: f32) {\n  let co = r / 4u;\n  let ph = r - co * 4u;\n  var v = sum + b[co];\n  if ((p.flags & ',
        )
        .concat(
          GEMM_GELU,
          'u) != 0u) { v = gelu(v); }\n  if (p.axis == 0u) {\n    let q = c / p.win;\n    let w = c - q * p.win;\n    let o = 4u * q + ph;\n    if (o < 2u || o - 2u >= 4u * p.hin) { return; }\n    y[(co * 4u * p.hin + o - 2u) * p.win + w] = v;\n  } else {\n    let h = c / (p.win + 1u);\n    let q = c - h * (p.win + 1u);\n    let o = 4u * q + ph;\n    if (o < 2u || o - 2u >= 4u * p.win) { return; }\n    y[(co * p.hin + h) * 4u * p.win + o - 2u] = v;\n  }\n}\n',
        )
        .concat(
          gemmBody('select(p.hin * (p.win + 1u), (p.hin + 1u) * p.win, p.axis == 0u)', mode),
          '\n',
        )
    );
  };
  var STATS_PARTIAL =
    /* wgsl */
    '\nstruct P { parts: u32, c: u32, inner: u32, cs: u32, gs: u32, mode: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read> stats: array<f32>;\n@group(0) @binding(2) var<storage, read_write> partial: array<f32>;\n@group(0) @binding(3) var<uniform> p: P;\nvar<workgroup> sums: array<f32, 256>;\n@compute @workgroup_size(256)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) li: u32) {\n  let g = wg.y;\n  let n = p.c * p.inner;\n  let chunk = (n + p.parts - 1u) / p.parts;\n  let lo = wg.x * chunk;\n  let hi = min(n, lo + chunk);\n  let mean = stats[g * 4u];\n  var s = 0.0;\n  for (var e = lo + li; e < hi; e += 256u) {\n    let ch = e / p.inner;\n    let v = x[ch * p.cs + g * p.gs + e - ch * p.inner];\n    if (p.mode == 0u) { s += v; } else { let d = v - mean; s = fma(d, d, s); }\n  }\n  sums[li] = s;\n  workgroupBarrier();\n  for (var span = 128u; span > 0u; span >>= 1u) {\n    if (li < span) { sums[li] += sums[li + span]; }\n    workgroupBarrier();\n  }\n  if (li == 0u) { partial[g * p.parts + wg.x] = sums[0]; }\n}\n';
  var STATS_FINAL =
    /* wgsl */
    '\nstruct P { parts: u32, n: u32, mode: u32 }\n@group(0) @binding(0) var<storage, read> partial: array<f32>;\n@group(0) @binding(1) var<storage, read_write> stats: array<f32>;\n@group(0) @binding(2) var<uniform> p: P;\nvar<workgroup> sums: array<f32, 256>;\n@compute @workgroup_size(256)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) li: u32) {\n  let g = wg.x;\n  var s = 0.0;\n  for (var i = li; i < p.parts; i += 256u) { s += partial[g * p.parts + i]; }\n  sums[li] = s;\n  workgroupBarrier();\n  for (var span = 128u; span > 0u; span >>= 1u) {\n    if (li < span) { sums[li] += sums[li + span]; }\n    workgroupBarrier();\n  }\n  if (li != 0u) { return; }\n  let total = sums[0];\n  let n = f32(p.n);\n  if (p.mode == 0u) {\n    stats[g * 4u] = total / n;\n  } else if (p.mode == 1u) {\n    stats[g * 4u + 1u] = 1.0 / sqrt(total / n + 1e-5);\n  } else {\n    let sd = sqrt(total / (n - 1.0));\n    stats[g * 4u + 1u] = 1.0 / (sd + 1e-5);\n    stats[g * 4u + 2u] = sd;\n  }\n}\n';
  var GN_INPLACE =
    /* wgsl */
    '\n'.concat(
      GELU2,
      '\nstruct P { c: u32, h: u32, w: u32, perH: u32, gelu: u32 }\n@group(0) @binding(0) var<storage, read_write> x: array<f32>;\n@group(0) @binding(1) var<storage, read> stats: array<f32>;\n@group(0) @binding(2) var<storage, read> gamma: array<f32>;\n@group(0) @binding(3) var<storage, read> beta: array<f32>;\n@group(0) @binding(4) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  let plane = p.h * p.w;\n  if (i >= p.c * plane) { return; }\n  let ch = i / plane;\n  let g = select(0u, (i - ch * plane) / p.w, p.perH == 1u);\n  var v = (x[i] - stats[g * 4u]) * stats[g * 4u + 1u] * gamma[ch] + beta[ch];\n  if (p.gelu == 1u) { v = gelu(v); }\n  x[i] = v;\n}\n',
    );
  var GN_GLU_RESIDUAL =
    /* wgsl */
    '\nstruct P { c: u32, h: u32, w: u32, perH: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read> stats: array<f32>;\n@group(0) @binding(2) var<storage, read> gamma: array<f32>;\n@group(0) @binding(3) var<storage, read> beta: array<f32>;\n@group(0) @binding(4) var<storage, read> scale: array<f32>;\n@group(0) @binding(5) var<storage, read_write> res: array<f32>;\n@group(0) @binding(6) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  let plane = p.h * p.w;\n  if (i >= p.c * plane) { return; }\n  let ch = i / plane;\n  let at = i - ch * plane;\n  let g = select(0u, at / p.w, p.perH == 1u);\n  let mean = stats[g * 4u];\n  let rstd = stats[g * 4u + 1u];\n  let a = (x[i] - mean) * rstd * gamma[ch] + beta[ch];\n  let gch = ch + p.c;\n  let gate = (x[gch * plane + at] - mean) * rstd * gamma[gch] + beta[gch];\n  res[i] = res[i] + scale[ch] * (a / (1.0 + exp(-gate)));\n}\n';
  var GLU =
    /* wgsl */
    '\nstruct P { c: u32, h: u32, w: u32, emb: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read> emb: array<f32>;\n@group(0) @binding(2) var<storage, read_write> y: array<f32>;\n@group(0) @binding(3) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  let plane = p.h * p.w;\n  if (i >= p.c * plane) { return; }\n  var v = x[i] / (1.0 + exp(-x[i + p.c * plane]));\n  if (p.emb == 1u) {\n    let ch = i / plane;\n    v += emb[ch * p.h + (i - ch * plane) / p.w];\n  }\n  y[i] = v;\n}\n';
  var ADD =
    /* wgsl */
    '\nstruct P { n: u32 }\n@group(0) @binding(0) var<storage, read_write> x: array<f32>;\n@group(0) @binding(1) var<storage, read> y: array<f32>;\n@group(0) @binding(2) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  if (i < p.n) { x[i] = x[i] + y[i]; }\n}\n';
  var NORMALIZE =
    /* wgsl */
    '\nstruct P { n: u32 }\n@group(0) @binding(0) var<storage, read_write> x: array<f32>;\n@group(0) @binding(1) var<storage, read> stats: array<f32>;\n@group(0) @binding(2) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  if (i < p.n) { x[i] = (x[i] - stats[0]) * stats[1]; }\n}\n';
  var LAYER_NORM =
    /* wgsl */
    '\nstruct P { c: u32, n: u32, pos: u32 }\n@group(0) @binding(0) var<storage, read> x: array<f32>;\n@group(0) @binding(1) var<storage, read> gamma: array<f32>;\n@group(0) @binding(2) var<storage, read> beta: array<f32>;\n@group(0) @binding(3) var<storage, read> pos: array<f32>;\n@group(0) @binding(4) var<storage, read_write> y: array<f32>;\n@group(0) @binding(5) var<uniform> p: P;\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let t = id.x;\n  if (t >= p.n) { return; }\n  var s = 0.0;\n  for (var ch = 0u; ch < p.c; ch++) { s += x[ch * p.n + t]; }\n  let mean = s / f32(p.c);\n  var q = 0.0;\n  for (var ch = 0u; ch < p.c; ch++) { let d = x[ch * p.n + t] - mean; q = fma(d, d, q); }\n  let rstd = 1.0 / sqrt(q / f32(p.c) + 1e-5);\n  for (var ch = 0u; ch < p.c; ch++) {\n    var v = (x[ch * p.n + t] - mean) * rstd * gamma[ch] + beta[ch];\n    if (p.pos == 1u) { v += pos[ch * p.n + t]; }\n    y[ch * p.n + t] = v;\n  }\n}\n';
  var attnScoresSource = function (mode) {
    if (mode === void 0) {
      mode = 'f32';
    }
    return (
      /* wgsl */
      '\n'
        .concat(
          enableF16(mode),
          '\nstruct P { nq: u32, nk: u32, qr0: u32, kr0: u32, head0: u32 }\n@group(0) @binding(0) var<storage, read> q: array<f32>;\n@group(0) @binding(1) var<storage, read> kv: array<f32>;\n@group(0) @binding(2) var<storage, read_write> s: array<f32>;\n@group(0) @binding(3) var<uniform> p: P;\n',
        )
        .concat(
          tiles(mode),
          ' // [16 d][16 query quads], [16 d][16 key quads]\n@compute @workgroup_size(16, 16)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {\n  let h = p.head0 + wg.z;\n  let row0 = wg.y * 64u;\n  let col0 = wg.x * 64u;\n  var acc: array<vec4f, 4>;\n  for (var d0 = 0u; d0 < 64u; d0 += 16u) {\n    // Invocation li: queries and keys 4 (li % 16).. at dimension li / 16.\n    let d = d0 + li / 16u;\n    var av = vec4f(0.0);\n    var bv = vec4f(0.0);\n    for (var c = 0u; c < 4u; c++) {\n      let e = (li % 16u) * 4u + c;\n      let i = row0 + e;\n      let j = col0 + e;\n      av[c] = select(0.0, q[(p.qr0 + h * 64u + d) * p.nq + min(i, p.nq - 1u)], i < p.nq);\n      bv[c] = select(0.0, kv[(p.kr0 + h * 64u + d) * p.nk + min(j, p.nk - 1u)], j < p.nk);\n    }\n    as_[li] = ',
        )
        .concat(tileVector(mode, 'av'), ';\n    bs_[li] = ')
        .concat(tileVector(mode, 'bv'), ';\n    workgroupBarrier();')
        .concat(
          kStep(mode),
          '\n    workgroupBarrier();\n  }\n  for (var i = 0u; i < 4u; i++) {\n    let r = row0 + l.y * 4u + i;\n    if (r >= p.nq) { continue; }\n    for (var j = 0u; j < 4u; j++) {\n      let c = col0 + l.x * 4u + j;\n      if (c < p.nk) { s[(wg.z * p.nq + r) * p.nk + c] = acc[i][j]; }\n    }\n  }\n}\n',
        )
    );
  };
  var ATTN_SOFTMAX =
    /* wgsl */
    '\nstruct P { nk: u32 }\n@group(0) @binding(0) var<storage, read_write> s: array<f32>;\n@group(0) @binding(1) var<uniform> p: P;\nvar<workgroup> red: array<f32, 256>;\n@compute @workgroup_size(256)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(num_workgroups) groups: vec3u, @builtin(local_invocation_index) li: u32) {\n  let base = (wg.x + wg.y * groups.x) * p.nk;\n  var m = -3.0e38;\n  for (var j = li; j < p.nk; j += 256u) { m = max(m, s[base + j]); }\n  red[li] = m;\n  workgroupBarrier();\n  for (var span = 128u; span > 0u; span >>= 1u) {\n    if (li < span) { red[li] = max(red[li], red[li + span]); }\n    workgroupBarrier();\n  }\n  m = red[0];\n  workgroupBarrier();\n  var sum = 0.0;\n  for (var j = li; j < p.nk; j += 256u) {\n    let e = exp(s[base + j] - m);\n    s[base + j] = e;\n    sum += e;\n  }\n  red[li] = sum;\n  workgroupBarrier();\n  for (var span = 128u; span > 0u; span >>= 1u) {\n    if (li < span) { red[li] += red[li + span]; }\n    workgroupBarrier();\n  }\n  let inv = 1.0 / red[0];\n  for (var j = li; j < p.nk; j += 256u) { s[base + j] *= inv; }\n}\n';
  var attnOutSource = function (mode) {
    if (mode === void 0) {
      mode = 'f32';
    }
    return (
      /* wgsl */
      '\n'
        .concat(
          enableF16(mode),
          '\nstruct P { nq: u32, nk: u32, vr0: u32, head0: u32 }\n@group(0) @binding(0) var<storage, read> s: array<f32>;\n@group(0) @binding(1) var<storage, read> kv: array<f32>;\n@group(0) @binding(2) var<storage, read_write> o: array<f32>;\n@group(0) @binding(3) var<uniform> p: P;\n',
        )
        .concat(
          tiles(mode),
          ' // [16 keys][16 query quads], [16 keys][16 d quads]\n@compute @workgroup_size(16, 16)\nfn main(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) l: vec3u, @builtin(local_invocation_index) li: u32) {\n  let h = p.head0 + wg.z;\n  let row0 = wg.y * 64u;\n  var acc: array<vec4f, 4>;\n  for (var j0 = 0u; j0 < p.nk; j0 += 16u) {\n    // Invocation li: queries and dimensions 4 (li / 16).. at key li % 16.\n    let jj = li % 16u;\n    let j = j0 + jj;\n    var a = vec4f(0.0);\n    var b = vec4f(0.0);\n    for (var c = 0u; c < 4u; c++) {\n      let r = (li / 16u) * 4u + c;\n      let i = row0 + r;\n      if (j < p.nk) {\n        if (i < p.nq) { a[c] = s[(wg.z * p.nq + i) * p.nk + j]; }\n        b[c] = kv[(p.vr0 + h * 64u + r) * p.nk + j];\n      }\n    }\n    as_[jj * 16u + li / 16u] = ',
        )
        .concat(tileVector(mode, 'a'), ';\n    bs_[jj * 16u + li / 16u] = ')
        .concat(tileVector(mode, 'b'), ';\n    workgroupBarrier();')
        .concat(
          kStep(mode),
          '\n    workgroupBarrier();\n  }\n  for (var i = 0u; i < 4u; i++) {\n    let r = row0 + l.y * 4u + i;\n    if (r >= p.nq) { continue; }\n    for (var j = 0u; j < 4u; j++) {\n      let d = l.x * 4u + j;\n      o[(h * 64u + d) * p.nq + r] = acc[i][j];\n    }\n  }\n}\n',
        )
    );
  };
  var TWIDDLE_N = 4096;
  var STFT_A =
    /* wgsl */
    '\nstruct P { frames: u32, len: u32 }\n@group(0) @binding(0) var<storage, read> mix: array<f32>;      // [2, len]\n@group(0) @binding(1) var<storage, read> win: array<f32>;\n@group(0) @binding(2) var<storage, read> tw: array<vec2f>;\n@group(0) @binding(3) var<storage, read_write> y: array<vec2f>;  // [2 frames, 64 k1, 64 n2]\n@group(0) @binding(4) var<uniform> p: P;\nfn reflect(i: i32, n: i32) -> i32 {\n  var j = select(i, -i, i < 0);\n  return select(j, 2 * (n - 1) - j, j >= n);\n}\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let n2 = id.x;\n  let k1 = id.y;\n  let s = id.z;\n  if (n2 >= 64u || k1 >= 64u || s >= 2u * p.frames) { return; }\n  let ch = s / p.frames;\n  let f = s - ch * p.frames;\n  let padded = i32(p.len) + 3072;\n  var acc = vec2f(0.0);\n  for (var n1 = 0u; n1 < 64u; n1++) {\n    let n = n1 * 64u + n2;\n    let at = reflect(reflect(i32((f + 2u) * 1024u + n) - 2048, padded) - 1536, i32(p.len));\n    let v = mix[ch * p.len + u32(at)] * win[n];\n    let t = tw[((k1 * n1) % 64u) * 64u];\n    acc += v * vec2f(t.x, -t.y);\n  }\n  let t = tw[k1 * n2];\n  y[(s * 64u + k1) * 64u + n2] = vec2f(acc.x * t.x + acc.y * t.y, acc.y * t.x - acc.x * t.y);\n}\n';
  var STFT_B =
    /* wgsl */
    '\nstruct P { frames: u32 }\n@group(0) @binding(0) var<storage, read> y: array<vec2f>;\n@group(0) @binding(1) var<storage, read> tw: array<vec2f>;\n@group(0) @binding(2) var<storage, read_write> spec: array<f32>;\n@group(0) @binding(3) var<uniform> p: P;\n@compute @workgroup_size(32)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let k2 = id.x;\n  let k1 = id.y;\n  let s = id.z;\n  if (k2 >= 32u || k1 >= 64u || s >= 2u * p.frames) { return; }\n  var acc = vec2f(0.0);\n  for (var n2 = 0u; n2 < 64u; n2++) {\n    let v = y[(s * 64u + k1) * 64u + n2];\n    let t = tw[((k2 * n2) % 64u) * 64u];\n    acc += vec2f(v.x * t.x + v.y * t.y, v.y * t.x - v.x * t.y);\n  }\n  let ch = s / p.frames;\n  let f = s - ch * p.frames;\n  let k = k1 + 64u * k2;\n  spec[((ch * 2u) * 2048u + k) * p.frames + f] = acc.x;\n  spec[((ch * 2u + 1u) * 2048u + k) * p.frames + f] = acc.y;\n}\n';
  var ISTFT_A =
    /* wgsl */
    '\nstruct P { frames: u32, source: u32 }\n@group(0) @binding(0) var<storage, read> z: array<f32>;     // [16, 2048, frames]\n@group(0) @binding(1) var<storage, read> stats: array<f32>;\n@group(0) @binding(2) var<storage, read> tw: array<vec2f>;\n@group(0) @binding(3) var<storage, read_write> y: array<vec2f>;\n@group(0) @binding(4) var<uniform> p: P;\nfn bin(ch: u32, f: u32, k: u32) -> vec2f {\n  let kk = select(k, 4096u - k, k > 2048u);\n  if (kk == 2048u) { return vec2f(0.0); }\n  let base = (p.source * 4u + ch * 2u) * 2048u + kk;\n  let re = z[base * p.frames + f] * stats[2] + stats[0];\n  let im = z[(base + 2048u) * p.frames + f] * stats[2] + stats[0];\n  return select(vec2f(re, im), vec2f(re, -im), k > 2048u);\n}\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let n2 = id.x;\n  let k1 = id.y;\n  let s = id.z;\n  if (n2 >= 64u || k1 >= 64u || s >= 2u * p.frames) { return; }\n  let ch = s / p.frames;\n  let f = s - ch * p.frames;\n  var acc = vec2f(0.0);\n  for (var k2 = 0u; k2 < 64u; k2++) {\n    let v = bin(ch, f, k1 + 64u * k2);\n    let t = tw[((k2 * n2) % 64u) * 64u];\n    acc += vec2f(v.x * t.x - v.y * t.y, v.x * t.y + v.y * t.x);\n  }\n  let t = tw[k1 * n2];\n  y[(s * 64u + k1) * 64u + n2] = vec2f(acc.x * t.x - acc.y * t.y, acc.x * t.y + acc.y * t.x);\n}\n';
  var ISTFT_B =
    /* wgsl */
    '\nstruct P { frames: u32 }\n@group(0) @binding(0) var<storage, read> y: array<vec2f>;\n@group(0) @binding(1) var<storage, read> tw: array<vec2f>;\n@group(0) @binding(2) var<storage, read> win: array<f32>;\n@group(0) @binding(3) var<storage, read_write> out: array<f32>;  // [2 frames, 4096]\n@group(0) @binding(4) var<uniform> p: P;\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) id: vec3u) {\n  let n2 = id.x;\n  let n1 = id.y;\n  let s = id.z;\n  if (n2 >= 64u || n1 >= 64u || s >= 2u * p.frames) { return; }\n  var acc = 0.0;\n  for (var k1 = 0u; k1 < 64u; k1++) {\n    let v = y[(s * 64u + k1) * 64u + n2];\n    let t = tw[((k1 * n1) % 64u) * 64u];\n    acc += v.x * t.x - v.y * t.y;\n  }\n  let n = n1 * 64u + n2;\n  out[s * 4096u + n] = acc / 4096.0 * win[n];\n}\n';
  var OVERLAP_ADD =
    /* wgsl */
    '\nstruct P { frames: u32, len: u32, source: u32, out_at: u32 }\n@group(0) @binding(0) var<storage, read> fr: array<f32>;\n@group(0) @binding(1) var<storage, read> env: array<f32>;\n@group(0) @binding(2) var<storage, read> xt: array<f32>;      // [8, len]\n@group(0) @binding(3) var<storage, read> tstats: array<f32>;\n@group(0) @binding(4) var<storage, read_write> out: array<f32>; // [2, len] at out_at (one per source asked for)\n@group(0) @binding(5) var<uniform> p: P;\n@compute @workgroup_size(256)\nfn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {\n  let i = id.x + id.y * groups.x * 256u;\n  if (i >= 2u * p.len) { return; }\n  let ch = i / p.len;\n  let j = i - ch * p.len;\n  let pos = 3584u + j;\n  var acc = 0.0;\n  let last = pos / 1024u;\n  for (var jj = select(0u, last - 3u, last >= 3u); jj <= last; jj++) {\n    if (jj < 2u || jj >= p.frames + 2u) { continue; }\n    let n = pos - jj * 1024u;\n    if (n < 4096u) { acc += fr[(ch * p.frames + jj - 2u) * 4096u + n]; }\n  }\n  let t = xt[(p.source * 2u + ch) * p.len + j] * tstats[2] + tstats[0];\n  out[p.out_at + i] = acc / env[1536u + j] + t;\n}\n';
  var HTDEMUCS_SEGMENT = 172032;
  var HTDEMUCS_SOURCES = ['drums', 'bass', 'other', 'vocals'];
  var FRAMES = 168;
  var FREQ_TOKENS = 8;
  var BINS = 2048;
  var DIM = 512;
  var HEADS = 8;
  var HEADS_PER_DISPATCH = 4;
  var RELEARN_RUNS2 = 50;
  var product2 = function (shape) {
    return shape.reduce(function (a, b) {
      return a * b;
    }, 1);
  };
  var HtdemucsWgsl = /** @class */ (function () {
    function _HtdemucsWgsl(device, steps, mixBuffer, outBuffer, owned) {
      this.device = device;
      this.steps = steps;
      this.mixBuffer = mixBuffer;
      this.outBuffer = outBuffer;
      this.owned = owned;
      this.fineStages = /* @__PURE__ */ new Set();
      this.autoRuns = 0;
    }
    Object.defineProperty(_HtdemucsWgsl.prototype, 'bytes', {
      /** GPU memory this model holds (weights, activations, uniforms), bytes: for a residency budget. */
      get: function () {
        return this.owned.reduce(function (sum, buffer) {
          return sum + buffer.size;
        }, 0);
      },
      enumerable: false,
      configurable: true,
    });
    /**
     * Uploads the weights (the .bin as written, f32 or --f16) and builds the whole graph. `source` is
     * which stem a run returns (an index into HTDEMUCS_SOURCES); `sources`, which several (all four
     * for automix), the first returned as left/right.
     */
    _HtdemucsWgsl.create = function (_0, _1, _2) {
      return __async2(this, arguments, function (device, weights, manifest, options) {
        var _a,
          _b,
          sources,
          mode,
          ATTN_SCORES,
          ATTN_OUT,
          L,
          owned,
          storage,
          weightBuffer,
          entry,
          tensor,
          isHalf,
          rows,
          twiddles,
          j,
          tw,
          top,
          mixBuffer,
          outBuffer,
          mixStats,
          specStats,
          scratchStats,
          partial,
          zeros,
          dft,
          xin,
          stream,
          hidden,
          wide,
          ping,
          freqOut,
          timeOut,
          frames,
          skips,
          tskips,
          i,
          nf,
          nt,
          xf,
          xt,
          normF,
          normT,
          otherF,
          otherT,
          qkvF,
          qkvT,
          kvF,
          kvT,
          attF,
          attT,
          ff,
          scores,
          pipelines2,
          pipeline,
          uniform,
          steps,
          scope,
          kernelNames,
          dispatch,
          flat,
          stage,
          statsOf,
          groupStats,
          conv,
          convTr,
          dconv,
          tIn,
          tCur,
          fCur,
          tLen,
          fBins,
          i,
          c,
          _f,
          tl,
          _g,
          fb,
          layerNorm,
          linear,
          attention,
          feedForward,
          layer,
          pf,
          pt,
          _h,
          _j,
          _k,
          x,
          p,
          n,
          norm,
          qkv,
          att,
          cur,
          i,
          c,
          plane,
          next,
          i,
          c,
          next,
          invalid,
          model;
        var e_7, _l;
        if (options === void 0) {
          options = {};
        }
        return __generator(this, function (_m) {
          switch (_m.label) {
            case 0:
              if (manifest.segment !== HTDEMUCS_SEGMENT)
                throw new Error(
                  'htdemucs weights are for a '
                    .concat(manifest.segment, '-sample segment, not ')
                    .concat(HTDEMUCS_SEGMENT),
                );
              if (options.f16 && !device.features.has('shader-f16'))
                throw new Error('htdemucs f16 needs a device opened with shader-f16');
              sources =
                (_b = options.sources) != null ? _b : [(_a = options.source) != null ? _a : 0];
              if (
                sources.length === 0 ||
                sources.some(function (source) {
                  return !(source >= 0 && source < HTDEMUCS_SOURCES.length);
                })
              )
                throw new Error(
                  'htdemucs sources must be indices into '
                    .concat(HTDEMUCS_SOURCES.join(', '), ', got ')
                    .concat(sources.join(', ')),
                );
              mode = options.f16 ? 'f16' : 'f32';
              ATTN_SCORES = attnScoresSource(mode);
              ATTN_OUT = attnOutSource(mode);
              device.pushErrorScope('validation');
              L = HTDEMUCS_SEGMENT;
              owned = [];
              storage = function (floats) {
                var buffer = device.createBuffer({
                  size: Math.max(16, Math.ceil(floats) * 4),
                  usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
                });
                owned.push(buffer);
                return buffer;
              };
              weightBuffer = storage(Math.ceil(weights.byteLength / 4));
              device.queue.writeBuffer(
                weightBuffer,
                0,
                weights.buffer,
                weights.byteOffset,
                weights.byteLength,
              );
              entry = function (name) {
                var found = manifest.tensors[name];
                if (!found) throw new Error('htdemucs weights: missing tensor '.concat(name));
                return found;
              };
              tensor = function (name) {
                var _f = entry(name),
                  offset = _f.byte_offset,
                  shape = _f.shape,
                  dtype = _f.dtype;
                var bytes = product2(shape) * (dtype === 'f16' ? 2 : 4);
                return { buffer: weightBuffer, offset: offset, size: Math.ceil(bytes / 4) * 4 };
              };
              isHalf = function (name) {
                return entry(name).dtype === 'f16';
              };
              rows = function (name) {
                return entry(name).shape[0];
              };
              twiddles = new Float32Array(TWIDDLE_N * 2);
              for (j = 0; j < TWIDDLE_N; j += 1) {
                twiddles[j * 2] = Math.cos((2 * Math.PI * j) / TWIDDLE_N);
                twiddles[j * 2 + 1] = Math.sin((2 * Math.PI * j) / TWIDDLE_N);
              }
              tw = storage(twiddles.length);
              device.queue.writeBuffer(tw, 0, twiddles);
              top = 48 * (BINS / 4) * FRAMES;
              mixBuffer = storage(2 * L);
              outBuffer = storage(2 * L * sources.length);
              mixStats = storage(4);
              specStats = storage(4);
              scratchStats = storage(4 * BINS);
              partial = storage(4096);
              zeros = storage(64);
              dft = storage(2 * FRAMES * 4096 * 2);
              xin = storage(4 * BINS * FRAMES);
              stream = storage(top);
              hidden = storage(top / 8);
              wide = storage(2 * top);
              ping = [storage(16 * BINS * FRAMES), storage(16 * BINS * FRAMES)];
              freqOut = storage(16 * BINS * FRAMES);
              timeOut = storage(8 * L);
              frames = storage(2 * FRAMES * 4096);
              skips = [];
              tskips = [];
              for (i = 0; i < 4; i += 1) {
                skips.push(storage(48 * Math.pow(2, i) * (BINS / Math.pow(4, i + 1)) * FRAMES));
                tskips.push(storage(48 * Math.pow(2, i) * (L / Math.pow(4, i + 1))));
              }
              nf = FREQ_TOKENS * FRAMES;
              nt = L / 256;
              xf = storage(DIM * nf);
              xt = storage(DIM * nt);
              normF = storage(DIM * nf);
              normT = storage(DIM * nt);
              otherF = storage(DIM * nf);
              otherT = storage(DIM * nt);
              qkvF = storage(3 * DIM * nf);
              qkvT = storage(3 * DIM * nt);
              kvF = storage(2 * DIM * nt);
              kvT = storage(2 * DIM * nf);
              attF = storage(DIM * nf);
              attT = storage(DIM * nt);
              ff = storage(4 * DIM * nf);
              scores = storage(HEADS_PER_DISPATCH * nf * nf);
              pipelines2 = new Map();
              pipeline = function (code) {
                var found = pipelines2.get(code);
                if (!found) {
                  found = device.createComputePipeline({
                    layout: 'auto',
                    compute: {
                      module: device.createShaderModule({ code: code }),
                      entryPoint: 'main',
                    },
                  });
                  pipelines2.set(code, found);
                }
                return found;
              };
              uniform = function (values) {
                var data = new Uint32Array(Math.ceil(values.length / 4) * 4);
                data.set(values);
                var buffer = device.createBuffer({
                  size: data.byteLength,
                  usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
                });
                device.queue.writeBuffer(buffer, 0, data);
                owned.push(buffer);
                return buffer;
              };
              steps = [];
              scope = 'input';
              kernelNames = new Map([
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
              dispatch = function (code, bindings, params, count, detail) {
                if (detail === void 0) {
                  detail = '';
                }
                var _a2;
                var compute = pipeline(code);
                var entries = __spreadArray(
                  __spreadArray([], __read(bindings), false),
                  [uniform(params)],
                  false,
                ).map(function (resource, binding) {
                  return {
                    binding: binding,
                    resource: resource instanceof GPUBuffer ? { buffer: resource } : resource,
                  };
                });
                var label = ''
                  .concat(scope, ' ')
                  .concat((_a2 = kernelNames.get(code)) != null ? _a2 : 'gemm')
                  .concat(detail ? ' '.concat(detail) : '');
                steps.push({
                  kind: 'dispatch',
                  label: label,
                  pipeline: compute,
                  group: device.createBindGroup({
                    layout: compute.getBindGroupLayout(0),
                    entries: entries,
                  }),
                  count: count,
                });
              };
              flat = function (n) {
                var groups = Math.ceil(n / 256);
                var x = Math.min(groups, 32768);
                return [x, Math.ceil(groups / x), 1];
              };
              stage = function (name, buffer, floats) {
                return steps.push({ kind: 'stage', name: name, buffer: buffer, floats: floats });
              };
              statsOf = function (input, target, groups, c, inner, cs, gs, final) {
                var n = c * inner;
                var parts = Math.max(
                  1,
                  Math.min(256, Math.floor(4096 / groups), Math.ceil(n / 16384)),
                );
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
              groupStats = function (input, c, h, w, perH) {
                if (perH) statsOf(input, scratchStats, h, c, w, h * w, w, 1);
                else statsOf(input, scratchStats, 1, 1, c * h * w, 0, 0, 1);
              };
              conv = function (input, name, output, g, flags, side) {
                if (flags === void 0) {
                  flags = 0;
                }
                if (side === void 0) {
                  side = zeros;
                }
                var _f = g.sh,
                  sh = _f === void 0 ? 1 : _f,
                  _g = g.sw,
                  sw = _g === void 0 ? 1 : _g,
                  _h = g.ph,
                  ph = _h === void 0 ? 0 : _h,
                  _j = g.pw,
                  pw = _j === void 0 ? 0 : _j,
                  _k = g.dh,
                  dh = _k === void 0 ? 1 : _k,
                  _l = g.dw,
                  dw = _l === void 0 ? 1 : _l;
                var m = rows(''.concat(name, '.w'));
                var k = entry(''.concat(name, '.w')).shape[1];
                var cin = k / (g.kh * g.kw);
                var hout = Math.floor((g.hin + 2 * ph - dh * (g.kh - 1) - 1) / sh) + 1;
                var wout = Math.floor((g.win + 2 * pw - dw * (g.kw - 1) - 1) / sw) + 1;
                dispatch(
                  gemmSource(isHalf(''.concat(name, '.w')), mode),
                  [
                    input,
                    tensor(''.concat(name, '.w')),
                    tensor(''.concat(name, '.b')),
                    side,
                    output,
                  ],
                  [m, k, cin, g.kh, g.kw, sh, sw, ph, pw, dh, dw, g.hin, g.win, hout, wout, flags],
                  [Math.ceil((hout * wout) / 64), Math.ceil(m / 64), 1],
                  name.split('.').slice(1).join('.'),
                );
                return [hout, wout];
              };
              convTr = function (input, name, output, hin, win, axis, flags) {
                var m = rows(''.concat(name, '.w'));
                var k = entry(''.concat(name, '.w')).shape[1];
                var n = axis === 0 ? (hin + 1) * win : hin * (win + 1);
                dispatch(
                  gemmTrSource(isHalf(''.concat(name, '.w')), mode),
                  [input, tensor(''.concat(name, '.w')), tensor(''.concat(name, '.b')), output],
                  [m, k, k / 2, hin, win, axis, flags],
                  [Math.ceil(n / 64), Math.ceil(m / 64), 1],
                  'tr',
                );
              };
              dconv = function (prefix, c, h, w, perH) {
                for (var j = 0; j < 2; j += 1) {
                  var p = ''.concat(prefix, '.dc').concat(j);
                  var d = Math.pow(2, j);
                  var hid = rows(''.concat(p, '.c1.w'));
                  conv(stream, ''.concat(p, '.c1'), hidden, {
                    hin: h,
                    win: w,
                    kh: 1,
                    kw: 3,
                    pw: d,
                    dw: d,
                  });
                  groupStats(hidden, hid, h, w, perH);
                  dispatch(
                    GN_INPLACE,
                    [
                      hidden,
                      scratchStats,
                      tensor(''.concat(p, '.n1.g')),
                      tensor(''.concat(p, '.n1.b')),
                    ],
                    [hid, h, w, perH ? 1 : 0, 1],
                    flat(hid * h * w),
                  );
                  conv(hidden, ''.concat(p, '.c2'), wide, { hin: h, win: w, kh: 1, kw: 1 });
                  groupStats(wide, 2 * c, h, w, perH);
                  dispatch(
                    GN_GLU_RESIDUAL,
                    [
                      wide,
                      scratchStats,
                      tensor(''.concat(p, '.n2.g')),
                      tensor(''.concat(p, '.n2.b')),
                      tensor(''.concat(p, '.ls')),
                      stream,
                    ],
                    [c, h, w, perH ? 1 : 0],
                    flat(c * h * w),
                  );
                }
              };
              tIn = timeOut;
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
              tCur = tIn;
              fCur = xin;
              tLen = L;
              fBins = BINS;
              for (i = 0; i < 4; i += 1) {
                c = 48 * Math.pow(2, i);
                scope = 'tenc'.concat(i);
                ((_f = __read(
                  conv(
                    tCur,
                    'tenc'.concat(i, '.conv'),
                    stream,
                    { hin: 1, win: tLen, kh: 1, kw: 8, sw: 4, pw: 2 },
                    GEMM_GELU,
                  ),
                  2,
                )),
                  (tl = _f[1]));
                tLen = tl;
                dconv('tenc'.concat(i), c, 1, tLen, false);
                conv(stream, 'tenc'.concat(i, '.rw'), wide, { hin: 1, win: tLen, kh: 1, kw: 1 });
                dispatch(GLU, [wide, zeros, tskips[i]], [c, 1, tLen, 0], flat(c * tLen));
                stage('tenc'.concat(i), tskips[i], c * tLen);
                tCur = tskips[i];
                scope = 'enc'.concat(i);
                ((_g = __read(
                  conv(
                    fCur,
                    'enc'.concat(i, '.conv'),
                    stream,
                    { hin: fBins, win: FRAMES, kh: 8, kw: 1, sh: 4, ph: 2 },
                    GEMM_GELU,
                  ),
                  1,
                )),
                  (fb = _g[0]));
                fBins = fb;
                dconv('enc'.concat(i), c, fBins, FRAMES, true);
                conv(stream, 'enc'.concat(i, '.rw'), wide, {
                  hin: fBins,
                  win: FRAMES,
                  kh: 1,
                  kw: 1,
                });
                dispatch(
                  GLU,
                  [wide, i === 0 ? tensor('freq_emb') : zeros, skips[i]],
                  [c, fBins, FRAMES, i === 0 ? 1 : 0],
                  flat(c * fBins * FRAMES),
                );
                stage('enc'.concat(i), skips[i], c * fBins * FRAMES);
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
              layerNorm = function (input, name, output, n) {
                return dispatch(
                  LAYER_NORM,
                  [
                    input,
                    tensor(''.concat(name, '.g')),
                    tensor(''.concat(name, '.b')),
                    zeros,
                    output,
                  ],
                  [DIM, n, 0],
                  [Math.ceil(n / 64), 1, 1],
                );
              };
              linear = function (input, name, output, n, flags, side) {
                if (flags === void 0) {
                  flags = 0;
                }
                if (side === void 0) {
                  side = zeros;
                }
                return conv(input, name, output, { hin: 1, win: n, kh: 1, kw: 1 }, flags, side);
              };
              attention = function (q, kv, out, nq, nk, rowsQ, rowsK, rowsV) {
                for (var head = 0; head < HEADS; head += HEADS_PER_DISPATCH) {
                  var heads = 'heads '.concat(head, '-').concat(head + HEADS_PER_DISPATCH - 1);
                  dispatch(
                    ATTN_SCORES,
                    [q, kv, scores],
                    [nq, nk, rowsQ, rowsK, head],
                    [Math.ceil(nk / 64), Math.ceil(nq / 64), HEADS_PER_DISPATCH],
                    heads,
                  );
                  var rowCount = HEADS_PER_DISPATCH * nq;
                  var x = Math.min(rowCount, 32768);
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
              feedForward = function (x, p, n, norm) {
                layerNorm(x, ''.concat(p, '.nf'), norm, n);
                linear(norm, ''.concat(p, '.ff1'), ff, n, GEMM_GELU);
                linear(ff, ''.concat(p, '.ff2'), x, n, GEMM_RESIDUAL, tensor(''.concat(p, '.g2')));
                statsOf(x, scratchStats, 1, 1, DIM * n, 0, 0, 1);
                dispatch(
                  GN_INPLACE,
                  [x, scratchStats, tensor(''.concat(p, '.no.g')), tensor(''.concat(p, '.no.b'))],
                  [DIM, 1, n, 0, 0],
                  flat(DIM * n),
                );
              };
              for (layer = 0; layer < 5; layer += 1) {
                pf = 'tf'.concat(layer);
                pt = 'tft'.concat(layer);
                scope = pf;
                if (layer % 2 === 0) {
                  try {
                    for (
                      _h =
                        ((e_7 = void 0),
                        __values([
                          [xf, pf, nf, normF, qkvF, attF],
                          [xt, pt, nt, normT, qkvT, attT],
                        ])),
                        _j = _h.next();
                      !_j.done;
                      _j = _h.next()
                    ) {
                      ((_k = __read(_j.value, 6)),
                        (x = _k[0]),
                        (p = _k[1]),
                        (n = _k[2]),
                        (norm = _k[3]),
                        (qkv = _k[4]),
                        (att = _k[5]));
                      layerNorm(x, ''.concat(p, '.n1'), norm, n);
                      linear(norm, ''.concat(p, '.qkv'), qkv, n);
                      attention(qkv, qkv, att, n, n, 0, DIM, 2 * DIM);
                      linear(
                        att,
                        ''.concat(p, '.out'),
                        x,
                        n,
                        GEMM_RESIDUAL,
                        tensor(''.concat(p, '.g1')),
                      );
                      feedForward(x, p, n, norm);
                    }
                  } catch (e_7_1) {
                    e_7 = { error: e_7_1 };
                  } finally {
                    try {
                      if (_j && !_j.done && (_l = _h.return)) _l.call(_h);
                    } finally {
                      if (e_7) throw e_7.error;
                    }
                  }
                } else {
                  layerNorm(xf, ''.concat(pf, '.n1'), normF, nf);
                  linear(normF, ''.concat(pf, '.q'), qkvF, nf);
                  layerNorm(xt, ''.concat(pf, '.nkv'), otherT, nt);
                  linear(otherT, ''.concat(pf, '.kv'), kvF, nt);
                  layerNorm(xt, ''.concat(pt, '.n1'), normT, nt);
                  linear(normT, ''.concat(pt, '.q'), qkvT, nt);
                  layerNorm(xf, ''.concat(pt, '.nkv'), otherF, nf);
                  linear(otherF, ''.concat(pt, '.kv'), kvT, nf);
                  attention(qkvF, kvF, attF, nf, nt, 0, 0, DIM);
                  attention(qkvT, kvT, attT, nt, nf, 0, 0, DIM);
                  linear(
                    attF,
                    ''.concat(pf, '.out'),
                    xf,
                    nf,
                    GEMM_RESIDUAL,
                    tensor(''.concat(pf, '.g1')),
                  );
                  linear(
                    attT,
                    ''.concat(pt, '.out'),
                    xt,
                    nt,
                    GEMM_RESIDUAL,
                    tensor(''.concat(pt, '.g1')),
                  );
                  feedForward(xf, pf, nf, normF);
                  feedForward(xt, pt, nt, normT);
                }
                stage('tf'.concat(layer), xf, DIM * nf);
                stage('tft'.concat(layer), xt, DIM * nt);
              }
              cur = ping[0];
              scope = 'dec0';
              linear(xf, 'down', cur, nf);
              for (i = 0; i < 4; i += 1) {
                c = 48 * Math.pow(2, 3 - i);
                scope = 'dec'.concat(i);
                plane = fBins * FRAMES;
                dispatch(ADD, [cur, skips[3 - i]], [c * plane], flat(c * plane));
                conv(cur, 'dec'.concat(i, '.rw'), wide, {
                  hin: fBins,
                  win: FRAMES,
                  kh: 3,
                  kw: 3,
                  ph: 1,
                  pw: 1,
                });
                dispatch(GLU, [wide, zeros, stream], [c, fBins, FRAMES, 0], flat(c * plane));
                dconv('dec'.concat(i), c, fBins, FRAMES, true);
                next = i === 3 ? freqOut : ping[(i + 1) % 2];
                convTr(
                  stream,
                  'dec'.concat(i, '.tr'),
                  next,
                  fBins,
                  FRAMES,
                  0,
                  i === 3 ? 0 : GEMM_GELU,
                );
                fBins *= 4;
                stage('dec'.concat(i), next, (rows('dec'.concat(i, '.tr.w')) / 4) * fBins * FRAMES);
                cur = next;
              }
              cur = ping[0];
              scope = 'tdec0';
              linear(xt, 'down_t', cur, nt);
              for (i = 0; i < 4; i += 1) {
                c = 48 * Math.pow(2, 3 - i);
                scope = 'tdec'.concat(i);
                dispatch(ADD, [cur, tskips[3 - i]], [c * tLen], flat(c * tLen));
                conv(cur, 'tdec'.concat(i, '.rw'), wide, {
                  hin: 1,
                  win: tLen,
                  kh: 1,
                  kw: 3,
                  pw: 1,
                });
                dispatch(GLU, [wide, zeros, stream], [c, 1, tLen, 0], flat(c * tLen));
                dconv('tdec'.concat(i), c, 1, tLen, false);
                next = i === 3 ? timeOut : ping[(i + 1) % 2];
                convTr(stream, 'tdec'.concat(i, '.tr'), next, 1, tLen, 1, i === 3 ? 0 : GEMM_GELU);
                tLen *= 4;
                stage('tdec'.concat(i), next, (rows('tdec'.concat(i, '.tr.w')) / 4) * tLen);
                cur = next;
              }
              scope = 'output';
              sources.forEach(function (source, index) {
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
                  stage('out_'.concat(HTDEMUCS_SOURCES[source]), outBuffer, 2 * L * (index + 1));
              });
              stage('out', outBuffer, 2 * L * sources.length);
              return [4 /*yield*/, device.popErrorScope()];
            case 1:
              invalid = _m.sent();
              if (invalid) {
                owned.forEach(function (buffer) {
                  return buffer.destroy();
                });
                throw new Error('htdemucs graph rejected: '.concat(invalid.message));
              }
              model = new _HtdemucsWgsl(device, steps, mixBuffer, outBuffer, owned);
              return [4 /*yield*/, device.queue.onSubmittedWorkDone()];
            case 2:
              _m.sent();
              return [2 /*return*/, model];
          }
        });
      });
    };
    /**
     * One segment: `left` and `right` of exactly HTDEMUCS_SEGMENT samples at 44.1 kHz. `slice` as in
     * BeatThisWgsl.run: 'stage' submits at every stage boundary, 'dispatch' after every dispatch,
     * 'auto' at stage boundaries but per dispatch inside any stage that outlasted `budgetMs` (one
     * 120 Hz frame by default), relearnt every RELEARN_RUNS runs. Results are identical whatever the
     * slicing.
     */
    _HtdemucsWgsl.prototype.run = function (_0, _1) {
      return __async2(this, arguments, function (left, right, options) {
        var _a,
          _b,
          L,
          device,
          encoder,
          pass,
          readbacks,
          gpuMs,
          slices,
          slice,
          submit,
          budgetMs,
          timings,
          lastLabel,
          stageIndex,
          wallStarted,
          _f,
          _g,
          step,
          perDispatch,
          took,
          coarse,
          took,
          readback,
          e_8_1,
          wallMs,
          stages,
          readbacks_2,
          readbacks_2_1,
          _h,
          name,
          buffer,
          e_9_1,
          out,
          stems;
        var e_8, _j, e_9, _k;
        var _this = this;
        if (options === void 0) {
          options = {};
        }
        return __generator(this, function (_l) {
          switch (_l.label) {
            case 0:
              L = HTDEMUCS_SEGMENT;
              if (left.length !== L || right.length !== L)
                throw new Error(
                  'htdemucs segment must be '
                    .concat(L, ' samples per channel, got ')
                    .concat(left.length, ' and ')
                    .concat(right.length),
                );
              device = this.device;
              device.queue.writeBuffer(
                this.mixBuffer,
                0,
                left.buffer,
                left.byteOffset,
                left.byteLength,
              );
              device.queue.writeBuffer(
                this.mixBuffer,
                L * 4,
                right.buffer,
                right.byteOffset,
                right.byteLength,
              );
              encoder = device.createCommandEncoder();
              pass = null;
              readbacks = [];
              gpuMs = 0;
              slices = 0;
              slice = (_a = options.slice) != null ? _a : 'none';
              submit = function () {
                return __async2(_this, null, function () {
                  var started, took;
                  return __generator(this, function (_f) {
                    switch (_f.label) {
                      case 0:
                        pass == null ? void 0 : pass.end();
                        pass = null;
                        started = performance.now();
                        device.queue.submit([encoder.finish()]);
                        return [4 /*yield*/, device.queue.onSubmittedWorkDone()];
                      case 1:
                        _f.sent();
                        took = performance.now() - started;
                        gpuMs += took;
                        slices += 1;
                        encoder = device.createCommandEncoder();
                        return [2 /*return*/, took];
                    }
                  });
                });
              };
              budgetMs = (_b = options.budgetMs) != null ? _b : 8;
              timings = [];
              lastLabel = '';
              if (slice === 'auto' && ++this.autoRuns % RELEARN_RUNS2 === 0)
                this.fineStages.clear();
              stageIndex = 0;
              wallStarted = performance.now();
              _l.label = 1;
            case 1:
              _l.trys.push([1, 9, 10, 11]);
              ((_f = __values(this.steps)), (_g = _f.next()));
              _l.label = 2;
            case 2:
              if (!!_g.done) return [3 /*break*/, 8];
              step = _g.value;
              perDispatch =
                slice === 'dispatch' || (slice === 'auto' && this.fineStages.has(stageIndex));
              if (!(perDispatch && pass)) return [3 /*break*/, 4];
              return [4 /*yield*/, submit()];
            case 3:
              took = _l.sent();
              if (options.timings) timings.push({ label: lastLabel, ms: took });
              _l.label = 4;
            case 4:
              if (step.kind === 'dispatch') {
                lastLabel = step.label;
                pass != null ? pass : (pass = encoder.beginComputePass());
                pass.setPipeline(step.pipeline);
                pass.setBindGroup(0, step.group);
                pass.dispatchWorkgroups.apply(pass, __spreadArray([], __read(step.count), false));
                return [3 /*break*/, 7];
              }
              if (step.kind === 'copy') {
                pass == null ? void 0 : pass.end();
                pass = null;
                encoder.copyBufferToBuffer(step.from, 0, step.to, 0, step.bytes);
                return [3 /*break*/, 7];
              }
              coarse = slice === 'stage' || (slice === 'auto' && !this.fineStages.has(stageIndex));
              if (!(coarse && step.name !== 'out')) return [3 /*break*/, 6];
              return [4 /*yield*/, submit()];
            case 5:
              took = _l.sent();
              if (slice === 'auto' && took > budgetMs) this.fineStages.add(stageIndex);
              _l.label = 6;
            case 6:
              stageIndex += 1;
              if (!options.stages && step.name !== 'out') return [3 /*break*/, 7];
              pass == null ? void 0 : pass.end();
              pass = null;
              readback = device.createBuffer({
                size: step.floats * 4,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
              });
              encoder.copyBufferToBuffer(step.buffer, 0, readback, 0, step.floats * 4);
              readbacks.push({ name: step.name, buffer: readback });
              _l.label = 7;
            case 7:
              _g = _f.next();
              return [3 /*break*/, 2];
            case 8:
              return [3 /*break*/, 11];
            case 9:
              e_8_1 = _l.sent();
              e_8 = { error: e_8_1 };
              return [3 /*break*/, 11];
            case 10:
              try {
                if (_g && !_g.done && (_j = _f.return)) _j.call(_f);
              } finally {
                if (e_8) throw e_8.error;
              }
              return [7 /*endfinally*/];
            case 11:
              return [4 /*yield*/, submit()];
            case 12:
              _l.sent();
              wallMs = performance.now() - wallStarted;
              stages = {};
              _l.label = 13;
            case 13:
              _l.trys.push([13, 18, 19, 20]);
              ((readbacks_2 = __values(readbacks)), (readbacks_2_1 = readbacks_2.next()));
              _l.label = 14;
            case 14:
              if (!!readbacks_2_1.done) return [3 /*break*/, 17];
              ((_h = readbacks_2_1.value), (name = _h.name), (buffer = _h.buffer));
              return [4 /*yield*/, buffer.mapAsync(GPUMapMode.READ)];
            case 15:
              _l.sent();
              stages[name] = new Float32Array(buffer.getMappedRange().slice(0));
              buffer.destroy();
              _l.label = 16;
            case 16:
              readbacks_2_1 = readbacks_2.next();
              return [3 /*break*/, 14];
            case 17:
              return [3 /*break*/, 20];
            case 18:
              e_9_1 = _l.sent();
              e_9 = { error: e_9_1 };
              return [3 /*break*/, 20];
            case 19:
              try {
                if (readbacks_2_1 && !readbacks_2_1.done && (_k = readbacks_2.return))
                  _k.call(readbacks_2);
              } finally {
                if (e_9) throw e_9.error;
              }
              return [7 /*endfinally*/];
            case 20:
              out = stages.out;
              if (!options.stages) delete stages.out;
              stems = Array.from({ length: out.length / (2 * L) }, function (_, index) {
                return {
                  left: out.subarray(index * 2 * L, index * 2 * L + L),
                  right: out.subarray(index * 2 * L + L, (index + 1) * 2 * L),
                };
              });
              return [
                2 /*return*/,
                __spreadValues(
                  __spreadValues(
                    {
                      left: stems[0].left,
                      right: stems[0].right,
                      stems: stems,
                      gpuMs: gpuMs,
                      wallMs: wallMs,
                      slices: slices,
                    },
                    options.stages ? { stages: stages } : {},
                  ),
                  options.timings ? { timings: timings } : {},
                ),
              ];
          }
        });
      });
    };
    _HtdemucsWgsl.prototype.destroy = function () {
      this.owned.forEach(function (buffer) {
        return buffer.destroy();
      });
    };
    return _HtdemucsWgsl;
  })();
  var MEL_BINS2 = 128;
  var WORKGROUP = 64;
  var SLICE_FRAMES = 1024;
  var PERCUSSIVE_WGSL =
    /* wgsl */
    '\nstruct Params { frames: u32, first: u32, count: u32 }\n@group(0) @binding(0) var<storage, read> mel: array<f32>;\n@group(0) @binding(1) var<storage, read_write> energy: array<f32>;\n@group(0) @binding(2) var<uniform> p: Params;\n\nconst BANDS: i32 = '
      .concat(
        MEL_BINS2,
        ';\nconst HALF: i32 = 8;\nconst TAPS: i32 = 17;\n\nfn magnitude(frame: i32, band: i32) -> f32 {\n  if (frame < 0 || frame >= i32(p.frames) || band < 0 || band >= BANDS) { return 0.0; }\n  let v = mel[u32(frame * BANDS + band)];\n  // expm1 without its cancellation near 0.\n  if (v < 1e-3) { return max(0.0, v + 0.5 * v * v); }\n  return exp(v) - 1.0;\n}\n\nfn median(values: ptr<function, array<f32, 17>>) -> f32 {\n  for (var i = 1; i < TAPS; i++) {\n    let value = (*values)[i];\n    var j = i - 1;\n    while (j >= 0 && (*values)[j] > value) {\n      (*values)[j + 1] = (*values)[j];\n      j--;\n    }\n    (*values)[j + 1] = value;\n  }\n  return (*values)[HALF];\n}\n\n@compute @workgroup_size(',
      )
      .concat(
        WORKGROUP,
        ')\nfn main(@builtin(global_invocation_id) id: vec3<u32>) {\n  if (id.x >= p.count) { return; }\n  let at = p.first + id.x;\n  let frame = i32(at);\n  var percussive = 0.0;\n  var total = 0.0;\n  var taps: array<f32, 17>;\n  for (var band = 0; band < BANDS; band++) {\n    for (var k = 0; k < TAPS; k++) { taps[k] = magnitude(frame + k - HALF, band); }\n    let harmonic = median(&taps);\n    for (var k = 0; k < TAPS; k++) { taps[k] = magnitude(frame, band + k - HALF); }\n    let hit = median(&taps);\n    let value = magnitude(frame, band);\n    let e = value * value;\n    let hh = harmonic * harmonic;\n    let pp = hit * hit;\n    if (hh + pp > 0.0) { percussive += e * pp / (hh + pp); }\n    total += e;\n  }\n  energy[2u * at] = percussive;\n  energy[2u * at + 1u] = total;\n}\n',
      );
  var pipelines = /* @__PURE__ */ new WeakMap();
  var runPercussiveEnergy = function (device, mel, frames) {
    return __async2(null, null, function () {
      var pipeline,
        input,
        output,
        slices,
        params,
        readback,
        slice,
        first,
        count,
        group,
        encoder,
        pass,
        interleaved,
        percussive,
        total,
        frame;
      return __generator(this, function (_f) {
        switch (_f.label) {
          case 0:
            if (frames < 1 || mel.length < frames * MEL_BINS2)
              throw new Error(
                'percussive: '.concat(mel.length, ' floats for ').concat(frames, ' frames'),
              );
            pipeline = pipelines.get(device);
            if (!!pipeline) return [3 /*break*/, 2];
            return [
              4 /*yield*/,
              device.createComputePipelineAsync({
                layout: 'auto',
                compute: {
                  module: device.createShaderModule({ code: PERCUSSIVE_WGSL }),
                  entryPoint: 'main',
                },
              }),
            ];
          case 1:
            pipeline = _f.sent();
            pipelines.set(device, pipeline);
            _f.label = 2;
          case 2:
            input = device.createBuffer({
              size: frames * MEL_BINS2 * 4,
              usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
            });
            output = device.createBuffer({
              size: frames * 2 * 4,
              usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            });
            slices = Math.ceil(frames / SLICE_FRAMES);
            params = device.createBuffer({
              size: slices * 256,
              usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });
            readback = device.createBuffer({
              size: frames * 2 * 4,
              usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
            });
            _f.label = 3;
          case 3:
            _f.trys.push([3, , 9, 10]);
            device.queue.writeBuffer(input, 0, mel.buffer, mel.byteOffset, frames * MEL_BINS2 * 4);
            slice = 0;
            _f.label = 4;
          case 4:
            if (!(slice < slices)) return [3 /*break*/, 7];
            first = slice * SLICE_FRAMES;
            count = Math.min(SLICE_FRAMES, frames - first);
            device.queue.writeBuffer(
              params,
              slice * 256,
              new Uint32Array([frames, first, count, 0]),
            );
            group = device.createBindGroup({
              layout: pipeline.getBindGroupLayout(0),
              entries: [
                { binding: 0, resource: { buffer: input } },
                { binding: 1, resource: { buffer: output } },
                { binding: 2, resource: { buffer: params, offset: slice * 256, size: 16 } },
              ],
            });
            encoder = device.createCommandEncoder();
            pass = encoder.beginComputePass();
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, group);
            pass.dispatchWorkgroups(Math.ceil(count / WORKGROUP));
            pass.end();
            if (slice === slices - 1)
              encoder.copyBufferToBuffer(output, 0, readback, 0, frames * 2 * 4);
            device.queue.submit([encoder.finish()]);
            return [4 /*yield*/, device.queue.onSubmittedWorkDone()];
          case 5:
            _f.sent();
            _f.label = 6;
          case 6:
            slice += 1;
            return [3 /*break*/, 4];
          case 7:
            return [4 /*yield*/, readback.mapAsync(GPUMapMode.READ)];
          case 8:
            _f.sent();
            interleaved = new Float32Array(readback.getMappedRange().slice(0));
            readback.unmap();
            percussive = new Float32Array(frames);
            total = new Float32Array(frames);
            for (frame = 0; frame < frames; frame += 1) {
              percussive[frame] = interleaved[2 * frame];
              total[frame] = interleaved[2 * frame + 1];
            }
            return [2 /*return*/, { percussive: percussive, total: total }];
          case 9:
            input.destroy();
            output.destroy();
            params.destroy();
            readback.destroy();
            return [7 /*endfinally*/];
          case 10:
            return [2 /*return*/];
        }
      });
    });
  };
  // tools/audio-analysis-helper/graph-recorder.mjs
  var alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  function base64(bytes) {
    var value = '';
    for (var i = 0; i < bytes.length; i += 3) {
      var a = bytes[i],
        b = bytes[i + 1],
        c = bytes[i + 2];
      value +=
        alphabet[a >> 2] +
        alphabet[((a & 3) << 4) | ((b || 0) >> 4)] +
        (i + 1 < bytes.length ? alphabet[((b & 15) << 2) | ((c || 0) >> 6)] : '=') +
        (i + 2 < bytes.length ? alphabet[c & 63] : '=');
    }
    return value;
  }
  function recordModelGraph(model, input, frames) {
    if (frames === void 0) {
      frames = 1500;
    }
    return __async(this, null, function () {
      var graph,
        tags,
        RecordedBuffer,
        manifest,
        modelBytes,
        weights,
        device,
        mel,
        runtime,
        mel,
        runtime,
        left,
        right;
      return __generator(this, function (_f) {
        switch (_f.label) {
          case 0:
            if (model !== 'beat_this' && model !== 'htdemucs' && model !== 'percussive')
              throw Error('unknown model');
            if (
              !Number.isInteger(frames) ||
              frames < 1 ||
              frames > (model === 'percussive' ? 6e4 : 1500)
            )
              throw Error('invalid frames');
            globalThis.GPUBufferUsage = {
              MAP_READ: 1,
              COPY_SRC: 4,
              COPY_DST: 8,
              UNIFORM: 64,
              STORAGE: 128,
            };
            globalThis.GPUMapMode = { READ: 1 };
            if (!globalThis.performance)
              globalThis.performance = {
                now: function () {
                  return Date.now();
                },
              };
            graph = {
              model: model,
              frames: frames,
              buffers: [],
              pipelines: [],
              groups: [],
              batches: [],
              writes: [],
              outputs: [],
              totalBytes: 0,
            };
            tags = new Map();
            RecordedBuffer = /** @class */ (function () {
              function RecordedBuffer(value) {
                Object.assign(this, value);
              }
              return RecordedBuffer;
            })();
            globalThis.GPUBuffer = RecordedBuffer;
            manifest = input.manifest;
            modelBytes = {
              slice: function (start, end) {
                var entry = manifest.tensors['rope.freqs'];
                if (
                  !entry ||
                  start !== entry.byte_offset ||
                  end - start !== 64 ||
                  input.rope.length !== 16
                )
                  throw Error('unexpected source weight read');
                return new Float32Array(input.rope).buffer;
              },
            };
            tags.set(modelBytes, { modelOffset: 0 });
            weights = { byteLength: manifest.bytes, byteOffset: 0, buffer: modelBytes };
            device = {
              features: /* @__PURE__ */ new Set(),
              pushErrorScope: function () {},
              popErrorScope: function () {
                return __async(this, null, function () {
                  return __generator(this, function (_f) {
                    return [2 /*return*/, null];
                  });
                });
              },
              createBuffer: function (_f) {
                var size = _f.size,
                  usage = _f.usage;
                if (!Number.isSafeInteger(size) || size < 4 || size > 256 * 1024 * 1024)
                  throw Error('buffer budget');
                var id = graph.buffers.length;
                graph.buffers.push({ size: size, usage: usage });
                graph.totalBytes += size;
                if (graph.totalBytes > 1024 * 1024 * 1024) throw Error('graph budget');
                return new RecordedBuffer({
                  id: id,
                  size: size,
                  mapAsync: function () {
                    return __async(this, null, function () {
                      return __generator(this, function (_f) {
                        return [2 /*return*/];
                      });
                    });
                  },
                  getMappedRange: function () {
                    graph.outputs.push({ buffer: id, size: size });
                    return new ArrayBuffer(size);
                  },
                  unmap: function () {},
                  destroy: function () {},
                });
              },
              createShaderModule: function (_f) {
                var code = _f.code;
                return code;
              },
              createComputePipeline: function (_f) {
                var compute = _f.compute;
                var id = graph.pipelines.length;
                graph.pipelines.push({ code: compute.module, entryPoint: compute.entryPoint });
                return {
                  id: id,
                  getBindGroupLayout: function (index) {
                    return { pipeline: id, index: index };
                  },
                };
              },
              createComputePipelineAsync: function (value) {
                return __async(this, null, function () {
                  return __generator(this, function (_f) {
                    return [2 /*return*/, this.createComputePipeline(value)];
                  });
                });
              },
              createBindGroup: function (_f) {
                var layout = _f.layout,
                  entries = _f.entries;
                var id = graph.groups.length;
                graph.groups.push({
                  pipeline: layout.pipeline,
                  index: layout.index,
                  entries: entries.map(function (_f) {
                    var binding = _f.binding,
                      resource = _f.resource;
                    return {
                      binding: binding,
                      buffer: resource.buffer.id,
                      offset: resource.offset || 0,
                      size: resource.size || resource.buffer.size - (resource.offset || 0),
                    };
                  }),
                });
                return { id: id };
              },
              createCommandEncoder: function () {
                var commands = [];
                return {
                  beginComputePass: function () {
                    var pipeline = null;
                    var groups = [];
                    return {
                      setPipeline: function (value) {
                        pipeline = value.id;
                      },
                      setBindGroup: function (index, value) {
                        groups[index] = value.id;
                      },
                      dispatchWorkgroups: function (x, y, z) {
                        if (y === void 0) {
                          y = 1;
                        }
                        if (z === void 0) {
                          z = 1;
                        }
                        commands.push({
                          kind: 'dispatch',
                          pipeline: pipeline,
                          groups: groups.slice(),
                          count: [x, y, z],
                        });
                      },
                      end: function () {},
                    };
                  },
                  copyBufferToBuffer: function (from, fromOffset, to, toOffset, size) {
                    commands.push({
                      kind: 'copy',
                      from: from.id,
                      fromOffset: fromOffset,
                      to: to.id,
                      toOffset: toOffset,
                      size: size,
                    });
                  },
                  finish: function () {
                    return commands;
                  },
                };
              },
              queue: {
                writeBuffer: function (buffer, offset, data, dataOffset, size) {
                  if (dataOffset === void 0) {
                    dataOffset = 0;
                  }
                  var isView = ArrayBuffer.isView(data);
                  var source = isView ? data.buffer : data;
                  var byteOffset = (isView ? data.byteOffset : 0) + dataOffset;
                  var length = size === void 0 ? data.byteLength - dataOffset : size;
                  var tag = tags.get(source);
                  var value = { buffer: buffer.id, offset: offset, size: length };
                  if (tag && tag.modelOffset !== void 0)
                    value.modelOffset = tag.modelOffset + byteOffset;
                  else if (tag) {
                    value.input = tag.input;
                    value.inputOffset = byteOffset;
                  } else value.data = base64(new Uint8Array(source, byteOffset, length));
                  graph.writes.push(value);
                },
                submit: function (commands) {
                  var _f;
                  (_f = graph.batches).push.apply(_f, __spreadArray([], __read(commands), false));
                },
                onSubmittedWorkDone: function () {
                  return __async(this, null, function () {
                    return __generator(this, function (_f) {
                      return [2 /*return*/];
                    });
                  });
                },
              },
            };
            if (!(model === 'percussive')) return [3 /*break*/, 2];
            mel = new Float32Array(frames * 128);
            tags.set(mel.buffer, { input: 'mel' });
            return [4 /*yield*/, runPercussiveEnergy(device, mel, frames)];
          case 1:
            _f.sent();
            return [3 /*break*/, 8];
          case 2:
            if (!(model === 'beat_this')) return [3 /*break*/, 5];
            return [4 /*yield*/, BeatThisWgsl.create(device, weights, manifest)];
          case 3:
            runtime = _f.sent();
            mel = new Float32Array(frames * 128);
            tags.set(mel.buffer, { input: 'mel' });
            return [4 /*yield*/, runtime.run(mel, { slice: 'stage' })];
          case 4:
            _f.sent();
            return [3 /*break*/, 8];
          case 5:
            return [
              4 /*yield*/,
              HtdemucsWgsl.create(device, weights, manifest, { sources: [0, 1, 2, 3] }),
            ];
          case 6:
            runtime = _f.sent();
            ((left = new Float32Array(172032)), (right = new Float32Array(172032)));
            tags.set(left.buffer, { input: 'left' });
            tags.set(right.buffer, { input: 'right' });
            return [4 /*yield*/, runtime.run(left, right, { slice: 'stage' })];
          case 7:
            _f.sent();
            _f.label = 8;
          case 8:
            return [2 /*return*/, graph];
        }
      });
    });
  }
  return __toCommonJS(graph_recorder_exports);
})();
