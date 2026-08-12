// ===========================================================================
//  find_3v3.ino — Identify 3.3V pin on LilyGo T5 4.7" S3 40-pin header
// ===========================================================================
//  Upload this sketch, open Serial Monitor (115200 baud), and follow the
//  on-screen instructions. Use a multimeter to probe the header pins.
// ===========================================================================

#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM!"
#endif

#include <Arduino.h>

// Free GPIOs on the LilyGo T5 4.7" S3 (per utilities.h)
#define GPIO_CS    39
#define GPIO_MISO  45
#define GPIO_SCLK  48
#define GPIO_MOSI  10

// Also test some other potentially free pins
#define TEST_PINS  {GPIO_CS, GPIO_MISO, GPIO_SCLK, GPIO_MOSI}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n========================================");
    Serial.println("  LilyGo T5 4.7\" S3 — 3.3V Pin Finder");
    Serial.println("========================================\n");
    
    Serial.println("STEP 1: Find 3.3V power pin");
    Serial.println("----------------------------------------");
    Serial.println("Set your multimeter to DC Voltage (20V range).");
    Serial.println("Connect BLACK probe to ANY GND pin on the header.");
    Serial.println("Connect RED probe to each pin on the header, one by one.");
    Serial.println("Look for a pin that reads ~3.3V (should be constant).");
    Serial.println();
    Serial.println("Common 3.3V locations on RPi-compatible headers:");
    Serial.println("  - Pin 1  (top-left corner, outer row)");
    Serial.println("  - Pin 17 (middle of outer row)");
    Serial.println();
    Serial.println("If you find 3.3V, note the position (Row A/B, Col 1-20).");
    Serial.println();
    
    delay(1000);
    
    Serial.println("STEP 2: Test GPIO output (alternative power source)");
    Serial.println("----------------------------------------");
    Serial.println("If you can't find 3.3V, we can power the INMP441");
    Serial.println("from a GPIO pin (it only draws ~600μA).");
    Serial.println();
    Serial.println("I will now set each free GPIO HIGH (3.3V output).");
    Serial.println("Measure each pin with your multimeter:");
    Serial.println();
    
    int testPins[] = TEST_PINS;
    int numPins = sizeof(testPins) / sizeof(testPins[0]);
    
    for (int i = 0; i < numPins; i++) {
        int pin = testPins[i];
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        Serial.printf("  GPIO %d → HIGH (measure now)\n", pin);
        delay(2000);  // Wait 2 seconds for you to measure
    }
    
    Serial.println();
    Serial.println("All GPIOs are now HIGH.");
    Serial.println("Any pin reading ~3.3V can power the INMP441 VDD.");
    Serial.println();
    Serial.println("STEP 3: Choose your power source");
    Serial.println("----------------------------------------");
    Serial.println("Option A: Use the 3.3V pin you found in Step 1");
    Serial.println("Option B: Use one of the GPIO pins from Step 2");
    Serial.println("          (Recommend GPIO 39 or 45 — avoid GPIO 10");
    Serial.println("           as it's analog-only and may be unstable)");
    Serial.println();
    Serial.println("Once you've identified the pin, connect INMP441 VDD to it.");
    Serial.println();
    Serial.println("Press RESET button to run this test again.");
}

void loop() {
    // Keep GPIOs HIGH for continuous testing
    delay(1000);
}
