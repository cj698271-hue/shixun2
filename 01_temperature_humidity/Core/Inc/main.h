/* USER CODE BEGIN Header */
/** @file main.h @brief Common application defines. */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DHT11_DATA_Pin GPIO_PIN_0
#define DHT11_DATA_GPIO_Port GPIOA
#define OLED_CS_Pin GPIO_PIN_10
#define OLED_CS_GPIO_Port GPIOB
#define OLED_DC_Pin GPIO_PIN_11
#define OLED_DC_GPIO_Port GPIOB
#define OLED_RST_Pin GPIO_PIN_12
#define OLED_RST_GPIO_Port GPIOB
#define OLED_SCK_Pin GPIO_PIN_13
#define OLED_SCK_GPIO_Port GPIOB
#define OLED_MOSI_Pin GPIO_PIN_14
#define OLED_MOSI_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
