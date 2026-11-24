// zz-prueba: segunda bateria (rincones del lenguaje); la salida tiene que ser igual que la de Node
const out = (...a) => console.log(...a.map(x => typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)));
// ambitos y cierres
function a1() { var v = 1; function b() { return v + c(); } function c() { return 10; } { let v = 5; out('bloque', v); } return b(); } out('anidadas', a1());
const fs = []; for (let i = 0; i < 3; i++) fs.push(() => i); out('for-let', fs.map(f => f()).join());
const gs = []; for (var j = 0; j < 3; j++) gs.push(() => j); out('for-var', gs.map(f => f()).join());
const hs = []; for (const k of ['x', 'y']) hs.push(() => k); out('for-of-const', hs.map(f => f()).join());
let tdz = 'fuera'; { try { tdz2; } catch (e) { out('tdz', e.name); } let tdz2 = 1; }
switch (2) { case 1: { let s = 'uno'; out(s); break; } case 2: { let s = 'dos'; out('switch', s); } default: out('cae'); }
try { throw new Error('a'); } catch ({ message }) { out('catch-destr', message); }
outer: for (let i = 0; i < 3; i++) { for (let j = 0; j < 3; j++) { if (j === 1) continue outer; if (i === 2) break outer; out('etiquetas', i, j); } }
function args() { return arguments.length + ':' + [].slice.call(arguments).join('-'); } out('arguments', args(1, 2, 3));
function defs(a, b = a * 2, { c = 3, d } = {}, ...r) { return [a, b, c, d, r.length]; } out('param', defs(1), defs(1, 5, { d: 4 }, 9, 9));
function nt() { return new.target === undefined ? 'llamada' : 'new'; } out('new.target', nt(), new nt() instanceof nt);
const o1 = { n: 'o', f() { return this.n; }, g: () => typeof this, get h() { return this.n + '!'; }, set h(v) { this.n = v; } }; o1.h = 'p'; out('this', o1.f(), o1.g(), o1.h);
const bound = o1.f.bind({ n: 'b' }); out('bind', bound(), bound.name, (function () { return this; }).call(7) instanceof Number);
// clases
class P { static #cuenta = 0; #x; constructor(x) { this.#x = x; P.#cuenta++; } static cuenta() { return P.#cuenta; } get x() { return this.#x; } #priv() { return 'p' + this.#x; } usa() { return this.#priv(); } static { this.inic = 'estatico'; } has(o) { return #x in o; } toString() { return `P(${this.#x})`; } }
class Q extends P { y = this.x * 2; constructor() { super(21); } usa() { return 'Q:' + super.usa(); } static crear() { return new this(); } }
const q = Q.crear(); out('clases', q.x, q.y, q.usa(), P.cuenta(), P.inic, q.has(q), q.has({}), `${q}`, Object.getPrototypeOf(Q) === P, Q.name);
class Ar extends Array { suma() { return this.reduce((a, b) => a + b, 0); } } const ar = Ar.from([1, 2, 3]); out('extiende Array', ar.suma(), ar.length, Array.isArray(ar));
class Er extends Error { constructor(m) { super(m); this.name = 'Er'; } } try { throw new Er('mal'); } catch (e) { out('extiende Error', e instanceof Er, e instanceof Error, e.message, String(e)); }
// generadores e iteradores
function* gen() { const r = yield 1; try { yield r * 2; } finally { out('finally gen'); } return 9; }
const it = gen(); out('gen', JSON.stringify(it.next()), JSON.stringify(it.next(5)), JSON.stringify(it.return(7)), JSON.stringify(it.next()));
function* deleg() { yield* [1, 2]; const x = yield* gen(); return x; } out('yield*', [...deleg()].join());
const iterable = { from: 1, to: 4, [Symbol.iterator]() { let c = this.from, t = this.to; return { next: () => c <= t ? { value: c++, done: false } : { value: undefined, done: true }, [Symbol.iterator]() { return this; } }; } };
out('iterable propio', [...iterable].join(), Math.max(...iterable), Array.from(iterable, x => x * x).join());
const [p1, , p3 = 'def', ...prest] = 'abcdef'; out('destr cadena', p1, p3, prest.join(''));
const { a: { b: [, segundo] = [] } = {}, ...otros } = { a: { b: [1, 2] }, c: 3, d: 4 }; out('destr anidada', segundo, JSON.stringify(otros));
let sx = 1, sy = 2; [sx, sy] = [sy, sx]; out('intercambio', sx, sy);
// Proxy y Reflect
const log = []; const px = new Proxy({ a: 1 }, { get(t, k, r) { log.push('get ' + String(k)); return Reflect.get(t, k, r); }, has(t, k) { return k.startsWith('a'); }, deleteProperty(t, k) { log.push('del ' + k); return delete t[k]; }, ownKeys(t) { return ['a', 'z']; }, getOwnPropertyDescriptor(t, k) { return { value: k, enumerable: true, configurable: true }; } });
out('proxy', px.a, 'ab' in px, 'b' in px, delete px.a, Object.keys(px).join(), log.join('|'));
out('reflect', Reflect.ownKeys({ b: 1, 2: 0, [Symbol('s')]: 1, a: 2 }).length, Reflect.has({ x: 1 }, 'x'), Reflect.apply(Math.max, null, [1, 5, 2]));
// expresiones regulares
out('lookbehind', '$10 y 20€'.match(/(?<=\$)\d+/)[0], 'precio 30'.replace(/(?<!\d)(\d+)/g, '[$1]'));
out('nombres', '2026-11-24'.replace(/(?<a>\d+)-(?<m>\d+)-(?<d>\d+)/, '$<d>.$<m>.$<a>'), /(?<x>a)|(?<y>b)/.exec('b').groups.y);
out('unicode', /^.$/u.test('😀'), /^.$/.test('😀'), '😀a'.match(/./gu).length, /\p{L}+/u.exec('¡Hola!')[0]);
out('lazy y back', '<a><b>'.match(/<.+?>/g).join(''), /(\w)\1/.test('xaab'), 'aAbB'.replace(/[a-z]/gi, c => c === c.toUpperCase() ? 'M' : 'm'));
out('sticky y flags', (() => { const r = /a/y; r.lastIndex = 1; return r.test('ba'); })(), /x/gimsuy.flags, new RegExp('a.b', 's').test('a\nb'));
out('split regex', 'uno, dos;tres'.split(/[,;]\s*/).join('|'), 'a1b2'.split(/(\d)/).join('|'), ''.split(',').length);
// numeros y texto
out('redondeos', (1.005).toFixed(2), (1.45).toFixed(1), (0.000001).toString(), (123.456).toExponential(2), (1e-7).toPrecision(2), (255).toString(36), (-0.5).toFixed(0));
out('conversion', +'  12  ', +'1_0', +'0x10', Number(''), parseInt('12px'), parseFloat('.5e1'), Number(null), Number([5]), Number([1, 2]), 1 / -0);
out('igualdad', null == undefined, null == 0, '' == 0, '0' == false, [] == false, NaN === NaN, Object.is(NaN, NaN), [1] == 1);
out('texto', 'Straße'.toUpperCase(), 'İ'.toLowerCase().length, 'abc'.localeCompare('abd'), 'a-b_c d'.split(/[-_ ]/).length, '  x '.trimEnd() + '|', 'ab'.at(-1), 'x'.codePointAt(0));
out('plantillas', `a${1 + 1}b${'c'}`, String.raw`\n${2}`, ((s, ...v) => s.raw.join('|') + v.join('+'))`x${1}y${2}z`);
// objetos
const ob = { b: 2, a: 1, 1: 'uno', [Symbol.toStringTag]: 'Mio' }; out('orden claves', Object.keys(ob).join(), String(ob), JSON.stringify(Object.entries(ob)));
const fr = Object.freeze({ x: 1 }); try { 'use strict'; fr.x = 2; } catch (e) { } out('congelado', fr.x, Object.isFrozen(fr));
const getset = Object.defineProperty({}, 'v', { get() { return 42; }, enumerable: false }); out('defineProperty', getset.v, Object.keys(getset).length, JSON.stringify(Object.getOwnPropertyDescriptor(getset, 'v')));
out('spread obj', JSON.stringify({ ...{ a: 1, b: 2 }, b: 3, ...null }), JSON.stringify(Object.assign({}, 'ab')));
out('json', JSON.stringify({ d: new Date(0), u: undefined, f() { }, n: NaN, nested: [undefined, () => 1] }), JSON.stringify([1, [2]], null, 1).length, JSON.parse('{"a":[1,{"b":2}]}', (k, v) => typeof v === 'number' ? v * 10 : v).a[1].b);
// asincronia
(async () => {
  try { await Promise.reject(new TypeError('rechazo')); } catch (e) { out('await catch', e.name); }
  const r = await Promise.race([new Promise(r => setTimeout(() => r('lento'), 50)), new Promise(r => setTimeout(() => r('rapido'), 5))]); out('race', r);
  const any = await Promise.any([Promise.reject(1), Promise.resolve(2)]); out('any', any);
  async function* ag() { yield 1; await null; yield 2; } const vals = []; for await (const v of ag()) vals.push(v); out('for await', vals.join());
  const fin = await Promise.resolve(5).finally(() => 'ignorado'); out('finally', fin);
  out('fin2');
})();
