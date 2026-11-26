// 载入 Emscripten 编出来的模块（-sMODULARIZE -sEXPORT_ES6），主线程和 Worker 通用。
// 模块文件放在 public/gen/<期>/wasm/ 下，运行时按地址动态 import，不进打包，按需下载。

export interface EmModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  stringToNewUTF8(s: string): number;
  _free(ptr: number): void;
  [k: string]: any;
}

export function genUrl(path: string): string {
  // BASE_URL 以 / 结尾；gen 目录在站点根下。打包后每期的目录带内容哈希（gen/ep6.1a2b3c4d/…，见 scripts/gen-version.mjs），可以长缓存
  const i = path.indexOf('/'), ep = i < 0 ? path : path.slice(0, i), v = __GEN_VER__[ep];
  const p = v ? `${ep}.${v}${i < 0 ? '' : path.slice(i)}` : path;
  return new URL(import.meta.env.BASE_URL + 'gen/' + p, location.href).href;
}

export async function loadModule(url: string, extra: Record<string, unknown> = {}): Promise<EmModule> {
  const mod = await import(/* @vite-ignore */ url);
  return (await mod.default({ ...extra })) as EmModule;
}
