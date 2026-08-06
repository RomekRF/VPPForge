/* ============================================================================
 * vfx.js - Red Faction VFX ("Visual Effect", magic 'VSFX') parser + Three.js
 *          mesh builder, with animation playback.  Read-only; for viewers.
 *
 * Companion to the embedded v3m.js engine.  Mirrors the binary layout in
 * rafalh/rf-reversed (vfx.ksy).  Validated byte-exact against stock RF1 files
 * (version 0x40006): respawn, NanoAttackHit, NanoShieldConstant,
 * Cutscene09_fx, RMKPalm5a.
 *
 * Renders the effect's MESHES and plays their animation:
 *   - morph          : per-frame vertex positions (flares, palm-tree sway)
 *   - per-frame xform : each frame stores translation/rotation/scale (debris)
 *   - keyframed xform : sampled from translation/rotation/scale keyframes
 *   - per-frame UVs   : scrolling/animated texture coords (shield shimmer)
 * Each mesh plays on a seconds timeline using its own fps and start/end time,
 * so staggered multi-mesh effects (cutscenes) play in sync.  Particle systems,
 * lights, cameras, dummies, spacewarps, chains are parsed past but not drawn.
 *
 * API (matches the V3M engine where it overlaps):
 *   VFX.parse(arrayBuffer)                       -> model object
 *   VFX.buildThreeGroup(model, THREE, options)   -> THREE.Group
 *   VFX.applyGroupTime(group, tSeconds, THREE)   -> update meshes to time t
 *
 * model.submeshes[].lods[].chunks[] carry numVerts/numTris so computeModelStats
 * and the existing viewer treat it like a V3M/V3C model.  model.anim =
 * { hasAnim, start, end, fps } describes the global timeline.
 *
 * Coordinate convention: geometry and per-mesh transforms are kept in native RF
 * space; the returned group carries scale.x = -1, which reflects the whole
 * scene to Three's handedness exactly like the V3M engine's negate-X (both
 * Y-up).  Materials are double-sided, so the reflected winding still renders.
 * UVs use (u, 1 - v).  Vertices are quantized: p = center + int16 * multiplier.
 * ========================================================================== */

(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.VFX = factory();
}(typeof self !== "undefined" ? self : this, function () {
    "use strict";

    const MAGIC_VSFX = 0x58465356; // 'VSFX'

    const SEC_MESH     = 0x4F584653;
    const SEC_MATERIAL = 0x4C54414D;

    const MAT_IMAGE = 0, MAT_VMIX = 1, MAT_COLOR = 2;

    const F_FACING     = 0x0001;
    const F_MORPH      = 0x0004;
    const F_FULLBRIGHT = 0x0010;
    const F_DUMP_UVS   = 0x0100;
    const F_FACING_ROD = 0x0800;

    /* ------------------------------------------------------------------ */
    function Reader(buf, pos, end) {
        this.dv = new DataView(buf);
        this.u8 = new Uint8Array(buf);
        this.pos = pos || 0;
        this.end = (end === undefined) ? buf.byteLength : end;
        this.len = buf.byteLength;
    }
    Reader.prototype = {
        i32() { const v = this.dv.getInt32(this.pos, true);  this.pos += 4; return v; },
        u32() { const v = this.dv.getUint32(this.pos, true); this.pos += 4; return v; },
        i16() { const v = this.dv.getInt16(this.pos, true);  this.pos += 2; return v; },
        f32() { const v = this.dv.getFloat32(this.pos, true); this.pos += 4; return v; },
        byte() { return this.u8[this.pos++]; },
        vec3() { return [this.f32(), this.f32(), this.f32()]; },
        quat() { return [this.f32(), this.f32(), this.f32(), this.f32()]; },
        strz() {
            let e = this.pos; const stop = this.end;
            while (e < stop && this.u8[e] !== 0) e++;
            let s = ""; for (let i = this.pos; i < e; i++) s += String.fromCharCode(this.u8[i]);
            this.pos = e + 1; return s;
        }
    };

    /* ------------------------------------------------------------------ */
    function parseHeader(r) {
        const magic = r.u32();
        if (magic !== MAGIC_VSFX)
            throw new Error("Not a VFX file (magic=0x" + (magic >>> 0).toString(16) + ")");
        const version = r.i32();
        if (version < 0x30000 || version > 0x4FFFF)
            throw new Error("Unsupported VFX version 0x" + (version >>> 0).toString(16));
        if (version >= 0x30008) r.i32();
        r.i32();
        const numMeshes = r.i32();
        r.i32(); r.i32(); r.i32(); r.i32(); r.i32();
        if (version >= 0x3000F) r.i32();
        if (version >= 0x40000) r.i32();
        if (version >= 0x40002) r.i32();
        if (version >= 0x40003) r.i32();
        if (version >= 0x40005) r.i32();
        if (version <  0x3000A) r.i32();
        r.i32(); r.i32(); r.i32(); r.i32(); r.i32();
        if (version >= 0x3000D) r.i32();
        if (version >= 0x30009) { r.i32(); r.i32(); r.i32(); r.i32(); r.i32(); }
        r.i32(); r.i32(); r.i32(); r.i32(); r.i32();
        if (version >= 0x3000F) r.i32();
        return { version, numMeshes };
    }

    /* ------------------------------------------------------------------ */
    function parseTexture(r, version) {
        const t = { name: r.strz() };
        if (version >= 0x30012) { r.i32(); r.f32(); r.i32(); }
        return t;
    }
    function parseMaterial(r, version) {
        const type = r.i32();
        const m = { type, textureName: null, additive: false, color: null };
        if (version >= 0x40003) r.i32();
        if (type === MAT_IMAGE || type === MAT_VMIX || version >= 0x40006) m.additive = r.byte() !== 0;
        if (type === MAT_IMAGE || type === MAT_VMIX) m.textureName = parseTexture(r, version).name;
        if (type === MAT_VMIX) parseTexture(r, version);
        if (type === MAT_VMIX) { const n = r.i32(); if (version < 0x40003) r.i32(); for (let i = 0; i < n; i++) r.f32(); }
        if (type === MAT_IMAGE || type === MAT_VMIX) { r.f32(); r.f32(); r.f32(); r.strz(); }
        if (type === MAT_COLOR) m.color = [r.i32(), r.i32(), r.i32()];
        if (version >= 0x40003) { const n = r.i32(); for (let i = 0; i < n; i++) r.f32(); } else r.f32();
        if (version >= 0x40005) { const n = r.i32(); for (let i = 0; i < n; i++) r.f32(); }
        return m;
    }
    function parseMeshMaterialOld(r, version, numFrames) {
        const type = r.i32();
        const m = { type, textureName: null, additive: false, color: null };
        if (version >= 0x30003 && (type === MAT_IMAGE || type === MAT_VMIX)) m.additive = r.byte() !== 0;
        if (type === MAT_IMAGE || type === MAT_VMIX) m.textureName = parseTexture(r, version).name;
        if (type === MAT_VMIX) parseTexture(r, version);
        if ((type === MAT_IMAGE || type === MAT_VMIX) && version < 0x30012) { r.i32(); r.i32(); }
        if ((type === MAT_IMAGE || type === MAT_VMIX) && version >= 0x30007) { r.f32(); r.f32(); r.f32(); }
        if (type === MAT_IMAGE || type === MAT_VMIX) r.strz();
        if (type === MAT_VMIX) for (let i = 0; i < numFrames; i++) r.f32();
        if (type === MAT_COLOR) m.color = [r.i32(), r.i32(), r.i32()];
        if (version >= 0x30011) r.f32();
        return m;
    }

    /* ------------------------------------------------------------------ */
    function parseMeshImpl(r, version) {
        const name = r.strz();
        const parent = r.strz();
        r.byte();

        const numVertices = r.i32();
        if (version < 0x3000A) for (let i = 0; i < numVertices; i++) r.vec3();

        const numFaces = r.i32();
        const faceIdx = new Int32Array(numFaces * 3);
        const faceMat = new Int32Array(numFaces);
        let oldFaceUv = null;
        if (version < 0x3000D) oldFaceUv = new Float32Array(numFaces * 6);
        for (let i = 0; i < numFaces; i++) {
            faceIdx[i * 3] = r.i32(); faceIdx[i * 3 + 1] = r.i32(); faceIdx[i * 3 + 2] = r.i32();
            if (version < 0x3000D) for (let k = 0; k < 6; k++) oldFaceUv[i * 6 + k] = r.f32();
            r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); // colors
            r.vec3(); r.vec3(); r.f32();                    // normal, center, radius
            faceMat[i] = r.i32();
            r.i32();                                        // smoothing_group
            r.i32(); r.i32(); r.i32();                      // face_vertex_indices
        }

        let fps = 15;
        if (version >= 0x30009) fps = r.i32();              // frames_per_second

        let startTime = 0, endTime = 0, numFrames;
        if (version >= 0x40004) { startTime = r.f32(); endTime = r.f32(); numFrames = r.i32(); }
        else { const sf = r.i32(), ef = r.i32(); numFrames = (version >= 0x3000C) ? (ef - sf + 1) : (ef - sf); }
        if (numFrames < 1) numFrames = 1;

        const numMaterials = r.i32();
        let materialIndices = null, inlineMaterials = null;
        if (version >= 0x40000) {
            materialIndices = new Array(numMaterials);
            for (let i = 0; i < numMaterials; i++) materialIndices[i] = r.i32();
        } else {
            inlineMaterials = new Array(numMaterials);
            for (let i = 0; i < numMaterials; i++) inlineMaterials[i] = parseMeshMaterialOld(r, version, numFrames);
        }

        r.vec3(); r.f32();                                  // bounding center/radius
        if (version < 0x30002) r.i32();
        const flags = r.u32();
        const facing = (flags & F_FACING) !== 0;
        const morph = (flags & F_MORPH) !== 0;
        const dumpUvs = (flags & F_DUMP_UVS) !== 0;
        const facingRod = (flags & F_FACING_ROD) !== 0;
        const fullbright = (flags & F_FULLBRIGHT) !== 0;
        if (facing && version === 0x3000A) { r.f32(); r.f32(); }

        const numFaceVerts = r.i32();
        for (let i = 0; i < numFaceVerts; i++) {
            r.i32(); r.i32(); r.f32(); r.f32();
            const na = r.i32();
            for (let j = 0; j < na; j++) r.i32();
        }

        let isKeyframed = 0;
        if (version >= 0x30009) isKeyframed = r.byte();

        // Per-frame data
        const framePos = [];   // {c:[3], m:[3], i16:Int16Array} for frames that store positions
        const frameUv = [];    // Float32Array(6*numFaces) for frames that store uvs
        const xformFrames = []; // {t:[3], q:[4], s:[3]} for frames that store an object transform
        let anyPos = false, anyUv = false, anyXform = false;

        for (let fi = 0; fi < numFrames; fi++) {
            const first = (fi === 0);
            if (morph || first) {
                const c = r.vec3(), m = r.vec3();
                const p = new Int16Array(numVertices * 3);
                for (let k = 0; k < numVertices * 3; k++) p[k] = r.i16();
                framePos[fi] = { c, m, i16: p }; anyPos = true;
                if ((facing || facingRod) && version >= 0x3000B) { r.f32(); r.f32(); }
                if (facingRod && first && version >= 0x40001) r.vec3();
            }
            if ((dumpUvs || first) && version >= 0x3000D) {
                const uv = new Float32Array(numFaces * 6);
                for (let k = 0; k < numFaces * 3; k++) { uv[k * 2] = r.f32(); uv[k * 2 + 1] = r.f32(); }
                frameUv[fi] = uv; anyUv = true;
            }
            const cond = (!morph) && ((!isKeyframed) || (version < 0x3000E && first));
            if (cond) { xformFrames[fi] = { t: r.vec3(), q: r.quat(), s: r.vec3() }; anyXform = true; }
            if (version < 0x30009) r.byte();
            if (version < 0x40005) r.f32();
        }

        // Keyframed transform tail
        let pivot = null, kfT = null, kfR = null, kfS = null;
        if (isKeyframed && version >= 0x3000A) pivot = { t: r.vec3(), q: r.quat(), s: r.vec3() };
        if (isKeyframed) {
            let n = r.i32(); kfT = [];
            for (let i = 0; i < n; i++) { const time = r.i32(); const val = r.vec3(); r.vec3(); r.vec3(); kfT.push({ time, val }); }
            n = r.i32(); kfR = [];
            for (let i = 0; i < n; i++) { const time = r.i32(); const q = r.quat(); r.f32(); r.f32(); r.f32(); r.f32(); r.f32(); kfR.push({ time, q }); }
            n = r.i32(); kfS = [];
            for (let i = 0; i < n; i++) { const time = r.i32(); const val = r.vec3(); r.vec3(); r.vec3(); kfS.push({ time, val }); }
        }

        return {
            name, parent, flags, facing, morph, fullbright, dumpUvs, isKeyframed,
            numVertices, numFaces, faceIdx, faceMat, oldFaceUv,
            fps, numFrames, startTime, endTime,
            framePos, frameUv, xformFrames, anyPos, anyUv, anyXform,
            pivot, kfT, kfR, kfS,
            materialIndices, inlineMaterials
        };
    }

    /* ------------------------------------------------------------------ */
    /*  Keyframe sampling (linear; TCB tangents not applied)              */
    /* ------------------------------------------------------------------ */
    function sampleVec3(keys, frac) {
        if (!keys || keys.length === 0) return [0, 0, 0];
        if (keys.length === 1) return keys[0].val.slice();
        const t0 = keys[0].time, t1 = keys[keys.length - 1].time;
        const target = t0 + frac * (t1 - t0);
        for (let i = 0; i < keys.length - 1; i++) {
            const a = keys[i], b = keys[i + 1];
            if (target <= b.time || i === keys.length - 2) {
                const span = (b.time - a.time) || 1;
                const u = Math.max(0, Math.min(1, (target - a.time) / span));
                return [a.val[0] + (b.val[0] - a.val[0]) * u,
                        a.val[1] + (b.val[1] - a.val[1]) * u,
                        a.val[2] + (b.val[2] - a.val[2]) * u];
            }
        }
        return keys[keys.length - 1].val.slice();
    }
    function sampleQuat(keys, frac) {
        if (!keys || keys.length === 0) return [0, 0, 0, 1];
        if (keys.length === 1) return keys[0].q.slice();
        const t0 = keys[0].time, t1 = keys[keys.length - 1].time;
        const target = t0 + frac * (t1 - t0);
        for (let i = 0; i < keys.length - 1; i++) {
            const a = keys[i], b = keys[i + 1];
            if (target <= b.time || i === keys.length - 2) {
                const span = (b.time - a.time) || 1;
                let u = Math.max(0, Math.min(1, (target - a.time) / span));
                let ax = a.q[0], ay = a.q[1], az = a.q[2], aw = a.q[3];
                let bx = b.q[0], by = b.q[1], bz = b.q[2], bw = b.q[3];
                if (ax * bx + ay * by + az * bz + aw * bw < 0) { bx = -bx; by = -by; bz = -bz; bw = -bw; }
                let x = ax + (bx - ax) * u, y = ay + (by - ay) * u, z = az + (bz - az) * u, w = aw + (bw - aw) * u;
                const l = Math.hypot(x, y, z, w) || 1;
                return [x / l, y / l, z / l, w / l];
            }
        }
        return keys[keys.length - 1].q.slice();
    }

    /* ------------------------------------------------------------------ */
    /*  Top-level parse                                                   */
    /* ------------------------------------------------------------------ */
    function parse(arrayBuffer) {
        const r = new Reader(arrayBuffer);
        const hdr = parseHeader(r);
        const version = hdr.version;

        const rawMeshes = [];
        const materials = [];

        while (r.pos <= r.len - 8) {
            const secType = r.u32();
            const secLen = r.i32();
            const bodyStart = r.pos;
            const bodyEnd = bodyStart + (secLen - 4);
            if (bodyEnd < bodyStart || bodyEnd > r.len) break;

            if (secType === SEC_MESH) {
                const sub = new Reader(arrayBuffer, bodyStart, bodyEnd);
                try { rawMeshes.push(parseMeshImpl(sub, version)); }
                catch (e) { /* skip a bad mesh */ }
            } else if (secType === SEC_MATERIAL) {
                const sub = new Reader(arrayBuffer, bodyStart, bodyEnd);
                try { materials.push(parseMaterial(sub, version)); }
                catch (e) { materials.push({ type: MAT_IMAGE, textureName: null, additive: false, color: null }); }
            }
            r.pos = bodyEnd;
        }

        const textureSet = new Map();
        const submeshes = [];
        let animStart = Infinity, animEnd = -Infinity, hasAnim = false, maxFps = 1;

        for (const rm of rawMeshes) {
            if (!rm.framePos[0]) continue;                  // no renderable geometry

            const nv = rm.numVertices, nf = rm.numFaces;

            // decompress a frame's vertices into out (Float32Array nv*3), native RF coords
            function decompress(fr, out) {
                const c = fr.c, m = fr.m, P = fr.i16;
                for (let i = 0; i < nv; i++) {
                    out[i * 3]     = c[0] + P[i * 3]     * m[0];
                    out[i * 3 + 1] = c[1] + P[i * 3 + 1] * m[1];
                    out[i * 3 + 2] = c[2] + P[i * 3 + 2] * m[2];
                }
            }

            const texFor = (localMat) => {
                if (rm.materialIndices) {
                    const g = rm.materialIndices[localMat];
                    if (g != null && g >= 0 && g < materials.length) return materials[g];
                } else if (rm.inlineMaterials && localMat >= 0 && localMat < rm.inlineMaterials.length) {
                    return rm.inlineMaterials[localMat];
                }
                return null;
            };

            // group faces by material -> chunks; record source vertex + uv offset per corner
            const chunkMap = new Map();
            for (let fi = 0; fi < nf; fi++) {
                const localMat = rm.faceMat[fi];
                const key = rm.materialIndices ? rm.materialIndices[localMat] : localMat;
                let ch = chunkMap.get(key);
                if (!ch) {
                    const mat = texFor(localMat);
                    ch = { textureName: mat ? mat.textureName : null, additive: mat ? !!mat.additive : false,
                           fullbright: rm.fullbright, cv: [], uo: [], used: new Set() };
                    chunkMap.set(key, ch);
                }
                const a = rm.faceIdx[fi * 3], b = rm.faceIdx[fi * 3 + 1], c = rm.faceIdx[fi * 3 + 2];
                ch.cv.push(a, b, c);
                ch.uo.push(fi * 6, fi * 6 + 2, fi * 6 + 4);
                ch.used.add(a); ch.used.add(b); ch.used.add(c);
            }

            // base UVs (frame 0) as a per-face-corner array of length 6*nf
            let baseUvArr = rm.frameUv[0];
            if (!baseUvArr && rm.oldFaceUv) baseUvArr = rm.oldFaceUv;   // old per-face UVs
            const uvAnim = rm.dumpUvs && rm.frameUv.length > 1 && rm.frameUv.filter(Boolean).length > 1;

            // classify animation kind
            let kind = "static";
            if (rm.morph && rm.numFrames > 1) kind = "morph";
            else if (rm.isKeyframed) kind = "keyframed";
            else if (rm.anyXform && rm.numFrames > 1) kind = "xform";

            // sample keyframed transforms into per-frame TRS (native)
            let kfFrames = null;
            if (kind === "keyframed") {
                kfFrames = new Array(rm.numFrames);
                for (let f = 0; f < rm.numFrames; f++) {
                    const frac = rm.numFrames > 1 ? f / (rm.numFrames - 1) : 0;
                    kfFrames[f] = { t: sampleVec3(rm.kfT, frac), q: sampleQuat(rm.kfR, frac), s: sampleVec3(rm.kfS, frac) };
                }
            }

            // frame0 base vertices
            const baseVerts = new Float32Array(nv * 3);
            decompress(rm.framePos[0], baseVerts);

            // shared per-mesh animation state
            const meshAnimated = (kind === "morph") || uvAnim ||
                                 (kind === "xform" && rm.numFrames > 1) ||
                                 (kind === "keyframed" && ((rm.kfR && rm.kfR.length > 1) || (rm.kfT && rm.kfT.length > 1) || (rm.kfS && rm.kfS.length > 1) || uvAnim));
            const animState = {
                kind, uvAnim,
                fps: rm.fps || 15, numFrames: rm.numFrames,
                startTime: rm.startTime, endTime: (rm.endTime > rm.startTime ? rm.endTime : rm.startTime + (rm.numFrames - 1) / (rm.fps || 15)),
                numVertices: nv, numFaces: nf,
                framePos: rm.framePos, frameUv: rm.frameUv, oldFaceUv: rm.oldFaceUv,
                xformFrames: rm.xformFrames, kfFrames, pivot: rm.pivot,
                scratchV: new Float32Array(nv * 3), lastFrame: -1,
                decompress
            };

            if (meshAnimated) {
                hasAnim = true;
                animStart = Math.min(animStart, animState.startTime);
                animEnd = Math.max(animEnd, animState.endTime);
                maxFps = Math.max(maxFps, animState.fps);
            }

            const chunks = [];
            for (const ch of chunkMap.values()) {
                if (!ch.cv.length) continue;
                if (ch.textureName) textureSet.set(ch.textureName, true);
                const cornerVtx = new Int32Array(ch.cv);
                const cornerUvOff = new Int32Array(ch.uo);
                const nCorners = cornerVtx.length;
                // frame0 position + uv buffers (native coords)
                const position = new Float32Array(nCorners * 3);
                for (let c = 0; c < nCorners; c++) {
                    const vi = cornerVtx[c] * 3;
                    position[c * 3] = baseVerts[vi]; position[c * 3 + 1] = baseVerts[vi + 1]; position[c * 3 + 2] = baseVerts[vi + 2];
                }
                const uv = new Float32Array(nCorners * 2);
                if (baseUvArr) for (let c = 0; c < nCorners; c++) { const o = cornerUvOff[c]; uv[c * 2] = baseUvArr[o]; uv[c * 2 + 1] = 1 - baseUvArr[o + 1]; }
                chunks.push({
                    textureName: ch.textureName, additive: ch.additive, fullbright: ch.fullbright,
                    numTris: nCorners / 3, numVerts: ch.used.size,
                    position, uv, cornerVtx, cornerUvOff, animState
                });
            }
            if (chunks.length) submeshes.push({ name: rm.name, animState, lods: [{ chunks }] });
        }

        if (!hasAnim) { animStart = 0; animEnd = 0; }
        return {
            isSkeletal: false, isVfx: true, version, submeshes, materials,
            textureNames: Array.from(textureSet.keys()),
            anim: { hasAnim, start: (animStart === Infinity ? 0 : animStart), end: (animEnd === -Infinity ? 0 : animEnd), fps: maxFps }
        };
    }

    /* ------------------------------------------------------------------ */
    /*  Three.js builder                                                  */
    /* ------------------------------------------------------------------ */
    const _p = { }; // lazy scratch (created against THREE on first use)
    function scratch(THREE) {
        if (!_p.v) { _p.v = new THREE.Vector3(); _p.q = new THREE.Quaternion(); _p.s = new THREE.Vector3();
                     _p.v2 = new THREE.Vector3(); _p.q2 = new THREE.Quaternion(); _p.s2 = new THREE.Vector3();
                     _p.mA = new THREE.Matrix4(); _p.mB = new THREE.Matrix4(); }
        return _p;
    }

    function setMeshMatrix(mesh, THREE) {
        const u = mesh.userData.vfx; const a = u.animState; const p = scratch(THREE);
        const f = u.frame;
        if (a.kind === "xform" && a.xformFrames[f]) {
            const x = a.xformFrames[f];
            p.v.set(x.t[0], x.t[1], x.t[2]); p.q.set(x.q[0], x.q[1], x.q[2], x.q[3]); p.s.set(x.s[0], x.s[1], x.s[2]);
            mesh.matrix.compose(p.v, p.q, p.s);
        } else if (a.kind === "keyframed" && a.kfFrames) {
            const x = a.kfFrames[f];
            p.v.set(x.t[0], x.t[1], x.t[2]); p.q.set(x.q[0], x.q[1], x.q[2], x.q[3]); p.s.set(x.s[0], x.s[1], x.s[2]);
            p.mA.compose(p.v, p.q, p.s);
            if (a.pivot) {
                const pv = a.pivot;
                p.v2.set(pv.t[0], pv.t[1], pv.t[2]); p.q2.set(pv.q[0], pv.q[1], pv.q[2], pv.q[3]); p.s2.set(pv.s[0], pv.s[1], pv.s[2]);
                p.mB.compose(p.v2, p.q2, p.s2);
                mesh.matrix.multiplyMatrices(p.mA, p.mB);
            } else mesh.matrix.copy(p.mA);
        }
        // morph / static: identity (left as set)
    }

    function applyMeshFrame(mesh, f, THREE) {
        const u = mesh.userData.vfx; const a = u.animState;
        u.frame = f;
        if (a.kind === "morph") {
            if (a.lastFrame !== f) { a.decompress(a.framePos[f] || a.framePos[0], a.scratchV); a.lastFrame = f; }
            const pos = mesh.geometry.attributes.position.array, cv = u.cornerVtx, V = a.scratchV;
            for (let c = 0; c < cv.length; c++) { const vi = cv[c] * 3; pos[c * 3] = V[vi]; pos[c * 3 + 1] = V[vi + 1]; pos[c * 3 + 2] = V[vi + 2]; }
            mesh.geometry.attributes.position.needsUpdate = true;
            mesh.geometry.computeVertexNormals();
        }
        if (a.uvAnim) {
            const fu = a.frameUv[f] || a.frameUv[0];
            if (fu) {
                const uv = mesh.geometry.attributes.uv.array, co = u.cornerUvOff;
                for (let c = 0; c < co.length; c++) { const o = co[c]; uv[c * 2] = fu[o]; uv[c * 2 + 1] = 1 - fu[o + 1]; }
                mesh.geometry.attributes.uv.needsUpdate = true;
            }
        }
        if (a.kind === "xform" || a.kind === "keyframed") setMeshMatrix(mesh, THREE);
    }

    function buildThreeGroup(model, THREE, options) {
        options = options || {};
        const resolveTexture = options.resolveTexture || (() => null);
        const wireframe = !!options.wireframe;

        const group = new THREE.Group();
        group.name = "VFX";
        group.scale.x = -1;                                 // RF -> Three reflection (matches V3M negate-X)
        const texByName = new Map();

        for (const sm of model.submeshes) {
            for (const ch of sm.lods[0].chunks) {
                const geo = new THREE.BufferGeometry();
                geo.setAttribute("position", new THREE.BufferAttribute(ch.position.slice(), 3));
                geo.setAttribute("uv", new THREE.BufferAttribute(ch.uv.slice(), 2));
                geo.computeVertexNormals();

                let tex = null;
                if (ch.textureName) {
                    if (texByName.has(ch.textureName)) tex = texByName.get(ch.textureName);
                    else { tex = resolveTexture(ch.textureName); texByName.set(ch.textureName, tex); }
                }
                const matOpts = { side: THREE.DoubleSide, wireframe };
                if (tex) matOpts.map = tex; else matOpts.color = 0x9a9488;
                const mat = new THREE.MeshStandardMaterial(matOpts);
                mat.roughness = 1.0; mat.metalness = 0.0;
                if (ch.fullbright && tex) { mat.emissive = new THREE.Color(0xffffff); mat.emissiveMap = tex; }
                if (ch.additive) { mat.blending = THREE.AdditiveBlending; mat.transparent = true; mat.depthWrite = false; }

                const mesh = new THREE.Mesh(geo, mat);
                mesh.name = sm.name;
                mesh.matrixAutoUpdate = false;
                mesh.userData.vfx = { animState: ch.animState, cornerVtx: ch.cornerVtx, cornerUvOff: ch.cornerUvOff, frame: 0 };
                setMeshMatrix(mesh, THREE);                 // frame-0 transform for correct initial framing
                mesh.updateMatrixWorld(true);
                group.add(mesh);
            }
        }
        return group;
    }

    /* ------------------------------------------------------------------ */
    /*  Playback: set every mesh in the group to global time t (seconds)  */
    /* ------------------------------------------------------------------ */
    function applyGroupTime(group, tSec, THREE) {
        group.traverse(o => {
            if (!o.isMesh || !o.userData.vfx) return;
            const a = o.userData.vfx.animState;
            if (a.numFrames <= 1) { o.visible = true; return; }   // static mesh, always shown
            if (tSec < a.startTime - 1e-4 || tSec > a.endTime + 1e-4) { o.visible = false; return; }
            o.visible = true;
            let f = Math.round((tSec - a.startTime) * a.fps);
            if (f < 0) f = 0; else if (f >= a.numFrames) f = a.numFrames - 1;
            applyMeshFrame(o, f, THREE);
            o.updateMatrixWorld(true);
        });
    }

    return { parse, buildThreeGroup, applyGroupTime, MAGIC_VSFX };
}));
