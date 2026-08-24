#!/usr/bin/env bash
# ci.sh — the CI gate for this (Aether) engine. See AE-MIGRATION.md.
#
# Phase 0: headless unit tests (pure modules — no display, no GTK).
# Phase 1: build every ported example against ../aether-ui (aeb fan-out).
# Phase 2: (with a display or xvfb-run) smoke-run snake via the driver:
#          key in, key up, screenshot, shutdown.
set -u
cd "$(cd "$(dirname "$0")" && pwd)"
FAIL=0

echo "=== Phase 0: headless unit tests ==="
for t in test_core test_png test_util test_vector test_audio; do
    if ae run --extra ebiten/aether_ebiten_blit.c "tests/${t}.ae" > "/tmp/ci_ae_${t}.log" 2>&1; then
        echo "  PASS ${t}: $(tail -1 /tmp/ci_ae_${t}.log)"
    else
        echo "  FAIL ${t} (see /tmp/ci_ae_${t}.log)"
        FAIL=$((FAIL + 1))
    fi
done

echo "=== Phase 1: build all ported examples ==="
if aeb all.ae > /tmp/ci_ae_build.log 2>&1; then
    echo "  PASS build"
else
    echo "  FAIL build (see /tmp/ci_ae_build.log)"
    FAIL=$((FAIL + 1))
fi

echo "=== Phase 2: snake driver smoke ==="
LAUNCH=""
if [ -z "${DISPLAY:-}" ]; then
    if command -v xvfb-run > /dev/null 2>&1; then
        LAUNCH="xvfb-run -a -s '-screen 0 800x600x24'"
    else
        echo "  SKIP (no display, no xvfb-run)"
    fi
fi
if [ $FAIL -eq 0 ] && { [ -n "${DISPLAY:-}" ] || [ -n "$LAUNCH" ]; }; then
    PORT=9231
    BIN=target/build/examples/snake/bin/snake
    AETHER_UI_TEST_PORT=$PORT eval "$LAUNCH $BIN" > /tmp/ci_ae_snake.log 2>&1 &
    PID=$!
    UP=0
    for _ in $(seq 1 40); do
        curl -sf -o /dev/null "http://127.0.0.1:$PORT/widgets" && { UP=1; break; }
        sleep 0.25
    done
    if [ $UP -eq 1 ]; then
        OK=1
        curl -sf -o /dev/null -X POST "http://127.0.0.1:$PORT/canvas/1/key?name=Right" || OK=0
        curl -sf -o /dev/null -X POST "http://127.0.0.1:$PORT/canvas/1/keyup?name=Right" || OK=0
        sleep 1
        curl -sf -o /tmp/ci_ae_snake.png "http://127.0.0.1:$PORT/screenshot" || OK=0
        curl -sf -o /dev/null -X POST "http://127.0.0.1:$PORT/shutdown" || true
        if [ $OK -eq 1 ]; then echo "  PASS snake smoke"; else echo "  FAIL snake smoke"; FAIL=$((FAIL + 1)); fi
    else
        echo "  FAIL snake did not come up"
        FAIL=$((FAIL + 1))
    fi
    kill $PID 2>/dev/null
    wait 2>/dev/null
fi

echo "=== ci result: $FAIL failure(s) ==="
exit $FAIL
