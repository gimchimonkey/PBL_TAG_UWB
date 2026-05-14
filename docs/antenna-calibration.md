# Antenna Delay Calibration

DW1000 (BU01) 의 거리 측정 bias 를 제거하기 위한 펌웨어 캘리브레이션 절차.

## 왜 필요한가

- DW1000 은 신호가 안테나·PCB 트레이스·RF front-end 를 통과하는 시간을 직접 측정 못 함 → **antenna delay 레지스터** (TX_ANTD, LDE_RXANTD) 로 빼줘야 정확한 거리 도출
- 미보정 시 라이브러리 디폴트값 (16384) 사용 → 보통 **+70~110 cm bias**
- 베이스라인 측정 ([../src/main.cpp:20-25](../src/main.cpp#L20)) 의 OFFSET 값 (+72/+75/+80/+106 cm) 이 이 미보정 bias 를 앱 레벨에서 빼준 것
- 데이터시트 사양 **±10 cm 는 캘리브레이션 후 noise σ (random)** — bias 와는 별개 축
- 펌웨어 캘리브레이션이 끝나면 ESP32 측 OFFSET[] 은 모두 0 으로 만든다

## 원리

| 항목 | 값 |
|------|----|
| DW1000 time unit | ≈ 15.65 ps |
| 1 unit 거리 환산 | ≈ 4.69 mm (편도) |
| 양쪽 보드 모두 1 unit 변경 시 측정 거리 변화 | ≈ 9.4 mm |
| 라이브러리 `setAntennaDelay(v)` 동작 | TX_ANTD, LDE_RXANTD 모두 v 로 설정 |

수식 (DS-TWR 측정 거리에 들어가는 보정):

```
r_measured = r_true + (TX_delay_A + RX_delay_A + TX_delay_B + RX_delay_B) / 4 [거리 환산]
           ≈ r_true + (ANT_DELAY_A + ANT_DELAY_B) × 4.69mm / 2
```

양쪽 보드를 같은 값 X 로 두면 보정량이 보드 쌍에 무관해진다 → Phase 1 단순화의 근거.

## 측정 셋업

- 직선 LOS, 안테나끼리 마주봄 (방향성 영향 최소)
- 안테나 높이 ≥ 1 m (지면 멀티패스 회피)
- 반경 1~2 m 내 큰 금속 (책상 다리, 모니터) 없음
- 기준 거리: **5.00 m 권장**
  - 1 m 는 너무 짧아 측정 노이즈가 거리 대비 큼
  - 10 m+ 는 셋업이 번거롭고 노이즈 자체도 커짐
- 거리는 줄자로 안테나 중심 기준 ± 5 mm 이내 정확히

## Phase 1 — 공통 값 캘리브레이션 (필수)

모든 5 개 보드 (Tag + Anchor 1~4) 를 같은 ANT_DELAY 값으로 설정해 공통 bias 제거.

### 1.1 초기 빌드

`C:\PBL_ANCHOR_TAG/src/main.cpp` 상단:

```cpp
#define ANT_DELAY_TAG     16384   // 모두 동일 값 X 로 시작
#define ANT_DELAY_ANCHOR1 16384
#define ANT_DELAY_ANCHOR2 16384
#define ANT_DELAY_ANCHOR3 16384
#define ANT_DELAY_ANCHOR4 16384
```

ROLE 매크로를 바꿔가며 5 개 보드 모두 업로드.

### 1.2 측정

1. Tag + Anchor 1 을 5.00 m 떨어뜨려 LOS 배치 (나머지 anchor 는 무시 또는 제거)
2. ESP32 시리얼 모니터로 `Anchor: 0x0001, Distance: D.DDm` 라인 ≥ **60 초** 수집
3. raw 측정값 (OFFSET 적용 전) 의 산술 평균 D̄ 계산
   - 주의: 현재 ESP32 코드의 `OFFSET[]` 이 적용된 후 값을 보지 말 것. 임시로 OFFSET 모두 0 으로 두고 측정하거나, 시리얼 raw 라인을 보고 직접 평균

### 1.3 새 ANT_DELAY 계산

오차 e [mm] = (D̄ − 5.000) × 1000

```
Δunits = round(e / 9.4)
새 X = 현재 X − Δunits
```

예: D̄ = 5.752 m → e = +752 mm → Δunits = 80 → 새 X = 16384 − 80 = 16304

### 1.4 반복

5 개 보드 모두 새 X 값으로 다시 빌드/업로드. 1.2~1.3 반복.

**수렴 기준**: |e| < 50 mm (= 5 cm) 들어오면 완료.

보통 2~3 iteration 으로 수렴. 보드 5 개 × 3 iteration = 빌드/업로드 15 회, 소요 30~45 분.

## Phase 2 — 검증

같은 X 값으로 다음 거리에서 raw 측정:

| 거리 | 합격 기준 |
|------|----------|
| 1.00 m | ±10 cm |
| 3.00 m | ±10 cm |
| 5.00 m | ±5 cm (캘리브레이션 거리) |
| 8.00 m | ±15 cm |

### 결과 해석

- 모든 거리 합격 → Phase 3 스킵, ESP32 OFFSET[] 모두 0 으로
- 한 거리만 큼 → 멀티패스 의심, 셋업 재확인
- 거리에 비례해 어긋남 → 펌웨어 캘리브레이션 외 추가 보정 (linear correction) 필요 → 별도 ADR

## Phase 3 — 개체 미세조정 (선택)

Phase 2 통과 후에도 ±10 cm 가 안 만족스러우면 보드 개체차 조정.

1. 각 anchor 와 Tag 를 5 m 에서 따로 측정 (Anchor1, Anchor2, Anchor3, Anchor4 각각)
2. 평균 오차 e_i 가 다른 anchor 와 다르면 그 anchor 의 ANT_DELAY 만 별도 조정
3. Tag 값은 건드리지 않는다 (기준점)

소요 시간 1 시간+. 효과 보통 ±2~3 cm. ≤15 cm sprint contract 만족이면 안 해도 됨.

## 캘리브레이션 후

1. ESP32 `src/main.cpp` 의 `OFFSET[]` 4 개 모두 `0.0f` 로
2. 같은 거리 재측정 → 측정값 ≈ 실제 거리 (±10 cm) 확인
3. 정지 노이즈 측정 (1 분 로깅, anchor 별 σ) — 이게 데이터시트 사양 ±10 cm 와 비교 가능한 숫자
4. [../CLAUDE.md](../CLAUDE.md) 검증기준 (≤15 cm 평균 오차) 재확인

## 참고

- [hardware-layout.md](hardware-layout.md) — Anchor 좌표 (캘리브레이션 위치 무관)
- [uwb-phy-spec.md](uwb-phy-spec.md) — PHY 변경 시 antenna delay 재캘리 필요 (PHY 모드별 delay 다름)
- DW1000 User Manual section 8.2 (Antenna Delay) — Decawave 공식 캘리브레이션 가이드
- `C:\PBL_ANCHOR_TAG/.pio/libdeps/genericSTM32F103C8/DW1000/src/DW1000.cpp:1011` — `setAntennaDelay` 구현
- `C:\PBL_ANCHOR_TAG/.pio/libdeps/genericSTM32F103C8/DW1000/src/DW1000.cpp:1085` — 디폴트 16384 fallback
