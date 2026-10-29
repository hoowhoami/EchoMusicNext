// Independent helper component; AGPL-3.0-only, see LICENSE and README.md.
import { BeatThisWgsl, HtdemucsWgsl, runPercussiveEnergy } from './generated/source-models.mjs';

const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function base64(bytes) {
  let value = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i],
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

export async function recordModelGraph(model, input, frames = 1500) {
  if (model !== 'beat_this' && model !== 'htdemucs' && model !== 'percussive')
    throw Error('unknown model');
  if (!Number.isInteger(frames) || frames < 1 || frames > (model === 'percussive' ? 60000 : 1500))
    throw Error('invalid frames');
  globalThis.GPUBufferUsage = { MAP_READ: 1, COPY_SRC: 4, COPY_DST: 8, UNIFORM: 64, STORAGE: 128 };
  globalThis.GPUMapMode = { READ: 1 };
  if (!globalThis.performance) globalThis.performance = { now: () => Date.now() };
  const graph = {
    model,
    frames,
    buffers: [],
    pipelines: [],
    groups: [],
    batches: [],
    writes: [],
    outputs: [],
    totalBytes: 0,
  };
  const tags = new Map();
  class RecordedBuffer {
    constructor(value) {
      Object.assign(this, value);
    }
  }
  globalThis.GPUBuffer = RecordedBuffer;
  const manifest = input.manifest;
  // create() reads only rope.freqs; all other weight access is an immutable model-file offset.
  // This descriptor avoids allocating a fake 41/92MB byte array just to construct a graph.
  const modelBytes = {
    slice(start, end) {
      const entry = manifest.tensors['rope.freqs'];
      if (!entry || start !== entry.byte_offset || end - start !== 64 || input.rope.length !== 16)
        throw Error('unexpected source weight read');
      return new Float32Array(input.rope).buffer;
    },
  };
  tags.set(modelBytes, { modelOffset: 0 });
  const weights = { byteLength: manifest.bytes, byteOffset: 0, buffer: modelBytes };
  const device = {
    features: new Set(),
    pushErrorScope() {},
    async popErrorScope() {
      return null;
    },
    createBuffer({ size, usage }) {
      if (!Number.isSafeInteger(size) || size < 4 || size > 256 * 1024 * 1024)
        throw Error('buffer budget');
      const id = graph.buffers.length;
      graph.buffers.push({ size, usage });
      graph.totalBytes += size;
      if (graph.totalBytes > 1024 * 1024 * 1024) throw Error('graph budget');
      return new RecordedBuffer({
        id,
        size,
        async mapAsync() {},
        getMappedRange() {
          graph.outputs.push({ buffer: id, size });
          return new ArrayBuffer(size);
        },
        unmap() {},
        destroy() {},
      });
    },
    createShaderModule({ code }) {
      return code;
    },
    createComputePipeline({ compute }) {
      const id = graph.pipelines.length;
      graph.pipelines.push({ code: compute.module, entryPoint: compute.entryPoint });
      return {
        id,
        getBindGroupLayout(index) {
          return { pipeline: id, index };
        },
      };
    },
    async createComputePipelineAsync(value) {
      return this.createComputePipeline(value);
    },
    createBindGroup({ layout, entries }) {
      const id = graph.groups.length;
      graph.groups.push({
        pipeline: layout.pipeline,
        index: layout.index,
        entries: entries.map(({ binding, resource }) => ({
          binding,
          buffer: resource.buffer.id,
          offset: resource.offset || 0,
          size: resource.size || resource.buffer.size - (resource.offset || 0),
        })),
      });
      return { id };
    },
    createCommandEncoder() {
      const commands = [];
      return {
        beginComputePass() {
          let pipeline = null;
          const groups = [];
          return {
            setPipeline(value) {
              pipeline = value.id;
            },
            setBindGroup(index, value) {
              groups[index] = value.id;
            },
            dispatchWorkgroups(x, y = 1, z = 1) {
              commands.push({
                kind: 'dispatch',
                pipeline,
                groups: groups.slice(),
                count: [x, y, z],
              });
            },
            end() {},
          };
        },
        copyBufferToBuffer(from, fromOffset, to, toOffset, size) {
          commands.push({ kind: 'copy', from: from.id, fromOffset, to: to.id, toOffset, size });
        },
        finish() {
          return commands;
        },
      };
    },
    queue: {
      writeBuffer(buffer, offset, data, dataOffset = 0, size) {
        const isView = ArrayBuffer.isView(data);
        const source = isView ? data.buffer : data;
        const byteOffset = (isView ? data.byteOffset : 0) + dataOffset;
        const length = size === undefined ? data.byteLength - dataOffset : size;
        const tag = tags.get(source);
        const value = { buffer: buffer.id, offset, size: length };
        if (tag && tag.modelOffset !== undefined) value.modelOffset = tag.modelOffset + byteOffset;
        else if (tag) {
          value.input = tag.input;
          value.inputOffset = byteOffset;
        } else value.data = base64(new Uint8Array(source, byteOffset, length));
        graph.writes.push(value);
      },
      submit(commands) {
        graph.batches.push(...commands);
      },
      async onSubmittedWorkDone() {},
    },
  };
  if (model === 'percussive') {
    const mel = new Float32Array(frames * 128);
    tags.set(mel.buffer, { input: 'mel' });
    await runPercussiveEnergy(device, mel, frames);
  } else if (model === 'beat_this') {
    const runtime = await BeatThisWgsl.create(device, weights, manifest);
    const mel = new Float32Array(frames * 128);
    tags.set(mel.buffer, { input: 'mel' });
    await runtime.run(mel, { slice: 'stage' });
  } else {
    const runtime = await HtdemucsWgsl.create(device, weights, manifest, { sources: [0, 1, 2, 3] });
    const left = new Float32Array(172032),
      right = new Float32Array(172032);
    tags.set(left.buffer, { input: 'left' });
    tags.set(right.buffer, { input: 'right' });
    await runtime.run(left, right, { slice: 'stage' });
  }
  return graph;
}
