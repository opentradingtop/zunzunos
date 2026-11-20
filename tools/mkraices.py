#!/usr/bin/env python3
"""Convierte el almacen de autoridades raiz de Mozilla (cacert.pem, licencia MPL 2.0) en user/res/raices.bin:
'ZRA1', numero (u32) y por cada raiz: u16 + sujeto DER, u16 + clave publica (SubjectPublicKeyInfo) DER."""
import base64, struct, sys
src = sys.argv[1] if len(sys.argv) > 1 else 'tools/datos/cacert.pem'; dst = sys.argv[2] if len(sys.argv) > 2 else 'user/res/raices.bin'
def tlv(b, i):
    t = b[i]; l = b[i + 1]; i += 2
    if l & 0x80: n = l & 0x7f; l = int.from_bytes(b[i:i + n], 'big'); i += n
    return t, i, l   # etiqueta, inicio del contenido, longitud
def elem(b, i):
    t, c, l = tlv(b, i); return b[i:c + l], c + l
pem = open(src).read().split('-----BEGIN CERTIFICATE-----')[1:]; out = []
for p in pem:
    der = base64.b64decode(''.join(p.split('-----END CERTIFICATE-----')[0].split()))
    _, c, _ = tlv(der, 0); _, c, tl = tlv(der, c)  # Certificate -> tbsCertificate
    i = c
    if der[i] == 0xA0: _, i = elem(der, i)          # version
    _, i = elem(der, i); _, i = elem(der, i)        # serial, signature
    _, i = elem(der, i); _, i = elem(der, i)        # issuer, validity
    subj, i = elem(der, i); spki, i = elem(der, i)
    out.append(struct.pack('>H', len(subj)) + subj + struct.pack('>H', len(spki)) + spki)
data = b'ZRA1' + struct.pack('>I', len(out)) + b''.join(out)
open(dst, 'wb').write(data); print(dst, len(out), 'raices', len(data), 'bytes')
