#!/bin/bash
# 개발 환경용 자체 서명 SSL 인증서 생성 스크립트

echo "Generating self-signed SSL certificate for development..."

# SSL 디렉토리 생성
mkdir -p ssl/certs ssl/private

# 개인 키 생성
openssl genrsa -out ssl/private/cherryrecorder.key 2048

# 인증서 서명 요청(CSR) 생성
openssl req -new -key ssl/private/cherryrecorder.key -out ssl/certs/cherryrecorder.csr \
  -subj "/C=KR/ST=Seoul/L=Seoul/O=CherryRecorder/CN=localhost"

# 자체 서명 인증서 생성 (1년 유효)
openssl x509 -req -days 365 -in ssl/certs/cherryrecorder.csr \
  -signkey ssl/private/cherryrecorder.key -out ssl/certs/cherryrecorder.crt

# DH 파라미터 생성 (보안 강화를 위해 권장)
echo "Generating DH parameters (this may take a while)..."
openssl dhparam -out ssl/certs/dhparam.pem 2048

# 권한 설정
chmod 600 ssl/private/cherryrecorder.key
chmod 644 ssl/certs/cherryrecorder.crt
chmod 644 ssl/certs/dhparam.pem

echo "SSL certificate generated successfully!"
echo "Certificate: ssl/certs/cherryrecorder.crt"
echo "Private key: ssl/private/cherryrecorder.key"
echo "DH params: ssl/certs/dhparam.pem" 