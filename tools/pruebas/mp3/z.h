/* z.h minimo para compilar el codificador y el decodificador MP3 (user/lib/zmp3.c y zmp3d.c) en el Mac */
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32;
#define z_sqrt sqrt
#define z_cos cos
#define z_sin sin
#define z_pow pow
#define z_tan tan
typedef struct zmp3 zmp3; typedef struct zmp3d zmp3d;
