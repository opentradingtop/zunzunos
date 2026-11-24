// script externo: eventos, formulario, fetch, temporizador y localStorage
ok('externo');
let n = 0;
const boton = document.getElementById('boton');
boton.addEventListener('click', e => { n++; document.getElementById('cuenta').textContent = String(n); ok('clic ' + n); });
const f = document.getElementById('f');
f.addEventListener('submit', e => { e.preventDefault(); const v = document.getElementById('nombre').value; document.getElementById('res').textContent = 'Enviado: ' + v; ok('formulario ' + v); });
fetch('datos.json').then(r => r.json()).then(d => { document.getElementById('red').textContent = 'Datos: ' + d.mensaje + ' (' + d.lista.length + ')'; ok('fetch ' + d.mensaje); }).catch(e => console.error('fetch fallo ' + e));
setTimeout(() => { document.getElementById('tiempo').textContent = 'pasaron 300 ms'; ok('temporizador'); }, 300);
localStorage.setItem('visitas', String(+(localStorage.getItem('visitas') || 0) + 1)); if (localStorage.getItem('visitas') >= 1) ok('localStorage');
const u = new URL('datos.json?a=1&b=dos', location.href); if (u.searchParams.get('b') === 'dos' && u.pathname.endsWith('/datos.json')) ok('URL');
window.addEventListener('load', () => {
  ok('load');
  // posiciones para que la prueba sepa donde pulsar
  const r = el => { const b = document.getElementById(el).getBoundingClientRect(); return Math.round(b.left + b.width / 2) + ',' + Math.round(b.top + b.height / 2); };
  console.log('POS boton ' + r('boton') + ' boton2 ' + r('boton2') + ' nombre ' + r('nombre'));
});
try { null.x; } catch (e) { if (e instanceof TypeError) ok('errores'); }
// expresiones regulares (se guardan en la cache del motor) y una promesa rechazada sin gestionar: la segunda pagina
// arranca otro motor y no debe tocar nada del anterior
if (/^d(at)os\.json$/.test('datos.json') && 'a-b-c'.replace(/-/g, '+') === 'a+b+c') ok('regex');
Promise.reject(new Error('sin gestionar a proposito'));
