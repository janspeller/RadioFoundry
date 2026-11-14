# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# AT32F423: arm-none-eabi GCC, Cortex-M4F hard float, CMSIS core and FreeRTOS's CM4F port. No vendor library: the
# registers come from the reference manual (at32f423.h)
ARM_GCC_DIR ?= $(firstword $(wildcard $(HOME)/opt/arm-gnu-toolchain-*-arm-none-eabi/bin/) /usr/bin/)
CROSS ?= $(ARM_GCC_DIR)arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size

CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
# Bytes a task's stack holds beyond its call chain (make stack-check), as on the STM32F4: the exception frame with the
# FPU's lazy space (27 words) and the port's context switch (25 words)
STACK_OVERHEAD := 208

CMSIS_CORE := third_party/cmsis-core
RTOS := third_party/FreeRTOS-Kernel

$(if $(APP_BASE),,$(error boards/$(BOARD)/board.mk: APP_BASE, the image address, is missing))
LDSCRIPT ?= boards/$(BOARD)/memory.ld

PLATFORM_DIRS := platform/at32f423
PLATFORM_INC := -I$(CMSIS_CORE)/Include -I$(RTOS)/include -I$(RTOS)/portable/GCC/ARM_CM4F

PLATFORM_3P_SRC := $(RTOS)/tasks.c $(RTOS)/queue.c $(RTOS)/list.c $(RTOS)/timers.c $(RTOS)/event_groups.c \
	$(RTOS)/portable/GCC/ARM_CM4F/port.c $(RTOS)/portable/MemMang/heap_4.c

# Our own start-up (startup.c) and syscalls; newlib-nano for memcpy / snprintf and friends
LDSPECS := -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-rwx-segments
