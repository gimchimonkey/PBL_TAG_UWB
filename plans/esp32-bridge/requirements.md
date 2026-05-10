# Requirements: ESP32 Bridge

## Requirements

### R0: ESP32 가 OpenMV 로부터 AprilTag 정렬 정보를 주기적으로 수신한다 (D3)

#### R0.1: SPI 마스터-폴링 통신 동작
- **Given**: ESP32 와 OpenMV 가 SPI 로 결선되어 있고 OpenMV 가 SPI 슬레이브로 동작 중
- **When**: ESP32 가 100 ms 주기로 SPI 트랜잭션을 시작 (CS LOW → 6바이트 전송 → CS HIGH)
- **Then**: 한 트랜잭션에서 6바이트 패킷을 수신한다

#### R0.2: SPI 패킷 포맷 (OpenMV → ESP32, 6바이트 고정)
- **Given**: OpenMV 가 응답 패킷을 작성
- **When**: ESP32 가 패킷을 디코딩
- **Then**: 다음 구조로 해석된다 (Big-Endian)

| 인덱스 | 크기 | 필드 | 의미 |
|--------|-----|------|------|
| [0] | 1 byte | tag_id | 인식 태그 ID. 미감지 = 0xFF |
| [1-2] | 2 byte (int16) | yaw | AprilTag yaw, deg × 10 (예: 387 = 38.7°) |
| [3-4] | 2 byte (int16) | dist | 태그까지 거리, mm |
| [5] | 1 byte | arrived_flag | 0 = 미정렬, 1 = 정렬 도달 |

#### R0.3: 미감지 처리
- **Given**: OpenMV 시야에 등록 ZONE 태그가 없음
- **When**: ESP32 가 SPI 트랜잭션 수행
- **Then**: tag_id = 0xFF, 나머지 필드는 의미 없는 값 (0 권장)으로 수신된다

#### R0.4: 슬레이브 미응답 처리
- **Given**: OpenMV 가 부팅 안 됐거나 SPI 슬레이브 미준비
- **When**: ESP32 가 트랜잭션 수행
- **Then**: 6바이트가 모두 0x00 또는 0xFF 로 수신되며, ESP32 는 이를 "유효하지 않음" 으로 판단한다

---

### R1: ESP32 가 BU01 UWB 시스템으로 절대 위치를 추정한다 (기존 PBL_TAG_UWB 핵심)

#### R1.1: 좌승법 위치 계산
- **Given**: 4개 UWB 앵커로부터의 거리 측정값이 ESP32 에 들어옴 (`docs/uart-protocol.md` 형식)
- **When**: ESP32 가 최소 좌승법 연산 수행
- **Then**: 로봇의 (x, y) 절대 좌표가 계산된다 (단위: m, 추후 mm 변환은 송신 시점)

#### R1.2: 위치 필터링 (약한 LPF 또는 미적용)
- **Given**: UWB 원시 거리 측정에 ±100mm 정도의 노이즈가 존재
- **When**: ESP32 가 위치 추정 후 STM32 송신 직전
- **Then**: ESP32 단계에서는 강한 필터를 적용하지 않는다. 명백한 outlier 제거(`0 < d < 50m`)만 수행하고, 본격적인 평활화는 STM32 의 엔코더 융합(D7)에 위임한다

---

### R2: ESP32 가 STM32 F411RE 모터 제어부에 단계별 명령을 송신한다 (D1, D8)

#### R2.1: UART 송신 주기
- **Given**: STM32 F411RE 가 UART 수신 준비 완료, baudrate 합의됨
- **When**: ESP32 메인 루프 또는 송신 태스크
- **Then**: 50~100 ms 주기로 명령 패킷을 송신한다

#### R2.2: UART 패킷 포맷 (ESP32 → STM32 F411RE)
- **Given**: ESP32 가 패킷을 송신
- **When**: STM32 F411RE 가 디코딩
- **Then**: 다음 구조로 해석된다 (Big-Endian, 헤더 + 페이로드 + CRC)

| 영역 | 크기 | 필드 | 의미 |
|------|-----|------|------|
| Header | 2 byte | 0xAA, 0x55 | 프레임 동기 |
| Length | 1 byte | payload_len = 11 | 페이로드 길이 |
| Payload [0-1] | int16 | x_now | 현재 x, mm (Phase A) |
| Payload [2-3] | int16 | y_now | 현재 y, mm (Phase A) |
| Payload [4-5] | int16 | x_target | 목표 x, mm (Phase A) |
| Payload [6-7] | int16 | y_target | 목표 y, mm (Phase A) |
| Payload [8] | uint8 | mode | 0=Idle, 1=GoToPoint(A), 2=Align(C), 3=Stop |
| Payload [9-10] | int16 | align_yaw | 정렬용 AprilTag yaw, deg × 10 (Phase C 일 때만 의미) |
| CRC | 2 byte | crc16 | 페이로드 CRC-16 (CCITT) |

총 16 byte / 패킷.

#### R2.3: Phase A — 원거리 이동
- **Given**: 미션 매니저가 Phase A 로 판정
- **When**: ESP32 가 명령 패킷 생성
- **Then**: mode = 1, (x_now, y_now, x_target, y_target) 모두 의미 있는 값으로 채워짐. align_yaw 는 0

#### R2.4: Phase B — 전환 (도착 임박, 카메라 시야 진입)
- **Given**: 목표까지 거리가 임계값 이내, OpenMV 가 tag_id 안정적으로 인식
- **When**: ESP32 가 모드 전환 결정
- **Then**: 짧은 시간(예: 200ms) 동안 OpenMV 데이터 안정화 검증 후 Phase C 로 전환

#### R2.5: Phase C — 정밀 정렬
- **Given**: Phase C 로 전환됨
- **When**: ESP32 가 명령 패킷 생성
- **Then**: mode = 2, align_yaw 에 OpenMV 최신 yaw, x_now/y_now 는 마지막 추정값 유지하거나 0 (STM32 가 무시). STM32 는 카메라 정보 기반 정렬

#### R2.6: 정지 — 도착 완료
- **Given**: arrived_flag = 1 이 일정 시간 유지
- **When**: 미션 매니저가 도착 판정
- **Then**: mode = 3 으로 송신, STM32 정지

---

### R3: 통신이 좌승법 연산을 차단하지 않는다 (D5, blocking-analysis.md 참조)

#### R3.1: 비차단 SPI 트랜잭션
- **Given**: ESP32 가 좌승법 연산 중
- **When**: SPI 폴링 시각이 도래
- **Then**: 좌승법 연산이 SPI 트랜잭션 시간(약 50 µs) 만큼 멈추지 않는다 (Phase 1 에서는 허용 가능, Phase 2 에서 태스크 분리)

#### R3.2: 비차단 UART 송신
- **Given**: ESP32 가 좌승법 연산 또는 SPI 폴링 중
- **When**: UART 송신 시각이 도래
- **Then**: 다른 작업이 UART 송신 시간(약 1.4 ms @ 115200) 만큼 멈추지 않는다 (Phase 2 또는 DMA 도입)

---

### R4: 시스템이 미감지 / 통신 실패 상황에서도 안전하게 동작한다

#### R4.1: AprilTag 미감지 시 동작
- **Given**: tag_id = 0xFF 가 일정 시간 (예: 500 ms) 이상 지속
- **When**: ESP32 가 모드 결정
- **Then**: Phase A 유지 또는 정의된 fallback 동작 수행 (Phase C 전환하지 않음)

#### R4.2: SPI 트랜잭션 실패 시 동작
- **Given**: SPI 수신 결과가 명백히 비정상 (모든 바이트 0xFF, sanity check 실패)
- **When**: ESP32 가 패킷 검증
- **Then**: 마지막 유효 데이터를 유지하거나 0xFF 로 처리하여 시스템이 정지하지 않는다

#### R4.3: Phase 전환 oscillation 방지
- **Given**: 로봇이 Phase 임계 경계에서 흔들림
- **When**: 거리 측정값이 경계 근처에서 변동
- **Then**: hysteresis 적용 (예: A→B 200mm 진입, B→A 300mm 이탈) — 전환 잦은 떨림 방지

---

### R5: STM32 F411RE 가 엔코더 오도메트리 + UWB 보정으로 위치/헤딩을 추정한다 (D2, D4, D7)

#### R5.1: 엔코더 오도메트리 단기 추정 (P1)
- **Given**: STM32 가 좌/우 모터 엔코더로 회전수 측정 가능
- **When**: STM32 제어 루프 (예: 매 1 ms)
- **Then**: 디퍼렌셜 운동학으로 (x, y, θ) 를 적분 갱신한다

#### R5.2: UWB 가중평균 보정 (P1, 1차 융합 전략)
- **Given**: ESP32 로부터 UART 패킷 수신 (x_now, y_now, mode=1)
- **When**: STM32 가 패킷 도착 시점
- **Then**: 단순 가중평균으로 자체 추정 위치를 보정한다 (예: pos = α × encoder_pos + (1-α) × uwb_pos, α ≈ 0.7)

#### R5.3: 헤딩 활용
- **Given**: STM32 가 자체 헤딩(θ) 보유
- **When**: mode = 1(GoToPoint, Phase A) 인 경우
- **Then**: 목표까지의 방향과 현재 헤딩 차이로 회전 명령 결정 (Pure Pursuit)

#### R5.4: AprilTag yaw 보정 (mode = 2, Phase C)
- **Given**: mode = 2(Align), align_yaw 필드에 유효값
- **When**: STM32 가 정렬 제어
- **Then**: 자체 헤딩과 align_yaw 비교, 필요 시 헤딩값 보정 또는 정렬 명령에 직접 활용

---

## Out of Scope (이 명세의 범위 외)

- BU01 UWB Tag 와 ESP32 간 통신 (이미 `docs/uart-protocol.md` 에 정의됨, 변경 없음)
- STM32 측 Pure Pursuit 알고리즘 구현 세부 (STM32 F411RE 팀 책임)
- OpenMV 측 SPI 슬레이브 구현 세부 (OpenMV 동기 책임, PBL_SPI_TEST_01 에서 검증 완료)
- 미션 매니저 (다음 목표 좌표 결정 로직)
- 칼만 필터 / 슬립 감지 (D7 의 P2/P3 단계, 추후 별도 결정)
- IMU(AM-GYRO-V02) 통합 (사양 확인 후 결정)
