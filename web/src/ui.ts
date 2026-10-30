// DOM e estados visuais. Nada de lógica de protocolo aqui.
import type { LinkState } from './serial';
import type { DestState } from './storage';

function $<T extends HTMLElement>(id: string): T {
  const el = document.getElementById(id);
  if (!el) throw new Error(`#${id} não encontrado`);
  return el as T;
}

export function fmtBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let v = n / 1024;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return `${v.toLocaleString('pt-BR', { maximumFractionDigits: v < 10 ? 2 : 1 })} ${units[i]}`;
}

export function fmtSpeed(bps: number): string {
  return `${fmtBytes(Math.round(bps))}/s`;
}

function fmtDuration(ms: number): string {
  const s = Math.round(ms / 1000);
  if (s < 60) return `${s}s`;
  return `${Math.floor(s / 60)}min ${s % 60}s`;
}

export interface UiActions {
  connect(): void;
  chooseFolder(): void;
  unlockFolder(): void;
  forgetFolder(): void;
}

/** Uma linha da lista de transferências */
export class TransferRow {
  private readonly el: HTMLLIElement;
  private readonly bar: HTMLElement;
  private readonly status: HTMLElement;
  private readonly meta: HTMLElement;
  private readonly nameEl: HTMLElement;

  constructor(list: HTMLUListElement, name: string, private readonly size: number, index: number, count: number) {
    this.el = document.createElement('li');
    this.el.className = 'xfer';
    this.el.innerHTML = `
      <div class="xfer-top"><span class="xfer-name"></span><span class="xfer-meta"></span></div>
      <div class="bar"><i></i></div>
      <div class="xfer-status">Iniciando…</div>`;
    this.nameEl = this.el.querySelector('.xfer-name')!;
    this.meta = this.el.querySelector('.xfer-meta')!;
    this.bar = this.el.querySelector('.bar > i')!;
    this.status = this.el.querySelector('.xfer-status')!;
    this.nameEl.textContent = name; // textContent: nome vem do celular, nunca vira HTML
    this.nameEl.title = name;
    this.meta.textContent = (count > 1 ? `${index}/${count} · ` : '') + fmtBytes(size);
    list.prepend(this.el);
  }

  progress(done: number, bps: number): void {
    const pct = this.size ? Math.min(100, (done / this.size) * 100) : 0;
    this.bar.style.width = `${pct.toFixed(1)}%`;
    const eta = bps > 0 ? ` · faltam ${fmtDuration(((this.size - done) / bps) * 1000)}` : '';
    this.status.textContent = `${pct.toFixed(1)}% · ${fmtBytes(done)} de ${fmtBytes(this.size)} · ${fmtSpeed(bps)}${eta}`;
  }

  succeed(finalName: string, elapsedMs: number): void {
    this.el.classList.add('ok');
    this.bar.style.width = '100%';
    this.nameEl.textContent = finalName;
    this.nameEl.title = finalName;
    const avg = elapsedMs > 0 ? ` · média ${fmtSpeed((this.size / elapsedMs) * 1000)}` : '';
    this.status.textContent = `✓ Salvo · ${fmtBytes(this.size)} em ${fmtDuration(elapsedMs)}${avg}`;
  }

  fail(reason: string): void {
    this.el.classList.add('err');
    this.status.textContent = `✕ ${reason} — arquivo parcial apagado`;
  }

  get finished(): boolean {
    return this.el.classList.contains('ok') || this.el.classList.contains('err');
  }

  remove(): void {
    this.el.remove();
  }
}

export class Ui {
  private readonly pill = $<HTMLSpanElement>('link-pill');
  private readonly dongleStatus = $<HTMLParagraphElement>('dongle-status');
  private readonly dongleInfo = $<HTMLParagraphElement>('dongle-info');
  private readonly btnConnect = $<HTMLButtonElement>('btn-connect');
  private readonly destStatus = $<HTMLParagraphElement>('dest-status');
  private readonly btnUnlock = $<HTMLButtonElement>('btn-unlock');
  private readonly btnChoose = $<HTMLButtonElement>('btn-choose');
  private readonly btnForget = $<HTMLButtonElement>('btn-forget');
  private readonly list = $<HTMLUListElement>('xfers');
  private readonly empty = $<HTMLParagraphElement>('xfers-empty');
  private readonly totals = $<HTMLSpanElement>('totals');
  private readonly logEl = $<HTMLPreElement>('log');
  private rows: TransferRow[] = [];
  private okCount = 0;
  private okBytes = 0;
  private logLines: string[] = [];

  constructor(actions: UiActions) {
    this.btnConnect.addEventListener('click', () => actions.connect());
    this.btnChoose.addEventListener('click', () => actions.chooseFolder());
    this.btnUnlock.addEventListener('click', () => actions.unlockFolder());
    this.btnForget.addEventListener('click', () => actions.forgetFolder());
    $<HTMLButtonElement>('btn-clear').addEventListener('click', () => this.clearFinished());
  }

  showUnsupported(): void {
    $<HTMLElement>('unsupported').hidden = false;
    this.btnConnect.disabled = true;
    this.dongleStatus.textContent = 'Web Serial indisponível';
  }

  setLink(state: LinkState, detail?: string): void {
    const map: Record<LinkState, [string, string, string]> = {
      unsupported: ['pill-off', 'Sem suporte', 'Web Serial indisponível'],
      disconnected: ['pill-off', 'Desconectado', 'Dongle não conectado'],
      connecting: ['pill-wait', 'Conectando…', 'Abrindo a porta…'],
      connected: ['pill-on', 'Conectado', 'Dongle conectado ✓'],
    };
    const [cls, pill, text] = map[state];
    this.pill.className = `pill ${cls}`;
    this.pill.textContent = pill;
    this.dongleStatus.textContent = detail && state === 'disconnected' ? `${text} (${detail})` : text;
    this.btnConnect.hidden = state === 'connected' || state === 'connecting';
    if (state !== 'connected') this.dongleInfo.textContent = 'Primeira vez? Clique em Conectar e escolha "T-Dongle Bridge".';
  }

  setDongleInfo(text: string): void {
    this.dongleInfo.textContent = text;
  }

  setDest(state: DestState, folderName: string | null, apiSupported: boolean): void {
    this.btnUnlock.hidden = state !== 'locked';
    this.btnForget.hidden = state === 'download';
    this.btnChoose.hidden = !apiSupported;
    this.btnChoose.textContent = folderName ? 'Trocar pasta' : 'Escolher pasta';
    switch (state) {
      case 'folder':
        this.destStatus.textContent = `📁 ${folderName}`;
        break;
      case 'locked':
        this.destStatus.textContent = `📁 ${folderName} — o Chrome precisa de um clique para liberar o acesso`;
        break;
      case 'download':
        this.destStatus.textContent = apiSupported
          ? '⬇ Sem pasta: cada arquivo vai para Downloads ao terminar'
          : '⬇ Este navegador não permite escolher pasta: os arquivos vão para Downloads';
        break;
    }
  }

  addTransfer(name: string, size: number, index: number, count: number): TransferRow {
    const row = new TransferRow(this.list, name, size, index, count);
    this.rows.push(row);
    this.empty.hidden = true;
    return row;
  }

  countSuccess(bytes: number): void {
    this.okCount++;
    this.okBytes += bytes;
    this.totals.textContent = `${this.okCount} recebido${this.okCount > 1 ? 's' : ''} · ${fmtBytes(this.okBytes)}`;
  }

  setTitleProgress(pct: number | null): void {
    document.title = pct === null ? 'T-Dongle Bridge' : `${pct.toFixed(0)}% · T-Dongle Bridge`;
  }

  log(text: string): void {
    const t = new Date().toLocaleTimeString('pt-BR');
    this.logLines.push(`[${t}] ${text}`);
    if (this.logLines.length > 500) this.logLines.splice(0, this.logLines.length - 500);
    this.logEl.textContent = this.logLines.join('\n');
    this.logEl.scrollTop = this.logEl.scrollHeight;
  }

  private clearFinished(): void {
    this.rows = this.rows.filter((r) => {
      if (r.finished) r.remove();
      return !r.finished;
    });
    this.empty.hidden = this.rows.length > 0;
  }
}
