// zz-prueba: bateria de JavaScript; la salida tiene que ser igual que la de Node (tools/js_test.py)
const out = (...a) => console.log(...a);
const p = x => Number(x).toPrecision(14);
// numeros y conversion a texto (dtoa)
out(0.1 + 0.2, 1 / 3, 2 ** 53, 2 ** -1074, 1e21, 123456789012345680000, -0 === 0, Object.is(-0, 0));
out((1234.5678).toFixed(2), (0.000001234).toExponential(3), (123.456).toPrecision(4), (255).toString(16), (255).toString(2), (-7.5).toString(36));
out(parseInt('0x1f'), parseInt('077', 8), parseFloat('3.14abc'), Number('  42 '), Number('1e3'), Number(''), Number('abc'));
out(Number.MAX_SAFE_INTEGER, Number.EPSILON, Number.MIN_VALUE, Number.MAX_VALUE, 5e-324, 1.7976931348623157e308);
out(Math.floor(-2.5), Math.ceil(-2.5), Math.round(-2.5), Math.round(2.5), Math.trunc(-4.7), Math.sign(-3), Math.abs(-0));
out(Math.sqrt(2), Math.cbrt(27), Math.hypot(3, 4), 2 ** 10, 10 ** 22, (-2) ** 3, 0.5 ** 3, 7 % -3, -7 % 3, 5.5 % 2);
out(p(Math.sin(1)), p(Math.cos(1)), p(Math.tan(1)), p(Math.sin(Math.PI)), p(Math.cos(Math.PI / 3)), p(Math.sin(1e6)), p(Math.atan2(1, -1)));
out(p(Math.exp(1)), p(Math.log(10)), p(Math.log2(1024)), p(Math.log10(0.001)), p(Math.expm1(1e-10)), p(Math.log1p(1e-10)), p(Math.pow(2, 0.5)), p(Math.pow(1.0001, 10000)));
out(p(Math.asin(0.5)), p(Math.acos(0.5)), p(Math.atan(1)), p(Math.sinh(1)), p(Math.cosh(1)), p(Math.tanh(0.5)), p(Math.asinh(1)), p(Math.acosh(2)), p(Math.atanh(0.5)));
out(Math.max(), Math.min(1, -Infinity), Math.max(NaN, 1), Math.pow(NaN, 0), Math.pow(1, Infinity), 1 / 0, -1 / 0, 0 / 0);
// cadenas, Unicode y expresiones regulares
out('Hola, ZunzúnOS'.toUpperCase(), 'ÁÉÍÓÚ ñ'.toLowerCase(), 'abc'.padStart(6, '*'), ' x '.trim() + '|', 'a-b-c'.split('-').join('+'), 'canción'.length, [...'año'].length);
out('2026-11-23'.replace(/(\d+)-(\d+)-(\d+)/, '$3/$2/$1'), /^[\w.+-]+@\w+\.\w+$/.test('ana@ejemplo.es'), 'uno dos tres'.match(/\b\w/g).join(''), 'aXbXc'.replaceAll('X', '_'));
out('😀'.codePointAt(0), String.fromCodePoint(0x1F600).length, 'é'.normalize('NFD').length, 'straße'.localeCompare('strasse') !== undefined, encodeURIComponent('a b&ñ'), decodeURIComponent('%C3%B1'));
const re = /(?<dia>\d{2})\/(?<mes>\d{2})/u; const m = '25/12'.match(re); out(m.groups.dia, m.groups.mes, 'xAy'.search(/A/), 'abc'.at(-1));
// objetos, JSON, clases y estructuras
out(JSON.stringify({ a: 1, b: [true, null, 'x'], c: { d: 2.5 } }), JSON.stringify(JSON.parse('{"x":[1,{"y":"ñ"}]}')), JSON.stringify('línea\n"cita"'));
class Animal { #nombre; constructor(n) { this.#nombre = n; } get nombre() { return this.#nombre; } habla() { return `${this.#nombre} hace ruido`; } static crear(n) { return new this(n); } }
class Perro extends Animal { habla() { return super.habla() + ' (guau)'; } }
out(Perro.crear('Rex').habla(), new Perro('Toby').nombre, Perro.name, typeof Animal, Object.getPrototypeOf(Perro) === Animal);
const { x, y = 5, ...resto } = { x: 1, z: 3, w: 4 }; const [a1, , a3 = 9, ...as] = [1, 2, undefined, 4, 5]; out(x, y, JSON.stringify(resto), a1, a3, as.join());
const mp = new Map([[1, 'uno'], ['dos', 2]]); const st = new Set([3, 1, 3, 2]); out(mp.get(1), mp.size, [...st].join(), st.has(2), [...mp.keys()].join());
out([5, 1, 10, 2].sort().join(), [5, 1, 10, 2].sort((a, b) => a - b).join(), [1, 2, 3, 4].filter(n => n % 2).map(n => n * 10).reduce((s, n) => s + n, 0), [[1, [2]], [3]].flat(Infinity).join(), Array.from({ length: 4 }, (_, i) => i * i).join());
out([3, 1, 2].toSorted().join(), Object.entries({ a: 1, b: 2 }).map(([k, v]) => k + v).join(), Object.fromEntries([['k', 1]]).k, [1, 2, 3].includes(2), [1, 2, 3].findLast(n => n < 3), 'ab'.split('').reverse().join(''));
// funciones, cierres, generadores, simbolos, proxies
const contador = (() => { let n = 0; return () => ++n; })(); contador(); contador(); out(contador());
function* fib() { let [a, b] = [0, 1]; for (;;) { yield a; [a, b] = [b, a + b]; } } const f = []; for (const v of fib()) { if (v > 100) break; f.push(v); } out(f.join(' '));
const s1 = Symbol('id'); const o = { [s1]: 7, get doble() { return this.v * 2; }, v: 21 }; out(o[s1], o.doble, s1.toString(), Object.keys(o).join());
const px = new Proxy({}, { get: (t, k) => typeof k === 'string' ? k.toUpperCase() : undefined }); out(px.hola, Reflect.has({ q: 1 }, 'q'));
out(typeof null, typeof undefined, typeof 1n, typeof Symbol(), null ?? 'def', 0 || 'or', 0 ?? 'nn', ({}).a?.b, [] + [], [] + {}, '5' * '2', '5' + 2, true + 1);
// BigInt, matrices tipadas, fechas (UTC), errores
out(2n ** 100n, (2n ** 64n - 1n).toString(16), BigInt.asIntN(8, 255n), 10n / 3n, -7n % 3n);
const ta = new Uint8Array([250, 5, 300]); const dv = new DataView(new ArrayBuffer(8)); dv.setFloat64(0, Math.PI); out(ta.join(), new Float32Array([1.1])[0], dv.getUint8(0), dv.getFloat64(0) === Math.PI, new Int16Array(2).length);
const d = new Date(Date.UTC(2026, 10, 23, 18, 30, 15, 250)); out(d.toISOString(), d.getTime(), d.getUTCDay(), new Date(0).toISOString(), Date.UTC(2000, 1, 29), new Date('2024-02-29T12:00:00Z').getUTCMonth(), JSON.stringify({ d }));
try { null.x; } catch (e) { out(e instanceof TypeError, e.name); } try { JSON.parse('{mal'); } catch (e) { out(e.name); } try { throw new RangeError('fuera'); } catch (e) { out(String(e)); }
try { undefinedVar; } catch (e) { out(e.name); } out(isNaN('x'), Number.isInteger(5.0), Number.isSafeInteger(2 ** 53), (25).toString(36), Number('0b101'), Number('0o17'));
// asincronia: promesas, async/await, temporizadores
const espera = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const orden = []; setTimeout(() => orden.push('t0'), 0); Promise.resolve().then(() => orden.push('micro')); orden.push('sinc');
  await espera(30); out(orden.join(','));
  const r = await Promise.all([1, espera(10).then(() => 2), Promise.resolve(3)]); out(r.join());
  const rr = await Promise.allSettled([Promise.reject(new Error('no')), 1]); out(rr.map(x => x.status).join());
  let n = 0; await new Promise(res => { const id = setInterval(() => { if (++n === 3) { clearInterval(id); res(); } }, 5); }); out('intervalo', n);
  out('fin');
})();
