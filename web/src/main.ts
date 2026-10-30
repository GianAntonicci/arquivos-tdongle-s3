// Orquestração: serial -> parser -> fila de frames -> gravação em disco -> ACKs.
import './style.css';

import {
  FrameParser,
  FrameType,
  Reason,
  ackFrame,
  crc32,
  doneFrame,
  helloFrame,
  nakFrame,
  reasonText,
  type DongleFrame,
  type StartFrame,
} from './protocol';
import { SerialLink, type LinkState } from './serial';
import { Destination, folderApiSupported, type FileSink } from './storage';
import { Ui, type TransferRow } from './ui';

const HELLO_INTERVAL_MS = 1000;
const UI_UPDATE_MS = 150;

// ---------- Transferência em andamento ----------
interface Active {
  id: number;
  size: number;
  received: number;
  crc: number;
  sink: FileSink;
  row: TransferRow;
  startedAt: number;
  // medidor de velocidade (média móvel)
  speedAt: number;
  speedBytes: number;
  bps: number;
  lastUi: number;
}

let active: Active | null = null;
/** id recusado (ex.: falha ao criar arquivo): ignora os DATA que ainda chegarem */
let ignoredId: number | null = null;

const dest = new Destination();

const ui = new Ui({
  connect: () => void link.request(),
  chooseFolder: () => void chooseFolder(),
  unlockFolder: () => void unlockFolder(),
  forgetFolder: () => void forgetFolder(),
});

const parser = new FrameParser(
  (f) => enqueue(f),
  (why) => onCorrupt(why),
);

const link = new SerialLink({
  onState: (s, detail) => onLinkState(s, detail),
  onData: (chunk) => parser.push(chunk),
});

// ---------- Fila de frames: processa um por vez, na ordem ----------
// As escritas em disco são assíncronas; a fila garante que um DATA só é
// processado depois do anterior estar gravado (e o ACK só sai depois disso).
const queue: DongleFrame[] = [];
let draining = false;

function enqueue(f: DongleFrame): void {
  queue.push(f);
  if (!draining) void drain();
}

async function drain(): Promise<void> {
  draining = true;
  try {
    while (queue.length) {
      const f = queue.shift()!;
      try {
        await handleFrame(f);
      } catch (err) {
        ui.log(`erro ao processar frame: ${(err as Error).message}`);
      }
    }
  } finally {
    draining = false;
  }
}

function send(bytes: Uint8Array): void {
  link.write(bytes).catch((err: Error) => ui.log(`falha ao escrever na serial: ${err.message}`));
}

async function handleFrame(f: DongleFrame): Promise<void> {
  switch (f.type) {
    case FrameType.HelloAck:
      ui.setDongleInfo(`Firmware ${f.fwVersion} · protocolo v${f.proto} · janela ${f.window} × ${f.chunk} B`);
      return;
    case FrameType.Log:
      ui.log(`[dongle] ${f.text}`);
      return;
    case FrameType.Start:
      return onStart(f);
    case FrameType.Data: {
      const a = active;
      if (!a || a.id !== f.id) {
        if (f.id !== ignoredId) ui.log(`DATA de transferência desconhecida (id ${f.id}) ignorado`);
        return;
      }
      if (f.offset !== a.received) {
        await failActive(`fora de ordem (esperado ${a.received}, veio ${f.offset})`, Reason.Crc, true);
        return;
      }
      try {
        await a.sink.write(f.data);
      } catch (err) {
        await failActive(`erro ao gravar: ${(err as Error).message}`, Reason.DiskError, true);
        return;
      }
      a.crc = crc32(a.crc, f.data);
      a.received += f.data.length;
      send(ackFrame(a.id, a.received));
      updateProgress(a, false);
      return;
    }
    case FrameType.End: {
      const a = active;
      if (!a || a.id !== f.id) return;
      if (a.received !== a.size || a.crc !== f.crc) {
        const why =
          a.received !== a.size
            ? `tamanho não confere (${a.received} de ${a.size})`
            : `CRC não confere (PC ${hex(a.crc)}, dongle ${hex(f.crc)})`;
        await failActive(why, Reason.Crc, false);
        send(doneFrame(f.id, Reason.Crc));
        return;
      }
      try {
        await a.sink.close();
      } catch (err) {
        await failActive(`erro ao finalizar: ${(err as Error).message}`, Reason.DiskError, false);
        send(doneFrame(f.id, Reason.DiskError));
        return;
      }
      send(doneFrame(f.id, Reason.None));
      const elapsed = performance.now() - a.startedAt;
      a.row.succeed(a.sink.finalName, elapsed);
      ui.countSuccess(a.size);
      ui.log(`✓ ${a.sink.finalName} (${a.size} B, CRC ${hex(a.crc)})`);
      active = null;
      ui.setTitleProgress(null);
      return;
    }
    case FrameType.Abort: {
      const a = active;
      if (!a || a.id !== f.id) return;
      await failActive(reasonText(f.reason), null, false);
      return;
    }
  }
}

async function onStart(f: StartFrame): Promise<void> {
  if (active) await failActive('interrompido por um novo arquivo', null, false);
  const row = ui.addTransfer(f.name, f.size, f.index, f.count);
  let sink: FileSink;
  try {
    sink = await dest.createSink(f.name, f.size);
  } catch (err) {
    row.fail(`não foi possível criar o arquivo: ${(err as Error).message}`);
    ignoredId = f.id;
    send(nakFrame(f.id, Reason.DiskError));
    refreshDest();
    return;
  }
  const now = performance.now();
  active = {
    id: f.id, size: f.size, received: 0, crc: 0, sink, row,
    startedAt: now, speedAt: now, speedBytes: 0, bps: 0, lastUi: 0,
  };
  ui.log(`→ ${f.name} (${f.size} B)${sink.finalName !== f.name ? ` salvo como "${sink.finalName}"` : ''}`);
  // ACK do offset 0: libera o dongle a começar a mandar DATA
  send(ackFrame(f.id, 0));
  updateProgress(active, true);
  // Arquivo vazio: só vai chegar o END
}

/** Cancela a transferência ativa, apaga o parcial e (opcional) avisa o dongle com NAK. */
async function failActive(why: string, nak: Reason | null, sendNak: boolean): Promise<void> {
  const a = active;
  if (!a) return;
  active = null;
  ignoredId = a.id;
  if (sendNak && nak !== null) send(nakFrame(a.id, nak));
  await a.sink.abort();
  a.row.fail(why);
  ui.log(`✕ ${a.sink.finalName}: ${why}`);
  ui.setTitleProgress(null);
}

function updateProgress(a: Active, force: boolean): void {
  const now = performance.now();
  // velocidade: janela de ~1 s com suavização exponencial
  if (now - a.speedAt >= 1000) {
    const inst = ((a.received - a.speedBytes) / (now - a.speedAt)) * 1000;
    a.bps = a.bps ? a.bps * 0.6 + inst * 0.4 : inst;
    a.speedAt = now;
    a.speedBytes = a.received;
  }
  if (!force && now - a.lastUi < UI_UPDATE_MS) return;
  a.lastUi = now;
  a.row.progress(a.received, a.bps);
  ui.setTitleProgress(a.size ? (a.received / a.size) * 100 : 0);
}

function onCorrupt(why: string): void {
  ui.log(`frame corrompido: ${why}`);
  if (active) void failActive(`dados corrompidos na USB (${why})`, Reason.Crc, true);
}

// ---------- Conexão ----------
let helloTimer: number | undefined;

function sendHello(): void {
  if (link.current === 'connected') send(helloFrame(dest.ready));
}

function onLinkState(s: LinkState, detail?: string): void {
  ui.setLink(s, detail);
  if (detail) ui.log(`serial: ${s} (${detail})`);
  else ui.log(`serial: ${s}`);
  if (s === 'connected') {
    parser.reset();
    sendHello();
    window.clearInterval(helloTimer);
    helloTimer = window.setInterval(sendHello, HELLO_INTERVAL_MS);
  } else if (s === 'disconnected') {
    window.clearInterval(helloTimer);
    queue.length = 0;
    if (active) void failActive('dongle desconectado', null, false);
  }
}

// ---------- Pasta ----------
function refreshDest(): void {
  ui.setDest(dest.state, dest.folderName, folderApiSupported());
  sendHello(); // avisa o dongle na hora se ficou pronto/não pronto
}

async function chooseFolder(): Promise<void> {
  try {
    await dest.choose();
  } catch (err) {
    if ((err as DOMException).name !== 'AbortError') ui.log(`pasta: ${(err as Error).message}`);
  }
  refreshDest();
}

async function unlockFolder(): Promise<void> {
  try {
    await dest.unlock();
  } catch (err) {
    ui.log(`permissão: ${(err as Error).message}`);
  }
  refreshDest();
}

async function forgetFolder(): Promise<void> {
  await dest.forget();
  refreshDest();
}

function hex(n: number): string {
  return n.toString(16).padStart(8, '0');
}

// Avisa antes de fechar a aba no meio de uma transferência
window.addEventListener('beforeunload', (e) => {
  if (active) e.preventDefault();
});

// ---------- Início ----------
async function start(): Promise<void> {
  await dest.restore().catch((err: Error) => ui.log(`restaurar pasta: ${err.message}`));
  refreshDest();
  if (!SerialLink.supported()) {
    ui.showUnsupported();
    return;
  }
  await link.init();
}

void start();
