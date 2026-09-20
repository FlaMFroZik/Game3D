# Game3D — сборка
#
#   make           — собрать ./game3d
#   make run       — собрать и запустить (TEXTURE=... MAP=... — пути)
#   make asan      — пересобрать с AddressSanitizer/UBSan
#   make clean     — удалить build/ и бинарник
#
# Зависимости (Debian/Ubuntu): libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev

TARGET  := game3d
BUILD   := build
TEXTURE ?= texture.raw
MAP     ?=

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -I.
LDLIBS  := -lglfw -lGLU -lGL -lm

SRC := main.c \
       map/gen.c \
       map/map.c \
       image.c \
       render/prim.c \
       render/window.c \
       render/render.c \
       physics/physics.c \
       physics/coll.c

OBJ := $(SRC:%.c=$(BUILD)/%.o)
DEP := $(OBJ:.o=.d)

.PHONY: all run asan clean

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo "  LD  $@"
	@$(CC) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET) $(TEXTURE) $(MAP)

asan: clean
	@$(MAKE) --no-print-directory CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"

clean:
	@rm -rf $(BUILD) $(TARGET)

-include $(DEP)
