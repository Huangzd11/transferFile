#!/usr/bin/env bash
# V0.0.4 验收：编译并运行全部单元/集成测试
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build-native.sh"
cd "$ROOT/build"
echo "=== V0.0.4 验收测试 (ctest) ==="
ctest --output-on-failure
echo ""
echo "通过（期望 47 项）。召唤 V0.0.4: document/17-V0.0.4-验收说明.md"
echo "      召唤 V0.0.2: document/12-V0.0.2-验收说明.md"
echo "      推送 V0.0.3: document/15-V0.0.3-验收说明.md"
