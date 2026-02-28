# CherryRecorder Server

CherryRecorder 클라이언트의 장소 검색과 실시간 통신을 담당하는 C++20 서버 복원본입니다. Boost.Asio/Beast 기반 HTTP·WebSocket 구조는 유지하고, 현재 CMake/Homebrew 환경에서 다시 빌드·기동할 수 있도록 툴체인 호환성만 정리했습니다.

> 과거 팀 프로젝트의 개인 연락처·개발자 목록·폐기된 운영 주소는 문서에서 제거했습니다. API 구조와 서버 동작은 원래 코드베이스를 기준으로 보존했습니다.

## 주요 기능

- `GET /health` — HTTP health check
- `GET /status` — JSON status
- `GET /maps/key` — 서버 환경변수 기반 Maps key 전달
- `POST /places/nearby` — 주변 장소 검색
- `POST /places/search` — 텍스트 장소 검색
- `GET /places/details/{placeId}` — 장소 상세 조회
- `GET /place/photo/{photoReference}` — 장소 사진 조회
- Boost.Asio 기반 WebSocket 서버

## Stack

- C++20
- Boost.Asio / Boost.Beast
- OpenSSL
- spdlog
- CMake
- optional vcpkg toolchain

## Build

Homebrew 환경 예시:

```bash
brew install cmake boost spdlog openssl@3

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF

cmake --build build --target CherryRecorder-Server-App -j 4
```

실행:

```bash
export HTTP_PORT=8080
export WS_PORT=33334
export GOOGLE_MAPS_API_KEY="..." # Places 기능을 사용할 때 필요
./build/CherryRecorder-Server-App
```

현재 복원본은 `GOOGLE_MAPS_API_KEY`가 없어도 서버 전체가 종료되지 않고 health/WebSocket 기반은 기동됩니다. Places 요청만 key 설정을 요구합니다.

## Runtime verification

Release binary를 실제 실행해 다음을 확인했습니다.

```text
GET /health -> 200 OK / "OK"
GET /maps/key without key -> 400
graceful shutdown -> success
```

## Architecture

```text
Flutter Client
   ├── HTTP ──────> HttpServer / Boost.Beast
   │                 └── PlacesApiHandler ──> Google Places API
   └── WebSocket ──> ChatServer / Boost.Asio
```

이번 복원에서는 별도의 새로운 persistence/domain API를 추가하지 않았습니다.
