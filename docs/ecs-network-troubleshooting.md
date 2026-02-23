# ECS 네트워크 문제 해결 가이드

## "Cannot assign requested address" 에러 해결

### 1. 보안 그룹 확인

ECS 태스크의 보안 그룹에서 아웃바운드 규칙 확인:

```bash
# 보안 그룹 ID 확인
aws ecs describe-services --cluster YOUR_CLUSTER --services YOUR_SERVICE \
  --query 'services[0].networkConfiguration.awsvpcConfiguration.securityGroups'

# 보안 그룹 규칙 확인
aws ec2 describe-security-groups --group-ids sg-xxxxxxxxx
```

필요한 아웃바운드 규칙:
- **HTTPS (443)**: `0.0.0.0/0` (Google API 접근용)
- **DNS (53)**: `0.0.0.0/0` (도메인 해석용)

### 2. VPC 및 서브넷 확인

```bash
# 서브넷이 퍼블릭인지 프라이빗인지 확인
aws ec2 describe-subnets --subnet-ids subnet-xxxxxxxxx

# NAT Gateway 확인 (프라이빗 서브넷의 경우)
aws ec2 describe-nat-gateways --filter "Name=state,Values=available"
```

### 3. ECS 태스크 내부에서 디버깅

```bash
# ECS Exec을 사용하여 컨테이너 접속
aws ecs execute-command --cluster YOUR_CLUSTER \
  --task YOUR_TASK_ID \
  --container cherryrecorder-server \
  --interactive \
  --command "/bin/bash"

# 네트워크 상태 확인
./scripts/check-ecs-network.sh
```

### 4. 태스크 정의 수정

```json
{
  "containerDefinitions": [{
    // 기존 설정...
    "ulimits": [
      {
        "name": "nofile",
        "softLimit": 65536,
        "hardLimit": 65536
      }
    ],
    "environment": [
      // 추가 환경 변수
      {
        "name": "GOMAXPROCS",
        "value": "2"  // CPU 코어 수에 맞게 조정
      }
    ]
  }]
}
```

### 5. 시스템 파라미터 최적화

ECS 호스트 인스턴스에서:

```bash
# TIME_WAIT 소켓 재사용 활성화
sudo sysctl -w net.ipv4.tcp_tw_reuse=1

# FIN 타임아웃 단축
sudo sysctl -w net.ipv4.tcp_fin_timeout=30

# 영구 적용
echo "net.ipv4.tcp_tw_reuse = 1" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.tcp_fin_timeout = 30" | sudo tee -a /etc/sysctl.conf
```

### 6. CloudFormation으로 보안 그룹 수정

```yaml
ECSSecurityGroup:
  Type: AWS::EC2::SecurityGroup
  Properties:
    GroupDescription: Security group for ECS tasks
    VpcId: !Ref VPC
    SecurityGroupIngress:
      # 인바운드 규칙...
    SecurityGroupEgress:
      - IpProtocol: tcp
        FromPort: 443
        ToPort: 443
        CidrIp: 0.0.0.0/0
        Description: HTTPS for Google APIs
      - IpProtocol: tcp
        FromPort: 53
        ToPort: 53
        CidrIp: 0.0.0.0/0
        Description: DNS
      - IpProtocol: udp
        FromPort: 53
        ToPort: 53
        CidrIp: 0.0.0.0/0
        Description: DNS UDP
```

### 7. 모니터링 및 알림 설정

```bash
# CloudWatch 메트릭 확인
aws cloudwatch get-metric-statistics \
  --namespace AWS/ECS \
  --metric-name NetworkPacketsDropped \
  --dimensions Name=ServiceName,Value=YOUR_SERVICE \
  --start-time 2024-01-01T00:00:00Z \
  --end-time 2024-01-02T00:00:00Z \
  --period 300 \
  --statistics Average
``` 