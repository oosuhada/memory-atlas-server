# Multi-Architecture Build Guide for CherryRecorder-Server

이 가이드는 CherryRecorder-Server를 AMD64와 ARM64(라즈베리파이5) 아키텍처 모두에서 실행할 수 있도록 빌드하는 방법을 설명합니다.

## 로컬 빌드

### 단일 아키텍처 빌드 (현재 시스템용)
```bash
# 현재 시스템 아키텍처 자동 감지하여 빌드
python docker_manager.py --target app

# 특정 아키텍처 지정
python docker_manager.py --target app --platform amd64
python docker_manager.py --target app --platform arm64
```

### 멀티 아키텍처 빌드
```bash
# 두 아키텍처 모두 빌드 (push 없이는 로컬에서 실행 불가)
python docker_manager.py --target app --platform both

# 레지스트리에 푸시하면서 빌드
python docker_manager.py --target app --platform both --push
```

## CI/CD 파이프라인

GitHub Actions를 통해 자동으로 멀티 아키텍처 이미지가 빌드되고 배포됩니다:

1. **빌드**: linux/amd64와 linux/arm64 모두 지원하는 이미지 빌드
2. **GHCR 푸시**: GitHub Container Registry에 멀티 아키텍처 이미지 푸시
3. **Docker Hub 푸시**: Docker Hub에 멀티 아키텍처 이미지 푸시
4. **ECR 푸시**: AWS ECR에 멀티 아키텍처 이미지 푸시

## 라즈베리파이5에서 실행

### 1. Docker 설치
```bash
# 라즈베리파이5 (Ubuntu 또는 Raspberry Pi OS)
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
```

### 2. 이미지 다운로드 및 실행
```bash
# Docker Hub에서 이미지 다운로드 (자동으로 ARM64 버전 선택)
docker pull <your-dockerhub-username>/cherryrecorder-server:latest

# 컨테이너 실행
docker run -d \
  --name cherryrecorder-server \
  -p 8080:8080 \
  -p 33334:33334 \
  --env-file .env.docker \
  <your-dockerhub-username>/cherryrecorder-server:latest
```

### 3. 리소스 최적화 (라즈베리파이5)
```bash
# 메모리 제한 설정 (라즈베리파이5는 8GB RAM)
docker run -d \
  --name cherryrecorder-server \
  --memory="2g" \
  --cpus="2.0" \
  -p 8080:8080 \
  -p 33334:33334 \
  --env-file .env.docker \
  <your-dockerhub-username>/cherryrecorder-server:latest
```

## 아키텍처별 최적화

### AMD64 (x86_64)
- 기본 최적화 플래그 사용
- 병렬 빌드 최대화 (`-j $(nproc)`)

### ARM64 (라즈베리파이5)
- ARM64 특화 최적화 플래그: `-march=armv8-a+crc+crypto`
- 메모리 제약을 고려한 병렬 빌드 제한 (`-j 2`)
- vcpkg triplet: `arm64-linux-ecs.cmake`

## 문제 해결

### 빌드 실패 시
1. Docker Buildx 설치 확인: `docker buildx version`
2. QEMU 에뮬레이션 확인: `docker run --rm --privileged multiarch/qemu-user-static --reset -p yes`
3. 빌드 캐시 정리: `docker buildx prune -f`

### 실행 시 문제
1. 아키텍처 확인: `docker inspect <image> | grep Architecture`
2. 로그 확인: `docker logs cherryrecorder-server`
3. 리소스 사용량 확인: `docker stats cherryrecorder-server`

## 주의사항

- 멀티 아키텍처 빌드는 시간이 오래 걸립니다 (특히 첫 빌드)
- ARM64 빌드는 에뮬레이션으로 인해 더 느릴 수 있습니다
- 라즈베리파이5에서는 메모리 사용량을 모니터링하세요
