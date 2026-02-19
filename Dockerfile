# syntax=docker/dockerfile:1
# ======================================================================
# Dockerfile for CherryRecorder Server (Optimized Multi-stage Build with Boost.Beast)
# ======================================================================

# ----------------------------------------------------------------------
# 스테이지 1: "builder" - 애플리케이션 빌드 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

LABEL stage="builder"

ENV DEBIAN_FRONTEND=noninteractive

# --- STEP 1: 빌드 도구 및 Boost.Beast 의존성 설치 ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    ninja-build \
    libssl-dev \
    gperf \
    libunwind-dev \
    autoconf \
    automake \
    libtool \
    linux-libc-dev \
    python3 \
    python3-setuptools \
    && rm -rf /var/lib/apt/lists/*

# --- STEP 1.5: 최신 CMake 설치 ---
ARG CMAKE_VERSION=3.30.1
ARG TARGETARCH
RUN \
    # TARGETARCH가 없으면(로컬 빌드), 현재 아키텍처(uname -m)를 사용
    BUILD_ARCH=${TARGETARCH:-$(uname -m)} \
    && case ${BUILD_ARCH} in \
        "amd64" | "x86_64") CMAKE_ARCH="x86_64" ;; \
        "arm64" | "aarch64") CMAKE_ARCH="aarch64" ;; \
        *) echo "Unsupported architecture: ${BUILD_ARCH}"; exit 1 ;; \
    esac \
    && wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.sh \
    -q -O /tmp/cmake-install.sh \
    && chmod +x /tmp/cmake-install.sh \
    && /tmp/cmake-install.sh --skip-license --prefix=/usr/local \
    && rm /tmp/cmake-install.sh

ENV PATH=/usr/local/bin:$PATH

# --- STEP 2: vcpkg 설치 (특정 버전 고정) ---
ARG VCPKG_COMMIT_HASH=b02e341c927f16d991edbd915d8ea43eac52096c
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    git checkout ${VCPKG_COMMIT_HASH} && \
    cd .. && \
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# vcpkg 빌드 최적화 환경 변수
ARG VCPKG_MAX_CONCURRENCY=4
ENV VCPKG_MAX_CONCURRENCY=${VCPKG_MAX_CONCURRENCY}
ENV VCPKG_DISABLE_METRICS=1
# 아키텍처에 따른 triplet 설정
ARG TARGETARCH
ENV VCPKG_DEFAULT_TRIPLET=${TARGETARCH:+${TARGETARCH}-linux-ecs}${TARGETARCH:-x64-linux-ecs}
# 추가 최적화: 병렬 다운로드 증가, 빌드 타입 최적화
ENV VCPKG_DOWNLOADS_CONCURRENCY=8
ENV VCPKG_BUILD_TYPE=release
ENV VCPKG_INSTALL_OPTIONS="--x-use-aria2"
# vcpkg 캐싱 개선
ENV VCPKG_FEATURE_FLAGS="manifests,binarycaching"
ENV VCPKG_KEEP_ENV_VARS="VCPKG_BINARY_SOURCES"

# vcpkg 바이너리 캐시 경로를 빌드 인수로 받음
ARG VCPKG_BINARY_SOURCES="clear;default,readwrite"
ENV VCPKG_BINARY_SOURCES=${VCPKG_BINARY_SOURCES}

# aria2c 설치 (더 빠른 다운로드를 위해)
RUN apt-get update && apt-get install -y --no-install-recommends aria2 && rm -rf /var/lib/apt/lists/*

# --- STEP 3: 의존성 설치 (vcpkg) ---
# vcpkg.json과 CMakeLists.txt를 먼저 복사하여 의존성 레이어를 분리합니다.
# 이 파일들이 변경되지 않으면 아래 RUN 레이어는 캐시됩니다.
WORKDIR /app
COPY vcpkg.json .
COPY CMakeLists.txt .
# 커스텀 triplet 파일 복사
COPY x64-linux-ecs.cmake /opt/vcpkg/triplets/
COPY arm64-linux-ecs.cmake /opt/vcpkg/triplets/

# 더미 소스 파일 및 디렉토리 생성 (CMake 구성이 실패하지 않도록)
# CMakeLists.txt에 명시된 모든 소스 파일 경로를 기반으로 빈 파일을 생성합니다.
RUN mkdir -p src/handlers && \
    touch src/main.cpp \
          src/HttpServer.cpp \
          src/ChatServer.cpp \
          src/ChatSession.cpp \
          src/ChatRoom.cpp \
          src/ChatListener.cpp \
          src/MessageHistory.cpp \
          src/WebSocketListener.cpp \
          src/WebSocketSession.cpp \
          src/handlers/PlacesApiHandler.cpp && \
    mkdir -p include/handlers && \
    touch include/HttpServer.hpp \
          include/ChatServer.hpp \
          include/ChatSession.hpp \
          include/ChatRoom.hpp \
          include/ChatListener.hpp \
          include/MessageHistory.hpp \
          include/SessionInterface.hpp \
          include/WebSocketListener.hpp \
          include/WebSocketSession.hpp \
          include/handlers/PlacesApiHandler.hpp

# vcpkg 의존성을 설치합니다.
# 이 단계는 시간이 오래 걸리지만, vcpkg.json이 변경되지 않는 한 캐시됩니다.
# Architecture 감지 및 triplet 설정
ARG TARGETARCH
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    --mount=type=cache,target=/opt/vcpkg/downloads \
    --mount=type=cache,target=/opt/vcpkg/buildtrees \
    --mount=type=cache,target=/opt/vcpkg/packages \
    --mount=type=cache,target=/opt/vcpkg/installed \
    --mount=type=cache,target=/app/build/vcpkg_installed \
    --mount=type=cache,target=/tmp/vcpkg \
    # 1. vcpkg로 의존성 명시적 설치 (ECS 호환 triplet 사용)
    # TARGETARCH 디버깅 및 triplet 설정
    echo "TARGETARCH value: '${TARGETARCH}'" && \
    if [ -z "${TARGETARCH}" ]; then \
        TRIPLET="x64-linux-ecs"; \
    elif [ "${TARGETARCH}" = "amd64" ]; then \
        TRIPLET="x64-linux-ecs"; \
    elif [ "${TARGETARCH}" = "arm64" ]; then \
        TRIPLET="arm64-linux-ecs"; \
    else \
        echo "Unknown TARGETARCH: ${TARGETARCH}"; exit 1; \
    fi && \
    echo "Using triplet: $TRIPLET" && \
    /opt/vcpkg/vcpkg install --triplet $TRIPLET --clean-after-build && \
    # 2. CMake 실행
    cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=$TRIPLET \
      -DBUILD_TESTING=OFF \
      -DCMAKE_VERBOSE_MAKEFILE=ON && \
    # 설치된 패키지 정보 출력
    echo "Installed vcpkg packages:" && \
    /opt/vcpkg/vcpkg list

# --- STEP 4: 소스 코드 복사 및 빌드 ---
# 실제 소스 코드를 복사합니다. 이 단계부터는 소스 코드가 변경될 때마다 다시 실행됩니다.
COPY src/ src/
COPY include/ include/

# SSL 인증서 파일은 AWS NLB에서 SSL termination을 처리하므로 컨테이너에서는 필요하지 않음
# 로컬 개발이나 자체 HTTPS가 필요한 경우 아래 주석을 해제
# COPY cert.pem ./cert.pem
# COPY key.pem ./key.pem

# 이미 구성된 빌드 디렉터리를 사용하여 애플리케이션을 빌드합니다.
# ARM64에서는 메모리 제약을 고려하여 병렬성 조정
ARG TARGETARCH
RUN if [ "${TARGETARCH}" = "arm64" ]; then \
        cmake --build build --target CherryRecorder-Server-App -j 2; \
    else \
        cmake --build build --target CherryRecorder-Server-App -j $(nproc); \
    fi

# --- STEP 5: 빌드된 실행 파일 의존성 확인 ---
# ldd를 사용하여 실행 파일의 동적 라이브러리 의존성을 확인합니다 (디버깅용).
RUN ldd /app/build/CherryRecorder-Server-App || echo "ldd check skipped or failed"

# ----------------------------------------------------------------------
# 스테이지 2: "final" - 애플리케이션 실행 전용 환경
# ----------------------------------------------------------------------
FROM ubuntu:24.04 AS final

LABEL maintainer="Kim Hyeonwoo <ialskdji@gmail.com>" \
      description="CherryRecorder Server Application - Beast HTTP/S, WebSocket Services" \
      version="0.3.0"

ENV DEBIAN_FRONTEND=noninteractive

# 런타임 의존성 설치
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libssl3 \
    libunwind8 \
    libgoogle-glog0v6 \
    libgflags2.2 \
    libzstd1 \
    libsodium23 \
    libdouble-conversion3 \
    libc6-dev \
    strace \
    && rm -rf /var/lib/apt/lists/*

# Static 링크를 사용하므로 라이브러리 복사 불필요
# 필요한 런타임 의존성만 이미 apt-get으로 설치됨

# 사용자 생성 및 디렉토리 설정
RUN useradd --system --create-home --shell /bin/bash appuser
WORKDIR /home/appuser/app

# 최종 실행 파일 및 인증서 복사
COPY --from=builder --chown=appuser:appuser /app/build/CherryRecorder-Server-App ./CherryRecorder-App
# SSL 인증서는 AWS NLB에서 처리하므로 복사하지 않음
# COPY --from=builder --chown=appuser:appuser /app/cert.pem ./cert.pem
# COPY --from=builder --chown=appuser:appuser /app/key.pem ./key.pem

# 애플리케이션이 사용할 디렉토리 생성 및 권한 설정
RUN mkdir -p history && chown appuser:appuser history

# 사용자 전환
USER appuser

# ECS/EC2 환경에서 libevent 실행을 위한 환경 변수 설정
# EventBase backend 설정 - poll 백엔드 우선 사용
ENV FOLLY_EVENTBASE_BACKEND=poll
ENV FOLLY_DISABLE_EPOLL=1
ENV FOLLY_USE_EPOLL=0
# libevent 백엔드 강제 설정
ENV EVENT_NOKQUEUE=1
ENV EVENT_NOPOLL=0
ENV EVENT_NODEVPOLL=1
ENV EVENT_NOEVPORT=1
ENV EVENT_NOSELECT=0
ENV EVENT_NOEPOLL=1
# libevent 디버깅
ENV EVENT_DEBUG_MODE=1
ENV EVENT_SHOW_METHOD=1
# 추가 디버깅을 위한 로깅
ENV GLOG_minloglevel=0
ENV GLOG_v=2
ENV GLOG_logtostderr=1
# ECS 네트워킹 모드를 위한 추가 설정
ENV FOLLY_EVENTBASE_THREAD_FACTORY_BACKEND=std
ENV FOLLY_DISABLE_THREAD_LOCAL_SINGLETON_CTOR=1

# 기본 포트 노출 (HTTP, WS)
EXPOSE 8080 33334

# Health Check (HTTP 포트 사용)
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
  CMD curl --fail http://127.0.0.1:${HTTP_PORT:-8080}/health || exit 1

# --- 컨테이너 실행 가이드 ---
# docker-compose.yml 또는 docker run 명령어의 --env-file 옵션을 통해 환경 변수 주입
# AWS 환경에서는 NLB가 SSL termination을 처리하므로 HTTP 포트만 사용
# 자체 HTTPS가 필요한 경우 cert.pem과 key.pem 파일을 추가하고 아래 CMD 사용
# CMD ["./CherryRecorder-App", "--cert_path=./cert.pem", "--key_path=./key.pem"]

CMD ["./CherryRecorder-App"]