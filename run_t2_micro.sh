#!/bin/bash
# run_t2_micro.sh - t2.micro ECS 환경에서 CherryRecorder Server 실행

echo "=== CherryRecorder Server - t2.micro ECS Deployment ==="

# 환경 변수 설정
export ECS_INSTANCE_TYPE="t2.micro"
export DOCKER_BUILDKIT=1

# 컨테이너 이름
CONTAINER_NAME="cherryrecorder-server-container"

# 1. 시스템 리소스 확인
echo "--- System Resources ---"
echo "Memory: $(free -h | grep Mem | awk '{print $2}')"
echo "CPU: $(nproc) cores"
echo "Disk: $(df -h / | tail -1 | awk '{print $4}' ) available"
echo ""

# 2. Docker 정리 (메모리 확보)
echo "--- Cleaning up Docker resources ---"
docker system prune -f --volumes
echo ""

# 3. 이미지 빌드 (이미 빌드된 경우 스킵)
if [[ "$1" == "--build" ]]; then
    echo "--- Building Docker image (this may take 20-30 minutes) ---"
    python3 docker_manager.py --target app
else
    echo "--- Using existing Docker image ---"
    echo "To rebuild, run: $0 --build"
fi

# 4. 컨테이너 실행
echo ""
echo "--- Starting container ---"
docker run -d \
    --name $CONTAINER_NAME \
    --memory 900m \
    --memory-swap 900m \
    --cpus 0.95 \
    --privileged \
    --restart unless-stopped \
    --log-driver awslogs \
    --log-opt awslogs-group=/ecs/cherryrecorder-server \
    --log-opt awslogs-region=ap-northeast-2 \
    --log-opt awslogs-stream=t2-micro-local \
    -p 8080:8080 \
    -p 33334:33334 \
    --env-file server-config.env \
    cherryrecorder-server:latest

# 5. 상태 확인
echo ""
echo "--- Waiting for server startup (10 seconds) ---"
sleep 10

echo ""
echo "--- Container Status ---"
docker ps --filter "name=$CONTAINER_NAME" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo ""
echo "--- Health Check ---"
if docker exec $CONTAINER_NAME curl -f http://localhost:8080/health 2>/dev/null; then
    echo "✓ Server is healthy!"
else
    echo "✗ Health check failed. Checking logs..."
    docker logs $CONTAINER_NAME --tail 50
fi

echo ""
echo "--- Resource Usage ---"
docker stats --no-stream --format "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}" $CONTAINER_NAME

echo ""
echo "=== Deployment Complete ==="
echo "View logs:  docker logs $CONTAINER_NAME -f"
echo "Stop:       docker kill $CONTAINER_NAME"
echo "Clean up:   docker rm $CONTAINER_NAME" 