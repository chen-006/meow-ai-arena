// 回放链接的编码，所有游戏通用。浏览器和 Node（构建脚本）都用这一份。
//
// 一局 = 一个头（JSON：游戏、种子、双方是谁……）+ 一串字节（逐回合的命令，怎么编由各游戏自己定）。
// 编码：UTF-8(JSON) + "\n" + 字节 → deflate-raw 压缩 → base64url（不带 =）。
// 头里必须有 v（格式版本）和 g（游戏名）。
const enc = new TextEncoder();
const dec = new TextDecoder();

async function pipe(bytes, stream) {
  const out = new Response(new Blob([bytes]).stream().pipeThrough(stream));
  return new Uint8Array(await out.arrayBuffer());
}

function toBase64Url(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function fromBase64Url(text) {
  const s = atob(text.replace(/-/g, '+').replace(/_/g, '/'));
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

/** @param {object} header @param {Uint8Array} body @returns {Promise<string>} */
export async function encodeReplay(header, body) {
  const head = enc.encode(JSON.stringify(header) + '\n');
  const all = new Uint8Array(head.length + body.length);
  all.set(head, 0);
  all.set(body, head.length);
  return toBase64Url(await pipe(all, new CompressionStream('deflate-raw')));
}

/** @param {string} code @returns {Promise<{header: any, body: Uint8Array}>} */
export async function decodeReplay(code) {
  const raw = await pipe(fromBase64Url(code.trim()), new DecompressionStream('deflate-raw'));
  const nl = raw.indexOf(10);
  if (nl < 0) throw Object.assign(new Error('回放数据不完整'), { code: 'replayIncomplete' });
  const header = JSON.parse(dec.decode(raw.subarray(0, nl)));
  if (!header || typeof header.v !== 'number' || typeof header.g !== 'string') throw Object.assign(new Error('不是本站的回放数据'), { code: 'replayForeign' });
  return { header, body: raw.subarray(nl + 1) };
}
