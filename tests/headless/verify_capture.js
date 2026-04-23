#!/usr/bin/env node
"use strict";

const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");
const zlib = require("zlib");
const { spawnSync } = require("child_process");

function parseArgs(argv) {
  const opts = {
    runner: path.join(__dirname, "gpsp_headless.js"),
    seconds: 3,
    screenshot: path.join(__dirname, "capture.png"),
    backend: "interp",
  };

  for (let i = 2; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--runner" && i + 1 < argv.length) {
      opts.runner = argv[++i];
    } else if (arg === "--rom" && i + 1 < argv.length) {
      opts.rom = argv[++i];
    } else if (arg === "--seconds" && i + 1 < argv.length) {
      opts.seconds = Number(argv[++i]);
    } else if (arg === "--frames" && i + 1 < argv.length) {
      opts.frames = Number(argv[++i]);
    } else if (arg === "--screenshot" && i + 1 < argv.length) {
      opts.screenshot = argv[++i];
    } else if (arg === "--backend" && i + 1 < argv.length) {
      opts.backend = argv[++i];
    } else {
      throw new Error(
        "usage: verify_capture.js --rom path [--runner path] [--seconds n] [--frames n] [--screenshot out.png] [--backend interp|jsjit]"
      );
    }
  }

  if (!opts.rom) {
    throw new Error("--rom is required");
  }

  opts.runner = path.resolve(opts.runner);
  opts.rom = path.resolve(opts.rom);
  opts.screenshot = path.resolve(opts.screenshot);

  return opts;
}

function runCapture(runner, rom, runDir, ppmPath, seconds, frames, backend) {
  const args = [runner, "--rom", rom, "--base-dir", runDir, "--screenshot", ppmPath, "--backend", backend];
  if (Number.isFinite(frames)) {
    args.push("--frames", String(frames));
  } else {
    args.push("--seconds", String(seconds));
  }

  const result = spawnSync("node", args, {
    cwd: __dirname,
    encoding: "utf8",
  });

  if (result.status !== 0) {
    throw new Error(
      `headless run failed (${result.status})\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`
    );
  }

  return result.stdout.trim();
}

function parseBackendSummary(stdout) {
  const lines = stdout.split(/\r?\n/);
  const line = lines.find((entry) => entry.startsWith("backend="));
  if (!line) {
    throw new Error(`missing backend summary in output:\n${stdout}`);
  }

  const fields = Object.create(null);
  for (const token of line.trim().split(/\s+/)) {
    const eq = token.indexOf("=");
    if (eq <= 0) {
      continue;
    }
    fields[token.slice(0, eq)] = token.slice(eq + 1);
  }

  return {
    backend: fields.backend || "",
    jsjitExec: Number(fields.jsjit_exec || 0),
    jsjitFallbackExec: Number(fields.jsjit_fallback_exec || 0),
    jsjitFallbackCycles: Number(fields.jsjit_fallback_cycles || 0),
  };
}

function parsePpm(filePath) {
  const data = fs.readFileSync(filePath);
  let offset = 0;

  function nextToken() {
    while (offset < data.length) {
      const ch = data[offset];
      if (ch === 35) {
        while (offset < data.length && data[offset] !== 10) {
          offset++;
        }
      } else if (ch === 9 || ch === 10 || ch === 13 || ch === 32) {
        offset++;
      } else {
        break;
      }
    }

    const start = offset;
    while (offset < data.length) {
      const ch = data[offset];
      if (ch === 9 || ch === 10 || ch === 13 || ch === 32) {
        break;
      }
      offset++;
    }

    return data.subarray(start, offset).toString("ascii");
  }

  const magic = nextToken();
  const width = Number(nextToken());
  const height = Number(nextToken());
  const maxval = Number(nextToken());

  while (offset < data.length) {
    const ch = data[offset];
    if (ch === 9 || ch === 10 || ch === 13 || ch === 32) {
      offset++;
    } else {
      break;
    }
  }

  if (magic !== "P6") {
    throw new Error(`unsupported ppm magic ${magic}`);
  }
  if (maxval !== 255) {
    throw new Error(`unsupported ppm maxval ${maxval}`);
  }

  return {
    width,
    height,
    rgb: data.subarray(offset),
  };
}

function crc32Table() {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let j = 0; j < 8; j++) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c >>> 0;
  }
  return table;
}

const CRC_TABLE = crc32Table();

function crc32(buffer) {
  let crc = 0xffffffff;
  for (let i = 0; i < buffer.length; i++) {
    crc = CRC_TABLE[(crc ^ buffer[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const typeBuf = Buffer.from(type, "ascii");
  const crcBuf = Buffer.alloc(4);
  crcBuf.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
  return Buffer.concat([len, typeBuf, data, crcBuf]);
}

function rgbToPng(rgb, width, height) {
  const stride = width * 3;
  const raw = Buffer.alloc(height * (stride + 1));

  for (let y = 0; y < height; y++) {
    const dstRow = y * (stride + 1);
    const srcRow = y * stride;
    raw[dstRow] = 0;
    rgb.copy(raw, dstRow + 1, srcRow, srcRow + stride);
  }

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    pngChunk("IHDR", ihdr),
    pngChunk("IDAT", zlib.deflateSync(raw)),
    pngChunk("IEND", Buffer.alloc(0)),
  ]);
}

function analyzeRgb(rgb) {
  let nonBlack = 0;
  const samples = new Set();

  for (let i = 0; i < rgb.length; i += 3) {
    const r = rgb[i];
    const g = rgb[i + 1];
    const b = rgb[i + 2];
    if ((r | g | b) !== 0) {
      nonBlack++;
    }
  }

  const step = Math.max(1, Math.floor((rgb.length / 3) / 2048));
  for (let pixel = 0; pixel < rgb.length / 3; pixel += step) {
    const i = pixel * 3;
    samples.add((rgb[i] << 16) | (rgb[i + 1] << 8) | rgb[i + 2]);
  }

  return {
    nonBlack,
    sampledUniqueColors: samples.size,
    sha256: crypto.createHash("sha256").update(rgb).digest("hex"),
  };
}

function ensureDir(filePath) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
}

function main() {
  const opts = parseArgs(process.argv);
  const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), "gpsp-headless-"));
  const run1Dir = path.join(tmpRoot, "run1");
  const run2Dir = path.join(tmpRoot, "run2");
  const ppm1 = path.join(run1Dir, "capture.ppm");
  const ppm2 = path.join(run2Dir, "capture.ppm");

  fs.mkdirSync(run1Dir, { recursive: true });
  fs.mkdirSync(run2Dir, { recursive: true });

  const stdout1 = runCapture(opts.runner, opts.rom, run1Dir, ppm1, opts.seconds, opts.frames, opts.backend);
  const stdout2 = runCapture(opts.runner, opts.rom, run2Dir, ppm2, opts.seconds, opts.frames, opts.backend);
  const backend1 = parseBackendSummary(stdout1);
  const backend2 = parseBackendSummary(stdout2);

  const shot1 = parsePpm(ppm1);
  const shot2 = parsePpm(ppm2);

  if (shot1.width !== 240 || shot1.height !== 160) {
    throw new Error(`unexpected screenshot size ${shot1.width}x${shot1.height}`);
  }
  if (shot1.width !== shot2.width || shot1.height !== shot2.height) {
    throw new Error("capture dimensions differ between runs");
  }

  const info1 = analyzeRgb(shot1.rgb);
  const info2 = analyzeRgb(shot2.rgb);
  const stable = Buffer.compare(shot1.rgb, shot2.rgb) === 0;

  if (info1.nonBlack < 1000) {
    throw new Error(`screenshot looks blank: nonBlack=${info1.nonBlack}`);
  }
  if (info1.sampledUniqueColors < 8) {
    throw new Error(`screenshot has too little color variation: unique=${info1.sampledUniqueColors}`);
  }
  if (!stable) {
    throw new Error("repeated headless captures are not deterministic");
  }

  if (backend1.backend !== opts.backend || backend2.backend !== opts.backend) {
    throw new Error(
      `backend mismatch: requested=${opts.backend} run1=${backend1.backend} run2=${backend2.backend}`
    );
  }
  if (opts.backend === "jsjit") {
    if (backend1.jsjitExec <= 0 || backend2.jsjitExec <= 0) {
      throw new Error(
        `jsjit backend did not execute: run1=${backend1.jsjitExec} run2=${backend2.jsjitExec}`
      );
    }
  } else {
    if (backend1.jsjitExec !== 0 || backend2.jsjitExec !== 0) {
      throw new Error(
        `interpreter run unexpectedly touched jsjit: run1=${backend1.jsjitExec} run2=${backend2.jsjitExec}`
      );
    }
  }

  ensureDir(opts.screenshot);
  fs.writeFileSync(opts.screenshot, rgbToPng(shot1.rgb, shot1.width, shot1.height));

  console.log(stdout1);
  console.log(stdout2);
  console.log(
    `verified width=${shot1.width} height=${shot1.height} non_black=${info1.nonBlack} ` +
      `sampled_unique=${info1.sampledUniqueColors} sha256=${info1.sha256} screenshot=${opts.screenshot}`
  );
}

main();
