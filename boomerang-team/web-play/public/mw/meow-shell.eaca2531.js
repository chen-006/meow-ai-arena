/* meow-shell.js v1 · 主题三态（跟随系统/浅/深）+ 站点切换菜单收起。
   用法：<head> 里、meow.css 之后，同步引入（不要 defer/async），这样首帧前就设好主题，不闪白：
     <link rel="stylesheet" href="/meow.css"><script src="/meow-shell.js"></script>
   外链文件，CSP script-src 'self' 下可用，不需要内联脚本。没有 JS 时：主题跟随系统，主题按钮隐藏，切换菜单（<details>）照常可用。
   四站共享偏好：写 cookie mw_theme（Domain=.meowllm.top; Path=/; Max-Age=1年; SameSite=Lax; Secure），读取时 cookie 优先、
   localStorage('mw_theme') 兜底；localhost / 其他域名上不带 Domain、http 下不带 Secure，自动降级为本站 cookie + localStorage。
   兼容旧键：检测器 localStorage('meow-theme')、arena 的 'arena-theme'（auto 视为跟随系统），首次读取时沿用。
   主题变化时在 document 上派发 'mw:theme' 事件（detail: {pref, theme}），canvas 类页面可据此重绘。 */
(function (d, w) {
  var K = 'mw_theme', r = d.documentElement, mq = w.matchMedia('(prefers-color-scheme: dark)');
  var NEXT = { system: 'light', light: 'dark', dark: 'system' }, NAME = { system: '跟随系统', light: '浅色', dark: '深色' };
  function read() {
    var m = d.cookie.match(/(?:^|; )mw_theme=(\w+)/);
    if (m) return m[1];
    try { return localStorage.getItem(K) || localStorage.getItem('meow-theme') || localStorage.getItem('arena-theme'); } catch (e) { return null; }
  }
  function apply(p) {
    if (p !== 'light' && p !== 'dark') p = 'system';
    r.setAttribute('data-mw-pref', p);
    if (p === 'system') r.removeAttribute('data-theme'); else r.setAttribute('data-theme', p);
    return p;
  }
  function save(p) {
    try { localStorage.setItem(K, p); } catch (e) {}
    d.cookie = K + '=' + p + ';path=/;max-age=31536000;SameSite=Lax' +
      (/(^|\.)meowllm\.top$/.test(location.hostname) ? ';domain=.meowllm.top' : '') +
      (location.protocol === 'https:' ? ';Secure' : '');
  }
  function theme() { return r.getAttribute('data-theme') || (mq.matches ? 'dark' : 'light'); }
  function sync() {
    var p = r.getAttribute('data-mw-pref'), t = '主题：' + NAME[p] + '，点击切换';
    d.querySelectorAll('[data-mw-theme]').forEach(function (b) { b.setAttribute('aria-label', t); b.title = t; });
    d.dispatchEvent(new CustomEvent('mw:theme', { detail: { pref: p, theme: theme() } }));
  }
  function set(p) { save(apply(p)); sync(); }
  function closeMenus(except) {
    d.querySelectorAll('details.mw-switch[open]').forEach(function (x) { if (x !== except) x.open = false; });
  }

  apply(read());
  r.classList.add('mw-js');

  d.addEventListener('click', function (e) {
    var b = e.target.closest && e.target.closest('[data-mw-theme]');
    if (b) set(NEXT[r.getAttribute('data-mw-pref')]);
    closeMenus(e.target.closest && e.target.closest('details.mw-switch'));
  });
  d.addEventListener('keydown', function (e) {
    if (e.key !== 'Escape') return;
    var o = d.querySelector('details.mw-switch[open]');
    if (o) { o.open = false; o.querySelector('summary').focus(); }
  });
  d.addEventListener('DOMContentLoaded', sync);
  mq.addEventListener && mq.addEventListener('change', sync);
  /* 别的标签页或别的子站改了主题：回到本页时跟上 */
  w.addEventListener('storage', function (e) { if (e.key === K) { apply(e.newValue); sync(); } });
  d.addEventListener('visibilitychange', function () {
    if (!d.hidden) { var p = read() || 'system'; if (p !== r.getAttribute('data-mw-pref')) { apply(p); sync(); } }
  });
  w.MeowShell = { set: set, theme: theme };
})(document, window);
