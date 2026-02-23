# GitHub Actions Variables 설정 가이드 (SSL/TLS)

이 가이드는 ECS에서 HTTPS/WSS를 올바르게 작동시키기 위한 GitHub Variables 설정 방법을 설명합니다.

## 현재 문제

1. 컨테이너는 HTTP(8080)와 WS(33334)만 제공
2. NLB가 SSL termination을 수행해야 함 (HTTPS→HTTP, WSS→WS)
3. Connection check가 잘못된 포트로 연결 시도

## 필수 GitHub Variables 설정

GitHub 저장소 → Settings → Secrets and variables → Actions → Variables 탭에서 다음 변수들을 설정하세요:

### 1. NLB 관련 변수

```yaml
# NLB 도메인 (SSL 인증서가 설치된 도메인)
SERVER_ADDRESS: your-domain.com  # 또는 your-nlb.elb.amazonaws.com

# NLB 리스너 포트 (SSL termination이 이루어지는 포트)
NLB_HTTP_PORT: 58080      # HTTPS 리스너
NLB_WEBSOCKET_PORT: 33335 # WSS 리스너
```

### 2. 컨테이너 포트 (변경하지 마세요)

```yaml
# 컨테이너 내부 포트 (평문 HTTP/WS)
HTTP_PORT_VALUE: 8080       # HTTP 서버 포트
WS_PORT_VALUE: 33334        # WebSocket 서버 포트
HEALTH_CHECK_PORT_VALUE: 8080 # 헬스체크 포트
```

### 3. ECS 설정

```yaml
# ECS 클러스터 및 서비스
ECS_CLUSTER_NAME: your-cluster-name
ECS_SERVICE_NAME: cherryrecorder-service
ECS_TASK_DEF_FAMILY: cherryrecorder-task
CONTAINER_NAME: cherryrecorder-server

# 네트워크 설정
SUBNET_IDS: subnet-xxx,subnet-yyy  # Private 서브넷 권장
SECURITY_GROUP_IDS: sg-xxx         # ECS 태스크용 보안 그룹
```

## NLB 설정 확인사항

### 1. Target Groups

- **HTTP Target Group**
  - 프로토콜: TCP
  - 포트: 8080
  - 헬스체크: HTTP, 경로 /health

- **WS Target Group**
  - 프로토콜: TCP
  - 포트: 33334
  - 헬스체크: TCP

### 2. 리스너 설정

- **HTTPS 리스너 (포트 443)**
  - 프로토콜: TLS
  - 인증서: ACM 인증서
  - 기본 동작: HTTP Target Group으로 전달

- **WSS 경로 기반 라우팅** (선택사항)
  - 조건: 경로가 /ws/* 인 경우
  - 동작: WS Target Group으로 전달

### 3. 보안 그룹

**NLB 보안 그룹 (인바운드)**:
- 443/tcp from 0.0.0.0/0 (HTTPS/WSS)

**ECS 보안 그룹 (인바운드)**:
- 8080/tcp from NLB 보안 그룹
- 33334/tcp from NLB 보안 그룹

## 검증 방법

### 1. 로컬에서 테스트

```bash
# HTTPS 헬스체크
curl -v https://your-domain.com/health

# WSS 연결 테스트
wscat -c wss://your-domain.com/ws/chat
```

### 2. 디버깅 스크립트 실행

```bash
chmod +x debug-ecs-ssl.sh
./debug-ecs-ssl.sh your-domain.com your-cluster cherryrecorder-service ap-northeast-2
```

### 3. CloudWatch 로그 확인

```bash
# 최근 SSL 관련 로그
aws logs filter-log-events \
  --log-group-name /ecs/cherryrecorder-task \
  --filter-pattern "SSL OR certificate OR https OR wss" \
  --max-items 20
```

## 문제 해결

### HTTPS가 전혀 작동하지 않을 때

1. **NLB 리스너 확인**: 포트 443에 TLS 리스너가 있는지 확인
2. **ACM 인증서**: 인증서가 검증되었고 NLB에 연결되었는지 확인
3. **Target Group**: HTTP(8080) 타겟이 healthy 상태인지 확인
4. **보안 그룹**: NLB → ECS 간 8080 포트 통신 허용 확인

### WSS가 간헐적으로 작동할 때

1. **Connection draining**: Target Group의 deregistration delay를 30초로 줄이기
2. **헬스체크 간격**: 더 자주, 빠르게 체크하도록 조정
3. **ECS 태스크 개수**: 최소 2개 이상 유지하여 가용성 확보
4. **Keep-alive 설정**: NLB의 idle timeout을 늘리기 (기본 350초)

## 권장 아키텍처

```
Internet → Route53 → NLB → ECS Tasks
                       ├─ TLS:58080 → TCP:8080 (HTTPS → HTTP)
                       └─ TLS:33335 → TCP:33334 (WSS → WS)
                       ↓
                 ACM Certificate
```

- **Route53**: 도메인을 NLB로 라우팅
- **NLB**: SSL termination 수행
- **ECS**: 평문 HTTP/WS 서비스 제공
