#!/bin/bash

# ECS 컨테이너 SSL 문제 디버깅 스크립트

echo "=== ECS SSL/TLS 디버깅 스크립트 ==="
echo "이 스크립트는 ECS에서 HTTPS/WSS 연결 문제를 진단합니다."
echo ""

# 환경 변수로부터 설정 읽기
NLB_DOMAIN="${1:-your-nlb-domain.elb.amazonaws.com}"
CLUSTER_NAME="${2:-your-cluster}"
SERVICE_NAME="${3:-cherryrecorder-service}"
REGION="${4:-ap-northeast-2}"

echo "설정:"
echo "  NLB 도메인: $NLB_DOMAIN"
echo "  ECS 클러스터: $CLUSTER_NAME"
echo "  ECS 서비스: $SERVICE_NAME"
echo "  리전: $REGION"
echo ""

# 1. ECS 태스크 상태 확인
echo "=== 1. ECS 태스크 상태 확인 ==="
aws ecs describe-services \
  --cluster $CLUSTER_NAME \
  --services $SERVICE_NAME \
  --region $REGION \
  --query 'services[0].{RunningCount:runningCount,DesiredCount:desiredCount,PendingCount:pendingCount}' \
  --output table

# 2. 실행 중인 태스크 ID 가져오기
TASK_ARN=$(aws ecs list-tasks \
  --cluster $CLUSTER_NAME \
  --service-name $SERVICE_NAME \
  --region $REGION \
  --query 'taskArns[0]' \
  --output text)

echo ""
echo "실행 중인 태스크: $TASK_ARN"

# 3. 태스크 세부 정보 확인
echo ""
echo "=== 2. 태스크 환경 변수 확인 ==="
aws ecs describe-tasks \
  --cluster $CLUSTER_NAME \
  --tasks $TASK_ARN \
  --region $REGION \
  --query 'tasks[0].containers[0].environment[?name==`USE_SSL` || name==`SSL_CERT_FILE` || name==`SSL_KEY_FILE` || name==`HTTP_PORT` || name==`WS_PORT`]' \
  --output table

# 4. Target Group 헬스 확인
echo ""
echo "=== 3. Target Group 헬스 체크 ==="
# HTTP Target Group
TG_ARN_HTTP=$(aws elbv2 describe-target-groups \
  --names cherryrecorder-http-tg \
  --region $REGION \
  --query 'TargetGroups[0].TargetGroupArn' \
  --output text 2>/dev/null)

if [ ! -z "$TG_ARN_HTTP" ]; then
  echo "HTTP Target Group 헬스:"
  aws elbv2 describe-target-health \
    --target-group-arn $TG_ARN_HTTP \
    --region $REGION \
    --query 'TargetHealthDescriptions[*].{Target:Target.Id,Health:TargetHealth.State,Description:TargetHealth.Description}' \
    --output table
fi

# 5. NLB 리스너 확인
echo ""
echo "=== 4. NLB 리스너 설정 확인 ==="
LB_ARN=$(aws elbv2 describe-load-balancers \
  --region $REGION \
  --query "LoadBalancers[?DNSName=='$NLB_DOMAIN'].LoadBalancerArn | [0]" \
  --output text)

if [ ! -z "$LB_ARN" ] && [ "$LB_ARN" != "None" ]; then
  echo "NLB 리스너:"
  aws elbv2 describe-listeners \
    --load-balancer-arn $LB_ARN \
    --region $REGION \
    --query 'Listeners[*].{Port:Port,Protocol:Protocol,Certificate:Certificates[0].CertificateArn}' \
    --output table
else
  echo "경고: NLB를 찾을 수 없습니다. 도메인 이름을 확인하세요."
fi

# 6. 컨테이너 로그 확인 (최근 10개)
echo ""
echo "=== 5. 컨테이너 최근 로그 (SSL 관련) ==="
LOG_GROUP="/ecs/$SERVICE_NAME"
LOG_STREAM=$(aws logs describe-log-streams \
  --log-group-name $LOG_GROUP \
  --region $REGION \
  --order-by LastEventTime \
  --descending \
  --max-items 1 \
  --query 'logStreams[0].logStreamName' \
  --output text 2>/dev/null)

if [ ! -z "$LOG_STREAM" ] && [ "$LOG_STREAM" != "None" ]; then
  aws logs filter-log-events \
    --log-group-name $LOG_GROUP \
    --log-stream-names $LOG_STREAM \
    --filter-pattern "SSL OR TLS OR certificate OR https OR wss" \
    --region $REGION \
    --max-items 10 \
    --query 'events[*].message' \
    --output text
else
  echo "로그 스트림을 찾을 수 없습니다."
fi

# 7. 직접 연결 테스트
echo ""
echo "=== 6. 직접 연결 테스트 ==="

# HTTPS 테스트
echo "HTTPS 연결 테스트 (Port 58080):"
curl -v --max-time 5 https://$NLB_DOMAIN:58080/health 2>&1 | grep -E "(SSL|HTTP/|Connected to)"

echo ""
echo "WSS 연결 테스트 (Port 33335):"
curl -v --max-time 5 \
  -H "Upgrade: websocket" \
  -H "Connection: Upgrade" \
  -H "Sec-WebSocket-Version: 13" \
  -H "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==" \
  https://$NLB_DOMAIN:33335/ws/chat 2>&1 | grep -E "(SSL|HTTP/|101 Switching)"

# 8. 보안 그룹 확인
echo ""
echo "=== 7. 보안 그룹 규칙 확인 ==="
# ECS 태스크의 ENI 찾기
ENI_ID=$(aws ecs describe-tasks \
  --cluster $CLUSTER_NAME \
  --tasks $TASK_ARN \
  --region $REGION \
  --query 'tasks[0].attachments[0].details[?name==`networkInterfaceId`].value | [0]' \
  --output text)

if [ ! -z "$ENI_ID" ] && [ "$ENI_ID" != "None" ]; then
  SG_IDS=$(aws ec2 describe-network-interfaces \
    --network-interface-ids $ENI_ID \
    --region $REGION \
    --query 'NetworkInterfaces[0].Groups[*].GroupId' \
    --output text)
  
  echo "ECS 태스크 보안 그룹: $SG_IDS"
  for SG_ID in $SG_IDS; do
    echo ""
    echo "보안 그룹 $SG_ID 인바운드 규칙:"
    aws ec2 describe-security-groups \
      --group-ids $SG_ID \
      --region $REGION \
      --query 'SecurityGroups[0].IpPermissions[?FromPort==`8080` || FromPort==`33334`]' \
      --output json
  done
fi

echo ""
echo "=== 디버깅 완료 ==="
echo ""
echo "일반적인 문제 해결 방법:"
echo "1. USE_SSL=false인 경우: 컨테이너는 HTTP/WS만 제공, NLB가 SSL termination 수행"
echo "2. 헬스 체크 실패: 보안 그룹에서 NLB → ECS 포트 8080, 33334 허용 확인"
echo "3. SSL 인증서 오류: ACM 인증서가 NLB 리스너에 올바르게 연결되었는지 확인"
echo "4. 간헐적 연결: Target Group의 deregistration delay를 30초로 줄이기"
echo "5. 포트 확인: NLB 리스너가 58080(HTTPS)과 33335(WSS)에 설정되어 있는지 확인"
