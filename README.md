# CherryRecorder Server

CherryRecorder Server는 지도 기반 기록 앱의 장소 검색과 실시간 통신을 담당하는 C++20 백엔드입니다. HTTP API와 WebSocket 서버를 하나의 비동기 네트워크 계층에서 제공합니다.

## 주요 기능

- `GET /health` — 서버 상태 확인
- `GET /status` — JSON 상태 정보
- `GET /maps/key` — 서버 환경변수 기반 Maps key 전달
- `POST /places/nearby` — 주변 장소 검색
- `POST /places/search` — 텍스트 장소 검색
- `GET /places/details/{placeId}` — 장소 상세 조회
- `GET /place/photo/{photoReference}` — 장소 사진 조회
- WebSocket 기반 실시간 메시징

## Architecture

```text
Flutter Client
   ├── HTTP ──────> HttpServer / Boost.Beast
   │                 └── PlacesApiHandler ──> Google Places API
   └── WebSocket ──> ChatServer / Boost.Asio
```

## Stack

- C++20
- Boost.Asio / Boost.Beast
- OpenSSL
- spdlog
- CMake
- optional vcpkg toolchain

## Build

```bash
brew install cmake boost spdlog openssl@3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target CherryRecorder-Server-App -j 4
```

## Run

```bash
export HTTP_PORT=8080
export WS_PORT=33334
export GOOGLE_MAPS_API_KEY="..."
./build/CherryRecorder-Server-App
```

장소 검색 기능에는 Google Maps API key가 필요합니다. `.env.example`을 참고해 로컬 환경변수로 설정하세요.
