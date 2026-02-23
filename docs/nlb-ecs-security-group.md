# NLB + ECS 보안 그룹 설정

## 중요: NLB는 클라이언트 IP를 보존합니다!

### 문제
ALB와 달리 NLB는 원본 클라이언트 IP를 보존하므로, ECS 보안 그룹에서 NLB의 보안 그룹이 아닌 인터넷(0.0.0.0/0)을 허용해야 합니다.

### 올바른 ECS 보안 그룹 설정

```bash
# ✅ 올바른 설정 - 인터넷에서 직접 허용
aws ec2 authorize-security-group-ingress \
  --group-id <ECS_SG_ID> \
  --protocol tcp \
  --port 8080 \
  --cidr 0.0.0.0/0

aws ec2 authorize-security-group-ingress \
  --group-id <ECS_SG_ID> \
  --protocol tcp \
  --port 33334 \
  --cidr 0.0.0.0/0

# ❌ 잘못된 설정 - NLB 보안 그룹만 허용 (작동 안 함!)
# --source-group <NLB_SG_ID>  # 이렇게 하면 안 됨!
```

### 보안을 위한 대안

VPC 내부 CIDR만 허용:
```bash
aws ec2 authorize-security-group-ingress \
  --group-id <ECS_SG_ID> \
  --protocol tcp \
  --port 8080 \
  --cidr 10.0.0.0/16  # VPC CIDR
```

또는 NLB 서브넷 CIDR만 허용:
```bash
# NLB가 있는 서브넷의 CIDR 블록만 허용
aws ec2 authorize-security-group-ingress \
  --group-id <ECS_SG_ID> \
  --protocol tcp \
  --port 8080 \
  --cidr 10.0.1.0/24  # NLB 서브넷 1
  
aws ec2 authorize-security-group-ingress \
  --group-id <ECS_SG_ID> \
  --protocol tcp \
  --port 8080 \
  --cidr 10.0.2.0/24  # NLB 서브넷 2
```