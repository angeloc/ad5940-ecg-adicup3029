# ECG firmware for EVAL-ADICUP3029 + EVAL-AD5940BIOZ.
#
#   make                 download what is missing and build firmware/EcgStream.c
#   make APP=LeadId      build another application (EcgChainTest, LeadId, EcgPathDiag)
#   make deps            only download: toolchain, device pack, CMSIS headers, AD5940 library
#   make flash           copy the hex to the DAPLink drive (then press URST)
#   make view            live viewer (PORT=COM5 or /dev/ttyACM1, default: search the board)
#   make clean
#
# Works with GNU make on Linux and on Windows (cmd.exe, with the curl and tar
# shipped with Windows 10 or later; install.bat installs make and Python).
# Use CROSS=arm-none-eabi- to build with a toolchain already in the PATH.

APP ?= EcgStream
BUILD ?= build
DEPS ?= .deps
OUT = $(BUILD)/$(APP)

DFP ?= $(DEPS)/ADuCM302x_DFP
CMSIS ?= $(DEPS)/cmsis
LIB ?= $(DEPS)/ad5940lib
TOOLCHAIN ?= $(DEPS)/toolchain
CROSS ?= $(TOOLCHAIN)/bin/arm-none-eabi-

DFP_URL = http://download.analog.com/tools/EZBoards/CM302x/Releases/AnalogDevices.ADuCM302x_DFP.3.2.0.pack
CMSIS_URL = https://raw.githubusercontent.com/ARM-software/CMSIS_5/5.4.0/CMSIS/Core/Include
CMSIS_HDRS = core_cm3.h cmsis_version.h cmsis_compiler.h cmsis_gcc.h mpu_armv7.h
LIB_URL = https://raw.githubusercontent.com/analogdevicesinc/ad5940lib/d4c2ba01f0c1f8cb47264905407a4a3f7bf1cd47
TC_URL = https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXE := .exe
TC_FILE := arm-gnu-toolchain-13.2.rel1-mingw-w64-i686-arm-none-eabi.zip
# native path, needed by cmd.exe builtins and to start a program
P = $(subst /,\,$1)
MKDIR = if not exist "$(call P,$1)" mkdir "$(call P,$1)"
RMDIR = if exist "$(call P,$1)" rmdir /s /q "$(call P,$1)"
UNZIP = tar -xf "$1" -C "$2"
COPY = copy /y "$(call P,$1)" "$(call P,$2)"
DAPLINK ?=
PYTHON ?= py -3
else
EXE :=
TC_FILE := arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz
P = $1
MKDIR = mkdir -p "$1"
RMDIR = rm -rf "$1"
UNZIP = unzip -q -o "$1" -d "$2"
COPY = cp "$1" "$2"
DAPLINK ?= /run/media/$(USER)/DAPLINK
PYTHON ?= python3
endif

SRCS = firmware/$(APP).c firmware/board_adicup3029.c $(LIB)/ad5940.c \
       $(DFP)/Source/GCC/startup_ADuCM3029.c $(DFP)/Source/system_ADuCM3029.c

CFLAGS = -mcpu=cortex-m3 -mthumb -Os -g -ffunction-sections -fdata-sections \
         -DCHIPSEL_594X -D__ADUCM3029__ -D__START=main -D__STARTUP_CLEAR_BSS \
         -I$(LIB) -I$(DFP)/Include -I$(DFP)/Include/config -I$(CMSIS)
LDFLAGS = -T$(DFP)/Source/GCC/ADuCM3029.ld -nostartfiles --specs=nano.specs \
          --specs=nosys.specs -u _printf_float -Wl,--gc-sections \
          -Wl,-Map=$(OUT)/$(APP).map

OBJS = $(addprefix $(OUT)/,$(notdir $(SRCS:.c=.o)))
VPATH = $(sort $(dir $(SRCS)))
GCC = $(call P,$(CROSS))gcc
OBJCOPY = $(call P,$(CROSS))objcopy
SIZE = $(call P,$(CROSS))size

# Downloaded components, each tracked by one of its files
DFP_STAMP = $(DFP)/Source/GCC/startup_ADuCM3029.c
CMSIS_STAMP = $(CMSIS)/core_cm3.h
LIB_STAMP = $(LIB)/ad5940.c
ifeq ($(CROSS),$(TOOLCHAIN)/bin/arm-none-eabi-)
TC_STAMP = $(TOOLCHAIN)/bin/arm-none-eabi-gcc$(EXE)
else
TC_STAMP =
endif
DEP_STAMPS = $(TC_STAMP) $(DFP_STAMP) $(CMSIS_STAMP) $(LIB_STAMP)

# The sources are searched after the downloads, in a second make run
all: $(DEP_STAMPS)
	$(MAKE) --no-print-directory $(OUT)/$(APP).hex

deps: $(DEP_STAMPS)

$(DEPS) $(OUT) $(CMSIS) $(LIB) $(TOOLCHAIN) $(DFP):
	$(call MKDIR,$@)

$(TOOLCHAIN)/bin/arm-none-eabi-gcc$(EXE): | $(DEPS) $(TOOLCHAIN)
	curl -L --fail -o "$(DEPS)/$(TC_FILE)" "$(TC_URL)/$(TC_FILE)"
	tar -xf "$(DEPS)/$(TC_FILE)" -C "$(TOOLCHAIN)" --strip-components=1

$(DFP_STAMP): | $(DEPS) $(DFP)
	curl -L --fail -o "$(DEPS)/ADuCM302x_DFP.zip" "$(DFP_URL)"
	$(call UNZIP,$(DEPS)/ADuCM302x_DFP.zip,$(DFP))

$(CMSIS_STAMP): | $(CMSIS)
	curl -L --fail $(foreach h,$(CMSIS_HDRS),-o "$(CMSIS)/$(h)" "$(CMSIS_URL)/$(h)")

$(LIB_STAMP): | $(LIB)
	curl -L --fail -o "$(LIB)/ad5940.h" "$(LIB_URL)/ad5940.h" -o "$(LIB)/ad5940.c" "$(LIB_URL)/ad5940.c"

$(OBJS): | $(OUT)

$(OUT)/%.o: %.c
	$(GCC) $(CFLAGS) -c $< -o $@

$(OUT)/$(APP).elf: $(OBJS)
	$(GCC) $(CFLAGS) $(OBJS) $(LDFLAGS) -lm -o $@
	$(SIZE) $@

$(OUT)/$(APP).hex: $(OUT)/$(APP).elf
	$(OBJCOPY) -O ihex $< $@

flash: all
	$(call COPY,$(OUT)/$(APP).hex,$(DAPLINK)/)

# Live viewer: Python 3 with Tk; pyserial is installed if missing
view:
	$(PYTHON) -c "import serial" || $(PYTHON) -m pip install --user -r requirements.txt
	$(PYTHON) ecg_view.py $(PORT)

clean:
	$(call RMDIR,$(BUILD))

.PHONY: all deps flash view clean
