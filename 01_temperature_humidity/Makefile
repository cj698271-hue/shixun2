TOOLCHAIN := D:/Arm GNU Toolchain arm-none-eabi/12.2 mpacbti-rel1/bin
CC := "$(TOOLCHAIN)/arm-none-eabi-gcc.exe"
OBJCOPY := "$(TOOLCHAIN)/arm-none-eabi-objcopy.exe"

BUILD_DIR := build
TARGET := $(BUILD_DIR)/temperature_humidity_monitor
STARTUP := Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
C_SOURCES := $(filter-out Core/Src/adc.c Core/Src/mq2.c,$(wildcard Core/Src/*.c)) \
    $(wildcard Core/SYSTEM/OLED/*.c) \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c

OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o)) $(BUILD_DIR)/$(STARTUP:.s=.o)
INCLUDES := -ICore/Inc -ICore/SYSTEM/OLED -IDrivers/STM32F1xx_HAL_Driver/Inc \
    -IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
    -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include
COMMON_FLAGS := -mcpu=cortex-m3 -mthumb -DUSE_HAL_DRIVER -DSTM32F103xB \
    -ffunction-sections -fdata-sections -Wall -Wextra -g3
CFLAGS := $(COMMON_FLAGS) -std=gnu11 -Os $(INCLUDES)
ASFLAGS := $(COMMON_FLAGS) -x assembler-with-cpp $(INCLUDES)
LDFLAGS := -mcpu=cortex-m3 -mthumb -TSTM32F103C8TX_FLASH.ld \
    -Wl,-Map=$(TARGET).map,--gc-sections -specs=nano.specs -specs=nosys.specs \
    -Wl,--start-group -lc -lm -Wl,--end-group -u _printf_float

.PHONY: all clean

all: $(TARGET).hex

$(TARGET).elf: $(OBJECTS) STM32F103C8TX_FLASH.ld
	@mkdir -p $(@D)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -rf build
