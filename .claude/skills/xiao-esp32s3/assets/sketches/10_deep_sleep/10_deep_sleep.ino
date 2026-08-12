// 10. Deep sleep — wiki "Sleep Modes" example: timer wake-up every 10 s.
// Boot count survives deep sleep in RTC memory.
#define uS_TO_S 1000000ULL
#define SLEEP_SECONDS 10

RTC_DATA_ATTR int bootCount = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);
  bootCount++;
  Serial.printf("Boot count: %d\n", bootCount);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Woke up from deep sleep (timer)");
  } else {
    Serial.println("Fresh boot (not a deep sleep wake)");
  }

  Serial.printf("Sleeping for %d seconds...\n", SLEEP_SECONDS);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_TO_S);
  esp_deep_sleep_start();
}

void loop() {}
