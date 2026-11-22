#include "hoja_core.c"
static void put(const char *r0, const char *v) { const char *p = r0; int x, y; ref(&p, &x, &y); free(C[IDX(x, y)]); C[IDX(x, y)] = strdup(v); recalc(); }
static int fails;
static void chk(const char *f, const char *want) { C[IDX(10, 900)] = strdup(f); recalc(); val v = ev(10, 900); char o[200]; str(v, o); if (strcmp(o, want)) { printf("FALLO %-45s -> %s (esperaba %s)\n", f, o, want); fails++; } free(C[IDX(10, 900)]); C[IDX(10, 900)] = 0; }
int main(void) { C = calloc(RN * CN, sizeof *C); V = malloc(RN * CN * sizeof *V); St = calloc(RN, CN);
  put("A1","10"); put("A2","20"); put("A3","30"); put("A4","texto"); put("A5","40");
  put("B1","manzana"); put("B2","pera"); put("B3","manzana"); put("B4","kiwi"); put("B5","pera");
  put("C1","1"); put("C2","2"); put("C3","3"); put("C4","4"); put("C5","5");
  put("D1","15/03/2026"); put("E1","Ana"); put("E2","Luis"); put("F1","100"); put("F2","200");
  chk("=SUMA(A1:A5)","100"); chk("=SUM(A1:A3;100)","160"); chk("=PROMEDIO(A1:A5)","25"); chk("=CONTAR(A1:A5)","4"); chk("=CONTARA(A1:A5)","5"); chk("=CONTAR.BLANCO(A1:A7)","2");
  chk("=MAX(A1:A5)","40"); chk("=MIN(A1:A5)","10"); chk("=PRODUCTO(C1:C4)","24"); chk("=MEDIANA(C1:C4)","2.5"); chk("=MODA(1;2;2;3)","2"); chk("=REDONDEAR(DESVEST(C1:C5);4)","1.5811");
  chk("=SUMAR.SI(B1:B5;\"manzana\";A1:A5)","40"); chk("=CONTAR.SI(B1:B5;\"pera\")","2"); chk("=CONTAR.SI(A1:A5;\">15\")","3"); chk("=SUMAR.SI(C1:C5;\">=3\")","12"); chk("=PROMEDIO.SI(B1:B5;\"p*\";C1:C5)","3.5");
  chk("=SUMAPRODUCTO(C1:C3;A1:A3)","140"); chk("=K.ESIMO.MAYOR(C1:C5;2)","4"); chk("=K.ESIMO.MENOR(C1:C5;2)","2"); chk("=JERARQUIA(4;C1:C5)","2");
  chk("=SI(A1>5;\"alto\";\"bajo\")","alto"); chk("=SI.ERROR(1/0;\"error\")","error"); chk("=Y(1;0)","0"); chk("=O(1;0)","1"); chk("=NO(0)","1"); chk("=ESBLANCO(A9)","1"); chk("=ESNUMERO(A1)","1"); chk("=ESTEXTO(A4)","1"); chk("=ELEGIR(2;\"a\";\"b\";\"c\")","b");
  chk("=IZQUIERDA(\"Hola mundo\";4)","Hola"); chk("=DERECHA(\"Hola mundo\";5)","mundo"); chk("=EXTRAE(\"Hola mundo\";6;3)","mun"); chk("=MAYUSC(\"cañón\")","CAÑÓN"); chk("=MINUSC(\"ÁRBOL\")","árbol"); chk("=NOMPROPIO(\"juan pérez\")","Juan Pérez");
  chk("=ESPACIOS(\"  a   b  \")","a b"); chk("=ENCONTRAR(\"m\";\"Hola mundo\")","6"); chk("=HALLAR(\"MUN\";\"Hola mundo\")","6"); chk("=SUSTITUIR(\"a-b-c\";\"-\";\"+\")","a+b+c"); chk("=REPETIR(\"ab\";3)","ababab"); chk("=LARGO(\"cañón\")","5");
  chk("=VALOR(\"3,5\")+1","4.5"); chk("=IGUAL(\"a\";\"A\")","0"); chk("=CONCATENAR(E1;\" y \";E2)","Ana y Luis"); chk("=TEXTO(3,14159;\"0,00\")","3,14");
  chk("=BUSCARV(\"Luis\";E1:F2;2;FALSO)","200"); chk("=BUSCARV(25;A1:C3;3)","2"); chk("=INDICE(C1:C5;3)","3"); chk("=INDICE(A1:C3;2;3)","2"); chk("=COINCIDIR(\"pera\";B1:B5;0)","2"); chk("=BUSCARH(2;C1:C1;1)","1"); chk("=BUSCARH(0;C1:C1;1)","#N/D"); chk("=SUMA(1,5;2)","3.5"); chk("=REDONDEAR(1,23456;2)","1.23");
  chk("=AÑO(D1)","2026"); chk("=MES(D1)","3"); chk("=DIA(D1)","15"); chk("=DIAS(\"20/03/2026\";D1)","5"); chk("=FECHA(2026;2;30)","02/03/2026"); chk("=HOY()","22/11/2026"); chk("=DIASEM(\"22/11/2026\")","1");
  chk("=REDONDEAR(PAGO(0,05/12;12;1000);2)","-85.61"); chk("=RESIDUO(-7;3)","2"); chk("=REDONDEAR.MAS(2,121;2)","2.13"); chk("=REDONDEAR.MENOS(-2,129;2)","-2.12"); chk("=TRUNCAR(3,99)","3"); chk("=MULTIPLO.SUPERIOR(7;5)","10"); chk("=MULTIPLO.INFERIOR(7;5)","5");
  chk("=LOG(8;2)","3"); chk("=LOG10(1000)","3"); chk("=GRADOS(PI())","180"); chk("=SIGNO(-3)","-1"); chk("=ABS(-2)+RAIZ(16)","6"); chk("=POTENCIA(2;10)","1024"); chk("=SUMA(A1:A3)*2&\" euros\"","120 euros"); chk("=NOEXISTE(1)","#NOMBRE");
  printf("%d fallos\n", fails); return fails != 0; }