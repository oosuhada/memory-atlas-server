@echo off
echo Generating test SSL certificate...

REM Check if openssl is available
where openssl >nul 2>nul
if %errorlevel% neq 0 (
    echo OpenSSL not found in PATH. Please install OpenSSL or add it to PATH.
    echo You can download OpenSSL from: https://slproweb.com/products/Win32OpenSSL.html
    pause
    exit /b 1
)

REM Generate private key and certificate
openssl req -x509 -newkey rsa:2048 -keyout test-key.pem -out test-cert.pem -days 365 -nodes -subj "/C=KR/ST=Seoul/L=Seoul/O=CherryRecorder/CN=localhost"

if %errorlevel% neq 0 (
    echo Failed to generate certificate
    pause
    exit /b 1
)

echo Certificate generated successfully!
echo - Certificate: test-cert.pem
echo - Private Key: test-key.pem
pause