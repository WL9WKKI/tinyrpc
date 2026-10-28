#!/bin/bash
# 微服务故障诊断 ReAct Agent 演示脚本
# 用法：先起服务（./build/diag_agent），再跑 bash demo.sh
set -e

BASE="http://127.0.0.1:8080"
pp() { python3 -m json.tool 2>/dev/null || cat; }

echo "==================== 1. 提交诊断任务 ===================="
RESP=$(curl -s -X POST "$BASE/api/tasks" -H "Content-Type: application/json" \
  -d '{"service_name":"order-service","symptom":"服务内存持续上涨，最终被 OOM 杀死"}')
echo "$RESP" | pp
TASK_ID=$(echo "$RESP" | grep -o '[0-9]*' | head -1)

echo ""
echo "==================== 2. 轮询诊断状态 ===================="
for i in $(seq 1 30); do
  BODY=$(curl -s "$BASE/api/tasks/$TASK_ID")
  STATUS=$(echo "$BODY" | grep -o '"status":"[A-Z]*"' | cut -d'"' -f4)
  echo "  [$i] status=$STATUS"
  case "$STATUS" in
    SUCCEEDED|FAILED|TIMEOUT|CANCELLED) break ;;
  esac
  sleep 1
done

echo ""
echo "==================== 3. 诊断结论（含置信度/证据） ===================="
curl -s "$BASE/api/tasks/$TASK_ID" | pp

echo ""
echo "==================== 4. 诊断轨迹 ===================="
curl -s "$BASE/api/tasks/$TASK_ID/trace" | pp

echo ""
echo "==================== 5. 指标（P50/P95/P99） ===================="
curl -s "$BASE/api/metrics" | pp

echo ""
echo "==================== 6. 错误路径演示 ===================="
echo "-- 空 symptom（期望 400）--"
curl -s -o /dev/null -w "  http=%{http_code}\n" -X POST "$BASE/api/tasks" \
  -H "Content-Type: application/json" -d '{"service_name":"svc","symptom":""}'
echo "-- 查不存在的任务（期望 404）--"
curl -s -o /dev/null -w "  http=%{http_code}\n" "$BASE/api/tasks/999999"
echo "-- 取消已结束任务（期望 409）--"
curl -s -o /dev/null -w "  http=%{http_code}\n" -X POST "$BASE/api/tasks/$TASK_ID/cancel"

echo ""
echo "==================== 演示结束 ===================="
