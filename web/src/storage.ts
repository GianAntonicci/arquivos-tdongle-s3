// Destino dos arquivos: pasta escolhida (File System Access API) ou download comum.

// ---------- IndexedDB mínimo (chave/valor) ----------
const DB_NAME = 'tdongle-bridge';
const STORE = 'kv';

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function kvGet<T>(key: string): Promise<T | undefined> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const req = db.transaction(STORE, 'readonly').objectStore(STORE).get(key);
    req.onsuccess = () => resolve(req.result as T | undefined);
    req.onerror = () => reject(req.error);
  });
}

async function kvSet(key: string, value: unknown): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, 'readwrite');
    if (value === undefined) tx.objectStore(STORE).delete(key);
    else tx.objectStore(STORE).put(value, key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

// ---------- Estado do destino ----------

/**
 * folder:   pasta escolhida e com permissão de escrita
 * locked:   pasta lembrada, mas o Chrome pede um clique para liberar de novo
 * download: sem pasta: cada arquivo vira um download do navegador
 */
export type DestState = 'folder' | 'locked' | 'download';

export function folderApiSupported(): boolean {
  return 'showDirectoryPicker' in window;
}

export class Destination {
  private dir: FileSystemDirectoryHandle | null = null;
  state: DestState = 'download';

  get folderName(): string | null {
    return this.dir?.name ?? null;
  }

  /** Pronto para receber? (vai no HELLO para o dongle) */
  get ready(): boolean {
    return this.state !== 'locked';
  }

  /** Recupera a pasta salva no IndexedDB, sem pedir nada ao usuário. */
  async restore(): Promise<void> {
    if (!folderApiSupported()) return;
    const dir = await kvGet<FileSystemDirectoryHandle>('dir').catch(() => undefined);
    if (!dir) return;
    this.dir = dir;
    const perm = await dir.queryPermission({ mode: 'readwrite' });
    this.state = perm === 'granted' ? 'folder' : 'locked';
    if (this.state === 'folder') await this.cleanupLeftover();
  }

  /** Abre o seletor de pasta (precisa de clique). */
  async choose(): Promise<void> {
    const dir = await window.showDirectoryPicker({ id: 'tdongle-bridge', mode: 'readwrite' });
    this.dir = dir;
    this.state = 'folder';
    await kvSet('dir', dir);
  }

  /** Pede de novo a permissão da pasta lembrada (precisa de clique). */
  async unlock(): Promise<boolean> {
    if (!this.dir) return false;
    const perm = await this.dir.requestPermission({ mode: 'readwrite' });
    this.state = perm === 'granted' ? 'folder' : 'locked';
    if (this.state === 'folder') await this.cleanupLeftover();
    return this.state === 'folder';
  }

  /** Esquece a pasta e passa a usar downloads. */
  async forget(): Promise<void> {
    this.dir = null;
    this.state = 'download';
    await kvSet('dir', undefined);
  }

  /** Cria o destino de um arquivo novo, sem sobrescrever nada existente. */
  async createSink(name: string, size: number): Promise<FileSink> {
    const safe = sanitizeName(name);
    if (this.state === 'folder' && this.dir) {
      const finalName = await uniqueName(this.dir, safe);
      const handle = await this.dir.getFileHandle(finalName, { create: true });
      // O Chrome grava num arquivo temporário (.crswap) e só troca no close():
      // nada fica inteiro em memória.
      const writable = await handle.createWritable({ keepExistingData: false });
      // Marca como "em andamento": se a página fechar no meio, limpamos depois
      await kvSet('partial', finalName);
      return new FolderSink(this.dir, finalName, writable);
    }
    if (this.state === 'locked') throw new Error('Pasta sem permissão');
    return new DownloadSink(safe, size);
  }

  /**
   * Página fechada no meio de uma transferência deixa um arquivo vazio
   * (criado pelo getFileHandle). Na próxima abertura ele é removido.
   */
  private async cleanupLeftover(): Promise<void> {
    const partial = await kvGet<string>('partial').catch(() => undefined);
    if (!partial || !this.dir) return;
    try {
      const fh = await this.dir.getFileHandle(partial);
      const f = await fh.getFile();
      if (f.size === 0) await this.dir.removeEntry(partial);
    } catch { /* já não existe */ }
    await kvSet('partial', undefined);
  }
}

// ---------- Destinos de arquivo ----------

export interface FileSink {
  readonly finalName: string;
  write(chunk: Uint8Array): Promise<void>;
  /** Conclui com sucesso */
  close(): Promise<void>;
  /** Descarta: apaga o arquivo parcial */
  abort(): Promise<void>;
}

class FolderSink implements FileSink {
  constructor(
    private readonly dir: FileSystemDirectoryHandle,
    readonly finalName: string,
    private readonly writable: FileSystemWritableFileStream,
  ) {}

  async write(chunk: Uint8Array): Promise<void> {
    await this.writable.write(chunk as Uint8Array<ArrayBuffer>);
  }

  async close(): Promise<void> {
    await this.writable.close();
    await kvSet('partial', undefined);
  }

  async abort(): Promise<void> {
    try { await this.writable.abort(); } catch { /* já fechado */ }
    try { await this.dir.removeEntry(this.finalName); } catch { /* não existe */ }
    await kvSet('partial', undefined).catch(() => undefined);
  }
}

/**
 * Alternativa sem pasta: junta os pedaços num Blob e dispara um download.
 * O Chrome guarda Blobs grandes em disco, mas o arquivo só aparece no fim.
 */
class DownloadSink implements FileSink {
  private parts: BlobPart[] = [];

  constructor(readonly finalName: string, _size: number) {}

  async write(chunk: Uint8Array): Promise<void> {
    // Blob de cada pedaço: o Chrome pode descarregar para disco
    this.parts.push(new Blob([chunk as Uint8Array<ArrayBuffer>]));
  }

  async close(): Promise<void> {
    const url = URL.createObjectURL(new Blob(this.parts));
    this.parts = [];
    const a = document.createElement('a');
    a.href = url;
    a.download = this.finalName;
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
  }

  async abort(): Promise<void> {
    this.parts = [];
  }
}

// ---------- Nomes ----------

/** Remove caracteres proibidos no Windows/macOS/Linux, preservando acentos e espaços. */
export function sanitizeName(name: string): string {
  let n = name.normalize('NFC').replace(/[\\/:*?"<>|\u0000-\u001f]/g, '_').trim();
  n = n.replace(/[. ]+$/, ''); // Windows não aceita ponto/espaço no fim
  if (!n || n === '.' || n === '..') n = 'arquivo';
  return n.slice(0, 200);
}

/** "foto.jpg" -> "foto (1).jpg", "foto (2).jpg"... se já existir */
async function uniqueName(dir: FileSystemDirectoryHandle, name: string): Promise<string> {
  const dot = name.lastIndexOf('.');
  const base = dot > 0 ? name.slice(0, dot) : name;
  const ext = dot > 0 ? name.slice(dot) : '';
  for (let i = 0; i < 10_000; i++) {
    const candidate = i === 0 ? name : `${base} (${i})${ext}`;
    if (!(await exists(dir, candidate))) return candidate;
  }
  return `${base} (${Date.now()})${ext}`;
}

async function exists(dir: FileSystemDirectoryHandle, name: string): Promise<boolean> {
  try {
    await dir.getFileHandle(name);
    return true;
  } catch (err) {
    // TypeMismatchError: existe uma pasta com esse nome
    return (err as DOMException).name === 'TypeMismatchError';
  }
}
