#!/usr/bin/env bash
# Generate a self-signed CA, server cert, and client cert into the directory
# given by $1 (default ./certs). Used by integration tests and for manual
# experimentation. NEVER use these for anything but local testing.
set -euo pipefail

OUT_DIR="${1:-certs}"
mkdir -p "$OUT_DIR"
cd "$OUT_DIR"

# Idempotent: regenerate only if a marker is missing.
if [[ -f .generated && -f ca.crt && -f server.crt && -f server.key && -f client.crt && -f client.key ]]; then
    echo "test certs already present in $OUT_DIR"
    exit 0
fi

# OpenSSL config for the server cert (with SAN for localhost / 127.0.0.1).
cat > server.cnf <<'EOF'
[req]
distinguished_name = req_distinguished_name
prompt             = no
[req_distinguished_name]
CN = localhost
[v3_req]
subjectAltName = DNS:localhost,IP:127.0.0.1
EOF

# 1) CA — self-signed root.
openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout ca.key -out ca.crt \
    -subj "/CN=ftx-test-ca" >/dev/null 2>&1

# 2) Server CSR + cert signed by CA.
openssl req -new -newkey rsa:2048 -nodes \
    -keyout server.key -out server.csr \
    -config server.cnf >/dev/null 2>&1

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server.crt -days 30 \
    -extfile server.cnf -extensions v3_req >/dev/null 2>&1

# 3) Client CSR + cert signed by CA.
openssl req -new -newkey rsa:2048 -nodes \
    -keyout client.key -out client.csr \
    -subj "/CN=ftx-test-client" >/dev/null 2>&1

openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out client.crt -days 30 >/dev/null 2>&1

# Tidy.
rm -f server.csr client.csr server.cnf ca.srl
touch .generated
echo "generated test certs in $OUT_DIR"
