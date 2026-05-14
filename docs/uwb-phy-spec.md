# UWB PHY 설정 및 TWR Cycle Rate 튜닝

DW1000 기반 BU01 모듈의 PHY 설정과 TWR(Two-Way Ranging) 사이클 주기를 줄이는 방법을 정리한 가이드. 펌웨어는 `C:\PBL_ANCHOR_TAG` (별도 레포)에 있고 Tag·Anchor 4개가 동일 라이브러리(`thotro/arduino-dw1000`)를 공유한다.

## 현재 설정

### Tag 펌웨어
`C:\PBL_ANCHOR_TAG/src/main.cpp:41`
```c
DW1000Ranging.startAsTag(TAG_ADDRESS, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
```

### PHY 파라미터 (모드 정의: `DW1000.h:412`)
| 파라미터 | 값 | 의미 |
|---------|-----|------|
| Air rate | 110 kbps | 가장 느림, 가장 안정 |
| PRF | 16 MHz | 저전력 |
| Preamble | 2048 symbols | 노이즈 강함, 메시지 ~2 ms |

### TWR 스케줄링 (`DW1000Ranging.h`)
```c
#define DEFAULT_RESET_PERIOD     200      // ms — 활동 없을 때 리셋
#define DEFAULT_REPLY_DELAY_TIME 7000     // µs — TWR 메시지 사이 대기
#define DEFAULT_TIMER_DELAY      80       // ms — cycle 사이 idle
```

### Cycle 주기 계산
```
timerDelay = DEFAULT_TIMER_DELAY + (N_anchors × 3 × DEFAULT_REPLY_DELAY_TIME / 1000)
N=4:         80 ms          +         (4 × 3 × 7) ms        =  164 ms
             └─idle─┘                  └─TWR exchange─┘

→ Cycle rate ≈ 6.1 Hz
```

자동 계산 위치: `DW1000Ranging.cpp:800` — Tag 가 anchor 발견할 때마다 `_timerDelay` 를 위 공식으로 재계산.

### 스케줄링 시각화

#### 한 cycle 통째 (164 ms)

```
t [ms]   0       21        42        63        84              164
         │       │         │         │         │                │
Tag    ──┼─A1────┼─A2──────┼─A3──────┼─A4──────┼─── idle 80ms ──┼──→ (cycle 2)
         │       │         │         │         │                │
         └─21ms──┴──21ms───┴──21ms───┴──21ms───┘                │
                                                                │
         ◄──────── 84ms (실제 TWR 교환) ────────►◄─ TIMER_DELAY ►│
         ◄──────────────── 164ms / cycle  (≈ 6.1 Hz) ───────────►
```

- A1~A4 직렬 진행 (동시 X) — DW1000 한 칩이 한 번에 한 노드만 통신
- TIMER_DELAY 80 ms 는 라이브러리가 일부러 둔 안전마진 — 줄일 여지 가장 큼

#### 한 anchor 와의 DS-TWR (21 ms, 3 메시지)

```
       Tag                                            Anchor i
        │                                                │
   t=0  ├──► Poll ─────────────────────────────────────►│
        │     (TX ~2ms)                                  │
        │              ◄── 7ms (REPLY_DELAY) ──►         │
        │                                                │
        │◄───────────────────────────────── Response ◄──┤
        │              ◄── 7ms ──►                       │
        │                                                │
        ├──► Final ────────────────────────────────────►│
        │              ◄── 7ms ──►                       │
        │                                                │
  t=21  │   (다음 anchor 로 넘어감)                       │
        ▼                                                ▼
```

| 메시지 | 방향 | 의미 |
|--------|------|------|
| Poll  | Tag → Anchor | "T1 에 너한테 쏜다" |
| Response | Anchor → Tag | "T2 에 받았고 T3 에 응답 보낸다" |
| Final | Tag → Anchor | "T4 에 너 응답 받았다, 4 개 timestamp 로 거리 계산해라" |

DS-TWR (Double-Sided) 의 3 메시지 덕분에 양쪽 클럭 drift 가 자동 보정 → SS-TWR (2 메시지) 보다 정확. 거리 계산은 anchor 가 수행해서 다음 cycle 의 메시지에 실어 보낸다.

#### 시간 분해

```
한 cycle 164ms 의 구성:

│██████ TWR 84ms (4 × 21ms) ██████│░░░░░ idle 80ms ░░░░░│
│                                   │                     │
│  └─ 4 anchor 각각:                │  └─ TIMER_DELAY    │
│     Poll(2) + gap(7) +            │     라이브러리 디폴트│
│     Resp(2) + gap(7) +            │     안전마진       │
│     Final(2) + gap(7) = 21ms      │                     │
│                                   │                     │
│  실제 무선 송신 ~24ms 전체 (15%)  │                     │
│  나머지 60ms 는 REPLY_DELAY 합    │                     │
```

## 튜닝 옵션

### 옵션 1 — PHY 모드 변경 (효과 최대)

| 모드 | air rate | preamble | 메시지 시간 | 사거리 | 정확도 |
|------|---------|----------|-----------|--------|-------|
| LONGDATA_RANGE_LOWPOWER (현재) | 110 kbps | 2048 | ~2 ms | ~100m+ | 최고 |
| LONGDATA_RANGE_ACCURACY | 110 kbps | 2048 | ~2 ms | 같음 | 같음 (PRF 64MHz, 전력↑) |
| LONGDATA_FAST_LOWPOWER | 6.8 Mbps | 1024 | ~1 ms | ~30m | 약간↓ |
| LONGDATA_FAST_ACCURACY | 6.8 Mbps | 1024 | ~1 ms | ~30m | 약간↓ (전력↑) |
| SHORTDATA_FAST_LOWPOWER | 6.8 Mbps | 128 | ~0.2 ms | ~10m | ↓ |

**양쪽 변경 필수** — Tag(`C:\PBL_ANCHOR_TAG/src/main.cpp:41`) + Anchor 4개 펌웨어 모두 동일 모드.

**사거리 확인** — [hardware-layout.md](hardware-layout.md) 보면 최대 anchor 간 ~10m. SHORTDATA는 한계 빠듯, **LONGDATA_FAST 가 안전**.

### 옵션 2 — REPLY_DELAY_TIME 줄이기

`DW1000Ranging.h:59` 수정 (라이브러리 헤더 직접 손대거나, 런타임 setter 노출 사용).

```c
#define DEFAULT_REPLY_DELAY_TIME 7000   // 현재
#define DEFAULT_REPLY_DELAY_TIME 3000   // 권장 하한
```

DW1000이 메시지 받고 응답 준비하는 데 걸리는 시간. 너무 짧으면 패킷 드롭. **3000~4000 µs** 가 보통 안전선.

```
N=4 기준 변경 효과:
  7ms → 3ms : 4 × 3 × 4ms = 48ms 절약
  cycle: 164ms → 116ms (8.6 Hz)
```

### 옵션 3 — TIMER_DELAY 줄이기

`DW1000Ranging.h:66` 수정.

```c
#define DEFAULT_TIMER_DELAY 80   // 현재
#define DEFAULT_TIMER_DELAY 30   // 권장
```

Cycle 사이 idle 시간. 큰 부작용 없이 깎을 수 있음. 0이면 처리 안 끝나고 다음 cycle 시작 위험.

```
변경 효과:
  80ms → 30ms : 50ms 절약
  cycle: 164ms → 114ms (8.8 Hz)
```

### 옵션 4 — SS-TWR 사용 (비추)

DS-TWR(현재, 3 메시지) → SS-TWR(2 메시지). 33% 시간 절약.

단, 양쪽 클럭 drift 보정 사라져 정확도 ↓ → [CLAUDE.md](../CLAUDE.md) 검증기준 ≤15cm 위협. **권장하지 않음.**

## 조합별 예상 cycle rate

| 조합 | TIMER | REPLY | PHY | cycle | rate |
|------|-------|-------|-----|-------|------|
| 현재 | 80 | 7ms | LONG_RANGE | 164ms | 6.1 Hz |
| **옵션 2+3 (안전)** | 30 | 3ms | LONG_RANGE | **66ms** | **15 Hz** |
| 옵션 1만 (LONG_FAST) | 80 | 7ms | LONG_FAST | ~120ms | 8.3 Hz |
| 옵션 1+2+3 | 30 | 3ms | LONG_FAST | **~40ms** | **25 Hz** |
| 극한 (SHORT_FAST) | 30 | 3ms | SHORT_FAST | ~30ms | 33 Hz |

## 단계별 권장

### 1단계 (현재 위치) — 건드리지 말기
ESP32 측 6-cycle 누적 mean + 델타 게이팅 + LS 구현 검증 단계. 6 Hz 입력으로 충분.

### 2단계 — EKF 도입 시 옵션 2+3 적용
- TIMER 30 ms + REPLY 3 ms → **~15 Hz 입력**
- EKF dt = 67 ms → 0.3 m/s 로봇이 cycle당 **2 cm** 이동 → 매우 부드러운 추정
- **PHY 모드는 그대로** (정확도 우선, 그리고 아직 정확도 측정 안 함)

### 3단계 — 빠른 추적이 필요하면 PHY까지
실내 사거리 ≤30m이면 `LONGDATA_FAST_LOWPOWER`로 전환. 25 Hz 시스템.

## 변경 시 체크리스트

변경할 때 빠뜨리지 말 것:

- [ ] **Tag 펌웨어** (`C:\PBL_ANCHOR_TAG/src/main.cpp`) 모드 변경
- [ ] **Anchor 4개 펌웨어** 모두 동일 변경 (anchor ID 외엔 PHY 동일해야 함)
- [ ] 라이브러리 `DW1000Ranging.h` 수정 시 PlatformIO 캐시 주의 (`pio run -t clean`)
- [ ] **OFFSET 재측정** — PHY 바뀌면 거리 보정값 달라짐 (현재 [main.cpp:20-25](../src/main.cpp#L20))
- [ ] **정지 노이즈 측정** — 1분 로깅 → anchor별 σ 비교 (정확도 회귀 확인)
- [ ] **동적 검증** — [CLAUDE.md](../CLAUDE.md) 검증기준 ≤15cm 만족 여부
- [ ] **사거리 검증** — anchor 최대 거리 위치에서 통신 끊김 여부

## 참고

- [uart-protocol.md](uart-protocol.md) — UART 패킷 포맷
- [hardware-layout.md](hardware-layout.md) — Anchor 좌표 / 결선
- `C:\PBL_ANCHOR_TAG/.pio/libdeps/genericSTM32F103C8/DW1000/src/` — DW1000 라이브러리 소스
- DW1000 User Manual (Decawave) — PHY 모드 상세 사양
