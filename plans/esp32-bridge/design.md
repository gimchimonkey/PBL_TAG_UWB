# Design: ESP32 Bridge

## Meta

- **Created**: 2026-05-09
- **Status**: drafting
- **Approved by**: (pending)
- **Approved at**: (pending)
- **Origin**: PBL_SPI_TEST_01 의 SPI 검증 결과를 PBL_TAG_UWB 본 프로젝트로 이관

---

## Goal

ESP32-S3 가 PBL_TAG_UWB 의 단순 측위 출력기에서 **데이터 허브** 로 확장된다. UWB 좌승법 위치 추정 결과를 모터 제어부로 보내고, 카메라(OpenMV)로부터 정밀 정렬 정보를 받아 도착 단계에서 제어 모드를 전환한다.

## Confirmed Goal

ESP32-S3 가 다음을 수행:
1. (기존) BU01 UWB Tag 의 4개 anchor 거리값(UART)으로부터 최소좌승법으로 (x, y) 추정
2. **(신규)** OpenMV H7 (SPI 슬레이브) 으로부터 AprilTag 정렬 정보(태그 ID, yaw, 거리, 정렬 플래그)를 100ms 주기 폴링
3. **(신규)** STM32 F411RE 모터 제어부(UART)에 단계별 명령 패킷 송신
   - Phase A (원거리): (x, y) 좌표
   - Phase C (도착 정렬): (yaw, 거리, arrived_flag) 카메라 정보
4. 위 통신·연산이 서로 차단하지 않는 비동기 운용

## Non-goals

- ESP32 가 직접 모터 PWM/PID 제어 — STM32 F411RE 책임
- ESP32 가 직접 카메라/AprilTag 인식 — OpenMV 책임
- 칼만 필터 / 센서 퓨전 — 1차 안정화 이후 (D7 의 P2 단계)
- IMU 자이로 헤딩 추정 — 보유 IMU(AM-GYRO-V02) 사양 미확인 상태로 보류
- 다중 로봇 / 무선 통신 — 범위 외

---

## Research

### 시스템 구조

```
[BU01 Anchor 1~4]
     │ UWB 레인징 (거리)
     ↓
[BU01 Tag (STM32+DW1000)]
     │ UART (기존, docs/uart-protocol.md)
     ↓
[ESP32-S3] ─── SPI ───→ [OpenMV H7]   (AprilTag 정렬)
     │
     └── UART ───→ [STM32 F411RE]      (모터 제어, Pure Pursuit)
                       │
                       ↓
                   디퍼렌셜 드라이브 로봇 (0.3 m/s)
```

### 운영 단계 (Phase 모델)

| Phase | 위치 | 출력 정보 | 제어 알고리즘 |
|-------|------|----------|--------------|
| A | 원거리 (UWB 영역) | (x, y) UWB 좌표 | Pure Pursuit |
| B | 전환 임계 (팀 결정 거리) | 모드 전환, OpenMV 데이터 안정화 대기 | — |
| C | 도착 정렬 (카메라 시야) | (yaw, dist, arrived) AprilTag 정보 | 정밀 정렬 |

### 결정에 영향을 준 요소

- 로봇 속도 0.3 m/s (느림) → 100ms 주기에 30mm 이동
- UWB 정확도 ~±100mm → 도착 단계의 정밀도 한계 → 카메라 보조 필요
- ESP32 가 좌승법 + SPI + UART 모두 담당 → 부하 분산 필요
- STM32 F411RE 는 100MHz Cortex-M4F (Pure Pursuit 짤 여력 충분)
- 디퍼렌셜 드라이브 → "향한 방향 = 진행 방향" 특성, 회전 후 전진 가능
- STM32 측 모터 엔코더 보유 확인 → 단기 정확 추정 가능

### 헤딩 추정 옵션 비교 (이 세션에서 검토)

| 옵션 | 장점 | 단점 | 채택 |
|------|------|------|------|
| AprilTag yaw | OpenMV 이미 구현, 절대값 | 시야 제약 (멀리서 못 봄) | **부분 (Phase C 한정)** |
| 위치 변화 기반 | 추가 센서 X | 0.3 m/s + UWB 노이즈 → 부정확 | 미채택 |
| IMU 자이로 (AM-GYRO-V02) | 정밀, 시야 무관 | 사양 미확인, ESP32 측 연결 부담 | **보류** |
| 듀얼 UWB Tag | 정확, 절대값, 정지 시 OK | 하드웨어 추가 | 미채택 |
| STM32 엔코더 오도메트리 | 단기 정확, 헤딩 자동 산출 | 장기 드리프트 | **채택 (D2, D4)** |

---

## Decisions

### D1: STM32 가 Pure Pursuit 알고리즘 담당 (고수준 인터페이스)

- **Status**: provisional (STM32 팀 합의 대기)
- **Rationale**:
  - ESP32 부하 분산 (좌승법 + 양방향 통신 이미 무거움)
  - STM32 F411RE 는 100MHz Cortex-M4F 로 충분
  - 디퍼렌셜 드라이브 + Pure Pursuit 는 검증된 패턴
  - ESP32 → STM32 인터페이스가 단순 (좌표만 전달)

### D2: STM32 가 엔코더 + UWB 융합 추정 (Source of Truth = 융합 결과)

- **Status**: provisional
- **Rationale**:
  - STM32 측 모터 엔코더 존재 → 활용 가치 높음
  - 엔코더 단독: 단기 정확, 장기 드리프트
  - UWB 단독: 장기 절대값, 단기 jitter
  - 두 센서를 STM32 에서 융합 → "단기 부드러움 + 장기 절대값" 동시 달성
  - ESP32 는 거의 raw UWB 좌표를 송신 (LPF 약하게 또는 없이)
  - STM32 가 매 ms 단위 엔코더 오도메트리, 매 100ms ESP32 UWB 값으로 보정

### D3: SPI 마스터-폴링 패턴 (ESP32 → OpenMV)

- **Status**: resolved (PBL_SPI_TEST_01 에서 검증 완료)
- **Rationale**:
  - ESP32 가 SPI 마스터, OpenMV 가 슬레이브
  - 100ms 폴링 주기 — 0.3 m/s 속도 대비 충분
  - SPI 모드 0, MSBFIRST, 1 MHz
  - OpenMV 슬레이브 측 `pyb.SPI(2, SPI.SLAVE, ...)` 검증 완료

### D4: 헤딩은 STM32 가 엔코더 오도메트리로 자체 추정

- **Status**: provisional
- **Rationale**:
  - D2 갱신으로 STM32 가 엔코더 오도메트리 수행 → 헤딩(θ) 자동 산출
  - 디퍼렌셜 운동학: θ = ∫(v_R - v_L) / L dt
  - ESP32 는 헤딩 추정 책임 없음
  - AprilTag yaw 는 Phase C 에서 STM32 의 엔코더 헤딩과 비교/보정용
  - **정지 상태에서의 절대 헤딩 보장은 한계**: 시작 위치 강제 + Phase C 의 AprilTag 캘리브레이션 의존
  - 추후 IMU 도입 검토 시 D4 갱신 또는 ADR 추가

### D5: SPI/UART 비차단 운용 (단계적 도입)

- **Status**: provisional (구현 미정)
- **Rationale**:
  - 좌승법 연산이 통신에 의해 차단되면 안 됨 (R3 참조)
  - 단계적 접근 (Phase 1 단순 → Phase 2 분리 → Phase 3 정교화)
  - 자세한 분석: [docs/blocking-analysis.md](../../docs/blocking-analysis.md)

### D6: 패킷 포맷 — 고정 길이, Big-Endian

- **Status**: provisional
- **Rationale**:
  - SPI: 6바이트 고정 (헤더 없음, CS 엣지로 동기화)
  - UART (ESP32 → STM32 모터부): 헤더 + 페이로드 + CRC 가변
  - 기존 UART (BU01 → ESP32) 는 ASCII 텍스트 (docs/uart-protocol.md) — 변경 없음
  - Big-Endian: hex 덤프가 사람이 읽기 쉬움
  - 정수 스케일링 (× 10) 으로 부동소수점 회피

### D7: 엔코더 + UWB 융합 전략 — 단계적 적용

- **Status**: provisional
- **Rationale**:
  - 1차에 칼만 필터 들어가면 디버깅 어려움 → 단순 → 복잡 단계적 적용
  - **P1 (1차 구동)**: STM32 가 단순 가중평균 — 예: pos = 0.7 × encoder_pos + 0.3 × uwb_pos
  - **P2 (정밀화)**: 칼만 필터 도입, 각 센서 분산에 따라 가중치 자동
  - **P3 (강건화)**: 슬립/이상치 감지, IMU 추가 시 자이로 융합
  - P1 으로 일단 동작 검증 → 실측 데이터로 P2 도입 여부 결정

### D8: Phase 전환 정책 (A → B → C)

- **Status**: provisional (전환 거리 임계값 미정)
- **Rationale**:
  - Phase A → B: 목표까지 거리 임계값 이내 진입 (팀에서 결정, 잠정 200~500 mm)
  - Phase B → C: OpenMV 의 tag_id != 0xFF 가 안정적으로 일정 시간(예: 200ms) 이상 유지
  - Phase C → 종료: arrived_flag = 1 일정 시간 유지 또는 mode=Stop 명시
  - Phase 전환 시 패킷 빌드는 atomic (mutex 또는 이중 버퍼)
  - oscillation 방지를 위해 hysteresis 적용 (예: A→B 200mm, B→A 300mm)

---

## Constraints

### 통신
- SPI 마스터: ESP32 (확정)
- SPI 슬레이브: OpenMV H7
- SPI 모드: SPI_MODE0, MSBFIRST, 초기 1 MHz
- UART (BU01 → ESP32): 기존 단방향 텍스트 — `docs/uart-protocol.md` 참조
- UART (ESP32 → STM32 F411RE): 신규 바이너리, baudrate 미정 (115200 제안)

### 하드웨어
- ESP32-S3-DevKitC-1 (PlatformIO + Arduino framework)
- OpenMV H7 / H7 Plus
- STM32 F411RE (Nucleo 또는 자체 보드)
- BU01 UWB 모듈 (앵커 4 + 태그 1, 별도 레포 `C:\PBL_ANCHOR_TAG`)
- 디퍼렌셜 드라이브 로봇

### 성능 / 환경
- 로봇 속도: 0.3 m/s
- UWB 위치 정확도: ~±100mm 가정
- SPI 폴링 주기: 100 ms (10 Hz)
- UART 송신 주기 (ESP32 → STM32 F411RE): 50~100 ms
- 좌승법 갱신 주기: ≥ 1 Hz (PBL_TAG_UWB Sprint Contract), 가능하면 20~50 Hz

### 시스템
- 좌승법 연산이 통신으로 인해 차단되지 않을 것 (R3)
- 카메라 처리(OpenMV)가 SPI 슬레이브 응답으로 인해 멈추지 않을 것
- Phase 전환 시 패킷 일관성 유지 (D8)

---

## Known Gaps

- STM32 F411RE 팀과 D1, D2 합의 필요 (가장 큰 미결 의존성)
- AM-GYRO-V02 IMU 사양 확인 후 D4 갱신 여부 재검토 (Z축 자이로 유무가 핵심)
- Phase A → B 전환 임계 거리 (실측 후 결정)
- AprilTag 미감지 시 STM32 측 동작 정의 필요 (정지? 마지막 명령 유지?)
- 시작 위치 강제 메커니즘 (충전 도크? 수동 캘리브레이션?)
- D5 의 구체적 구현 방식 결정 — 단일 코어 + 짧은 폴링 vs 듀얼 코어 + 태스크 (`docs/blocking-analysis.md` 의 Phase 1/2/3 단계 참조)
