# AWS Network Load Balancer SSL 설정 가이드

이 가이드는 CherryRecorder Server를 위한 NLB SSL termination 설정 방법을 설명합니다.

## 아키텍처 개요

```
Client → [HTTPS:58080/WSS:33335] → NLB → [HTTP:8080/WS:33334] → ECS Container
```

- **클라이언트**: HTTPS(58080)와 WSS(33335)로 접속
- **NLB**: SSL termination 수행 (각 포트별 독립적인 리스너)
- **컨테이너**: 평문 HTTP(8080)와 WS(33334) 제공

## NLB 설정 단계

### 1. SSL 인증서 준비

AWS Certificate Manager(ACM)에서 SSL 인증서를 생성하거나 가져오기:

```bash
# ACM에서 인증서 요청 (DNS 검증 권장)
aws acm request-certificate \
  --domain-name "your-domain.com" \
  --validation-method DNS \
  --region ap-northeast-2
```

### 2. Target Groups 생성

#### HTTP Target Group (포트 8080)
```bash
aws elbv2 create-target-group \
  --name cherryrecorder-http-tg \
  --protocol TCP \
  --port 8080 \
  --vpc-id vpc-xxxxx \
  --target-type ip \
  --health-check-protocol HTTP \
  --health-check-path /health \
  --health-check-interval-seconds 30 \
  --healthy-threshold-count 2 \
  --unhealthy-threshold-count 2
```

#### WebSocket Target Group (포트 33334)
```bash
aws elbv2 create-target-group \
  --name cherryrecorder-ws-tg \
  --protocol TCP \
  --port 33334 \
  --vpc-id vpc-xxxxx \
  --target-type ip \
  --health-check-protocol TCP \
  --health-check-interval-seconds 30 \
  --healthy-threshold-count 2 \
  --unhealthy-threshold-count 2
```

### 3. NLB 리스너 설정

#### HTTPS 리스너 (58080 → 8080)
```bash
aws elbv2 create-listener \
  --load-balancer-arn arn:aws:elasticloadbalancing:... \
  --protocol TLS \
  --port 58080 \
  --certificates CertificateArn=arn:aws:acm:... \
  --default-actions Type=forward,TargetGroupArn=arn:aws:elasticloadbalancing:.../cherryrecorder-http-tg
```

#### WSS 리스너 (33335 → 33334)
```bash
aws elbv2 create-listener \
  --load-balancer-arn arn:aws:elasticloadbalancing:... \
  --protocol TLS \
  --port 33335 \
  --certificates CertificateArn=arn:aws:acm:... \
  --default-actions Type=forward,TargetGroupArn=arn:aws:elasticloadbalancing:.../cherryrecorder-ws-tg
```

### 5. ECS 서비스 업데이트

Target Group을 ECS 서비스에 연결:

```bash
aws ecs update-service \
  --cluster your-cluster \
  --service cherryrecorder-service \
  --load-balancers targetGroupArn=arn:aws:elasticloadbalancing:.../cherryrecorder-http-tg,containerName=cherryrecorder-server,containerPort=8080 \
  --load-balancers targetGroupArn=arn:aws:elasticloadbalancing:.../cherryrecorder-ws-tg,containerName=cherryrecorder-server,containerPort=33334
```

## 보안 그룹 설정

### NLB 보안 그룹
- Inbound: 
  - 58080/tcp from 0.0.0.0/0 (HTTPS)
  - 33335/tcp from 0.0.0.0/0 (WSS)
- Outbound:
  - 8080/tcp to ECS 보안 그룹
  - 33334/tcp to ECS 보안 그룹

### ECS 보안 그룹
- Inbound:
  - 8080/tcp from NLB 보안 그룹
  - 33334/tcp from NLB 보안 그룹
- Outbound:
  - 443/tcp to 0.0.0.0/0 (외부 API 호출용)

## 클라이언트 접속 방법

### HTTPS 접속
```bash
curl https://your-nlb-domain.com/health
```

### WSS 접속
```javascript
const ws = new WebSocket('wss://your-nlb-domain.com:33335/ws/chat');
```

## 문제 해결

### 1. 502 Bad Gateway
- Target Group 헬스체크 확인
- ECS 태스크가 실행 중인지 확인
- 보안 그룹 규칙 확인

### 2. SSL 인증서 오류
- ACM 인증서가 검증되었는지 확인
- 인증서 도메인이 접속 도메인과 일치하는지 확인

### 3. WebSocket 연결 실패
- NLB가 WebSocket을 지원하는지 확인 (기본 지원)
- 타임아웃 설정 확인 (기본 350초)

### 4. 간헐적 연결 문제
- Target Group의 Deregistration delay 조정 (기본 300초 → 30초)
- ECS 서비스의 헬스체크 설정 최적화

## 모니터링

CloudWatch 메트릭 확인:
- `HealthyHostCount`: 정상 타겟 수
- `UnHealthyHostCount`: 비정상 타겟 수
- `TargetResponseTime`: 응답 시간
- `ActiveConnectionCount`: 활성 연결 수
