# System Architecture

PBL_TAG_UWB 프로젝트의 **확장된 시스템 구조**. 단순 측위 출력기에서 모터 제어 통합 시스템으로 발전한 형태.

---

## 1. 컴포넌트 목록

| 컴포넌트 | 역할 | 통신 | 위치 |
|---------|-----|------|------|
| **BU01 Anchor × 4** | UWB 거리 측정 기준점 | UWB ↔ Tag | 환경 고정 |
| **BU01 Tag (STM32)** | 4개 anchor 와 거리 레인징 | UART(텍스트) → ESP32 | 로봇 위 |
| **ESP32-S3** | 좌승법 측위 + 데이터 허브 | UART/SPI 다채널 | 로봇 위 |
| **OpenMV H7** | AprilTag 인식 (정렬용) | SPI(슬레이브) ↔ ESP32 | 로봇 위 |
| **STM32 F411RE** | 모터 제어 (Pure Pursuit + 엔코더 오도메트리) | UART ← ESP32 | 로봇 위 |

---

## 2. 데이터 흐름 (확장됨)

```
[Anchor 1]─UWB─┐
[Anchor 2]─UWB─┤
[Anchor 3]─UWB─┼──→ [BU01 Tag (STM32)]
[Anchor 4]─UWB─┘            │
                            │ UART (단방향, ASCII)
                            │ "Anchor: 0xAABB, Distance: 1.23m"
                            ↓
                     ┌─────────────┐
                     │  ESP32-S3   │  ◀── 데이터 허브 ───
                     │             │
                     │  좌승법      │
                     │  (x, y) 계산 │
                     │             │
                     └─┬───────┬───┘
                       │       │
              SPI (마스터) │       │ UART (송신)
              MODE0, 1MHz  │       │ 16 byte 바이너리
              6 byte       │       │
                       │       │
                       ▼       ▼
              [OpenMV H7]   [STM32 F411RE]
              AprilTag      Pure Pursuit
              인식          + 엔코더 오도메트리
                                │
                                ▼
                          [모터 PWM, 디퍼렌셜 드라이브]
                          0.3 m/s
```

---

## 3. 운영 단계 — 3 Phase 모델

로봇은 미션 도중 다음 3단계를 순차적으로 거친다.

### Phase A — 원거리 이동 (UWB 기반)

```
조건: 로봇이 목표 ZONE 으로부터 멀리 떨어져 있음
      OpenMV 카메라 시야에 AprilTag 미감지

ESP32 → STM32 UART:
  mode      = 1 (GoToPoint)
  x_now     = UWB 좌승법 결과
  y_now     = UWB 좌승법 결과
  x_target  = 미션 매니저가 정한 목표
  y_target  = 미션 매니저가 정한 목표
  align_yaw = 0 (의미 없음)

STM32 동작:
  - 엔코더 오도메트리로 (x, y, θ) 자체 추정
  - ESP32 의 UWB (x_now, y_now) 로 위치 가중평균 보정
  - Pure Pursuit 알고리즘으로 목표까지 추종
  - 헤딩(θ) 은 자체 추정값 사용
```

### Phase B — 전환 (도착 임박, 카메라 시야 진입)

```
트리거: 목표까지 거리가 임계값 이내 (팀 결정, 잠정 200~500 mm)
        OpenMV 가 등록 ZONE 의 tag_id 안정적으로 인식 (200ms 이상)

ESP32 동작:
  - OpenMV 데이터 안정화 검증 (yaw, dist 가 흔들리지 않는지)
  - Phase C 로 전환 준비

이 단계는 짧음 (보통 200~500 ms). 검증 통과 시 즉시 C 로.
```

### Phase C — 정밀 정렬 (AprilTag 기반)

```
조건: 카메라가 태그를 안정적으로 보고 있음

ESP32 → STM32 UART:
  mode      = 2 (Align)
  x_now     = 마지막 추정값 (또는 0, STM32 가 무시)
  y_now     = 마지막 추정값
  x_target  = 마지막 (또는 0)
  y_target  = 마지막 (또는 0)
  align_yaw = OpenMV 의 AprilTag yaw (deg × 10)

STM32 동작:
  - 자체 헤딩과 align_yaw 비교
  - 카메라 기반 정밀 정렬 (AprilTag 의 위치/yaw 활용)
  - 도착 판정 시 mode=3 받으면 정지
```

### 종료 조건

```
OpenMV: arrived_flag = 1 일정 시간 유지
ESP32 → STM32: mode = 3 (Stop) 송신
STM32: 모터 정지
```

---

## 4. 인터페이스 요약

세부 패킷 포맷은 `plans/esp32-bridge/requirements.md` 참조.

| 인터페이스 | 방향 | 프로토콜 | 빈도 | 비고 |
|-----------|------|---------|------|------|
| **BU01 ↔ Anchors** | 양방향 | UWB | 칩 자체 | 별도 레포 `C:\PBL_ANCHOR_TAG` |
| **BU01 → ESP32** | 단방향 | UART 115200 8N1, ASCII | 1 사이클 ≥ 50ms | `docs/uart-protocol.md` |
| **ESP32 ↔ OpenMV** | 양방향 | SPI MODE0 1MHz, 6 byte | 100 ms 폴링 | `plans/esp32-bridge/requirements.md` R0 |
| **ESP32 → STM32 F411RE** | 단방향 | UART (115200 제안), 16 byte 바이너리 | 50~100 ms | `plans/esp32-bridge/requirements.md` R2 |
| **ESP32 → PC** | 단방향 | USB CDC, 디버그 로그 | 가변 | 기존 |

---

## 5. 좌표계 + 단위 정합성

각 컴포넌트가 사용하는 단위가 다르니 변환 책임을 명확히:

| 컴포넌트 | 위치 단위 | 헤딩 단위 |
|---------|---------|---------|
| BU01 거리값 | m (소수점 둘째 자리) | — |
| ESP32 좌승법 | m | — |
| **ESP32 → STM32 패킷** | **mm (int16)** | **deg × 10 (int16)** |
| STM32 내부 (엔코더) | mm | rad 또는 deg |
| OpenMV AprilTag | mm (yaw_deg × 10) | deg × 10 |

**핵심 변환 규칙**:
- ESP32 가 m → mm 변환 (× 1000) 후 송신
- STM32 가 받은 후 mm 단위 그대로 처리
- 부동소수점 변수 송신 회피 (정수 스케일링)

좌표계 정의는 `docs/coordinate-system.md` 참조 (변경 없음).

---

## 6. 모드별 데이터 사용 매트릭스

| 모드 | x_now/y_now | x_target/y_target | align_yaw | STM32 활용 |
|------|------------|-------------------|-----------|----------|
| 0 (Idle) | 무시 | 무시 | 무시 | 정지 대기 |
| 1 (GoToPoint, Phase A) | **사용** | **사용** | 무시 | Pure Pursuit |
| 2 (Align, Phase C) | 무시 또는 참고 | 무시 | **사용** | 카메라 기반 정렬 |
| 3 (Stop) | 무시 | 무시 | 무시 | 즉시 정지 |

---

## 7. Known Gaps / 미결 사항

- [ ] STM32 F411RE 팀과 인터페이스 합의 (`plans/esp32-bridge/design.md` D1)
- [ ] Phase A → B 전환 임계 거리 (실측)
- [ ] AprilTag 좌표 정의 (각 ZONE 의 절대 좌표) — 현재 OpenMV 의 ZONE_NAME 매핑만 존재
- [ ] 시작 위치 강제 메커니즘 (충전 도크 또는 수동 캘리브레이션)
- [ ] AM-GYRO-V02 IMU 사양 확인 (Z축 자이로 유무) → 채택 여부 결정
- [ ] 비차단 운용의 구체적 구현 방식 (`docs/blocking-analysis.md`)

---

## 관련 문서

- 측위 좌표계: `docs/coordinate-system.md`
- Anchor HW 배치: `docs/hardware-layout.md`
- BU01 → ESP32 UART: `docs/uart-protocol.md`
- 블로킹 분석: `docs/blocking-analysis.md`
- ESP32 Bridge 명세: `plans/esp32-bridge/design.md` + `plans/esp32-bridge/requirements.md`
