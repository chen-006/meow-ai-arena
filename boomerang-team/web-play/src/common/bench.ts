// 独立开源版关闭外站跨站查询，保留空实现以兼容前端模块接口。
export function modelKey(name: string): string {
  return name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
}

export async function fillBench(_root: ParentNode = document, _long = false): Promise<void> {
  return;
}
