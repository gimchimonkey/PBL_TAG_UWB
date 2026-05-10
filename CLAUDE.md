# PBL_TAG_UWB

UWB 기반 실내 측위 시스템. STM32+DW1000(BU01) tag로부터 4개 anchor 거리값을 UART로 받아, ESP32-S3가 2D 위치를 계산한다.

## 하드웨어

- **Tag · Anchor**: STM32+BU01(DW1000), 펌웨어는 **별도 레포 `C:\PBL_ANCHOR_TAG`**
  - anchor 4개는 같은 코드, anchor ID(주소값)만 바꿔서 빌드/업로드
  - tag 1개도 같은 레포 안
- **Anchor 좌표**: [docs/hardware-layout.md](docs/hardware-layout.md)
- **Center**: ESP32-S3-DevKitC-1 (PlatformIO + Arduino framework)
  - UART RX = `GPIO16`, baudrate `115200`, 8N1
  - USB CDC 활성 (`-DARDUINO_USB_CDC_ON_BOOT=1`)
  - 개발 PC: `COM7`

## 데이터 흐름

### 기존 (단순 측위)
```
4 Anchors ─UWB─> Tag(STM32) ─UART─> ESP32-S3 ─Serial─> PC
                                              └─CSV──> 로그 파일
```

### 확장 (모터 제어 통합 — `docs/system-architecture.md` 참조)
```
4 Anchors ─UWB─> Tag(STM32) ─UART─> ESP32-S3 ─┬─Serial──> PC (디버그)
                                               ├─SPI ────> OpenMV H7 (AprilTag 정렬)
                                               └─UART ───> STM32 F411RE (모터 제어)
```
3 Phase 운영 (UWB 원거리 → 카메라 정렬). 자세한 건 `docs/system-architecture.md`

## 측위 방식

- **현재**: 2D, 4개 anchor → Least Squares
- **향후**: 3D는 Known Gap. 가정은 [docs/coordinate-system.md](docs/coordinate-system.md) 참조

## 금지사항 (요약 — 자세한 규칙은 [.claude/rules/embedded.md](.claude/rules/embedded.md))

- `String` 클래스 남용 금지 — heap 단편화, char 버퍼 우선
- ISR 안에서 `Serial.print` / `delay` / heap 할당 금지 — 플래그만
- `delay(ms)`로 메인 루프 블로킹 금지 — `millis()` 기반 비동기
- 거리값처럼 외부 입력은 반드시 범위 검증 (0 < d < 50m)

## 검증 기준 (Sprint Contract)

- [ ] 정적 위치 5~10곳 평균 오차 ≤ 15cm
- [ ] 위치 출력 주기 ≥ 1Hz
- [ ] 빌드 성공: `pio run`
- [ ] 단위테스트 통과: `pio test`
- [ ] 알고리즘 변경 시 로그 리플레이로 회귀 검증

## 폴더 구조

```
PBL_TAG_UWB/
├── src/           — 펌웨어 코드 (ranging/, localization/, calibration/, io/)
├── include/       — 공용 헤더
├── lib/           — 외부 라이브러리
├── test/          — PlatformIO Unity 단위테스트
├── docs/          — 사람의 진실 (HW 배치, 좌표계, 프로토콜)
├── tools/         — PC 측 스크립트 (로그 리플레이 등)
├── .dev/          — AI 작업 흔적 (스캐폴드, ADR)
├── plans/         — /specify 산출물 (피처별 design+requirements+tasks)
└── .claude/       — AI 설정 (rules, skills)
```

## 작업 시작 전 확인

1. 이 파일의 "현재 문서 목록"을 본다
2. 작업과 관련된 `plans/{feature}/` 가 있으면 design.md → requirements.md → tasks.md 순서로 읽는다
3. 임베디드 코드 작업이면 `.claude/rules/embedded.md` 가 자동 로드됨

## 현재 문서 목록

### docs/ — 사람의 진실
| 파일 | 설명 |
|------|------|
| [docs/hardware-layout.md](docs/hardware-layout.md) | Anchor 4개 좌표 · 결선 · STM32 펌웨어 위치 |
| [docs/coordinate-system.md](docs/coordinate-system.md) | 좌표계 정의 (원점·축·단위·3D 확장 가정) |
| [docs/uart-protocol.md](docs/uart-protocol.md) | BU01 Tag(STM32) → ESP32 UART 패킷 포맷 |
| [docs/system-architecture.md](docs/system-architecture.md) | 전체 시스템 컴포넌트·데이터 흐름·3 Phase 운영 |
| [docs/blocking-analysis.md](docs/blocking-analysis.md) | ESP32 블로킹 포인트 분석 + 비차단 설계 단계 |

### plans/ — 피처별 설계
| 디렉토리 | 설명 |
|---------|------|
| [plans/esp32-bridge/design.md](plans/esp32-bridge/design.md) | ESP32 데이터 허브 — 결정사항 D1~D8 |
| [plans/esp32-bridge/requirements.md](plans/esp32-bridge/requirements.md) | ESP32 데이터 허브 — 요구사항 R0~R5 (GWT) |

### .claude/ — AI 설정
| 파일 | 설명 |
|------|------|
| [.claude/rules/embedded.md](.claude/rules/embedded.md) | 임베디드 코딩 규칙 (`*.cpp`/`*.h` 자동 적용) |
