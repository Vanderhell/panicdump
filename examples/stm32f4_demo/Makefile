# Makefile — panicdump STM32F4 demo
#
# Requires: arm-none-eabi-gcc toolchain in PATH
#
# Usage:
#   make                         # build
#   make flash                   # flash via OpenOCD (ST-Link)
#   make flash PROBE=jlink       # flash via J-Link
#   make clean
#   make demo                    # build with deliberate fault trigger enabled

TOOLCHAIN  ?= arm-none-eabi
CC         := $(TOOLCHAIN)-gcc
AS         := $(TOOLCHAIN)-gcc
LD         := $(TOOLCHAIN)-gcc
OBJCOPY    := $(TOOLCHAIN)-objcopy
SIZE       := $(TOOLCHAIN)-size

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

ROOT    := ../..
INC     := $(ROOT)/include
SRC     := $(ROOT)/src
PORTS   := $(ROOT)/ports/cortexm

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

SRCS := \
    main.c \
    stm32f4_uart.c \
    startup_stm32f4.S \
    $(SRC)/panicdump.c \
    $(SRC)/panicdump_port_cortexm.c \
    $(PORTS)/panicdump_fault_entry.S

OBJS := $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(notdir $(SRCS))))

VPATH := . $(SRC) $(PORTS)

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------

CPU_FLAGS := \
    -mcpu=cortex-m4 \
    -mthumb \
    -mfpu=fpv4-sp-d16 \
    -mfloat-abi=hard

CFLAGS := \
    $(CPU_FLAGS) \
    -I$(INC) \
    -I. \
    -std=c11 \
    -Wall -Wextra \
    -ffunction-sections \
    -fdata-sections \
    -Os \
    -g

# Enable deliberate fault trigger for demo/testing
ifeq ($(MAKECMDGOALS),demo)
    CFLAGS += -DPANICDUMP_DEMO_TRIGGER_FAULT
endif

ASFLAGS := $(CPU_FLAGS) -g

LDFLAGS := \
    $(CPU_FLAGS) \
    -T stm32f407vg.ld \
    -Wl,--gc-sections \
    -Wl,-Map=panicdump_demo.map \
    -nostartfiles \
    -nostdlib \
    -lc \
    -lgcc

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

TARGET := panicdump_demo

.PHONY: all demo flash flash-jlink clean size

all: $(TARGET).elf $(TARGET).bin size

demo: $(TARGET).elf $(TARGET).bin size

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

size: $(TARGET).elf
	$(SIZE) $<

# Flash via OpenOCD (ST-Link)
flash: $(TARGET).bin
	openocd \
	    -f interface/stlink.cfg \
	    -f target/stm32f4x.cfg \
	    -c "program $(TARGET).bin verify reset exit 0x08000000"

# Flash via J-Link
flash-jlink: $(TARGET).bin
	JLinkExe -device STM32F407VG -if SWD -speed 4000 \
	    -CommanderScript flash_jlink.jlink

clean:
	rm -f *.o *.elf *.bin *.map
