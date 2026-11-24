/* Colibri: lo que depende del sistema (la hora). En ZunzunOS el reloj del equipo se toma como UTC. */
#include "cbi.h"
#ifdef CB_HOST
#include <time.h>
double cb_date_now(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (double)t.tv_sec * 1000.0 + (double)(t.tv_nsec / 1000000); }
#else
static long days_from_civil(long y, unsigned m, unsigned d) { y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400); unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (long)doe - 719468; }
static double base_ms; static u64 base_tick; static int based;
double cb_date_now(void) {
    if (!based) { u32 t[6]; z_time(t); long y = t[5] < 100 ? 2000 + t[5] : t[5]; base_ms = ((double)days_from_civil(y, t[4], t[3]) * 86400.0 + t[2] * 3600.0 + t[1] * 60.0 + t[0]) * 1000.0; base_tick = z_ms(); based = 1; }
    return base_ms + (double)(z_ms() - base_tick); }
#endif
