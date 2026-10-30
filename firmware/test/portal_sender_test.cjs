// Executa a função real de envio da página com XMLHttpRequest simulado.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../include/web_portal_html.h'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const sender = script.slice(script.indexOf('function send('), script.indexOf("$('go').onclick"));
let request;
const context = vm.createContext({
  performance, encodeURIComponent, cur: null, fmt: String,
  XMLHttpRequest: class {
    constructor() { this.headers = {}; this.upload = {}; request = this; }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(key, value) { this.headers[key] = value; }
    send(file) { this.file = file; this.status = 200; this.responseText = '{"ok":true}'; this.onload(); }
  },
});
vm.runInContext(sender, context);
(async () => {
  const file = { name: 'férias & + #?.jpg', size: 4097 };
  const result = await context.send({ f: file }, 1, 2);
  assert(result.ok);
  assert.equal(request.method, 'POST');
  assert.equal(request.url, '/upload');
  assert.equal(request.file, file);
  assert.equal(request.headers['Content-Type'], 'application/octet-stream');
  const metadata = new URLSearchParams(request.headers['X-Upload-Metadata']);
  assert.equal(metadata.get('name'), file.name);
  assert.equal(metadata.get('size'), '4097');
  assert.equal(metadata.get('index'), '1');
  assert.equal(metadata.get('count'), '2');
  console.log('Envio real da página: metadados no cabeçalho e corpo cru OK');
})().catch(error => { console.error(error); process.exitCode = 1; });
