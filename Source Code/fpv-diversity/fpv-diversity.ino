// 3 channel Video Diversity Controller

// selects the best video signal, based on the detection
// of line sync. pulses (coming from using LM1881 chpis).
// The souce with the most 'regular' pulses (in the last few hundered lines)
// is chosen.
//
// Hardware:
//    - Wemos D1 mini clone (ESP8266) running at 160Mhz
//    - Video switch: Max4545
//      (allows to commute 4 sources -- but code is here for 3 sources)
//    - 3 LM1881, to generate the line sync. pulses
//
// https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// ====================================================================

#include <ESP8266WiFi.h>
#include "user_interface.h"

//N is the number of video lines that are averaged over to decide which video source has the highest quality
#define N 500

//Un-comment the line below in order to get some debug prints on the serial output
//#define SERIAL_DEBUG

//Pin connected to the LM1881 sync outputs (INPUTS for mcu)
#define PIN_SyncA D2  // BPOUT1
#define PIN_SyncB D7  // D3 // BPOUT2
#define PIN_SyncC D4 // D4 // BPOUT3

//Pin connected to the MAX4545 (switch) pin that selects the desired source (OUTPUT for MCU)
#define PIN_selectA D1    //IN1
#define PIN_selectB D5    //IN2
#define PIN_selectC D6   //IN3

//#define PIN_ESP_LED D4    //BUILTIN-LED = wemos d1 mini clone

// keyword "Volatile" means that this variable may be changed by a routine called by some interrupt.
volatile unsigned long lastA; // Time of the last sync signal
//volatile long periodA;// Period of the last video line, in microseconds

volatile unsigned long lastB; // Time of the last sync signal
//volatile long periodB;// Period of the last video line, in microseconds

volatile unsigned long lastC; // Time of the last sync signal
//volatile long periodC;// Period of the last video line, in microseconds

unsigned long now;

#define t0 63
//const long t0 = 63;
const long unsigned mus = t0 * N; //Period to update the selected source, in micro sec.
const long unsigned mis = mus / 1000; //same as above, in milli sec.

#define sourceA 0
#define sourceB 1
#define sourceC 2
int selected;

unsigned long lastMillis;

//-----------------------------------------------------------------
//-----------------------------------------------------------------
class ExpAverage {
  private:
    volatile float x;
    const float a, b;
  public:
    ExpAverage(int n): a(1.0f / n), b(1.0f - a) { //exponential average over the ~n previous values
      x = 0.0f;
    }
    inline float getAverage() {
      return x;
    }
    void addValue(unsigned long v) {
      x = a * v + b * x;
    }
};

//-----------------------------------------------------------------
//-----------------------------------------------------------------
ExpAverage A(N), B(N), C(N);//Average video line duration for the different sources
#ifdef SERIAL_DEBUG
ExpAverage AA(N), BB(N), CC(N);//Average video line duration for the different sources
#endif
//-----------------------------------------------------------------
//-----------------------------------------------------------------

const int CLEAR = (1 << PIN_selectA) + (1 << PIN_selectB) + (1 << PIN_selectC);
int Bin[3] = {1 << PIN_selectA , 1 << PIN_selectB, 1 << PIN_selectC};
inline void SwitchTo(int s) {
  if (selected != s) {
    selected = s;
    GPOC = CLEAR;//Clear PIN_selectA, PIN_selectB and PIN_selectC simultaneously
    GPOS = Bin[s];
  }
}

//-----------------------------------------------------------------
//-----------------------------------------------------------------
class PeriodicTask {
  private:
    os_timer_t myTimer;
    int t;
  public:
    //-----------------------------
    static void DoTask(void *pArg) {//static needed here, because below we want to pass a pointer to this member function
      const unsigned long  now = micros();
      if (now - lastA > mus) {
        //if no pulse was received since last update, put the value 'mus' into the average
        //... so, if no pulse is received at all (disconnected cable, etc.à, the average line duration will be 'mus=N*64' (very large compared to 64) so this
        // source will not be selected.
        A.addValue(mus); lastA = now;
      }
      if (now - lastB > mus) B.addValue(mus), lastB = now;
      if (now - lastC > mus) C.addValue(mus), lastC = now;

      //Get the average deviation (from ~63 micro seconds) of the line durations
      const float avA = A.getAverage();
      const float avB = B.getAverage();
      const float avC = C.getAverage();

      if (avB < avA) {
        //The best is B or C
        if (avC < avB)
        {
          SwitchTo(sourceC);
          //Serial.println(avA); Serial.println(avB); Serial.println(avC);
        }
        else {
          SwitchTo(sourceB);
        }

      } else {//The best is A or C
        if (avA < avC)
          SwitchTo(sourceA); else {
            SwitchTo(sourceC);
            //Serial.println(avA); Serial.println(avB); Serial.println(avC);
          }
      }
    }
    //-----------------------------
    PeriodicTask () {
      os_timer_setfn(&myTimer, DoTask, NULL);
    }
    void Start(int t, bool repeat = true) {
      os_timer_arm(&myTimer, t, repeat);
    }
};
PeriodicTask T;

//--------Count---------------------------------------------------------
//-----------------------------------------------------------------
IRAM_ATTR void CountA() {
  const unsigned long  now = micros();
  const long period = now - lastA;
  lastA = now;
  A.addValue(abs(period - t0));
#ifdef SERIAL_DEBUG
  AA.addValue(period);
#endif
}
IRAM_ATTR void CountB() {
  const unsigned long now = micros();
  const long period = now - lastB;
  lastB = now;
  B.addValue(abs(period - t0));
#ifdef SERIAL_DEBUG
  BB.addValue(period);
#endif
}
IRAM_ATTR void CountC() {
  const unsigned long now = micros();
  const long period = now - lastC;
  lastC = now;
  C.addValue(abs(period - t0));
#ifdef SERIAL_DEBUG
  CC.addValue(period);
#endif
}
//-----------------------------------------------------------------
//-----------------------------------------------------------------

void setup() {
  //ESP.wdtDisable(); // To avoid some resets due to the software Watch Dog Timer

  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
#ifdef SERIAL_DEBUG
  Serial.begin(115200);
  Serial.println("Hello World.");
#endif
  lastA = 0; // Time of the last signal on sensor A
  lastB = 0; // Time of the last signal on sensor B
  lastC = 0; // Time of the last signal on sensor C

  pinMode(PIN_SyncA, INPUT);
  pinMode(PIN_SyncB, INPUT);
  pinMode(PIN_SyncC, INPUT);

  pinMode(PIN_selectA, OUTPUT);
  pinMode(PIN_selectB, OUTPUT);
  pinMode(PIN_selectC, OUTPUT);
//  pinMode(PIN_ESP_LED, OUTPUT);

  selected = sourceB;
  //Some blinking to start
  for (int i = 0; i < 3; i++) {
    SwitchTo(sourceA); delay(50);
    SwitchTo(sourceB); delay(50);
    SwitchTo(sourceC); delay(50);
  }
  //Declare 3 interrupts, to measure the duration between horizontal sync. pulses
  //on the 3 sources
  attachInterrupt(PIN_SyncA, CountA, FALLING);
  attachInterrupt(PIN_SyncB, CountB, FALLING);
  attachInterrupt(PIN_SyncC, CountC, FALLING);

  T.Start(mis, true); //Start the process that updates the selected source every 'mis' milliseconds (timer-based)
}
//-----------------------------------------------------------------
//-----------------------------------------------------------------

int z = 1;
void loop() {
  if (millis() > lastMillis + 500) {
    //ESP.wdtFeed();
    // Some blinking of the bluit-in led
//   if (z == 1) digitalWrite(PIN_ESP_LED, HIGH); else digitalWrite(PIN_ESP_LED, LOW); z = -z;
    lastMillis = millis();
#ifdef SERIAL_DEBUG
    //Prints some debug information to the serial output, every 1000 milli sec.
    Serial.print("Time: "); Serial.println(millis());
    if (selected == sourceA)     Serial.print("=>");
    Serial.print("RX1:");
    const float avA = A.getAverage();
    Serial.print(avA);
    const float avAA = AA.getAverage();
    Serial.print(" - "); Serial.println(avAA);

    if (selected == sourceB)     Serial.print("=>");
    Serial.print("RX2:");
    const float avB = B.getAverage();
    Serial.print(avB);
    const float avBB = BB.getAverage();
    Serial.print(" - "); Serial.println(avBB);

    if (selected == sourceC)     Serial.print("=>");
    Serial.print("RX3:");
    const float avC = C.getAverage();
    Serial.print(avC);
    const float avCC = CC.getAverage();
    Serial.print(" - "); Serial.println(avCC);


    Serial.println("");
#endif
  }
  yield();
}
