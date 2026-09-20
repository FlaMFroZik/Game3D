# Удобная обёртка над CMake. Сам CMake остаётся кроссплатформенным
# и одинаково работает с Visual Studio, MinGW и GCC/Clang.
#
#   make           — сконфигурировать и собрать Release
#   make run       — собрать и запустить (TEXTURE=... MAP=...)
#   make asan      — сборка с AddressSanitizer/UBSan (GCC/Clang)
#   make clean     — удалить каталог сборки

BUILD_DIR ?= build
CONFIG ?= Release
CMAKE ?= cmake
TEXTURE ?= texture.raw
MAP ?=

ifeq ($(OS),Windows_NT)
  EXE := .exe
  # Visual Studio uses build/Release; MinGW is single-config and uses build/.
  CONFIG_DIR ?= $(CONFIG)/
else
  EXE :=
  CONFIG_DIR ?=
endif

.PHONY: all configure run asan clean

all: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(CONFIG)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

run: all
	$(BUILD_DIR)/$(CONFIG_DIR)game3d$(EXE) $(TEXTURE) $(MAP)

asan:
	$(CMAKE) -S . -B $(BUILD_DIR)-asan -DCMAKE_BUILD_TYPE=Debug -DGAME3D_SANITIZERS=ON
	$(CMAKE) --build $(BUILD_DIR)-asan --config Debug

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
	$(CMAKE) -E remove_directory $(BUILD_DIR)-asan
