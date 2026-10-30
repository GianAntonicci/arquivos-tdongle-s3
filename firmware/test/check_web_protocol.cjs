// Decodifica uma transferência produzida pelo C++ usando o parser real da web.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { FrameParser, FrameType, crc32 } = require(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
let active = null, completed = 0;
const parser = new FrameParser((f) => {
  if (f.type === FrameType.Start) {
    assert.equal(active, null);
    active = { ...f, received: 0, crc: 0 };
    if (!completed) assert.equal(f.name, 'ação teste.bin');
  } else if (f.type === FrameType.Data) {
    assert.equal(f.id, active.id);
    assert.equal(f.offset, active.received);
    active.received += f.data.length;
    active.crc = crc32(active.crc, f.data);
  } else if (f.type === FrameType.End) {
    assert.equal(f.id, active.id);
    assert.equal(f.crc, active.crc);
    assert.equal(active.received, active.size);
    assert.equal(active.size, completed ? 0 : 20001);
    active = null;
    completed++;
  } else assert.equal(f.type, FrameType.HelloAck);
}, (why) => assert.fail(why));
for (let pos = 0; pos < bytes.length; pos += 7) parser.push(bytes.subarray(pos, pos + 7));
assert.equal(completed, 2);
assert.equal(active, null);
console.log('C++ → parser web: arquivo com acentos, chunks, CRC e arquivo vazio OK');
