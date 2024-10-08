/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 *
 * Copyright (c) 2022 Jan Zwiener (jan@zwiener.org)
 * All rights reserved.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_gyroscope.h"
#include <math.h>

/* Private includes ----------------------------------------------------------*/
#include "engine.h"
#include "e1m1.h"
#include "sdl_scancodes.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart1;
// DMA_HandleTypeDef hdma_usart1_tx;
DMA2D_HandleTypeDef hdma2d;
LTDC_HandleTypeDef hltdc;
SDRAM_HandleTypeDef hsdram1;
RNG_HandleTypeDef hrng;

static int LCD_LAYER_FRONT; // active display layer (front buffer)
static int LCD_LAYER_BACK;
static uint32_t* g_fb[2];
static bool g_gyroReady;

static gamestate_t g_game;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
// static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);

void defaultTask(void);
void doomTask(void);
void render(const float dt_sec, float forward, float left);
void clearFrameBuffer(void);

/* Private user code ---------------------------------------------------------*/

// Generated by https://lvgl.io/tools/imageconverter
// CF_RAW
#include "texture_wood1.h"
#include "texture_stone2.h"
bool load_texture_wood(texture_t* texture)
{
    texture->bytesperpixel = 3;
    texture->width = 128;
    texture->height = 128;
    texture->rowlength = texture->width * texture->bytesperpixel;
    texture->pixels = WOOD1_map;
    return true;
}
bool load_texture_stone(texture_t* texture)
{
    texture->bytesperpixel = 3;
    texture->width = 128;
    texture->height = 128;
    texture->rowlength = texture->width * texture->bytesperpixel;
    texture->pixels = STONE2_map;
    return true;
}

static void game_init(void)
{
    g_game.player_dir.e = 0.0f;
    g_game.player_dir.n = 1.0f;

    g_game.player_pos.n = 2.0f;
    g_game.player_pos.e = 2.0f;

    g_game.level = m_e1m1_mapdata;
    g_game.level_width = 16;
    g_game.level_height = 8;

    texture_t* textures = r_texture_dict();

    load_texture_wood(&textures[1]);
    load_texture_wood(&textures[2]);
    load_texture_stone(&textures[3]);
    load_texture_stone(&textures[4]);
    load_texture_wood(&textures[5]);
    load_texture_stone(&textures[6]);
    load_texture_stone(&textures[7]);

    // texture_t* sprites = r_sprite_dict();
    // load_texture(&sprites[0], "sprites/ball.bmp");
}

int main(void)
{
    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();
    SystemClock_Config();

    /* Gyroscope init */
    if (BSP_GYRO_Init() == GYRO_OK)
    {
        BSP_GYRO_Reset();
        g_gyroReady = true;
    }

    /* Serial output (UART)  */
    // MX_DMA_Init();
    MX_USART1_UART_Init();
    /* Setup hardware random number generator */
    hrng.Instance = RNG;
    HAL_RNG_Init(&hrng);

    /* Display setup */
    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_EXTI);

    BSP_LCD_Init();
    LCD_LAYER_FRONT = 1;
    LCD_LAYER_BACK = 0;
    g_fb[0] = (uint32_t*)LCD_FRAME_BUFFER;
    g_fb[1] = (uint32_t*)(LCD_FRAME_BUFFER + WIDTH * HEIGHT * BPP);
    BSP_LCD_LayerDefaultInit(0, (uint32_t)g_fb[0]);
    BSP_LCD_LayerDefaultInit(1, (uint32_t)g_fb[1]);
    BSP_LCD_SetLayerVisible(0, DISABLE);
    BSP_LCD_SetLayerVisible(1, ENABLE);
    BSP_LCD_SelectLayer(LCD_LAYER_BACK);

    /* ChromART (DMA2D) setup */
    hdma2d.Init.Mode         = DMA2D_M2M; // convert 8bit palette colors to 32bit ARGB888
    hdma2d.Init.ColorMode    = DMA2D_ARGB8888; // destination color format
    hdma2d.Init.OutputOffset = 0;
    hdma2d.Instance = DMA2D;
    hdma2d.LayerCfg[LCD_LAYER_FRONT].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[LCD_LAYER_FRONT].InputAlpha = 0xFF; // N/A only for A8 or A4
    hdma2d.LayerCfg[LCD_LAYER_FRONT].InputColorMode = DMA2D_INPUT_ARGB8888; // source format
    hdma2d.LayerCfg[LCD_LAYER_FRONT].InputOffset = 0;
    HAL_DMA2D_Init(&hdma2d);
    HAL_DMA2D_ConfigLayer(&hdma2d, LCD_LAYER_FRONT);

    /* Enable CPU cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    // access the cycle counter at: DWT->CYCCNT

    /* Run Main task */
    game_init();
    doomTask();

    while (1) {} /* should never end up here */
}

void screen_flip_buffers(void)
{
  // wait for VSYNC
    while (!(LTDC->CDSR & LTDC_CDSR_VSYNCS));
    BSP_LCD_SetLayerVisible(LCD_LAYER_FRONT, DISABLE);
    LCD_LAYER_BACK = LCD_LAYER_FRONT;
    LCD_LAYER_FRONT ^= 1;
    BSP_LCD_SetLayerVisible(LCD_LAYER_FRONT, ENABLE);
    BSP_LCD_SelectLayer(LCD_LAYER_BACK);

}
/* Using the Systick 1000 Hz millisecond timer to sleep */
static void sleep(uint32_t delayMs)
{
    const uint32_t tickstart = HAL_GetTick();
    while((HAL_GetTick() - tickstart) < delayMs)
    {
        __WFE(); // save a bit of power while we are waiting
    }
}

static uint8_t kb[SDL_NUM_SCANCODES];
void doomTask(void)
{
    float dt_sec = 0.0f;
    int frameTimeMs = 0; // current frametime in ms
    uint8_t uartAsciiOutput[128]; // debug ASCII output buffer for UART sending
    const int setpointframeTimeMs = 33;
    float rates[3] = {0,0,0};
    bool gyroMode = false;

    r_render(g_fb[0], &g_game);
    r_render(g_fb[1], &g_game);

    for(uint32_t epoch=0;;epoch++)
    {
        uint32_t tickStart = HAL_GetTick();

        if (g_gyroReady)
        {
            BSP_GYRO_GetXYZ(rates);
            for (int i=0;i<3;i++)
            {
                rates[i]*=(1/(1024.0f));
            }
        }

        kb[SDL_SCANCODE_A] = 1;
        // if (BSP_PB_GetState(BUTTON_KEY) != RESET) { gyroMode = true; }
        if (gyroMode)
        {
            kb[SDL_SCANCODE_W] = (BSP_PB_GetState(BUTTON_KEY) != RESET);
            kb[SDL_SCANCODE_A] = rates[1] >  2.5f;
            kb[SDL_SCANCODE_D] = rates[1] < -2.5f;
        }

        g_update(dt_sec, kb, &g_game);
        r_render(g_fb[LCD_LAYER_BACK], &g_game);

        screen_flip_buffers();

        frameTimeMs = (int)(HAL_GetTick() - tickStart);
        const int timeleftMs = setpointframeTimeMs - frameTimeMs;
        if (timeleftMs > 0)
        {
            sleep(timeleftMs);
        }
        dt_sec = ((int)(HAL_GetTick() - tickStart))/1000.0f;

        // Send the frametime in milliseconds via ASCII over UART to a host PC for debugging/optimization
        if (epoch % 60 == 0 && HAL_UART_GetState(&huart1) == HAL_UART_STATE_READY)
        {
            const int bytesInBuffer =
                    snprintf((char*)uartAsciiOutput, sizeof(uartAsciiOutput), "%i ms\r\n", frameTimeMs);
            HAL_UART_Transmit(&huart1, uartAsciiOutput, bytesInBuffer, 32);
        }
    }
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_OscInitTypeDef RCC_OscInitStruct;

    /* Enable Power Control clock */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* The voltage scaling allows optimizing the power consumption when the device is
     clocked below the maximum system frequency, to update the voltage scaling value
     regarding system frequency refer to product datasheet.  */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Enable HSE Oscillator and activate PLL with HSE as source */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 360;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /* Activate the Over-Drive mode */
    HAL_PWREx_EnableOverDrive();

    /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
     clocks dividers */
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}

#if 0
/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
    /* DMA controller clock enable */
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* DMA interrupt init */
    /* DMA2_Stream7_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
}
#endif

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM6 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        HAL_IncTick();
    }
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
}

void HAL_RNG_MspInit(RNG_HandleTypeDef *hrng)
{
    __RNG_CLK_ENABLE(); /* RNG Peripheral clock enable */
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
