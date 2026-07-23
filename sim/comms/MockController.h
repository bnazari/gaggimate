// Simulated controller board: a small thermal + hydraulic model that reacts to
// the boiler/pump/relay commands the display sends and emits sensor telemetry.
#pragma once

#include "GaggiMateComm.h"
#include <cstdint>
#include <functional>

class MockController {
  public:
    using SensorFn = std::function<void(float temp, float pressure, float puckFlow, float pumpFlow, float puckResistance,
                                        float pumpPower, float heaterPower)>;
    using VolumetricFn = std::function<void(float volume)>;
    using TofFn = std::function<void(uint32_t distance)>;

    void begin();
    void update();

    void setBoiler(const BoilerCommand &c);
    void setPump(const PumpCommand &c);
    void setRelay(const RelayCommand &c);
    void tareScale() { weight = 0.0f; }

    // --- Simulator machine panel hooks -------------------------------------
    // The Silvia's steam/hot-water wand valve is manual: the firmware never
    // sees it, it only changes where water/steam physically goes.
    void setWandValve(bool open) { wandValveOpen = open; }

    // Mode LEDs (channels 8=brew, 9=steam, 10=water) as sent by the display
    // firmware over the LedControl path: 0=off, 128=blink, 255=solid. Channels
    // 0-7 belong to the Sunrise/Alba PCA9634 and are ignored here.
    void setLed(uint8_t channel, uint8_t brightness) {
        if (channel >= 8 && channel < 11)
            ledState[channel - 8] = brightness;
    }

    struct SimMachineState {
        float temperature;  // boiler °C
        float targetTemp;   // setpoint (0 = boiler off)
        float pressure;     // bar
        float flow;         // ml/s
        bool pumpActive;    // display is driving the pump
        bool brewValveOpen; // 3-way solenoid routing to group head
        bool wandValveOpen; // manual wand valve
        uint8_t led[3];     // mode LEDs: 0=off, 128=blink, 255=solid
    };
    SimMachineState getSimState() const {
        const bool pumpActive = pumpPower > 1.0f || targetPressure > 0.1f || targetFlow > 0.1f;
        return {temperature, targetTemp,  pressure,      flow,
                pumpActive,  brewValveOpen, wandValveOpen, {ledState[0], ledState[1], ledState[2]}};
    }

    SensorFn onSensor;
    VolumetricFn onVolumetric;
    TofFn onTof;

  private:
    bool active = false;
    uint32_t lastUpdateMs = 0;
    uint32_t lastSensorMs = 0;
    uint32_t lastTofMs = 0;

    float ambient = 21.0f;
    float temperature = 21.0f;
    float targetTemp = 0.0f; // boiler setpoint (0 = off)

    PumpControlMode pumpMode = PumpControlMode::Power;
    float pumpPower = 0.0f;      // 0..100
    float targetPressure = 0.0f; // bar
    float targetFlow = 0.0f;     // ml/s
    bool brewValveOpen = false;
    bool wandValveOpen = false;
    uint8_t ledState[3] = {0, 0, 0};

    float pressure = 0.0f; // bar
    float flow = 0.0f;     // ml/s
    float weight = 0.0f;   // g accumulated on the scale
};
