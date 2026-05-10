# Blocking Analysis

ESP32-S3 데이터 허브 역할에서 발생할 수 있는 블로킹(차단) 포인트와 단계적 비차단 설계 지침. `plans/esp32-bridge/design.md` 의 D5 / R3 보강 문서.

---

## 1. 핵심 원칙

> **블로킹 함수는 작업이 끝날 때까지 CPU 를 잡고 안 놓아준다. 좌승법·SPI·UART·UWB 수신이 동시에 안 막히려면, 통신을 논블로킹으로 만들거나 코어를 분리해야 한다.**

ESP32-S3 의 무기:
- **듀얼 코어** (Core 0 PRO_CPU, Core 1 APP_CPU, 각 240 MHz)
- **FreeRTOS** 내장 (Arduino-ESP32)
- **DMA (GDMA)** 지원 (큰 버퍼에서 효과)

---

## 2. 블로킹 포인트 카테고리

### 카테고리 1: 통신 블로킹 (가장 흔함)

| # | 블로킹 지점 | 시간 | 영향 |
|---|------------|------|------|
| 1 | `SPI.transferBytes(6 byte)` | ~50 µs | OpenMV 폴링 시 매 100 ms 마다 |
| 2 | `delayMicroseconds(10) × 2` (CS 타이밍) | 20 µs | SPI 트랜잭션마다 추가 |
| 3 | `Serial1.write(16 byte)` UART 송신 | **~1.4 ms** @ 115200 | **가장 큰 단발 블로킹** |
| 4 | `Serial.println(...)` 디버그 출력 | 가변 | 부동소수점 포맷 시 ms 단위 |
| 5 | UWB 거리 수신 (BU01 UART) | 가변 | 텍스트 파싱 비용 |

### 카테고리 2: 연산 블로킹 (CPU 점유)

| # | 블로킹 지점 | 시간 | 영향 |
|---|------------|------|------|
| 6 | 좌승법 풀이 (4×2 행렬 의사역행렬) | 100 µs ~ 수 ms | 행렬 라이브러리에 따라 |
| 7 | `sqrt`, `atan2`, `cos/sin` | 수 µs | 좌승법에 다수 호출 |
| 8 | `sprintf("%.2f", x)` 부동소수점 포맷 | **수 ms** | 의외의 복병 |

### 카테고리 3: 동기화 블로킹 (멀티태스킹)

| # | 블로킹 지점 | 시간 | 영향 |
|---|------------|------|------|
| 9 | `xSemaphoreTake(mutex, portMAX_DELAY)` | 가변 | 다른 태스크가 안 놓으면 무한 대기 |
| 10 | `xQueueReceive(queue, portMAX_DELAY)` | 가변 | 메시지 안 오면 무한 대기 |
| 11 | mutex 잡고 안에서 긴 작업 | 자기가 잡은 시간 | 다른 태스크 다 막음 |

### 카테고리 4: 시스템 블로킹 (놓치기 쉬움)

| # | 블로킹 지점 | 시간 | 영향 |
|---|------------|------|------|
| 12 | `malloc / new` 동적 할당 | 수 µs ~ ms | RTOS 에서 비결정적 |
| 13 | Watchdog 미리셋 | ~5 초 후 리셋 | 태스크가 너무 오래 돌면 |
| 14 | ISR 안에서 긴 작업 | ISR 시간 | 다른 인터럽트 차단 |
| 15 | Flash 쓰기 (`Preferences`) | 수십 ms | 짧은 주기에 호출 금지 |

### 카테고리 5: Phase 전환 시 위험

| # | 위험 | 영향 |
|---|------|------|
| 16 | 모드 전환 중 패킷 송신 진행 | 일부 필드는 옛 모드, 일부는 새 모드 → STM32 혼란 |
| 17 | Phase B 진입 직후 yaw 안 안정화 | 정렬 모드 전환했는데 카메라 데이터 부정확 |
| 18 | OpenMV 시야 진입 깜빡임 | 모드 oscillation (A↔B 진동) |

---

## 3. 가장 위험한 TOP 5 + 대응

### 🔥 1순위 — UART 송신 1.4 ms (#3)
가장 큰 단발 블로킹. 좌승법 50 ms 주기 대비 큼.

**대응**:
- Phase 2 에서 Core 1 의 별도 태스크로 분리
- 추가로 줄이려면 ESP-IDF UART DMA (`uart_write_bytes_with_break`)

### 🔥 2순위 — `sprintf` 부동소수점 (#8)
디버그 출력에서 의외로 잡힘. `Serial.printf("%.2f", x)` 한 줄이 수 ms.

**대응**:
- 운영 시 부동소수점 포맷 빼기 (정수 스케일링 후 `%d`)
- 디버그 출력은 별도 태스크에 큐로 던지기

### 🔥 3순위 — UWB 거리 수신 텍스트 파싱 (#5)
ASCII `"Anchor: 0xAABB, Distance: 1.23m"` 파싱이 매 패킷마다.

**대응**:
- 인터럽트로 UART 라인 모으고, 메인 루프에서 파싱
- 향후 바이너리 패킷화 검토 (`docs/uart-protocol.md` Known Gap)

### 🔥 4순위 — Phase 전환 race (#16)
송신 중간에 모드 바뀌면 패킷 절반은 옛 데이터.

**대응**:
- 패킷 빌드는 mutex 안에서 한 번에
- 또는 atomic 한 패킷 swap (이중 버퍼)

### 🔥 5순위 — Watchdog (#13)
Core 0 의 좌승법 태스크가 `vTaskDelay` 안 부르면 5초 후 리셋.

**대응**:
- 좌승법 태스크 끝에 `vTaskDelay(pdMS_TO_TICKS(20))` 또는 `taskYIELD()` 필수

---

## 4. 비차단 만드는 4가지 패턴

### 패턴 A — 짧은 타임아웃 폴링 (가장 단순)
```cpp
if (시간_됐나(100ms)) {
    SPI.transferBytes(...);   // 여전히 블로킹이지만 자주 안 함
}
좌승법_한_단계();
```
- ✅ 단순
- ❌ transferBytes 호출 시 여전히 블로킹

### 패턴 B — 인터럽트 (콜백)
- ✅ CPU 안 막음
- ❌ Arduino-ESP32 SPI 라이브러리 기본 콜백 미지원 (ESP-IDF 직접 호출 필요)

### 패턴 C — DMA
- ✅ CPU 완전히 안 막음 (백그라운드)
- ❌ 작은 버퍼 (5~16 byte) 에서는 셋업 오버헤드가 더 큼 → 효과 미미

### 패턴 D — 멀티태스킹 (FreeRTOS) ⭐
- ✅ 각 통신이 다른 코어에서 동작 → 진짜 병렬
- ✅ Arduino 코어에서 `xTaskCreatePinnedToCore` 한 줄로 가능
- ⚠️ 데이터 공유는 mutex/queue 필요

---

## 5. 추천 코어 분담 (패턴 D)

```
┌── Core 0 (PRO_CPU) ─────────────────────────┐
│  Task: 좌승법 + UWB 수신                     │
│  - UART RX 인터럽트로 거리값 받기            │
│  - 좌승법 풀이 → robot_x, robot_y 갱신       │
│  - 미션 매니저 (Phase 결정)                   │
└─────────────────────────────────────────────┘

┌── Core 1 (APP_CPU) ─────────────────────────┐
│  Task: SPI 폴링 (100 ms)                     │
│  - OpenMV 6 byte 받기                        │
│  - openmv_tag_id, yaw, dist, arrived 갱신    │
│                                               │
│  Task: UART 송신 (50 ms)                     │
│  - 공유 변수 읽어 패킷 빌드                   │
│  - STM32 F411RE 로 16 byte 송신              │
└─────────────────────────────────────────────┘
```

### 데이터 공유 가이드

| 공유 변수 | 타입 | 보호 방식 |
|---------|------|---------|
| `robot_x`, `robot_y` (float) | 8 byte 합계 | **Mutex** 필수 |
| `openmv_tag_id` (int8) | 1 byte | volatile 만으로 OK (atomic) |
| `openmv_yaw_x10`, `dist_mm` (int16) | 각 2 byte | volatile 만으로 OK |
| 패킷 송신 버퍼 | 16 byte | **Mutex** 또는 이중 버퍼 |
| `phase_state` (enum) | 1 byte | volatile + atomic 갱신 |

---

## 6. 단계적 도입 권고

처음부터 다 짜지 말고 **단순 → 분리 → 정교화** 순서로.

### Phase 1: 단일 루프 (Day 1)
```cpp
void loop() {
    if (시간_됐나(50ms))   uart_send_to_stm32();    // 1.4 ms 블로킹 OK
    if (시간_됐나(100ms))  spi_poll_openmv();       // 50 µs 블로킹 OK
    if (uart_rx_available()) parse_bu01_distance();
    if (4개_거리_다_왔나) solve_least_squares();
    update_phase();
    delay(1);   // 또는 vTaskDelay
}
```
- **목표**: 동작 검증, Phase 전환 로직 확인
- **0.3 m/s 의 느린 로봇이라 단일 루프로도 충분히 돌아감**
- 1.4 ms UART 송신 × 20 Hz = 2.8% CPU 차단 — 무시 가능 수준

### Phase 2: 코어 분리 (Day N)
- UART 송신 태스크 → Core 1
- SPI 폴링 태스크 → Core 1
- 좌승법 + UWB 수신 → Core 0
- mutex / volatile 도입

### Phase 3: 정교화 (필요 시)
- `sprintf` 정리 (운영 시 부동소수점 포맷 제거)
- Phase 전환 atomic 보장
- UART DMA (필요할 때)
- 칼만 필터 (D7 의 P2)

---

## 7. 검증 방법

블로킹이 실제로 문제가 되는지 측정:

```cpp
uint32_t t1 = micros();
SPI.transferBytes(...);
uint32_t spi_time = micros() - t1;

if (spi_time > 200) {  // 임계값
    Serial.printf("SPI slow: %lu us\n", spi_time);
}
```

각 블로킹 지점에 타임스탬프 측정 코드 잠시 추가 → 로그로 확인 → 결정.

**측정 안 하고 처음부터 듀얼 코어 짜는 건 과잉 설계.**

---

## 8. 안 위험한 것들 (오해 방지)

| 항목 | 사실 |
|------|------|
| `digitalWrite()` | ~1 µs, 무시 가능 |
| `millis()` / `micros()` | ns 수준 |
| 변수 할당 (`a = b`) | 32 비트 이하면 atomic |
| 단순 산술 (+, -, *) | 1 사이클 |

---

## 한 줄 요약

> **사용자 시스템의 진짜 블로킹은 UART 송신(1.4 ms)과 sprintf(수 ms) 두 개. 나머지는 작아서 단일 루프로도 0.3 m/s 로봇은 충분히 굴린다. 블로킹 무서워서 처음부터 듀얼 코어 짜지 말고, 단순 → 분리 → 정교화 순서로.**
