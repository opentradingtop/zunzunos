// DOM del navegador con JavaScript de ZunzunOS. Se ejecuta antes que los scripts de la pagina, sobre las operaciones
// basicas de dom.c (objeto __z, que despues se esconde). Aqui: Node/Element/Document, eventos, selectores CSS,
// classList, dataset, style, formularios, location, history, localStorage, fetch, XMLHttpRequest, URL, temporizadores
// de animacion, atob/btoa, TextEncoder y algunos observadores sencillos.
(function () {
'use strict';
const Z = globalThis.__z; delete globalThis.__z;
const W = globalThis;
const def = (o, props) => { for (const k of Object.keys(props)) { const d = Object.getOwnPropertyDescriptor(props, k); d.enumerable = false; d.configurable = true; Object.defineProperty(o, k, d); } };

// ---------------- eventos ----------------
class Event {
  constructor(type, init = {}) { this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.timeStamp = Z.now() - T0; this.isTrusted = false; this._stop = false; this._now = false; }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; } stopImmediatePropagation() { this._stop = this._now = true; }
  get returnValue() { return !this.defaultPrevented; } set returnValue(v) { if (!v) this.preventDefault(); }
  get srcElement() { return this.target; } composedPath() { return this._path || []; }
}
class CustomEvent extends Event { constructor(t, i = {}) { super(t, i); this.detail = i.detail === undefined ? null : i.detail; } }
class UIEvent extends Event { constructor(t, i = {}) { super(t, i); this.detail = i.detail || 0; this.view = W; } }
class MouseEvent extends UIEvent { constructor(t, i = {}) { super(t, i); for (const k of ['clientX', 'clientY', 'pageX', 'pageY', 'screenX', 'screenY', 'offsetX', 'offsetY', 'button', 'buttons']) this[k] = i[k] || 0; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; this.relatedTarget = i.relatedTarget || null; } }
class PointerEvent extends MouseEvent {}
class KeyboardEvent extends UIEvent { constructor(t, i = {}) { super(t, i); this.key = i.key || ''; this.code = i.code || ''; this.keyCode = this.which = i.keyCode || 0; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; this.repeat = !!i.repeat; } }
class InputEvent extends UIEvent { constructor(t, i = {}) { super(t, i); this.data = i.data === undefined ? null : i.data; this.inputType = i.inputType || ''; } }
class FocusEvent extends UIEvent {}
class SubmitEvent extends Event { constructor(t, i = {}) { super(t, i); this.submitter = i.submitter || null; } }
class ErrorEvent extends Event { constructor(t, i = {}) { super(t, i); this.message = i.message || ''; this.error = i.error || null; } }
const LIS = new WeakMap(), ONH = new WeakMap();
const lis = (t, type) => { let m = LIS.get(t); if (!m) LIS.set(t, m = {}); return m[type] || (m[type] = []); };
class EventTarget {
  addEventListener(type, fn, o) { if (!fn) return; const cap = typeof o === 'boolean' ? o : !!(o && o.capture), once = !!(o && o.once); const L = lis(this, type); if (!L.some(l => l.fn === fn && l.cap === cap)) L.push({ fn, cap, once }); if (o && o.signal) o.signal.addEventListener('abort', () => this.removeEventListener(type, fn, o)); }
  removeEventListener(type, fn, o) { const cap = typeof o === 'boolean' ? o : !!(o && o.capture); const L = lis(this, type); const i = L.findIndex(l => l.fn === fn && l.cap === cap); if (i >= 0) L.splice(i, 1); }
  dispatchEvent(ev) { return dispatch(this, ev); }
}
function handlerFor(t, type) {   // propiedad onclick o atributo onclick="..."
  const m = ONH.get(t); if (m && type in m) return m[type];
  const src = t instanceof Element ? t : t === W && /^(load|unload|beforeunload|resize|scroll|hashchange|popstate|message)$/.test(type) ? document.body : null;   // <body onload> es de la ventana
  if (src) { const code = src.getAttribute('on' + type); if (code != null) { try { const f = new Function('event', code); (ONH.get(t) || (ONH.set(t, {}), ONH.get(t)))[type] = f; return f; } catch (e) { report(e); } } }
  return null;
}
// mode: 1 solo los de captura, 2 todos (en el destino), 3 solo los de burbuja; el manejador on... va con los de burbuja
function callL(t, ev, mode) {
  const L = (LIS.get(t) || {})[ev.type]; if (L) for (const l of L.slice()) { if ((mode === 1 && !l.cap) || (mode === 3 && l.cap)) continue; if (l.once) t.removeEventListener(ev.type, l.fn, l.cap); try { typeof l.fn === 'function' ? l.fn.call(t, ev) : l.fn.handleEvent(ev); } catch (e) { report(e); } if (ev._now) return; }
  if (mode !== 1) { const h = handlerFor(t, ev.type); if (h) { try { const r = h.call(t, ev); if (r === false) ev.preventDefault(); } catch (e) { report(e); } } }
}
function dispatch(target, ev) {
  ev.target = target; const path = [];
  for (let n = target; n; n = n.parentNode) path.push(n);
  if (path[path.length - 1] === document) path.push(W);
  ev._path = path;
  for (let i = path.length - 1; i > 0 && !ev._stop; i--) { ev.eventPhase = 1; ev.currentTarget = path[i]; callL(path[i], ev, 1); }
  if (!ev._stop) { ev.eventPhase = 2; ev.currentTarget = target; callL(target, ev, 2); }
  if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) { ev.eventPhase = 3; ev.currentTarget = path[i]; callL(path[i], ev, 3); }
  ev.eventPhase = 0; ev.currentTarget = null; return !ev.defaultPrevented;
}
function report(e) { console.error(e && e.stack ? String(e) + '\n' + e.stack : String(e)); }
const ONE = ['click', 'dblclick', 'mousedown', 'mouseup', 'mouseover', 'mouseout', 'mousemove', 'mouseenter', 'mouseleave', 'contextmenu', 'input', 'change', 'submit', 'reset', 'keydown', 'keyup', 'keypress', 'focus', 'blur', 'load', 'error', 'scroll', 'resize', 'select', 'wheel', 'pointerdown', 'pointerup', 'touchstart', 'touchend', 'animationend', 'transitionend', 'beforeunload', 'unload', 'hashchange', 'popstate', 'message', 'toggle', 'invalid'];
function onProps(proto) { for (const t of ONE) Object.defineProperty(proto, 'on' + t, { configurable: true, get() { const m = ONH.get(this); return m && m[t] || null; }, set(f) { let m = ONH.get(this); if (!m) ONH.set(this, m = {}); m[t] = typeof f === 'function' ? f : null; } }); }

// ---------------- selectores CSS ----------------
const SC = new Map();
function parseSel(s) {
  if (SC.has(s)) return SC.get(s);
  const groups = []; let i = 0; const n = s.length;
  const ws = () => { while (i < n && /\s/.test(s[i])) i++; };
  const ident = () => { let r = ''; while (i < n && /[\w\- -￿\\]/.test(s[i])) { if (s[i] === '\\') { i++; } r += s[i++]; } return r; };
  const str = () => { const q = s[i++]; let r = ''; while (i < n && s[i] !== q) { if (s[i] === '\\') i++; r += s[i++]; } i++; return r; };
  let seq = [], comb = null;
  const compound = () => {
    const c = { tag: null, id: null, cls: [], attrs: [], ps: [] };
    if (s[i] === '*') { i++; } else if (/[\w-]/.test(s[i] || '')) c.tag = ident().toUpperCase();
    for (;;) {
      if (s[i] === '#') { i++; c.id = ident(); }
      else if (s[i] === '.') { i++; c.cls.push(ident()); }
      else if (s[i] === '[') { i++; ws(); const name = ident().toLowerCase(); ws(); let op = null, val = null, ci = false;
        if (s[i] !== ']') { op = s[i] === '=' ? '=' : s.substr(i, 2); i += op.length; ws(); val = s[i] === '"' || s[i] === "'" ? str() : ident(); ws(); if (/[iI]/.test(s[i])) { ci = true; i++; ws(); } }
        i++; c.attrs.push({ name, op, val, ci }); }
      else if (s[i] === ':') { i++; if (s[i] === ':') i++; const name = ident().toLowerCase(); let arg = null; if (s[i] === '(') { let d = 1, j = ++i; while (j < n && d) { if (s[j] === '(') d++; else if (s[j] === ')') d--; j++; } arg = s.slice(i, j - 1).trim(); i = j; } c.ps.push({ name, arg }); }
      else break;
    }
    return c;
  };
  ws();
  while (i < n) {
    const c = compound(); seq.push({ c, comb }); comb = null;
    const before = i; ws();
    if (i >= n) break;
    if (s[i] === ',') { i++; ws(); groups.push(seq); seq = []; continue; }
    if ('>+~'.includes(s[i])) { comb = s[i++]; ws(); } else if (i > before) comb = ' '; else throw new SyntaxError("'" + s + "' is not a valid selector");
  }
  if (seq.length) groups.push(seq);
  if (!groups.length) throw new SyntaxError("'" + s + "' is not a valid selector");
  SC.set(s, groups); return groups;
}
function nthMatch(arg, idx) {   // idx empieza en 1
  arg = arg.replace(/\s/g, '').toLowerCase(); if (arg === 'odd') return idx % 2 === 1; if (arg === 'even') return idx % 2 === 0;
  const m = /^([+-]?\d*)n([+-]\d+)?$/.exec(arg); if (!m) return idx === parseInt(arg, 10);
  const a = m[1] === '' || m[1] === '+' ? 1 : m[1] === '-' ? -1 : parseInt(m[1], 10), b = m[2] ? parseInt(m[2], 10) : 0;
  return a === 0 ? idx === b : (idx - b) / a >= 0 && (idx - b) % a === 0;
}
function matchC(el, c) {
  if (c.tag && el.tagName !== c.tag) return false;
  if (c.id !== null && el.id !== c.id) return false;
  if (c.cls.length) { const k = ' ' + (el.className || '').replace(/\s+/g, ' ') + ' '; for (const x of c.cls) if (!k.includes(' ' + x + ' ')) return false; }
  for (const a of c.attrs) { let v = el.getAttribute(a.name); if (v === null) return false; if (!a.op) continue; let t = a.val; if (a.ci) { v = v.toLowerCase(); t = t.toLowerCase(); }
    if (a.op === '=' ? v !== t : a.op === '~=' ? !v.split(/\s+/).includes(t) : a.op === '^=' ? !v.startsWith(t) : a.op === '$=' ? !v.endsWith(t) : a.op === '*=' ? !v.includes(t) : a.op === '|=' ? !(v === t || v.startsWith(t + '-')) : true) return false; }
  for (const p of c.ps) {
    const sib = () => { const P = el.parentNode; return P ? P.children : [el]; };
    switch (p.name) {
      case 'first-child': if (el.previousElementSibling) return false; break;
      case 'last-child': if (el.nextElementSibling) return false; break;
      case 'only-child': if (el.previousElementSibling || el.nextElementSibling) return false; break;
      case 'nth-child': if (!nthMatch(p.arg, sib().indexOf(el) + 1)) return false; break;
      case 'nth-last-child': { const s = sib(); if (!nthMatch(p.arg, s.length - s.indexOf(el))) return false; break; }
      case 'first-of-type': if (sib().find(x => x.tagName === el.tagName) !== el) return false; break;
      case 'last-of-type': { const s = sib().filter(x => x.tagName === el.tagName); if (s[s.length - 1] !== el) return false; break; }
      case 'nth-of-type': if (!nthMatch(p.arg, sib().filter(x => x.tagName === el.tagName).indexOf(el) + 1)) return false; break;
      case 'not': if (el.matches(p.arg)) return false; break;
      case 'is': case 'where': case 'matches': if (!el.matches(p.arg)) return false; break;
      case 'has': if (!el.querySelector(p.arg.replace(/^>\s*/, ':scope > '))) return false; break;
      case 'checked': if (!el.checked) return false; break;
      case 'disabled': if (!el.hasAttribute('disabled')) return false; break;
      case 'enabled': if (el.hasAttribute('disabled')) return false; break;
      case 'empty': if (el.firstChild) return false; break;
      case 'root': if (el !== document.documentElement) return false; break;
      case 'focus': if (document.activeElement !== el) return false; break;
      case 'scope': if (el !== SCOPE) return false; break;
      case 'hover': case 'active': case 'visited': case 'focus-within': case 'focus-visible': return false;
      case 'link': case 'any-link': if (!(el.tagName === 'A' && el.hasAttribute('href'))) return false; break;
      case 'required': if (!el.hasAttribute('required')) return false; break;
      case 'placeholder-shown': if (!(el.hasAttribute('placeholder') && !el.value)) return false; break;
      default: break;   // pseudo-elementos y lo que no conocemos: no restringen
    }
  }
  return true;
}
function matchSeq(el, seq, k) {
  if (!matchC(el, seq[k].c)) return false; if (k === 0) return true;
  const comb = seq[k].comb;
  if (comb === '>') { const p = el.parentElement; return !!p && matchSeq(p, seq, k - 1); }
  if (comb === '+') { const p = el.previousElementSibling; return !!p && matchSeq(p, seq, k - 1); }
  if (comb === '~') { for (let p = el.previousElementSibling; p; p = p.previousElementSibling) if (matchSeq(p, seq, k - 1)) return true; return false; }
  for (let p = el.parentElement; p; p = p.parentElement) if (matchSeq(p, seq, k - 1)) return true; return false;
}
let SCOPE = null;
const matches = (el, s) => parseSel(s).some(seq => matchSeq(el, seq, seq.length - 1));
function qsa(root, s, one) { const g = parseSel(s); const prev = SCOPE; SCOPE = root; try { const r = []; for (const el of Z.all(root)) if (g.some(seq => matchSeq(el, seq, seq.length - 1))) { if (one) return el; r.push(el); } return one ? null : r; } finally { SCOPE = prev; } }

// ---------------- nodos ----------------
const frag = new WeakSet();
const toNode = x => x instanceof Node ? x : document.createTextNode(String(x));
function insertAll(parent, nodes, ref) { for (const x of nodes) { const n = toNode(x); if (frag.has(n)) { for (const c of n.childNodes) Z.insert(parent, c, ref || null); } else Z.insert(parent, n, ref || null); } }
const list = a => { a.item = i => a[i] || null; return a; };
class Node extends EventTarget {
  get nodeType() { return Z.type(this); }
  get nodeName() { const t = Z.type(this); return t === 3 ? '#text' : t === 9 ? '#document' : frag.has(this) ? '#document-fragment' : Z.tag(this); }
  get parentNode() { return Z.parent(this); }
  get parentElement() { const p = Z.parent(this); return p && Z.type(p) === 1 ? p : null; }
  get firstChild() { return Z.first(this); } get lastChild() { return Z.last(this); }
  get nextSibling() { return Z.next(this); } get previousSibling() { return Z.prev(this); }
  get childNodes() { const r = []; for (let c = Z.first(this); c; c = Z.next(c)) r.push(c); return list(r); }
  get ownerDocument() { return Z.type(this) === 9 ? null : document; }
  get isConnected() { let n = this; while (Z.parent(n)) n = Z.parent(n); return n === document; }
  getRootNode() { let n = this; while (Z.parent(n)) n = Z.parent(n); return n; }
  get textContent() { return Z.type(this) === 9 ? null : Z.text(this); } set textContent(v) { Z.setText(this, v == null ? '' : String(v)); }
  get nodeValue() { return Z.type(this) === 3 ? Z.text(this) : null; } set nodeValue(v) { if (Z.type(this) === 3) Z.setText(this, String(v)); }
  hasChildNodes() { return !!Z.first(this); }
  contains(o) { for (let n = o; n; n = Z.parent(n)) if (n === this) return true; return false; }
  appendChild(c) { if (frag.has(c)) { insertAll(this, [c]); return c; } Z.append(this, c); return c; }
  insertBefore(c, ref) { if (frag.has(c)) { insertAll(this, [c], ref); return c; } Z.insert(this, c, ref || null); return c; }
  removeChild(c) { if (Z.parent(c) !== this) throw new Error('NotFoundError: el nodo no es hijo de este'); Z.remove(c); return c; }
  replaceChild(n, o) { this.insertBefore(n, o); Z.remove(o); return o; }
  cloneNode(deep) { const c = Z.clone(this, !!deep); if (frag.has(this)) frag.add(c); return c; }
  isEqualNode(o) { return !!o && this.nodeName === o.nodeName && (Z.type(this) === 3 ? this.data === o.data : this.outerHTML === o.outerHTML); }
  isSameNode(o) { return this === o; }
  compareDocumentPosition(o) { if (this === o) return 0; const all = Z.all(document); const a = all.indexOf(this), b = all.indexOf(o); return this.contains(o) ? 20 : o.contains(this) ? 10 : a < b ? 4 : 2; }
  normalize() {}
  remove() { if (Z.parent(this)) Z.remove(this); }
  before(...n) { const p = Z.parent(this); if (p) insertAll(p, n, this); }
  after(...n) { const p = Z.parent(this); if (p) insertAll(p, n, Z.next(this)); }
  replaceWith(...n) { const p = Z.parent(this); if (p) { insertAll(p, n, this); Z.remove(this); } }
}
def(Node, { ELEMENT_NODE: 1, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_FRAGMENT_NODE: 11 });
class CharacterData extends Node {
  get data() { return Z.text(this); } set data(v) { Z.setText(this, String(v)); }
  get length() { return this.data.length; } get wholeText() { return this.data; }
  appendData(s) { this.data += s; } get nextElementSibling() { return nextEl(this); } get previousElementSibling() { return prevEl(this); }
}
class Text extends CharacterData { splitText(o) { const t = this.data; this.data = t.slice(0, o); const n = document.createTextNode(t.slice(o)); this.after(n); return n; } }
class Comment extends CharacterData {}
const nextEl = n => { for (let x = Z.next(n); x; x = Z.next(x)) if (Z.type(x) === 1) return x; return null; };
const prevEl = n => { for (let x = Z.prev(n); x; x = Z.prev(x)) if (Z.type(x) === 1) return x; return null; };
const ParentMixin = {
  get children() { const r = []; for (let c = Z.first(this); c; c = Z.next(c)) if (Z.type(c) === 1) r.push(c); r.namedItem = k => r.find(e => e.id === k || e.getAttribute('name') === k) || null; return list(r); },
  get firstElementChild() { for (let c = Z.first(this); c; c = Z.next(c)) if (Z.type(c) === 1) return c; return null; },
  get lastElementChild() { for (let c = Z.last(this); c; c = Z.prev(c)) if (Z.type(c) === 1) return c; return null; },
  get childElementCount() { return this.children.length; },
  append(...n) { insertAll(this, n, null); }, prepend(...n) { insertAll(this, n, Z.first(this)); },
  replaceChildren(...n) { while (Z.first(this)) Z.remove(Z.first(this)); insertAll(this, n, null); },
  querySelector(s) { return qsa(this, String(s), true); }, querySelectorAll(s) { return list(qsa(this, String(s), false)); },
  getElementsByTagName(t) { t = String(t).toUpperCase(); return list(Z.all(this).filter(e => t === '*' || e.tagName === t)); },
  getElementsByClassName(c) { const cs = String(c).trim().split(/\s+/); return list(Z.all(this).filter(e => cs.every(x => e.classList.contains(x)))); },
  getElementsByName(n) { return list(Z.all(this).filter(e => e.getAttribute('name') === n)); },
};

// ---------------- classList, dataset, style ----------------
class DOMTokenList {
  constructor(el, attr) { this._el = el; this._a = attr; }
  _get() { return (this._el.getAttribute(this._a) || '').split(/\s+/).filter(Boolean); } _set(l) { this._el.setAttribute(this._a, l.join(' ')); }
  get length() { return this._get().length; } get value() { return this._el.getAttribute(this._a) || ''; } set value(v) { this._el.setAttribute(this._a, v); }
  item(i) { return this._get()[i] || null; } contains(t) { return this._get().includes(t); }
  add(...t) { const l = this._get(); for (const x of t) if (!l.includes(x)) l.push(x); this._set(l); }
  remove(...t) { this._set(this._get().filter(x => !t.includes(x))); }
  toggle(t, f) { const has = this.contains(t); if (f === undefined ? has : !f) { this.remove(t); return false; } this.add(t); return true; }
  replace(a, b) { const l = this._get(); const i = l.indexOf(a); if (i < 0) return false; l[i] = b; this._set(l); return true; }
  forEach(f, t) { this._get().forEach(f, t); } [Symbol.iterator]() { return this._get()[Symbol.iterator](); } toString() { return this.value; }
  entries() { return this._get().entries(); } keys() { return this._get().keys(); } values() { return this._get().values(); }
}
const camel = s => s.replace(/-([a-z])/g, (_, c) => c.toUpperCase()), kebab = s => s.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
function dataset(el) { return new Proxy({}, {
  get: (t, k) => typeof k === 'string' ? (el.getAttribute('data-' + kebab(k)) ?? undefined) : undefined,
  set: (t, k, v) => { el.setAttribute('data-' + kebab(k), String(v)); return true; },
  deleteProperty: (t, k) => { el.removeAttribute('data-' + kebab(k)); return true; },
  has: (t, k) => el.hasAttribute('data-' + kebab(k)),
  ownKeys: () => el.getAttributeNames().filter(n => n.startsWith('data-')).map(n => camel(n.slice(5))),
  getOwnPropertyDescriptor: (t, k) => el.hasAttribute('data-' + kebab(k)) ? { value: el.getAttribute('data-' + kebab(k)), enumerable: true, configurable: true, writable: true } : undefined }); }
function parseStyle(s) { const m = new Map(); for (const part of (s || '').split(';')) { const i = part.indexOf(':'); if (i > 0) m.set(part.slice(0, i).trim().toLowerCase(), part.slice(i + 1).trim()); } return m; }
const STYLE_FNS = {
  getPropertyValue(el, p) { return parseStyle(el.getAttribute('style')).get(p) || ''; },
  setProperty(el, p, v) { const m = parseStyle(el.getAttribute('style')); if (v === null || v === '' || v === undefined) m.delete(p); else m.set(p, String(v)); el.setAttribute('style', [...m].map(([k, x]) => k + ': ' + x).join('; ')); },
  removeProperty(el, p) { const old = STYLE_FNS.getPropertyValue(el, p); STYLE_FNS.setProperty(el, p, ''); return old; },
};
function styleOf(el) { return new Proxy({}, {
  get(t, k) { if (typeof k !== 'string') return undefined; if (k === 'cssText') return el.getAttribute('style') || ''; if (k === 'length') return parseStyle(el.getAttribute('style')).size;
    if (k in STYLE_FNS) return (...a) => STYLE_FNS[k](el, ...a); return parseStyle(el.getAttribute('style')).get(k.startsWith('--') ? k : kebab(k)) || ''; },
  set(t, k, v) { if (k === 'cssText') el.setAttribute('style', String(v)); else STYLE_FNS.setProperty(el, k.startsWith('--') ? k : kebab(k), v === null ? '' : String(v)); return true; } }); }

// ---------------- elementos ----------------
const CL = new WeakMap(), DS = new WeakMap(), ST = new WeakMap();
class Element extends Node {
  get tagName() { return Z.tag(this); } get localName() { return Z.tag(this).toLowerCase(); } get namespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
  get id() { return Z.attr(this, 'id') || ''; } set id(v) { Z.setAttr(this, 'id', String(v)); }
  get className() { return Z.attr(this, 'class') || ''; } set className(v) { Z.setAttr(this, 'class', String(v)); }
  get classList() { let l = CL.get(this); if (!l) CL.set(this, l = new DOMTokenList(this, 'class')); return l; } set classList(v) { this.className = v; }
  get dataset() { let d = DS.get(this); if (!d) DS.set(this, d = dataset(this)); return d; }
  get style() { let s = ST.get(this); if (!s) ST.set(this, s = styleOf(this)); return s; } set style(v) { Z.setAttr(this, 'style', String(v)); }
  getAttribute(n) { return Z.attr(this, String(n)); } setAttribute(n, v) { Z.setAttr(this, String(n), String(v)); const k = String(n).toLowerCase(); if (k.startsWith('on')) { const m = ONH.get(this); if (m) delete m[k.slice(2)]; } }
  removeAttribute(n) { Z.rmAttr(this, String(n)); } hasAttribute(n) { return Z.attr(this, String(n)) !== null; } hasAttributes() { return Z.attrs(this).length > 0; }
  toggleAttribute(n, f) { const h = this.hasAttribute(n); if (f === undefined ? h : !f) { this.removeAttribute(n); return false; } if (!h) this.setAttribute(n, ''); return true; }
  getAttributeNames() { return Z.attrs(this).map(a => a[0]); }
  get attributes() { const a = Z.attrs(this).map(([name, value]) => ({ name, value, localName: name, nodeName: name })); a.getNamedItem = n => a.find(x => x.name === n) || null; return list(a); }
  getAttributeNS(ns, n) { return this.getAttribute(n); } setAttributeNS(ns, n, v) { this.setAttribute(n, v); }
  get innerHTML() { return Z.html(this, false); } set innerHTML(v) { Z.setHtml(this, String(v)); }
  get outerHTML() { return Z.html(this, true); } set outerHTML(v) { const t = document.createElement('div'); t.innerHTML = v; this.replaceWith(...t.childNodes); }
  get innerText() { return this.textContent; } set innerText(v) { this.textContent = v; }
  get outerText() { return this.textContent; }
  get hidden() { return this.hasAttribute('hidden'); } set hidden(v) { this.toggleAttribute('hidden', !!v); }
  get title() { return this.getAttribute('title') || ''; } set title(v) { this.setAttribute('title', v); }
  get lang() { return this.getAttribute('lang') || ''; } get dir() { return this.getAttribute('dir') || ''; }
  get tabIndex() { return +(this.getAttribute('tabindex') || -1); } set tabIndex(v) { this.setAttribute('tabindex', v); }
  get nextElementSibling() { return nextEl(this); } get previousElementSibling() { return prevEl(this); }
  matches(s) { return matches(this, String(s)); } webkitMatchesSelector(s) { return this.matches(s); }
  closest(s) { for (let e = this; e && Z.type(e) === 1; e = Z.parent(e)) if (e.matches(s)) return e; return null; }
  getBoundingClientRect() { const [x, y, w, h] = Z.rect(this), v = Z.view(); const top = y - v[2]; return { x, y: top, left: x, top, width: w, height: h, right: x + w, bottom: top + h, toJSON() { return this; } }; }
  getClientRects() { return [this.getBoundingClientRect()]; }
  get offsetWidth() { return Z.rect(this)[2]; } get offsetHeight() { return Z.rect(this)[3]; } get offsetLeft() { return Z.rect(this)[0]; } get offsetTop() { return Z.rect(this)[1]; }
  get clientWidth() { return Z.rect(this)[2]; } get clientHeight() { return Z.rect(this)[3]; } get scrollWidth() { return Z.rect(this)[2]; } get scrollHeight() { return Z.rect(this)[3]; }
  get offsetParent() { return this.parentElement; } get scrollTop() { return 0; } set scrollTop(v) {} get scrollLeft() { return 0; } set scrollLeft(v) {}
  scrollIntoView() { Z.scroll(Z.rect(this)[1]); } scrollTo() {} scroll() {} scrollBy() {}
  insertAdjacentHTML(pos, html) { const t = document.createElement('div'); t.innerHTML = html; this.insertAdjacentNodes(pos, t.childNodes); }
  insertAdjacentElement(pos, el) { this.insertAdjacentNodes(pos, [el]); return el; } insertAdjacentText(pos, s) { this.insertAdjacentNodes(pos, [document.createTextNode(s)]); }
  insertAdjacentNodes(pos, nodes) { pos = String(pos).toLowerCase(); const p = this.parentNode;
    if (pos === 'beforebegin') insertAll(p, nodes, this); else if (pos === 'afterend') insertAll(p, nodes, Z.next(this)); else if (pos === 'afterbegin') insertAll(this, nodes, Z.first(this)); else insertAll(this, nodes, null); }
  attachShadow() { return this; } animate() { return { finished: Promise.resolve(), cancel() {}, onfinish: null }; }
  setPointerCapture() {} releasePointerCapture() {}
}
Object.defineProperties(Element.prototype, Object.getOwnPropertyDescriptors(ParentMixin));
const reflect = (name, attr = name.toLowerCase(), bool = false) => Object.defineProperty(HTMLElement.prototype, name, { configurable: true,
  get() { const v = this.getAttribute(attr); return bool ? v !== null : v === null ? '' : v; }, set(v) { if (bool) this.toggleAttribute(attr, !!v); else this.setAttribute(attr, String(v)); } });
class HTMLElement extends Element {
  click() { const ev = new MouseEvent('click', { bubbles: true, cancelable: true }); if (!dispatch(this, ev)) return; defaultAction(this); }
  focus() { Z.focus(this); dispatch(this, new FocusEvent('focus')); } blur() { if (Z.active() === this) { Z.focus(null); dispatch(this, new FocusEvent('blur')); } }
  get value() { return Z.value(this); } set value(v) { Z.setValue(this, v == null ? '' : String(v)); }
  get checked() { return Z.checked(this); } set checked(v) { Z.setChecked(this, !!v); }
  get defaultValue() { return this.getAttribute('value') || ''; } set defaultValue(v) { this.setAttribute('value', v); }
  get selectedIndex() { return this.tagName === 'SELECT' ? Z.sel(this) : -1; } set selectedIndex(i) { Z.setSel(this, i); }
  get options() { const o = Z.all(this).filter(e => e.tagName === 'OPTION'); o.selectedIndex = this.selectedIndex; return list(o); }
  get selectedOptions() { const o = this.options[this.selectedIndex]; return list(o ? [o] : []); }
  get selected() { const s = this.closest('select'); return s ? s.options[s.selectedIndex] === this : this.hasAttribute('selected'); } set selected(v) { const s = this.closest('select'); if (s && v) s.selectedIndex = s.options.indexOf(this); }
  get text() { return this.textContent; } set text(v) { this.textContent = v; }
  get index() { const s = this.closest('select'); return s ? s.options.indexOf(this) : 0; }
  get form() { return this.closest('form'); }
  get elements() { const e = Z.all(this).filter(x => /^(INPUT|SELECT|TEXTAREA|BUTTON|FIELDSET)$/.test(x.tagName)); e.namedItem = n => e.find(x => x.name === n || x.id === n) || null; for (const x of e) { if (x.name && !(x.name in e)) e[x.name] = x; } return list(e); }
  get length() { return this.tagName === 'FORM' ? this.elements.length : this.tagName === 'SELECT' ? this.options.length : 0; }
  submit() { Z.submit(this); } requestSubmit(b) { if (dispatch(this, new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: b || null }))) Z.submit(this); }
  reset() { for (const e of this.elements) { if ('defaultValue' in e && /INPUT|TEXTAREA/.test(e.tagName) && !/checkbox|radio/.test(e.type)) e.value = e.defaultValue; } dispatch(this, new Event('reset', { bubbles: true })); }
  checkValidity() { return true; } reportValidity() { return true; } setCustomValidity() {} get validity() { return { valid: true }; } get validationMessage() { return ''; }
  select() {} setSelectionRange() {}
  get href() { const h = this.getAttribute('href'); return h === null ? '' : new URL(h, location.href).href; } set href(v) { this.setAttribute('href', v); }
  get src() { const h = this.getAttribute('src'); return h === null ? '' : new URL(h, location.href).href; } set src(v) { this.setAttribute('src', v); }
  get action() { return new URL(this.getAttribute('action') || '', location.href).href; } set action(v) { this.setAttribute('action', v); }
  get complete() { return true; } get naturalWidth() { return this.offsetWidth; } get naturalHeight() { return this.offsetHeight; }
  get content() { if (this.tagName !== 'TEMPLATE') return undefined; const f = document.createDocumentFragment(); for (const c of this.childNodes) f.appendChild(c.cloneNode(true)); return f; }
  get isContentEditable() { return false; } get contentEditable() { return 'inherit'; }
  get labels() { return list(this.id ? Z.all(document).filter(e => e.tagName === 'LABEL' && e.getAttribute('for') === this.id) : []); }
  get open() { return this.hasAttribute('open'); } set open(v) { this.toggleAttribute('open', !!v); }
  showModal() { this.open = true; } show() { this.open = true; } close(r) { this.open = false; this.returnValue = r || ''; dispatch(this, new Event('close')); }
  getContext() { return null; } play() { return Promise.resolve(); } pause() {} load() {}
}
for (const [n, a, b] of [['name'], ['type'], ['placeholder'], ['alt'], ['rel'], ['target'], ['method'], ['htmlFor', 'for'], ['width'], ['height'], ['min'], ['max'], ['step'], ['pattern'], ['accept'], ['autocomplete'], ['download'], ['label'],
  ['disabled', 'disabled', true], ['readOnly', 'readonly', true], ['required', 'required', true], ['multiple', 'multiple', true], ['autofocus', 'autofocus', true], ['noValidate', 'novalidate', true], ['defaultChecked', 'checked', true]]) reflect(n, a, b);
Object.defineProperty(HTMLElement.prototype, 'type', { configurable: true, get() { const t = this.getAttribute('type'); return t ? t.toLowerCase() : this.tagName === 'BUTTON' ? 'submit' : this.tagName === 'INPUT' ? 'text' : this.tagName === 'SELECT' ? 'select-one' : ''; }, set(v) { this.setAttribute('type', v); } });
onProps(HTMLElement.prototype);
function defaultAction(el) {   // lo que hace el navegador tras un clic hecho desde JavaScript (el.click())
  const a = el.closest('a[href]'); if (a) { const h = a.getAttribute('href'); if (h.startsWith('#')) { location.hash = h; } else if (!/^javascript:/i.test(h)) location.href = a.href; else (0, eval)(decodeURIComponent(h.slice(11))); return; }
  if (/^(checkbox)$/.test(el.type) && el.tagName === 'INPUT') { el.checked = !el.checked; dispatch(el, new InputEvent('input', { bubbles: true })); dispatch(el, new Event('change', { bubbles: true })); return; }
  if (el.type === 'radio' && el.tagName === 'INPUT') { el.checked = true; dispatch(el, new Event('change', { bubbles: true })); return; }
  if ((el.tagName === 'BUTTON' || el.tagName === 'INPUT') && el.type === 'submit' && el.form) el.form.requestSubmit(el);
  if (el.tagName === 'SUMMARY' && el.parentElement && el.parentElement.tagName === 'DETAILS') el.parentElement.open = !el.parentElement.open;
}

// ---------------- documento ----------------
class DocumentFragment extends Node {}
class Document extends Node {
  get documentElement() { return this.querySelector('html') || this.firstElementChild; }
  get head() { return this.querySelector('head'); } get body() { return this.querySelector('body'); }
  get title() { return Z.title(); } set title(v) { Z.setTitle(String(v)); }
  getElementById(id) { id = String(id); for (const e of Z.all(this)) if (e.id === id) return e; return null; }
  createElement(t) { return Z.create(String(t)); } createElementNS(ns, t) { return Z.create(String(t)); }
  createTextNode(s) { return Z.createText(String(s)); } createComment(s) { return Z.createText(''); }
  createDocumentFragment() { const f = Z.create('div'); frag.add(f); return f; }
  createEvent(t) { const e = new Event(''); e.initEvent = (type, b, c) => { e.type = type; e.bubbles = !!b; e.cancelable = !!c; }; e.initCustomEvent = (type, b, c, d) => { e.initEvent(type, b, c); e.detail = d; }; return e; }
  createRange() { return { selectNodeContents() {}, setStart() {}, setEnd() {}, collapse() {}, getBoundingClientRect: () => ({ x: 0, y: 0, width: 0, height: 0, top: 0, left: 0, right: 0, bottom: 0 }), createContextualFragment: html => { const t = this.createElement('div'); t.innerHTML = html; const f = this.createDocumentFragment(); for (const c of t.childNodes) f.appendChild(c); return f; } }; }
  importNode(n, deep) { return n.cloneNode(deep); } adoptNode(n) { return n; }
  get readyState() { return READY; } get URL() { return Z.url(); } get documentURI() { return Z.url(); } get baseURI() { return Z.url(); }
  get location() { return location; } set location(v) { location.href = v; } get defaultView() { return W; }
  get activeElement() { return Z.active() || this.body; } hasFocus() { return true; }
  get forms() { return this.getElementsByTagName('form'); } get images() { return this.getElementsByTagName('img'); } get links() { return list(this.querySelectorAll('a[href]')); }
  get scripts() { return this.getElementsByTagName('script'); } get currentScript() { return Z.current(); }
  get cookie() { return COOKIE; } set cookie(v) { const p = String(v).split(';')[0]; const i = p.indexOf('='); if (i > 0) { const k = p.slice(0, i).trim(); const m = new Map(COOKIE ? COOKIE.split('; ').map(x => [x.slice(0, x.indexOf('=')), x]) : []); m.set(k, p.trim()); COOKIE = [...m.values()].join('; '); } }
  get characterSet() { return 'UTF-8'; } get charset() { return 'UTF-8'; } get contentType() { return 'text/html'; } get compatMode() { return 'CSS1Compat'; } get referrer() { return ''; } get domain() { return location.hostname; }
  get visibilityState() { return 'visible'; } get hidden() { return false; } get fonts() { return { ready: Promise.resolve(), load: () => Promise.resolve([]) }; }
  write(...s) { Z.write(s.join('')); } writeln(...s) { Z.write(s.join('') + '\n'); } open() { return this; } close() {}
  execCommand() { return false; } getSelection() { return W.getSelection(); } elementFromPoint() { return this.body; }
}
Object.defineProperties(Document.prototype, Object.getOwnPropertyDescriptors(ParentMixin));
onProps(Document.prototype);
let READY = 'loading', COOKIE = '';
class HTMLDocument extends Document {}
Z.protos(HTMLElement.prototype, Text.prototype, HTMLDocument.prototype, (t, type, init) => {
  const C = /^(click|dblclick|mouse|contextmenu)/.test(type) ? MouseEvent : /^key/.test(type) ? KeyboardEvent : type === 'input' ? InputEvent : type === 'submit' ? SubmitEvent : /^(focus|blur)$/.test(type) ? FocusEvent : Event;
  const ev = new C(type, Object.assign({ bubbles: !/^(focus|blur|load)$/.test(type), cancelable: true }, init || {})); ev.isTrusted = true;
  if (init && init.clientX !== undefined) { const v = Z.view(); ev.pageX = init.clientX; ev.pageY = init.clientY; ev.clientY = init.clientY - v[2]; ev.offsetX = init.clientX; ev.offsetY = init.clientY; }
  return !dispatch(t, ev);
});
const document = Z.doc();

// ---------------- URL ----------------
class URLSearchParams {
  constructor(init) { this._l = []; if (typeof init === 'string') { init = init.replace(/^\?/, ''); for (const p of init.split('&')) if (p) { const i = p.indexOf('='); const d = s => decodeURIComponent(s.replace(/\+/g, ' ')); this._l.push(i < 0 ? [d(p), ''] : [d(p.slice(0, i)), d(p.slice(i + 1))]); } }
    else if (init && typeof init[Symbol.iterator] === 'function') for (const [k, v] of init) this._l.push([String(k), String(v)]); else if (init) for (const k of Object.keys(init)) this._l.push([k, String(init[k])]); }
  get(k) { const e = this._l.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this._l.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this._l.some(x => x[0] === k); }
  set(k, v) { const i = this._l.findIndex(x => x[0] === k); if (i < 0) this._l.push([k, String(v)]); else { this._l[i][1] = String(v); this._l = this._l.filter((x, j) => x[0] !== k || j === i); } this._u && this._u(); }
  append(k, v) { this._l.push([k, String(v)]); this._u && this._u(); } delete(k) { this._l = this._l.filter(x => x[0] !== k); this._u && this._u(); }
  sort() { this._l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); } forEach(f, t) { this._l.forEach(([k, v]) => f.call(t, v, k, this)); }
  keys() { return this._l.map(x => x[0])[Symbol.iterator](); } values() { return this._l.map(x => x[1])[Symbol.iterator](); } entries() { return this._l.map(x => [x[0], x[1]])[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }
  get size() { return this._l.length; }
  toString() { const e = s => encodeURIComponent(s).replace(/%20/g, '+'); return this._l.map(([k, v]) => e(k) + '=' + e(v)).join('&'); }
}
class URL {
  constructor(u, base) {
    u = String(u).trim(); let m = /^([a-zA-Z][a-zA-Z0-9+.-]*):(.*)$/.exec(u);
    if (!m) { if (base === undefined) throw new TypeError('Invalid URL: ' + u); const b = base instanceof URL ? base : new URL(base);
      if (u.startsWith('//')) u = b.protocol + u; else if (u.startsWith('/')) u = b.origin + u; else if (u.startsWith('?')) u = b.origin + b.pathname + u; else if (u.startsWith('#')) u = b.origin + b.pathname + b.search + u; else if (u === '') u = b.href.replace(/#.*$/, '');
      else u = b.origin + b.pathname.replace(/[^/]*$/, '') + u; m = /^([a-zA-Z][a-zA-Z0-9+.-]*):(.*)$/.exec(u); }
    this.protocol = m[1].toLowerCase() + ':'; let rest = m[2]; this.username = ''; this.password = ''; this.host = ''; this.hostname = ''; this.port = '';
    if (rest.startsWith('//')) { rest = rest.slice(2); const i = rest.search(/[/?#]/); const auth = i < 0 ? rest : rest.slice(0, i); rest = i < 0 ? '' : rest.slice(i);
      const at = auth.lastIndexOf('@'); const hp = at >= 0 ? auth.slice(at + 1) : auth; const c = hp.lastIndexOf(':'); this.hostname = (c > 0 && !hp.endsWith(']') ? hp.slice(0, c) : hp).toLowerCase(); this.port = c > 0 && !hp.endsWith(']') ? hp.slice(c + 1) : '';
      if ((this.protocol === 'http:' && this.port === '80') || (this.protocol === 'https:' && this.port === '443')) this.port = ''; this.host = this.hostname + (this.port ? ':' + this.port : ''); if (!rest.startsWith('/')) rest = '/' + rest; }
    const h = rest.indexOf('#'); this.hash = h >= 0 && h < rest.length - 1 ? rest.slice(h) : ''; if (h >= 0) rest = rest.slice(0, h);
    const q = rest.indexOf('?'); this.search = q >= 0 && q < rest.length - 1 ? rest.slice(q) : ''; if (q >= 0) rest = rest.slice(0, q);
    const segs = []; for (const s of rest.split('/')) { if (s === '..') { if (segs.length > 1) segs.pop(); } else if (s !== '.') segs.push(s); } this.pathname = segs.join('/') || '/';
    if (/\/\.\.?$/.test(rest) && !this.pathname.endsWith('/')) this.pathname += '/';
    this.searchParams = new URLSearchParams(this.search); this.searchParams._u = () => { const s = this.searchParams.toString(); this.search = s ? '?' + s : ''; };
  }
  get origin() { return this.protocol + '//' + this.host; }
  get href() { return this.protocol + (this.host || this.protocol === 'file:' ? '//' : '') + this.host + this.pathname + this.search + this.hash; } set href(v) { Object.assign(this, new URL(v)); }
  toString() { return this.href; } toJSON() { return this.href; }
  static canParse(u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }
  static createObjectURL() { return 'blob:zunzun'; } static revokeObjectURL() {}
}

// ---------------- ventana ----------------
const T0 = Z.now();
const location = {
  get href() { return Z.url(); }, set href(v) { Z.go(new URL(String(v), Z.url()).href); },
  assign(v) { this.href = v; }, replace(v) { this.href = v; }, reload() { Z.go(Z.url()); },
  get origin() { return new URL(Z.url()).origin; }, get protocol() { return new URL(Z.url()).protocol; }, get host() { return new URL(Z.url()).host; }, get hostname() { return new URL(Z.url()).hostname; },
  get port() { return new URL(Z.url()).port; }, get pathname() { return new URL(Z.url()).pathname; }, get search() { return new URL(Z.url()).search; },
  get hash() { return HASH; }, set hash(v) { v = String(v); HASH = v ? (v[0] === '#' ? v : '#' + v) : ''; const t = HASH && document.getElementById(HASH.slice(1)); if (t) t.scrollIntoView(); dispatch(W, new Event('hashchange')); },
  toString() { return Z.url(); },
};
let HASH = (/#.*$/.exec(Z.url()) || [''])[0];
const history = { length: 1, state: null, back() { Z.go('zunzun:volver'); }, forward() {}, go(n) { if (n < 0) this.back(); }, pushState(s, t, u) { this.state = s; if (u) HASH = (/#.*$/.exec(String(u)) || [''])[0]; }, replaceState(s) { this.state = s; }, scrollRestoration: 'auto' };
class Storage {
  constructor(persist) { this._p = persist; let d = {}; if (persist) { try { d = JSON.parse(Z.storeLoad()) || {}; } catch (e) { d = {}; } } this._d = d; }
  _save() { if (this._p) Z.storeSave(JSON.stringify(this._d)); }
  getItem(k) { k = String(k); return Object.prototype.hasOwnProperty.call(this._d, k) ? this._d[k] : null; } setItem(k, v) { this._d[String(k)] = String(v); this._save(); }
  removeItem(k) { delete this._d[String(k)]; this._save(); } clear() { this._d = {}; this._save(); } key(i) { return Object.keys(this._d)[i] ?? null; } get length() { return Object.keys(this._d).length; }
}
const wrapStore = s => new Proxy(s, { get: (t, k) => k in t || typeof k !== 'string' ? (typeof t[k] === 'function' ? t[k].bind(t) : t[k]) : t.getItem(k), set: (t, k, v) => { t.setItem(k, v); return true; }, deleteProperty: (t, k) => { t.removeItem(k); return true; }, ownKeys: t => Object.keys(t._d), getOwnPropertyDescriptor: (t, k) => k in t._d ? { value: t._d[k], enumerable: true, configurable: true } : undefined });
class Headers {
  constructor(h) { this._m = new Map(); if (h instanceof Headers) h.forEach((v, k) => this._m.set(k, v)); else if (Array.isArray(h)) for (const [k, v] of h) this._m.set(String(k).toLowerCase(), String(v)); else if (h) for (const k of Object.keys(h)) this._m.set(k.toLowerCase(), String(h[k])); }
  get(k) { return this._m.get(String(k).toLowerCase()) ?? null; } set(k, v) { this._m.set(String(k).toLowerCase(), String(v)); } append(k, v) { const o = this.get(k); this.set(k, o ? o + ', ' + v : v); } has(k) { return this._m.has(String(k).toLowerCase()); } delete(k) { this._m.delete(String(k).toLowerCase()); }
  forEach(f, t) { this._m.forEach((v, k) => f.call(t, v, k, this)); } entries() { return this._m.entries(); } keys() { return this._m.keys(); } values() { return this._m.values(); } [Symbol.iterator]() { return this._m.entries(); }
}
class Response {
  constructor(body, init = {}) { this._b = body == null ? '' : String(body); this.status = init.status ?? 200; this.statusText = init.statusText || ''; this.headers = new Headers(init.headers); this.url = init.url || ''; this.ok = this.status >= 200 && this.status < 300; this.redirected = false; this.type = 'basic'; this.bodyUsed = false; }
  text() { this.bodyUsed = true; return Promise.resolve(this._b); } json() { this.bodyUsed = true; return new Promise((res, rej) => { try { res(JSON.parse(this._b)); } catch (e) { rej(e); } }); }
  arrayBuffer() { return Promise.resolve(new TextEncoder().encode(this._b).buffer); } blob() { return Promise.resolve({ size: this._b.length, type: this.headers.get('content-type') || '', text: () => Promise.resolve(this._b) }); } clone() { return new Response(this._b, this); }
}
class Request { constructor(u, o = {}) { this.url = String(u instanceof Request ? u.url : u); this.method = (o.method || 'GET').toUpperCase(); this.headers = new Headers(o.headers); this.body = o.body; } }
function bodyOf(b, h) { if (b == null) return null; if (typeof b === 'string') return b; if (b instanceof URLSearchParams) { if (!h.has('content-type')) h.set('content-type', 'application/x-www-form-urlencoded'); return b.toString(); }
  if (b instanceof FormData) { if (!h.has('content-type')) h.set('content-type', 'application/x-www-form-urlencoded'); return new URLSearchParams(b._l).toString(); } return String(b); }
function fetch(input, o = {}) {
  return new Promise((resolve, reject) => setTimeout(() => {
    try { const r = input instanceof Request ? input : null; const u = new URL(r ? r.url : String(input), Z.url()).href; const h = new Headers(o.headers || (r && r.headers)); const method = (o.method || (r && r.method) || 'GET').toUpperCase(); const body = bodyOf(o.body ?? (r && r.body), h);
      const [status, text, type, fin] = Z.fetch(method, u, body, h.get('content-type') || ''); const res = new Response(text, { status, headers: { 'content-type': type }, url: fin }); res.redirected = fin !== u; resolve(res); }
    catch (e) { reject(e instanceof Error ? new TypeError(e.message) : e); } }, 0));
}
class FormData { constructor(form) { this._l = []; if (form) for (const e of form.elements) { if (!e.name || e.disabled) continue; if (/checkbox|radio/.test(e.type) && !e.checked) continue; if (/submit|button|reset/.test(e.type)) continue; this._l.push([e.name, e.value]); } }
  append(k, v) { this._l.push([String(k), String(v)]); } get(k) { const e = this._l.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this._l.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this._l.some(x => x[0] === k); }
  set(k, v) { this.delete(k); this.append(k, v); } delete(k) { this._l = this._l.filter(x => x[0] !== k); } entries() { return this._l[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); } forEach(f, t) { this._l.forEach(([k, v]) => f.call(t, v, k, this)); } keys() { return this._l.map(x => x[0])[Symbol.iterator](); } values() { return this._l.map(x => x[1])[Symbol.iterator](); } }
class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = ''; this.responseType = ''; this.responseURL = ''; this._h = new Headers(); this._rh = new Headers(); this.timeout = 0; this.withCredentials = false; this.upload = new EventTarget(); }
  open(m, u, async = true) { this._m = String(m).toUpperCase(); this._u = new URL(String(u), Z.url()).href; this._async = async !== false; this._set(1); }
  setRequestHeader(k, v) { this._h.set(k, v); } getResponseHeader(k) { return this._rh.get(k); } getAllResponseHeaders() { let s = ''; this._rh.forEach((v, k) => s += k + ': ' + v + '\r\n'); return s; } overrideMimeType() {}
  _set(s) { this.readyState = s; const ev = new Event('readystatechange'); dispatch(this, ev); if (typeof this.onreadystatechange === 'function') try { this.onreadystatechange(ev); } catch (e) { report(e); } }
  _fire(t) { const ev = new Event(t); dispatch(this, ev); const h = this['on' + t]; if (typeof h === 'function') try { h.call(this, ev); } catch (e) { report(e); } }
  send(body) { const go = () => { try { const b = bodyOf(body, this._h); const [st, text, type, fin] = Z.fetch(this._m, this._u, b, this._h.get('content-type') || ''); this.status = st; this.statusText = st === 200 ? 'OK' : ''; this.responseURL = fin; this._rh.set('content-type', type); this.responseText = text;
      this.response = this.responseType === 'json' ? (() => { try { return JSON.parse(text); } catch (e) { return null; } })() : this.responseType === 'document' ? null : text; this._set(2); this._set(3); this._set(4); this._fire('load'); }
    catch (e) { this.status = 0; this._set(4); this._fire('error'); } this._fire('loadend'); };
    if (this._async) setTimeout(go, 0); else go(); }
  abort() { this._fire('abort'); }
}
def(XMLHttpRequest, { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 });
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) { s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new Error('InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); o += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < s.length ? B64[n >> 6 & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '='); } return o; }
function atob(s) { s = String(s).replace(/[\s=]/g, ''); let o = '', bits = 0, n = 0; for (const ch of s) { const v = B64.indexOf(ch); if (v < 0) throw new Error('InvalidCharacterError'); n = n << 6 | v; bits += 6; if (bits >= 8) { bits -= 8; o += String.fromCharCode(n >> bits & 255); } } return o; }
class TextEncoder { get encoding() { return 'utf-8'; } encode(s = '') { const u = unescape(encodeURIComponent(String(s))); const a = new Uint8Array(u.length); for (let i = 0; i < u.length; i++) a[i] = u.charCodeAt(i); return a; } }
class TextDecoder { constructor(e = 'utf-8') { this.encoding = e; } decode(b) { if (!b) return ''; const a = b instanceof Uint8Array ? b : new Uint8Array(b.buffer || b); let s = ''; for (let i = 0; i < a.length; i++) s += String.fromCharCode(a[i]); try { return decodeURIComponent(escape(s)); } catch (e) { return s; } } }
class AbortController { constructor() { this.signal = new EventTarget(); this.signal.aborted = false; } abort() { this.signal.aborted = true; dispatch(this.signal, new Event('abort')); } }
class MutationObserver { constructor(f) { this._f = f; } observe() {} disconnect() {} takeRecords() { return []; } }
class IntersectionObserver { constructor(f) { this._f = f; } observe(el) { setTimeout(() => this._f([{ target: el, isIntersecting: true, intersectionRatio: 1, boundingClientRect: el.getBoundingClientRect() }], this), 0); } unobserve() {} disconnect() {} takeRecords() { return []; } }
class ResizeObserver { constructor(f) { this._f = f; } observe(el) { setTimeout(() => this._f([{ target: el, contentRect: el.getBoundingClientRect() }], this), 0); } unobserve() {} disconnect() {} }
const RAF = new Map(); let rafId = 0;
const view = () => Z.view();
const props = {
  window: W, self: W, top: W, parent: W, frames: W, opener: null, closed: false, frameElement: null, name: '', isSecureContext: true, origin: undefined,
  document, location, history, navigator: { userAgent: 'Mozilla/5.0 (ZunzunOS; x86_64) Colibri NavegadorJS/1.0', appName: 'Netscape', appVersion: '5.0', platform: 'ZunzunOS', vendor: 'ZunzunOS', language: 'es-ES', languages: ['es-ES', 'es', 'en'], onLine: true, cookieEnabled: true, hardwareConcurrency: 1, maxTouchPoints: 0, doNotTrack: '1', webdriver: false, clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') }, sendBeacon: () => true, serviceWorker: undefined, geolocation: undefined, mediaDevices: undefined },
  screen: { get width() { return view()[0]; }, get height() { return view()[1]; }, get availWidth() { return view()[0]; }, get availHeight() { return view()[1]; }, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0 } },
  get innerWidth() { return view()[0]; }, get innerHeight() { return view()[1]; }, get outerWidth() { return view()[0]; }, get outerHeight() { return view()[1]; }, devicePixelRatio: 1,
  get scrollX() { return 0; }, get scrollY() { return view()[2]; }, get pageXOffset() { return 0; }, get pageYOffset() { return view()[2]; },
  scrollTo(x, y) { Z.scroll(typeof x === 'object' ? x.top || 0 : y || 0); }, scroll(x, y) { W.scrollTo(x, y); }, scrollBy(x, y) { Z.scroll(view()[2] + (typeof x === 'object' ? x.top || 0 : y || 0)); },
  alert(m) { Z.dialog(0, m === undefined ? '' : String(m)); }, confirm(m) { return Z.dialog(1, m === undefined ? '' : String(m)); }, prompt(m, d) { return Z.dialog(2, m === undefined ? '' : String(m), d === undefined ? '' : String(d)); },
  print() {}, open(u) { if (u) location.href = u; return null; }, close() {}, focus() {}, blur() {}, stop() {}, postMessage() {},
  requestAnimationFrame(f) { const id = ++rafId; RAF.set(id, setTimeout(() => { RAF.delete(id); f(Z.now() - T0); }, 16)); return id; }, cancelAnimationFrame(id) { clearTimeout(RAF.get(id)); RAF.delete(id); },
  requestIdleCallback(f) { return setTimeout(() => f({ didTimeout: false, timeRemaining: () => 10 }), 1); }, cancelIdleCallback(id) { clearTimeout(id); },
  queueMicrotask(f) { Promise.resolve().then(f); }, structuredClone(v) { return v === undefined ? undefined : JSON.parse(JSON.stringify(v)); },
  getComputedStyle(el) { const s = el.style, r = el.getBoundingClientRect(); return new Proxy({}, { get: (t, k) => k === 'getPropertyValue' ? p => s.getPropertyValue(p) : k === 'display' ? (el.hidden ? 'none' : s.display || (/^(DIV|P|H\d|UL|OL|LI|SECTION|ARTICLE|HEADER|FOOTER|NAV|MAIN|FORM|TABLE|BODY|HTML)$/.test(el.tagName) ? 'block' : 'inline')) : k === 'width' ? r.width + 'px' : k === 'height' ? r.height + 'px' : k === 'visibility' ? 'visible' : typeof k === 'string' ? s[k] || '' : undefined }); },
  matchMedia(q) { const w = view()[0]; let m = false; const mm = /\((min|max)-width:\s*(\d+)px\)/.exec(q); if (mm) m = mm[1] === 'min' ? w >= +mm[2] : w <= +mm[2]; if (/prefers-color-scheme:\s*light/.test(q)) m = true; return { matches: m, media: q, onchange: null, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} }; },
  getSelection() { return { toString: () => '', rangeCount: 0, removeAllRanges() {}, addRange() {}, getRangeAt() { return null; } }; },
  performance: { now: () => Z.now() - T0, timeOrigin: Date.now() - (Z.now() - T0), mark() {}, measure() {}, getEntriesByType: () => [], getEntriesByName: () => [] },
  localStorage: wrapStore(new Storage(true)), sessionStorage: wrapStore(new Storage(false)),
  customElements: { define() {}, get() {}, whenDefined: () => Promise.resolve() }, CSS: { supports: () => false, escape: s => String(s).replace(/[^\w-]/g, c => '\\' + c) },
  crypto: { getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256 ** (a.BYTES_PER_ELEMENT || 1)); return a; }, randomUUID() { const h = [...Array(32)].map(() => Math.floor(Math.random() * 16).toString(16)); h[12] = '4'; h[16] = (8 + Math.floor(Math.random() * 4)).toString(16); const s = h.join(''); return s.slice(0, 8) + '-' + s.slice(8, 12) + '-' + s.slice(12, 16) + '-' + s.slice(16, 20) + '-' + s.slice(20); } },
  fetch, atob, btoa,
  Image: function (w, h) { const i = document.createElement('img'); if (w) i.width = w; if (h) i.height = h; return i; },
  Option: function (t, v, ds, s) { const o = document.createElement('option'); if (t !== undefined) o.textContent = t; if (v !== undefined) o.value = v; if (s) o.setAttribute('selected', ''); return o; },
};
Object.defineProperties(W, Object.getOwnPropertyDescriptors(props));
for (const [k, v] of Object.entries({ Event, CustomEvent, UIEvent, MouseEvent, PointerEvent, KeyboardEvent, InputEvent, FocusEvent, SubmitEvent, ErrorEvent, EventTarget, Node, CharacterData, Text, Comment, Element, HTMLElement, Document, HTMLDocument, DocumentFragment, DOMTokenList, URL, URLSearchParams, Headers, Request, Response, FormData, XMLHttpRequest, Storage, TextEncoder, TextDecoder, AbortController, MutationObserver, IntersectionObserver, ResizeObserver }))
  Object.defineProperty(W, k, { value: v, writable: true, configurable: true });
for (const n of ['HTMLDivElement', 'HTMLSpanElement', 'HTMLAnchorElement', 'HTMLButtonElement', 'HTMLInputElement', 'HTMLFormElement', 'HTMLSelectElement', 'HTMLOptionElement', 'HTMLTextAreaElement', 'HTMLImageElement', 'HTMLParagraphElement', 'HTMLUListElement', 'HTMLLIElement', 'HTMLTableElement', 'HTMLScriptElement', 'HTMLStyleElement', 'HTMLBodyElement', 'HTMLHeadingElement', 'HTMLLabelElement', 'HTMLCanvasElement', 'HTMLTemplateElement', 'HTMLDetailsElement', 'HTMLDialogElement', 'HTMLMediaElement', 'HTMLVideoElement', 'HTMLAudioElement', 'HTMLIFrameElement', 'SVGElement'])
  Object.defineProperty(W, n, { value: HTMLElement, writable: true, configurable: true });
onProps(W);
for (const m of ['addEventListener', 'removeEventListener', 'dispatchEvent']) Object.defineProperty(W, m, { value: EventTarget.prototype[m], writable: true, configurable: true });
// al terminar los scripts de la pagina: DOMContentLoaded y load
Object.defineProperty(W, '__zloaded', { configurable: true, value() { READY = 'interactive'; dispatch(document, new Event('DOMContentLoaded', { bubbles: true })); READY = 'complete'; dispatch(document, new Event('readystatechange')); dispatch(W, new Event('load')); for (const img of document.images) dispatch(img, new Event('load')); delete W.__zloaded; } });
})();
