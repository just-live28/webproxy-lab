#!/bin/bash

# ========== 설정 ==========
TINY_PORT=8000
TINY_EXEC="./.proxy/tiny"
TINY_LOG="tiny.log"

COLOR_PROXY="\033[32m"
COLOR_RESET="\033[0m"

# ========== 로그 초기화 ==========
# 혹시 tail -f tiny.log 중인 프로세스가 있으면 종료
pkill -f "tail -f $TINY_LOG" 2>/dev/null

# 로그 파일을 0바이트로 초기화
: > "$TINY_LOG"

# ========== Tiny 실행 ==========
echo "🔵 Starting Tiny server on port $TINY_PORT..."
stdbuf -oL "$TINY_EXEC" "$TINY_PORT" >> "$TINY_LOG" 2>&1 &
TINY_PID=$!
echo "📝 Tiny server started (PID: $TINY_PID), logging to $TINY_LOG"

# ========== Proxy 포트 확보 ==========
PROXY_PORT=$(./free-port.sh)
echo "🟢 Allocated proxy port: $PROXY_PORT"

# ========== 종료 처리 ==========
cleanup() {
    echo ""
    echo "🛑 Stopping Tiny server (PID: $TINY_PID)..."
    kill $TINY_PID
    wait $TINY_PID 2>/dev/null
    echo "✅ Cleanup complete."
}
trap cleanup EXIT

# ========== Proxy 빌드 ==========
if [ proxy.c -nt proxy ] || [ csapp.c -nt proxy ]; then
  echo "🔧 Rebuilding Proxy server (source changed)..."
  gcc -o proxy proxy.c csapp.c -lpthread
fi

# ========== Proxy 실행 ==========
echo "🟣 Starting Proxy server on port $PROXY_PORT..."
stdbuf -oL ./proxy $PROXY_PORT 2>&1 | while read -r line; do
  echo -e "${COLOR_PROXY}[PROXY]${COLOR_RESET} $line"
done