# STM32 Voltage Monitor

A breadboard DC voltage monitor built with a **NUCLEO-F411RE**, an SSD1306 OLED and a B10K potentiometer. The device displays the measured input voltage and a user-adjustable undervoltage threshold.

[Josef Benjamin's portfolio](https://josef-benjamin.lovable.app/)

## Features

- Tested operating range: **0–5 V DC**.
- 12-bit ADC measurement through a resistor divider.
- RC filtering and a **16-sample moving average**.
- Potentiometer-selected alarm threshold: **0.50–4.50 V**, in **0.05 V steps**.
- **0.20 V hysteresis** to prevent repeated switching near the threshold.
- OLED readout: **V** = measured input voltage; **T** = alarm-on threshold.
- Onboard green LED indicates undervoltage.
- ADC and I2C diagnostic counters, I2C reinitialization/retry and a manual OLED reinitialization request.

## Hardware and wiring

| Component | Connection |
|---|---|
| R1, measured 9.65 kΩ | Supply positive → divider midpoint |
| R2, measured 9.82 kΩ | Divider midpoint → common GND |
| A0 / PA0 | Divider midpoint, ADC1 channel 0 |
| 2.2 µF electrolytic capacitor, measured 2.43 µF | Positive at divider midpoint; negative at GND |
| B10K potentiometer | Outer terminals to 3V3 and GND; wiper to A1 / PA1 |
| OLED VCC / GND | Board 3V3 / common GND |
| OLED SCL | D15 / PB8 |
| OLED SDA | D14 / PB9 |
| LD2 / PA5 | Onboard green alarm indicator |

The supply negative and board ground are connected together. The OLED uses address **0x3C** and a **100 kHz I2C** bus.

**Operating limits:** keep the external input within the tested 0–5 V range. The divider is not an overvoltage protection circuit. The LED indicates a condition; it does not disconnect the load or supply. Power down before changing wiring.

## Measurement and filtering

The firmware uses a nominal ADC reference of 3.3 V:

```text
V_ADC = ADC_raw × 3.3 / 4095
V_input = V_ADC × (9650 + 9820) / 9820
```

The capacitor sees approximately R1 || R2 = 4.87 kΩ. With the measured capacitance, the calculated RC time constant is approximately **11.8 ms**.

The software average contains up to 16 valid samples. The potentiometer uses exponential smoothing with alpha = 0.2 before threshold quantization.

Sampling uses sequential ADC polling, with a 50 ms loop delay. OLED transfers and other processing add time, so this is **not a fixed 20 Hz sampling system**. Timer-triggered ADC and DMA are not implemented.

## Alarm behavior

The LED turns on when the averaged input is at or below T, and turns off when the input reaches T + 0.20 V. Between those thresholds, the previous state is retained.

The following sequence was checked with T = 1.50 V:

| Input sequence | Observed LED state |
|---|---|
| 1.40 V | On |
| Rise to 1.60 V | Remains on |
| Rise to 1.80 V | Off |
| Fall to 1.60 V | Remains off |

## Measurement comparison

A photographed comparison showed:

| Instrument | Reading |
|---|---|
| UNI-T digital multimeter | 1.996 V |
| STM32 OLED | 2.002 V |
| Difference | +0.006 V, approximately +0.30% relative to the multimeter |

This is a comparison at one operating point, **not an accuracy specification for the full range**. Reference-voltage assumptions, resistor measurements and instrument uncertainty affect the result.

## Firmware organization

The cleaned application separates:

- `ReadAdcChannel`: configure, start, read and stop a conversion.
- `UpdateVoltageMeasurement`: convert A0 and maintain the moving average.
- `UpdatePotentiometer`: read A1 and update the thresholds.
- `UpdateAlarm`: apply hysteresis and update LD2.
- `OLED_Initialize`, `OLED_ShowVoltage`, `UpdateDisplay`: render and transfer the display, with error handling.

The former two-channel capacitor comparison, capture experiments and obsolete WithCap/NoCap serial stream were removed from the final application. UART configuration remains, but that experimental stream is no longer transmitted.

## Known limitation: intermittent OLED blanking

The screen has intermittently gone blank while measurements and the alarm continued operating. Recorded faults included I2C timeout and acknowledgement failure. Reinitializing the STM32 I2C peripheral and retrying does not always restore the picture.

Setting `oled_reinit_request` to 1 invokes OLED initialization and restored the display in the observed test without resetting the MCU. The cause has **not** been established; this prototype should not be described as fully reliable or suitable for protection duties.

## Source and reproduction status

This repository currently documents the tested prototype. The final complete STM32CubeIDE project and media have not yet been uploaded. It is not currently a standalone buildable firmware distribution.

Development used STM32CubeIDE, STM32CubeMX and STM32 HAL. The physical circuit was assembled and tested by Josef Benjamin, with AI assistance for explanations and firmware development.
