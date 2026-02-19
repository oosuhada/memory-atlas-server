#!/bin/bash
# ECS 태스크 내부에서 네트워크 연결성 확인 스크립트

echo "=== ECS Network Connectivity Check ==="
echo

echo "1. DNS Resolution Test:"
nslookup places.googleapis.com
echo

echo "2. Network Interface Info:"
ip addr show
echo

echo "3. Routing Table:"
ip route
echo

echo "4. Active Connections:"
netstat -an | grep -E "(ESTABLISHED|TIME_WAIT)" | wc -l
echo

echo "5. Socket Statistics:"
ss -s
echo

echo "6. Test HTTPS Connection to Google:"
curl -v --connect-timeout 5 https://places.googleapis.com 2>&1 | head -20
echo

echo "7. Check Ephemeral Port Range:"
cat /proc/sys/net/ipv4/ip_local_port_range
echo

echo "8. TIME_WAIT Sockets:"
netstat -an | grep TIME_WAIT | wc -l
echo

echo "9. Check TCP Settings:"
sysctl net.ipv4.tcp_tw_reuse
sysctl net.ipv4.tcp_tw_recycle
sysctl net.ipv4.tcp_fin_timeout 