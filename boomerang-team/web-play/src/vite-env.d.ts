/// <reference types="vite/client" />
/** 各期生成物目录的内容哈希（vite.config.ts 注入；开发模式下是空对象） */
declare const __GEN_VER__: Record<string, string>;
/** 图片目录的内容哈希（开发模式下是空串） */
declare const __IMG_VER__: string;
interface ImportMetaEnv { readonly VITE_BENCH_SUMMARY_URL?: string }
/** 页面语言（vite.config.ts 按 --mode 注入：默认 'zh'，--mode en 时 'en'） */
declare const __LANG__: 'zh' | 'en';
