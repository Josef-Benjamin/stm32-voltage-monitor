# STM32 Voltage Monitor

A breadboard DC voltage monitor built with a **NUCLEO-F411RE**, an SSD1306 OLED and a B10K potentiometer. The device displays the measured input voltage and a user-adjustable undervoltage threshold.

[Project page](https://josef-benjamin.lovable.app/projects/stm32-voltage-monitor) · [Application source](Core/Src/main.c)

![STM32 voltage monitor prototype](https://raw.githubusercontent.com/Josef-Benjamin/hebrew-speaker-hub/main/public/media/stm32/overview.jpg)

[Watch the prototype demonstration](https://josef-benjamin.lovable.app/projects/stm32-voltage-monitor#demo)

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

## Firmware configuration

These values come from the supplied [main.c](Core/Src/main.c):

| Setting | Implementation |
|---|---|
| System clock | 84 MHz, PLL sourced from the internal HSI oscillator |
| ADC1 | 12-bit, software-triggered single conversions, sequential channel selection |
| ADC sampling time | 480 ADC cycles per channel |
| Voltage filter | 16-sample moving average; startup uses only collected samples |
| Potentiometer filter | Exponential smoothing, alpha = 0.2; initialized from the first valid reading |
| Main loop | 50 ms delay plus ADC, display and processing time |
| OLED refresh | At most once per 500 ms interval, when a valid voltage sample is available |
| Display driver | Local SSD1306 framebuffer renderer, 128 × 64 pixels, enlarged numeric glyphs |
| I2C1 | 100 kHz, 7-bit address 0x3C (shifted for the HAL API) |
| USART2 | Initialized at 115200 baud, 8N1; no UART transmission in this application |

### Error handling and debugging

- A failed A0 conversion increments `adc_errors`, leaves the last measurement intact and skips alarm/display updates for that loop.
- A failed A1 conversion increments `adc_errors_a1` and retains the previous threshold.
- A display-transfer failure records the I2C error, reinitializes the I2C peripheral and attempts one additional frame transfer.
- `oled_reinit_request = 1` requests a full OLED initialization from the main loop. This can be set through the debugger.
- If initial OLED initialization fails, regular display updates stay disabled until initialization succeeds through a manual request. This is not automatic full-display recovery.

Useful STM32CubeIDE Live Expressions include `adc_raw`, `supply_voltage_avg`, `adc_raw_a1`, `alarm_on_voltage`, `alarm_off_voltage`, `low_voltage_alarm`, `adc_errors`, `oled_init_ok`, `oled_errors` and `oled_i2c_error`.

## Source and reproduction status

The supplied application source is now available at [Core/Src/main.c](Core/Src/main.c). It is preserved as supplied. Photos are linked from the portfolio repository. The original demonstration recording is available on the project page (58.97 seconds, 910 × 512), with no additional compression.

This is **not yet a complete, standalone STM32CubeIDE project**: the repository does not contain the original `.ioc`, `main.h`, HAL/MSP support files, startup code or linker script.

To integrate the application into a NUCLEO-F411RE project:

1. Create or open the matching STM32CubeIDE/CubeMX project and generate the required STM32F4 HAL and startup files.
2. Configure PA0 and PA1 as ADC analog inputs, I2C1 on PB8/PB9, USART2, and the board's LD2/B1 definitions. Check the generated MSP initialization: PA0 and the I2C/UART pin setup are not provided by this file alone.
3. Use this file as `Core/Src/main.c` and reconcile generated configuration before building. Some configuration lives outside USER CODE blocks, so regeneration needs review.
4. Match the divider resistor constants and ADC reference to the actual hardware, then build and flash through ST-LINK.
5. Compare readings against a multimeter and verify alarm transitions in both directions. Check the OLED diagnostics during an extended run.

The source has been reviewed against this documentation; a fresh build and hardware regression test have not been performed from this repository.

Development used STM32CubeIDE, STM32CubeMX and STM32 HAL. The physical circuit was assembled and tested by Josef Benjamin, with AI assistance for explanations and firmware development.
