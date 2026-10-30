// Conexão Web Serial com o dongle: abrir, reconectar sozinho, ler e escrever.

/** VID/PID do T-Dongle S3 (iguais a USB_VID_VALUE/USB_PID_VALUE no config.h) */
export const DONGLE_FILTER: SerialPortFilter = { usbVendorId: 0x303a, usbProductId: 0x82c1 };

export type LinkState = 'unsupported' | 'disconnected' | 'connecting' | 'connected';

export interface SerialLinkEvents {
  onState(state: LinkState, detail?: string): void;
  onData(chunk: Uint8Array): void;
}

export class SerialLink {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private writeChain: Promise<void> = Promise.resolve();
  private state: LinkState = 'disconnected';
  private opening = false;

  constructor(private readonly ev: SerialLinkEvents) {}

  static supported(): boolean {
    return 'serial' in navigator;
  }

  get current(): LinkState {
    return this.state;
  }

  /** Liga os eventos de plug/desplug e tenta abrir uma porta já autorizada. */
  async init(): Promise<void> {
    if (!SerialLink.supported()) {
      this.setState('unsupported');
      return;
    }
    navigator.serial.addEventListener('connect', (e) => {
      const port = e.target as SerialPort;
      if (this.isDongle(port) && !this.port) void this.open(port);
    });
    navigator.serial.addEventListener('disconnect', (e) => {
      if (e.target === this.port) void this.close('dongle desplugado');
    });
    await this.autoConnect();
  }

  /** Reabre uma porta autorizada antes (sem diálogo). */
  async autoConnect(): Promise<boolean> {
    const ports = await navigator.serial.getPorts();
    const port = ports.find((p) => this.isDongle(p));
    if (!port) return false;
    await this.open(port);
    return this.state === 'connected';
  }

  /** Primeira vez: abre o seletor do Chrome filtrando pelo VID/PID do dongle. Exige clique. */
  async request(): Promise<void> {
    let port: SerialPort;
    try {
      port = await navigator.serial.requestPort({ filters: [DONGLE_FILTER] });
    } catch {
      return; // usuário cancelou o diálogo
    }
    await this.open(port);
  }

  /** Envia bytes; as escritas são serializadas numa fila. */
  write(bytes: Uint8Array): Promise<void> {
    const job = this.writeChain.then(async () => {
      if (!this.writer) throw new Error('porta fechada');
      await this.writer.write(bytes);
    });
    // a fila continua mesmo se uma escrita falhar
    this.writeChain = job.catch(() => undefined);
    return job;
  }

  async close(detail?: string): Promise<void> {
    const port = this.port;
    this.port = null;
    try { await this.reader?.cancel(); } catch { /* já fechado */ }
    try { this.writer?.releaseLock(); } catch { /* já liberado */ }
    this.reader = null;
    this.writer = null;
    try { await port?.close(); } catch { /* porta já sumiu */ }
    this.setState('disconnected', detail);
  }

  private isDongle(port: SerialPort): boolean {
    const info = port.getInfo();
    return info.usbVendorId === DONGLE_FILTER.usbVendorId && info.usbProductId === DONGLE_FILTER.usbProductId;
  }

  private async open(port: SerialPort): Promise<void> {
    if (this.opening || this.port) return;
    this.opening = true;
    this.setState('connecting');
    try {
      // baudRate é ignorado na CDC USB, mas é obrigatório na API
      await port.open({ baudRate: 921600, bufferSize: 256 * 1024 });
      try {
        // DTR ligado: o firmware usa isso para saber que há alguém na porta
        await port.setSignals({ dataTerminalReady: true, requestToSend: true });
      } catch { /* alguns SOs não suportam */ }
      this.port = port;
      this.writer = port.writable!.getWriter();
      this.setState('connected');
      void this.readLoop(port);
    } catch (err) {
      this.setState('disconnected', `não abriu a porta: ${(err as Error).message}`);
    } finally {
      this.opening = false;
    }
  }

  private async readLoop(port: SerialPort): Promise<void> {
    while (port.readable && this.port === port) {
      const reader = port.readable.getReader();
      this.reader = reader;
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value && value.length) this.ev.onData(value);
        }
      } catch {
        // erro de leitura (ex.: desplugado). Se a porta ainda estiver aberta, tenta de novo.
      } finally {
        reader.releaseLock();
      }
    }
    if (this.port === port) await this.close('leitura encerrada');
  }

  private setState(s: LinkState, detail?: string): void {
    this.state = s;
    this.ev.onState(s, detail);
  }
}
