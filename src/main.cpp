#include <Arduino.h>
#include <math.h>
#include <stdio.h>

HardwareSerial TagSerial(1);

#define TAG_RX 16

// ── 앵커 식별자 (16-bit) — STM32 펌웨어가 송신하는 실제 값으로 채울 것
#define ANCHOR1_ID 0x0001  // TODO
#define ANCHOR2_ID 0x0002  // TODO
#define ANCHOR3_ID 0x0003  // TODO
#define ANCHOR4_ID 0x0004  // TODO

// ── 앵커 좌표 [m]
static const float ANCHOR_X[4] = { 0.00f,  5.00f, 2.27f,  0.00f };  
static const float ANCHOR_Y[4] = { 0.00f, 0.0f, -8.47f,  -8.47f };  

// ── 거리 보정 오프셋 (측정값 - 실제값)
static const float OFFSET[4] = {
    4.16f-3.37f,
    5.28f-4.5f,
    6.2f-5.4f,
    7.31f-5.35f,                  
};

static float dist[4];
static bool  gotDist[4];

// ── 한 줄 파싱: "Anchor: 0xAABB, Distance: 1.23m"
static void parseAndStore(const char* line) {
    uint16_t addr;
    float    d;
    if (sscanf(line, "Anchor: %hx, Distance: %fm", &addr, &d) != 2) return; 
    // 입력이 제대로 들어오는지 확인. != 2 -> 값이 2개가 나와야 정상
    // 형식 안맞으면 0이나 1 반환. return
    //둘다 잘 나오면 2 반환. 통과. 
    if (d <= 0.0f || d >= 50.0f) return;
    // 거리가 음수 나오거나 50m 같은 이상한 값 나오면 return

    int slot;
    switch (addr) {
        case ANCHOR1_ID: slot = 0; break;
        case ANCHOR2_ID: slot = 1; break;
        case ANCHOR3_ID: slot = 2; break;
        case ANCHOR4_ID: slot = 3; break;
        default: return;  // 외 ID 폐기
    }
    dist[slot] = d - OFFSET[slot];
    gotDist[slot] = true;
}

// ── 4-앵커 최소자승법
//
// (x-xᵢ)² + (y-yᵢ)² = rᵢ²  를 i=1 식으로 빼서 선형화하면
//   2(xᵢ-x₁)·x + 2(yᵢ-y₁)·y = r₁² - rᵢ² + xᵢ² - x₁² + yᵢ² - y₁²
// i=2,3,4 → 3개 선형식의 over-determined 계 A·p = b
// 정규방정식 (AᵀA)·p = Aᵀ·b 를 2×2 닫힌형으로 푼다.
static bool leastSquares(float* outX, float* outY) {
    const float x1   = ANCHOR_X[0];
    const float y1   = ANCHOR_Y[0];
    const float r1sq = dist[0] * dist[0];

    float Saa = 0, Sab = 0, Sbb = 0, Sac = 0, Sbc = 0;
    for (int i = 1; i < 4; ++i) {
        const float a = 2.0f * (ANCHOR_X[i] - x1);
        const float b = 2.0f * (ANCHOR_Y[i] - y1);
        const float c = r1sq - dist[i] * dist[i]
                      + ANCHOR_X[i] * ANCHOR_X[i] - x1 * x1
                      + ANCHOR_Y[i] * ANCHOR_Y[i] - y1 * y1;
        Saa += a * a;  Sbb += b * b;  Sab += a * b;
        Sac += a * c;  Sbc += b * c;
    }

    const float det = Saa * Sbb - Sab * Sab;
    // 크래머 룰 쓰기 위한 det 값 계산
    if (fabsf(det) < 1e-6f) return false;  
    // det의 절댓값(fabsf)가 10^-6승 거의 0에 가까우면 
    // 호출자에게 false 값 반환.
    // 밑에 실행도 안됨. 
    *outX = (Sbb * Sac - Sab * Sbc) / det;
    *outY = (Saa * Sbc - Sab * Sac) / det;
    return true;
    // 성공
}

static void tryComputePosition() {
    if (!(gotDist[0] && gotDist[1] && gotDist[2] && gotDist[3])) return;
    // 4번째까지 true 되면 이제 통과. 
    float x, y;
    if (leastSquares(&x, &y))
    // det 거의 0에 근사 -> return false했었음. 그래서 if 조건
    // 안에 false되면 이 문장 실행 조차 안함.
    {
        Serial.printf("Position: x=%.2f, y=%.2f (d1=%.2f d2=%.2f d3=%.2f d4=%.2f)\n",
                      x, y, dist[0], dist[1], dist[2], dist[3]);
    }
    gotDist[0] = gotDist[1] = gotDist[2] = gotDist[3] = false;
}

void setup() {
    Serial.begin(115200);
    TagSerial.begin(115200, SERIAL_8N1, TAG_RX, -1);
    delay(2000);
    Serial.println("ESP32 Localization (LS, 4-anchor) Ready");
}

void loop() {
    static char    buf[64];
    static uint8_t idx = 0;

    while (TagSerial.available()) {
        const char c = (char)TagSerial.read();  // 1글자 읽기
        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                buf[idx] = '\0';
                Serial.println(buf);          // 수신 원문 그대로 PC로 흘림
                parseAndStore(buf);
                tryComputePosition();
                idx = 0;
            }
        } else if (idx < sizeof(buf) - 1) {
            buf[idx++] = (uint8_t)c;
        } else {
            idx = 0;  // 라인 길이 초과 — 폐기
        }
    }
}
