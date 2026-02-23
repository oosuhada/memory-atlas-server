#!/bin/bash
# 오래된/중복 문서 삭제 스크립트

cd "$(dirname "$0")"

echo "Cleaning up outdated documentation..."

# ALB 관련 문서 삭제 (NLB로 전환)
rm -f alb-acm-setup.md
rm -f alb-troubleshooting.md
rm -f alb-websocket-setup.md
rm -f aws-alb-best-practices.md
rm -f ecs-alb-troubleshooting.md

# Let's Encrypt 문서 삭제 (AWS ACM 사용)
rm -f letsencrypt-setup.md

# 중복 문서 삭제
rm -f github-variables-guide.md

echo "Cleanup complete!"
echo ""
echo "Remaining documents:"
ls -la *.md
