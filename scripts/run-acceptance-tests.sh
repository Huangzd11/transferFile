#!/usr/bin/env bash
# V0.0.2 验收：编译并运行全部单元/集成测试
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"${ROOT}/scripts/build-native.sh"
echo ""
echo "=== V0.0.2 验收测试 (ctest) ==="
cd "${ROOT}/build"
ctest --output-on-failure
echo ""
echo "通过。召唤验收: document/12-V0.0.2-验收说明.md；推送验收: document/15-V0.0.3-验收说明.md"
