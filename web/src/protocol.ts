// =============================================================================
// Protocolo T-Dongle Bridge (USB CDC) — DEVE ficar idêntico a
// firmware/src/protocol.cpp e firmware/include/protocol.h. Formato no README.
//
// Frame (todos os inteiros little-endian):
//   off  tam  campo
//   0    2    magic 0xA5 0x5A
//   2    1    tipo
//   3    1    flags (reservado, 0)
//   4    2    len = tamanho do payload (0..MAX_PAYLOAD)
//   6    len  payload
//   6+len 4   CRC32 (IEEE/zlib) sobre os bytes 2..6+len (tipo, flags, len, payload)
// =============================================================================

export const MAGIC0 = 0xa5;
export const MAGIC1 = 0x5a;
export const HEADER_SIZE = 6;
export const TRAILER_SIZE = 4;
export const PROTO_VERSION = 1;
/** Maior payload aceito (DATA: 12 bytes de cabeçalho + 4096 de dados, com folga). */
export const MAX_PAYLOAD = 8192;

export const FrameType = {
  // dongle -> PC
  Start: 0x01,
  Data: 0x02,
  End: 0x03,
  Abort: 0x04,
  Log: 0x05,
  HelloAck: 0x06,
  // PC -> dongle
  Hello: 0x10,
  Ack: 0x11,
  Nak: 0x12,
  Done: 0x13,
} as const;
export type FrameType = (typeof FrameType)[keyof typeof FrameType];

/** Motivos de erro (mesma numeração do enum ErrorReason do firmware). */
export const Reason = {
  None: 0,
  Crc: 1,
  HostGone: 2,
  WifiLost: 3,
  Aborted: 4,
  NoHost: 5,
  DiskError: 6,
  Busy: 7,
} as const;
export type Reason = (typeof Reason)[keyof typeof Reason];

export function reasonText(r: number): string {
  switch (r) {
    case Reason.Crc: return 'CRC inválido';
    case Reason.HostGone: return 'PC desconectou';
    case Reason.WifiLost: return 'Wi-Fi do celular caiu';
    case Reason.Aborted: return 'Cancelado';
    case Reason.NoHost: return 'PC não estava pronto';
    case Reason.DiskError: return 'Erro ao gravar no disco';
    case Reason.Busy: return 'Dongle ocupado';
    default: return `Erro ${r}`;
  }
}

/** Bits do campo flags do HELLO */
export const HelloFlags = {
  /** Destino pronto para gravar (pasta com permissão ou modo download) */
  Ready: 0x01,
} as const;

// ---------- Frames decodificados ----------

/** START: id u32 | size u64 | index u16 | count u16 | nome UTF-8 (resto) */
export interface StartFrame { type: typeof FrameType.Start; id: number; size: number; index: number; count: number; name: string }
/** DATA: id u32 | offset u64 | dados (resto) */
export interface DataFrame { type: typeof FrameType.Data; id: number; offset: number; data: Uint8Array }
/** END: id u32 | crc32 u32 do arquivo inteiro */
export interface EndFrame { type: typeof FrameType.End; id: number; crc: number }
/** ABORT: id u32 | motivo u8 */
export interface AbortFrame { type: typeof FrameType.Abort; id: number; reason: number }
/** LOG: texto UTF-8 */
export interface LogFrame { type: typeof FrameType.Log; text: string }
/** HELLO_ACK: proto u8 | janela u8 | chunk u16 | versão do firmware UTF-8 (resto) */
export interface HelloAckFrame { type: typeof FrameType.HelloAck; proto: number; window: number; chunk: number; fwVersion: string }

export type DongleFrame = StartFrame | DataFrame | EndFrame | AbortFrame | LogFrame | HelloAckFrame;

// =============================================================================
// CRC32 (IEEE 802.3, polinômio refletido 0xEDB88320, o mesmo do zlib)
// =============================================================================
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

/**
 * CRC32 incremental. Comece com crc = 0 e passe o resultado anterior para
 * continuar: crc32(crc32(0, a), b) === crc32(0, a+b).
 */
export function crc32(crc: number, data: Uint8Array): number {
  let c = ~crc >>> 0;
  for (let i = 0; i < data.length; i++) c = CRC_TABLE[(c ^ data[i]!) & 0xff]! ^ (c >>> 8);
  return ~c >>> 0;
}

// =============================================================================
// Codificação (PC -> dongle)
// =============================================================================
export function encodeFrame(type: FrameType, payload: Uint8Array = new Uint8Array(0)): Uint8Array {
  if (payload.length > MAX_PAYLOAD) throw new Error('payload grande demais');
  const out = new Uint8Array(HEADER_SIZE + payload.length + TRAILER_SIZE);
  const dv = new DataView(out.buffer);
  out[0] = MAGIC0;
  out[1] = MAGIC1;
  out[2] = type;
  out[3] = 0;
  dv.setUint16(4, payload.length, true);
  out.set(payload, HEADER_SIZE);
  const crc = crc32(0, out.subarray(2, HEADER_SIZE + payload.length));
  dv.setUint32(HEADER_SIZE + payload.length, crc, true);
  return out;
}

/** HELLO: proto u8 | flags u8 */
export function helloFrame(ready: boolean): Uint8Array {
  return encodeFrame(FrameType.Hello, Uint8Array.of(PROTO_VERSION, ready ? HelloFlags.Ready : 0));
}

/** ACK: id u32 | offset u64 (bytes do arquivo já gravados) */
export function ackFrame(id: number, offset: number): Uint8Array {
  const p = new Uint8Array(12);
  const dv = new DataView(p.buffer);
  dv.setUint32(0, id, true);
  dv.setBigUint64(4, BigInt(offset), true);
  return encodeFrame(FrameType.Ack, p);
}

/** NAK: id u32 | motivo u8 — pede para o dongle abortar o arquivo */
export function nakFrame(id: number, reason: Reason): Uint8Array {
  const p = new Uint8Array(5);
  new DataView(p.buffer).setUint32(0, id, true);
  p[4] = reason;
  return encodeFrame(FrameType.Nak, p);
}

/** DONE: id u32 | status u8 (0 = gravado e CRC ok; senão o motivo) */
export function doneFrame(id: number, status: Reason): Uint8Array {
  const p = new Uint8Array(5);
  new DataView(p.buffer).setUint32(0, id, true);
  p[4] = status;
  return encodeFrame(FrameType.Done, p);
}

// =============================================================================
// Decodificação (dongle -> PC)
// =============================================================================
const utf8 = new TextDecoder('utf-8');

function decodePayload(type: number, p: Uint8Array): DongleFrame | null {
  const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
  switch (type) {
    case FrameType.Start:
      if (p.length < 16) return null;
      return {
        type: FrameType.Start,
        id: dv.getUint32(0, true),
        size: Number(dv.getBigUint64(4, true)),
        index: dv.getUint16(12, true),
        count: dv.getUint16(14, true),
        name: utf8.decode(p.subarray(16)),
      };
    case FrameType.Data:
      if (p.length < 12) return null;
      return {
        type: FrameType.Data,
        id: dv.getUint32(0, true),
        offset: Number(dv.getBigUint64(4, true)),
        // cópia: o buffer do parser é reutilizado
        data: p.slice(12),
      };
    case FrameType.End:
      if (p.length < 8) return null;
      return { type: FrameType.End, id: dv.getUint32(0, true), crc: dv.getUint32(4, true) };
    case FrameType.Abort:
      if (p.length < 5) return null;
      return { type: FrameType.Abort, id: dv.getUint32(0, true), reason: p[4]! };
    case FrameType.Log:
      return { type: FrameType.Log, text: utf8.decode(p) };
    case FrameType.HelloAck:
      if (p.length < 4) return null;
      return {
        type: FrameType.HelloAck,
        proto: p[0]!,
        window: p[1]!,
        chunk: dv.getUint16(2, true),
        fwVersion: utf8.decode(p.subarray(4)),
      };
    default:
      return null;
  }
}

/**
 * Parser de stream: recebe pedaços arbitrários da serial e entrega frames
 * completos. Se o CRC falhar, descarta 1 byte e procura o próximo magic
 * (ressincronização), avisando via onCorrupt.
 */
export class FrameParser {
  private buf = new Uint8Array(64 * 1024);
  private start = 0;
  private end = 0;

  constructor(
    private readonly onFrame: (f: DongleFrame) => void,
    private readonly onCorrupt: (why: string) => void,
  ) {}

  push(chunk: Uint8Array): void {
    this.append(chunk);
    for (;;) {
      const avail = this.end - this.start;
      if (avail < HEADER_SIZE) break;
      const b = this.buf;
      const s = this.start;
      if (b[s] !== MAGIC0 || b[s + 1] !== MAGIC1) {
        // procura o próximo magic
        let i = s + 1;
        while (i < this.end && b[i] !== MAGIC0) i++;
        this.start = i;
        continue;
      }
      const type = b[s + 2]!;
      const len = b[s + 4]! | (b[s + 5]! << 8);
      if (len > MAX_PAYLOAD) {
        this.onCorrupt(`tamanho inválido (${len})`);
        this.start = s + 1;
        continue;
      }
      const total = HEADER_SIZE + len + TRAILER_SIZE;
      if (avail < total) break; // espera mais bytes
      const want =
        (b[s + HEADER_SIZE + len]! |
          (b[s + HEADER_SIZE + len + 1]! << 8) |
          (b[s + HEADER_SIZE + len + 2]! << 16) |
          (b[s + HEADER_SIZE + len + 3]! << 24)) >>> 0;
      const got = crc32(0, b.subarray(s + 2, s + HEADER_SIZE + len));
      if (want !== got) {
        this.onCorrupt(`CRC do frame tipo 0x${type.toString(16)}`);
        this.start = s + 1;
        continue;
      }
      const frame = decodePayload(type, b.subarray(s + HEADER_SIZE, s + HEADER_SIZE + len));
      this.start = s + total;
      if (frame) this.onFrame(frame);
      else this.onCorrupt(`frame desconhecido ou curto (tipo 0x${type.toString(16)})`);
    }
    // compacta quando o buffer esvazia
    if (this.start === this.end) this.start = this.end = 0;
  }

  reset(): void {
    this.start = this.end = 0;
  }

  private append(chunk: Uint8Array): void {
    if (this.end + chunk.length > this.buf.length) {
      // move o que sobrou para o início; cresce só se ainda faltar espaço
      const rest = this.buf.subarray(this.start, this.end);
      const need = rest.length + chunk.length;
      const target = need > this.buf.length ? new Uint8Array(Math.max(need, this.buf.length * 2)) : this.buf;
      target.set(rest, 0);
      this.buf = target;
      this.end = rest.length;
      this.start = 0;
    }
    this.buf.set(chunk, this.end);
    this.end += chunk.length;
  }
}
