# NLB vs ALB: WebSocket 지원의 차이

## 왜 NLB + TLS에서 WebSocket이 작동하지 않는가?

### 1. OSI 계층의 차이

```
┌─────────────────────────────────────────┐
│         Application Layer (L7)          │ ← ALB가 동작하는 계층
│    HTTP, HTTPS, WebSocket Protocol      │   (프로토콜 내용을 이해함)
├─────────────────────────────────────────┤
│         Transport Layer (L4)            │ ← NLB가 동작하는 계층  
│           TCP, UDP, TLS                 │   (단순 패킷 전달만 함)
└─────────────────────────────────────────┘
```

### 2. WebSocket 연결 과정

WebSocket은 먼저 HTTP로 시작한 후 프로토콜을 업그레이드합니다:

```
1. 클라이언트 → 서버: HTTP 요청
   GET /chat HTTP/1.1
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==

2. 서버 → 클라이언트: HTTP 응답
   HTTP/1.1 101 Switching Protocols
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk=

3. 이후 WebSocket 프로토콜로 통신
```

### 3. NLB의 한계

**NLB + TLS 리스너의 동작 방식:**
```
Client ─[WSS]→ NLB ─[WS]→ Server
         ↓
    SSL 암호화만 해제
    HTTP 헤더를 이해 못함!
```

NLB는:
- ❌ HTTP 프로토콜을 이해하지 못함
- ❌ WebSocket 업그레이드 헤더를 인식하지 못함
- ✅ 단순히 TCP 패킷을 전달만 함

### 4. ALB vs NLB 비교

| 기능 | NLB | ALB |
|------|-----|-----|
| 계층 | Layer 4 (TCP/UDP) | Layer 7 (HTTP/HTTPS) |
| WebSocket 지원 | ❌ 수동 설정 필요 | ✅ 자동 지원 |
| HTTP 헤더 이해 | ❌ | ✅ |
| 경로 기반 라우팅 | ❌ | ✅ |
| 성능 | 매우 빠름 | 빠름 |
| 지연 시간 | 매우 낮음 | 낮음 |

## 해결 방법

### 방법 1: ALB로 변경 (권장) ⭐

ALB는 WebSocket을 자동으로 처리합니다:

```bash
# ALB 생성
aws elbv2 create-load-balancer \
  --name cherryrecorder-alb \
  --type application \
  --subnets <SUBNET_IDS>

# HTTPS 리스너 추가 (WebSocket 자동 지원)
aws elbv2 create-listener \
  --load-balancer-arn <ALB_ARN> \
  --protocol HTTPS \
  --port 58080 \
  --certificates CertificateArn=<ACM_CERT_ARN> \
  --default-actions Type=forward,TargetGroupArn=<TARGET_GROUP_ARN>
```

### 방법 2: NLB 유지 + TCP 패스스루

NLB를 계속 사용해야 한다면:

```bash
# 1. TCP 리스너로 변경 (TLS 종료를 서버에서 처리)
aws elbv2 create-listener \
  --load-balancer-arn <NLB_ARN> \
  --protocol TCP \
  --port 33335 \
  --default-actions Type=forward,TargetGroupArn=<TARGET_GROUP_ARN>
```

서버에서 SSL 처리 필요:
```cpp
// 서버에서 직접 SSL 처리
if (!ssl_cert.empty() && !ssl_key.empty()) {
    // WSS 리스너 생성
    wss_listener = std::make_shared<WebSocketListener>(
        ioc, endpoint, chat_server, ssl_ctx
    );
}
```

### 방법 3: NLB + ALB 조합

고성능이 필요한 경우:
```
Internet → NLB (TCP 33335) → ALB (HTTPS) → ECS
                ↓
         매우 빠른 L4 처리 + WebSocket 지원
```

## 비용 vs 기능 비교

### NLB만 사용
- ✅ 비용: 저렴
- ✅ 성능: 최고
- ❌ WebSocket: 복잡한 설정 필요
- ❌ HTTP 기능: 없음

### ALB만 사용
- ⚠️ 비용: 중간
- ✅ 성능: 충분
- ✅ WebSocket: 자동 지원
- ✅ HTTP 기능: 풍부

### NLB + 서버 SSL
- ✅ 비용: 저렴
- ✅ 성능: 높음
- ⚠️ WebSocket: 서버 설정 필요
- ⚠️ 관리: SSL 인증서 서버 관리

## 권장사항

1. **일반적인 웹 애플리케이션**: ALB 사용 ⭐⭐⭐
   - WebSocket 자동 지원
   - 관리 편의성
   - 충분한 성능

2. **초고성능이 필요한 경우**: NLB + 서버 SSL
   - 매우 낮은 지연시간
   - 높은 처리량
   - 복잡한 설정

3. **현재 상황 (빠른 해결)**: NLB → ALB 변경
   ```bash
   # 1. ALB 생성
   # 2. 동일한 대상 그룹 사용
   # 3. Route 53에서 ALB로 변경
   # 4. NLB 제거
   ```