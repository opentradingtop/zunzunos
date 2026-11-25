#!/bin/bash
# El TLS propio cuando el servidor pide certificado de cliente: openssl s_server -verify (TLS 1.3 y 1.2) y, si hay red,
# smtp.gmail.com:465 de verdad (solo el saludo 220; no se entra en ninguna cuenta). Uso: correr.sh DIR
set -e; cd "$(dirname "$0")"; D="${1:-/tmp/zz-tlscli}"; mkdir -p "$D"; R=../../..
cc -O2 -w -std=gnu11 -DZC_HOST -I$R/user/lib -o "$D/t" t.c $R/user/lib/ztls.c $R/user/lib/zcrypto.c
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/k.pem" -out "$D/c.pem" -days 2 -subj /CN=localhost >/dev/null 2>&1
mal=0
for v in 1.3 1.2; do
  # -verify 1: pide certificado de cliente, pero lo deja pasar sin el
  openssl s_server -accept 8443 -cert "$D/c.pem" -key "$D/k.pem" -verify 1 -tls${v/./_} -www >"$D/srv-$v.log" 2>&1 & S=$!; sleep 0.8
  perl -e 'alarm(15); exec(@ARGV)' "$D/t" $R/user/res/raices.bin localhost 8443 www || mal=1; kill $S 2>/dev/null; wait $S 2>/dev/null || true
done
if host smtp.gmail.com >/dev/null 2>&1; then perl -e 'alarm(15); exec(@ARGV)' "$D/t" $R/user/res/raices.bin smtp.gmail.com 465 smtp || mal=1; fi
[ $mal = 0 ] && echo "tls con certificado de cliente: OK" || { echo "tls con certificado de cliente: FALLO"; exit 1; }
