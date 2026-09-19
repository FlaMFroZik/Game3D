# Game3D — сборка
#
#   make           — собрать ./game3d
#   make run       — собрать и запустить (TEXTURE=... — путь к текстуре)
#   make asan      — пересобрать с AddressSanitizer/UBSan
#   make clean     — удалить build/ и бинарник
#
# Зависимости (Debian/Ubuntu): libx11-dev libgl1-mesa-dev libglu1-mesa-dev

TARGET  := game3d
BUILD   := build
TEXTURE ?= texture.raw

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -I.
LDLIBS  := -lGLU -lGL -lX11 -lm

SRC := main.c \
       gen.c \
       render/prim.c \
       render/glx.c \
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
	./$(TARGET) $(TEXTURE)

asan: clean
	@$(MAKE) --no-print-directory CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"

clean:
	@rm -rf $(BUILD) $(TARGET)

-include $(DEP)
