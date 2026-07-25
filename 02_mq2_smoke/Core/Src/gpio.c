/* USER CODE BEGIN Header */
/** @file gpio.c @brief GPIO configuration. */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, OLED_CS_Pin|OLED_DC_Pin|OLED_RST_Pin|OLED_SCK_Pin
                          |OLED_MOSI_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : OLED_CS_Pin OLED_DC_Pin OLED_RST_Pin OLED_SCK_Pin
                           OLED_MOSI_Pin */
  GPIO_InitStruct.Pin = OLED_CS_Pin|OLED_DC_Pin|OLED_RST_Pin|OLED_SCK_Pin
                          |OLED_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : BEEP_Pin */
  /* 蜂鸣器控制引脚配成推挽输出，上电默认下拉(低电平/关闭)，避免启动瞬间误触发蜂鸣器；
     SetBeeper()在app_environment.c里通过HAL_GPIO_WritePin控制这个引脚的高低电平 */
  GPIO_InitStruct.Pin = BEEP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BEEP_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
/* USER CODE END 2 */
