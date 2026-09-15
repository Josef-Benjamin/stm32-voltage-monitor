/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32 voltage monitor with adjustable undervoltage alarm
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Voltage measurement: A0, divider R1 above A0 and R2 below A0. */
#define ADC_REFERENCE_VOLTAGE     3.3f
#define ADC_FULL_SCALE           4095.0f
#define DIVIDER_R1_OHMS           9650.0f
#define DIVIDER_R2_OHMS           9820.0f
#define DIVIDER_GAIN             \
    ((DIVIDER_R1_OHMS + DIVIDER_R2_OHMS) / DIVIDER_R2_OHMS)

#define VOLTAGE_AVERAGE_SAMPLES   16U
#define ADC_TIMEOUT_MS           100U

/* Potentiometer on A1: threshold 0.50 to 4.50 V, in 0.05 V steps. */
#define POT_FILTER_ALPHA         0.2f
#define ALARM_THRESHOLD_MIN      0.5f
#define ALARM_THRESHOLD_MAX      4.5f
#define ALARM_THRESHOLD_STEP     0.05f
#define ALARM_HYSTERESIS          0.2f

/* This is a loop delay, not a fixed sampling period. */
#define LOOP_DELAY_MS            50U

/* SSD1306 OLED, 128 x 64, I2C address 0x3C. */
#define OLED_ADDRESS             (0x3CU << 1)
#define OLED_WIDTH               128U
#define OLED_HEIGHT              64U
#define OLED_FRAME_BYTES         (OLED_WIDTH * OLED_HEIGHT / 8U)
#define OLED_COMMAND_TIMEOUT_MS  100U
#define OLED_FRAME_TIMEOUT_MS    200U
#define OLED_UPDATE_INTERVAL_MS  500U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* A0 measurement and diagnostics; visible in Live Expressions. */
volatile uint32_t adc_raw = 0;
volatile float adc_voltage = 0.0f;
volatile float supply_voltage = 0.0f;
volatile float supply_voltage_avg = 0.0f;
volatile uint32_t measurement_count = 0;
volatile uint32_t adc_errors = 0;

/* A1 potentiometer measurement and diagnostics. */
volatile uint32_t adc_raw_a1 = 0;
volatile float adc_voltage_a1 = 0.0f;
volatile uint32_t measurement_count_a1 = 0;
volatile uint32_t adc_errors_a1 = 0;

/* Alarm state: 1 means the green user LED is on. */
volatile uint32_t low_voltage_alarm = 0;
volatile float alarm_on_voltage = 1.5f;
volatile float alarm_off_voltage = 1.7f;

/* OLED startup results and runtime diagnostics. */
volatile uint32_t oled_connected = 0;
volatile uint32_t oled_init_ok = 0;
volatile uint32_t oled_errors = 0;
volatile uint32_t oled_last_status = 0;
volatile uint32_t oled_reinit_request = 0;

/* Last I2C error before recovery; retained even after a successful retry. */
volatile uint32_t oled_i2c_error = 0;

/* Internal filter state. */
static uint32_t adc_samples[VOLTAGE_AVERAGE_SAMPLES] = {0};
static uint32_t adc_sum = 0;
static uint32_t adc_index = 0;
static uint32_t adc_sample_count = 0;

static float pot_filtered = 0.0f;
static uint32_t pot_initialized = 0;
static uint32_t oled_last_update = 0;

/* First byte is the SSD1306 data control byte. */
static uint8_t oled_frame[OLED_FRAME_BYTES + 1U];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
/* Application helpers are defined before main(). */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Read one ADC channel and stop the conversion before returning. */
static HAL_StatusTypeDef ReadAdcChannel(uint32_t channel, uint32_t *result)
{
    ADC_ChannelConfTypeDef config = {0};

    config.Channel = channel;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    HAL_StatusTypeDef status = HAL_ADC_ConfigChannel(&hadc1, &config);

    if (status != HAL_OK)
    {
        return status;
    }

    status = HAL_ADC_Start(&hadc1);

    if (status != HAL_OK)
    {
        return status;
    }

    status = HAL_ADC_PollForConversion(&hadc1, ADC_TIMEOUT_MS);

    if (status == HAL_OK)
    {
        *result = HAL_ADC_GetValue(&hadc1);
    }

    HAL_StatusTypeDef stop_status = HAL_ADC_Stop(&hadc1);

    return (status == HAL_OK) ? stop_status : status;
}

/* Return 1 only when a new valid voltage sample is available. */
static uint32_t UpdateVoltageMeasurement(void)
{
    uint32_t sample = 0;

    if (ReadAdcChannel(ADC_CHANNEL_0, &sample) != HAL_OK)
    {
        adc_errors++;
        return 0U;
    }

    adc_raw = sample;
    adc_voltage = (float)sample * ADC_REFERENCE_VOLTAGE / ADC_FULL_SCALE;
    supply_voltage = adc_voltage * DIVIDER_GAIN;
    measurement_count++;

    /* Moving average: replace the oldest sample with the newest one. */
    adc_sum -= adc_samples[adc_index];
    adc_samples[adc_index] = sample;
    adc_sum += sample;

    adc_index = (adc_index + 1U) % VOLTAGE_AVERAGE_SAMPLES;

    if (adc_sample_count < VOLTAGE_AVERAGE_SAMPLES)
    {
        adc_sample_count++;
    }

    float average_raw = (float)adc_sum / (float)adc_sample_count;

    supply_voltage_avg =
        average_raw * ADC_REFERENCE_VOLTAGE / ADC_FULL_SCALE * DIVIDER_GAIN;

    return 1U;
}

/* Read the knob, smooth its reading and calculate both alarm thresholds. */
static void UpdatePotentiometer(void)
{
    uint32_t sample = 0;

    if (ReadAdcChannel(ADC_CHANNEL_1, &sample) != HAL_OK)
    {
        adc_errors_a1++;
        return;
    }

    adc_raw_a1 = sample;
    adc_voltage_a1 = (float)sample * ADC_REFERENCE_VOLTAGE / ADC_FULL_SCALE;
    measurement_count_a1++;

    if (pot_initialized == 0U)
    {
        pot_filtered = (float)sample;
        pot_initialized = 1U;
    }
    else
    {
        pot_filtered += POT_FILTER_ALPHA * ((float)sample - pot_filtered);
    }

    float threshold =
        ALARM_THRESHOLD_MIN +
        (pot_filtered / ADC_FULL_SCALE) *
        (ALARM_THRESHOLD_MAX - ALARM_THRESHOLD_MIN);

    uint32_t step =
        (uint32_t)(threshold / ALARM_THRESHOLD_STEP + 0.5f);

    alarm_on_voltage = (float)step * ALARM_THRESHOLD_STEP;
    alarm_off_voltage = alarm_on_voltage + ALARM_HYSTERESIS;
}

/* Between the two thresholds, preserve the previous alarm state. */
static void UpdateAlarm(void)
{
    if ((low_voltage_alarm == 0U) &&
        (supply_voltage_avg <= alarm_on_voltage))
    {
        low_voltage_alarm = 1U;
    }
    else if ((low_voltage_alarm != 0U) &&
             (supply_voltage_avg >= alarm_off_voltage))
    {
        low_voltage_alarm = 0U;
    }

    HAL_GPIO_WritePin(
        LD2_GPIO_Port,
        LD2_Pin,
        low_voltage_alarm ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}

static HAL_StatusTypeDef OLED_Command(uint8_t command)
{
    uint8_t message[2] = {0x00, command};

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_ADDRESS,
        message,
        sizeof(message),
        OLED_COMMAND_TIMEOUT_MS
    );
}

/* Draw one 5x7 character, enlarged to 10x14 pixels. */
static void OLED_DrawCharacter(char character, uint32_t left, uint32_t top)
{
    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1E}
    };

    uint8_t glyph[5] = {0};

    if ((character >= '0') && (character <= '9'))
    {
        memcpy(glyph, digits[character - '0'], sizeof(glyph));
    }
    else if (character == '.')
    {
        glyph[2] = 0x60;
    }
    else if (character == 'V')
    {
        glyph[0] = 0x1F;
        glyph[1] = 0x20;
        glyph[2] = 0x40;
        glyph[3] = 0x20;
        glyph[4] = 0x1F;
    }
    else if (character == 'T')
    {
        glyph[0] = 0x01;
        glyph[1] = 0x01;
        glyph[2] = 0x7F;
        glyph[3] = 0x01;
        glyph[4] = 0x01;
    }

    for (uint32_t x = 0; x < 5U; x++)
    {
        for (uint32_t y = 0; y < 7U; y++)
        {
            if ((glyph[x] & (1U << y)) == 0U)
            {
                continue;
            }

            for (uint32_t dx = 0; dx < 2U; dx++)
            {
                for (uint32_t dy = 0; dy < 2U; dy++)
                {
                    uint32_t px = left + x * 2U + dx;
                    uint32_t py = top + y * 2U + dy;

                    if ((px < OLED_WIDTH) && (py < OLED_HEIGHT))
                    {
                        oled_frame[1U + (py / 8U) * OLED_WIDTH + px] |=
                            (uint8_t)(1U << (py % 8U));
                    }
                }
            }
        }
    }
}

/* Send a complete screen: V = measured voltage, T = alarm-on threshold. */
static HAL_StatusTypeDef OLED_ShowVoltage(uint32_t millivolts)
{
    char text[32];

    uint32_t threshold_mv =
        (uint32_t)(alarm_on_voltage * 1000.0f + 0.5f);

    if ((millivolts > 9999U) || (threshold_mv > 9999U))
    {
        return HAL_ERROR;
    }

    int length = snprintf(
        text,
        sizeof(text),
        "V %lu.%03lu\nT %lu.%03lu",
        (unsigned long)(millivolts / 1000U),
        (unsigned long)(millivolts % 1000U),
        (unsigned long)(threshold_mv / 1000U),
        (unsigned long)(threshold_mv % 1000U)
    );

    if ((length < 0) || (length >= (int)sizeof(text)))
    {
        return HAL_ERROR;
    }

    memset(oled_frame, 0, sizeof(oled_frame));
    oled_frame[0] = 0x40;

    uint32_t row = 0;
    uint32_t column = 0;

    for (uint32_t n = 0; text[n] != '\0'; n++)
    {
        if (text[n] == '\n')
        {
            row++;
            column = 0;
            continue;
        }

        OLED_DrawCharacter(text[n], 22U + column * 12U, 8U + row * 32U);
        column++;
    }

    const uint8_t address_commands[] = {
        0x21, 0, 127,
        0x22, 0, 7
    };

    for (uint32_t i = 0; i < sizeof(address_commands); i++)
    {
        HAL_StatusTypeDef status = OLED_Command(address_commands[i]);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_ADDRESS,
        oled_frame,
        sizeof(oled_frame),
        OLED_FRAME_TIMEOUT_MS
    );
}

/* Configure the display at startup. */
static HAL_StatusTypeDef OLED_Initialize(void)
{
    const uint8_t commands[] = {
        0xAE,           /* Display off */
        0xD5, 0x80,     /* Display clock */
        0xA8, 0x3F,     /* 64 rows */
        0xD3, 0x00,     /* Vertical offset */
        0x40,           /* Start line */
        0x8D, 0x14,     /* Charge pump */
        0x20, 0x00,     /* Horizontal addressing */
        0xA1,           /* Column direction */
        0xC8,           /* Row direction */
        0xDA, 0x12,     /* COM configuration */
        0x81, 0x3F,     /* Contrast */
        0xD9, 0xF1,     /* Pre-charge */
        0xDB, 0x40,     /* VCOMH */
        0x2E,           /* Scrolling off */
        0xA6,           /* Normal display */
        0xA4            /* Show display memory */
    };

    HAL_StatusTypeDef status =
        HAL_I2C_IsDeviceReady(&hi2c1, OLED_ADDRESS, 3, 100);

    oled_connected = (status == HAL_OK);
    oled_init_ok = 0U;

    if (status != HAL_OK)
    {
        return status;
    }

    for (uint32_t i = 0; i < sizeof(commands); i++)
    {
        status = OLED_Command(commands[i]);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    status = OLED_ShowVoltage(0U);

    if (status == HAL_OK)
    {
        status = OLED_Command(0xAF);
    }

    oled_init_ok = (status == HAL_OK);
    return status;
}

/* Update at most once per interval, with one recovery attempt on failure. */
static void UpdateDisplay(void)
{
    if ((oled_init_ok == 0U) ||
        ((uint32_t)(HAL_GetTick() - oled_last_update) <
         OLED_UPDATE_INTERVAL_MS))
    {
        return;
    }

    uint32_t display_mv =
        (uint32_t)(supply_voltage_avg * 1000.0f + 0.5f);

    HAL_StatusTypeDef status = OLED_ShowVoltage(display_mv);

    if (status != HAL_OK)
    {
        oled_i2c_error = HAL_I2C_GetError(&hi2c1);
        oled_errors++;

        HAL_StatusTypeDef recovery_status = HAL_I2C_DeInit(&hi2c1);

        if (recovery_status == HAL_OK)
        {
            recovery_status = HAL_I2C_Init(&hi2c1);
        }

        if (recovery_status == HAL_OK)
        {
            status = OLED_ShowVoltage(display_mv);
        }
        else
        {
            status = recovery_status;
        }
    }

    oled_last_status = (uint32_t)status;
    oled_last_update = HAL_GetTick();
}

/* USER CODE END 0 */

int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    HAL_Init();

    /* USER CODE BEGIN Init */
    /* USER CODE END Init */

    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */
    HAL_Delay(100);

    HAL_StatusTypeDef oled_status = OLED_Initialize();
    oled_last_status = (uint32_t)oled_status;

    if (oled_status != HAL_OK)
    {
        oled_i2c_error = HAL_I2C_GetError(&hi2c1);
        oled_errors++;
    }
    /* USER CODE END 2 */

    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    	if (oled_reinit_request == 1U)
    	{
    	    oled_reinit_request = 0U;

    	    HAL_StatusTypeDef status = OLED_Initialize();
    	    oled_last_status = (uint32_t)status;

    	    if (status != HAL_OK)
    	    {
    	        oled_i2c_error = HAL_I2C_GetError(&hi2c1);
    	        oled_errors++;
    	    }
    	}
        uint32_t voltage_valid = UpdateVoltageMeasurement();

        UpdatePotentiometer();

        if (voltage_valid)
        {
            UpdateAlarm();
            UpdateDisplay();
        }

        HAL_Delay(LOOP_DELAY_MS);
    }
    /* USER CODE END 3 */
}

/* Hardware configuration ----------------------------------------------------*/

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 4;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_ADC1_Init(void)
{
    /* USER CODE BEGIN ADC1_Init 0 */
    /* USER CODE END ADC1_Init 0 */

    ADC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN ADC1_Init 1 */
    /* USER CODE END ADC1_Init 1 */

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN ADC1_Init 2 */
    /* USER CODE END ADC1_Init 2 */
}

static void MX_I2C1_Init(void)
{
    /* USER CODE BEGIN I2C1_Init 0 */
    /* USER CODE END I2C1_Init 0 */

    /* USER CODE BEGIN I2C1_Init 1 */
    /* USER CODE END I2C1_Init 1 */

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN I2C1_Init 2 */
    /* USER CODE END I2C1_Init 2 */
}

static void MX_USART2_UART_Init(void)
{
    /* USER CODE BEGIN USART2_Init 0 */
    /* USER CODE END USART2_Init 0 */

    /* USER CODE BEGIN USART2_Init 1 */
    /* USER CODE END USART2_Init 1 */

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN USART2_Init 2 */
    /* USER CODE END USART2_Init 2 */
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* USER CODE BEGIN MX_GPIO_Init_1 */
    /* USER CODE END MX_GPIO_Init_1 */

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LD2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

    /* USER CODE BEGIN MX_GPIO_Init_2 */
    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();

    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    (void)file;
    (void)line;
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
