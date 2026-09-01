/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "classic_games_actions.h"
#include "classic_games_binds.h"
#include "classic_games_canvas_art.h"
#include "classic_games_model.h"
#include "classic_games_objects.h"
#include "mosaic_app_catalog.h"
#include "mosaic_hub_actions.h"
#include "mosaic_runtime.h"
#include "mosaic_ui.h"

#if defined(ESP_PLATFORM)
#include "nvs.h"
#endif

#define CLASSIC_GAMES_APP_ID 45U
#define GAME_COUNT 4
#define RESUME_GUARD_MS 900U
#define IMPACT_HOLD_MS 360U
#define RESTART_CONFIRM_MS 2600U
#define MINE_HOLD_MS 360U
#define BIRD_REPEAT_GUARD_MS 80U
#define TAP_SLOP_PX 12
#define AXIS_LOCK_PX 14
#define SNAKE_COMMIT_PX 24
#define BLOCK_COLUMN_PX 30.5714f
#define BLOCK_DROP_PX 52
#define BLOCK_FLICK_PX 36
#define BLOCK_FLICK_MS 220U
#define CANVAS_X 4
#define CANVAS_Y 50
#define CANVAS_W 472
#define CANVAS_H 428
#define INPUT_BOTTOM 460
#define NVS_NAMESPACE "classic_games"
#define NVS_ONBOARD_KEY "onboard"

typedef enum {
    CG_STAGE_HOME = 0,
    CG_STAGE_ONBOARDING,
    CG_STAGE_GAME,
} cg_stage_t;

typedef enum {
    CG_GAME_MINES = 0,
    CG_GAME_SNAKE,
    CG_GAME_BLOCKS,
    CG_GAME_BIRD,
} cg_game_id_t;

typedef enum {
    POINTER_NONE = 0,
    POINTER_MINE,
    POINTER_SNAKE,
    POINTER_BLOCKS,
    POINTER_BIRD,
} pointer_kind_t;

typedef enum {
    AXIS_NONE = 0,
    AXIS_X,
    AXIS_Y,
} gesture_axis_t;

typedef struct {
    bool active;
    bool cancelled;
    bool applied;
    bool long_pressed;
    bool soft_dropped;
    pointer_kind_t kind;
    gesture_axis_t axis;
    int mine_index;
    int x;
    int y;
    int last_x;
    int last_y;
    int max_distance;
    int max_downward;
    uint32_t started_ms;
} cg_pointer_t;

typedef struct {
    bool has[GAME_COUNT];
    uint32_t value[GAME_COUNT];
} cg_records_t;

typedef struct {
    cg_game_id_t game;
    cg_mines_t mines;
    cg_snake_t snake;
    cg_blocks_t blocks;
    cg_bird_t bird;
} cg_render_snapshot_t;

typedef struct {
    atomic_uint sequence;
    cg_render_snapshot_t snapshot;
} cg_canvas_state_t;

typedef struct {
    cg_stage_t stage;
    cg_game_id_t active_game;
    cg_records_t records;
    uint8_t onboarded_mask;
    cg_mines_t mines;
    cg_snake_t snake;
    cg_blocks_t blocks;
    cg_bird_t bird;
    cg_pointer_t pointer;
    uint32_t seed_cursor;
    int64_t last_tick_us;
    uint32_t tick_remainder_us;
    uint32_t elapsed_ms;
    uint32_t mines_acc_ms;
    uint32_t snake_acc_ms;
    uint32_t bird_render_acc_ms;
    uint32_t resume_until_ms;
    uint32_t impact_until_ms;
    uint32_t restart_until_ms;
    uint32_t last_bird_input_ms;
    uint32_t previous_best;
    bool resume_guard;
    bool impact;
    bool result_ready;
    bool restart_confirm;
    bool new_best;
} classic_games_t;

static classic_games_t s_app;
static cg_canvas_state_t s_canvas;
static bool s_initialized;

__attribute__((weak)) void mosaic_classic_games_sound(cg_event_t event)
{
    (void)event;
}

static void emit_feedback(uint32_t duration_ms, cg_event_t event)
{
    (void)mosaic_ui_haptic_feedback(duration_ms);
    mosaic_classic_games_sound(event);
}

#if !defined(ESP_PLATFORM)
static cg_records_t s_host_records;
static uint8_t s_host_onboarded_mask;
#endif

#if defined(ESP_PLATFORM)
static const char *const s_record_keys[GAME_COUNT] = {
    "mines", "snake", "blocks", "bird",
};
#endif

static const uint16_t s_home_continue_bind[GAME_COUNT] = {
    GSP_BIND_HOME_MINES_CONTINUE,
    GSP_BIND_HOME_SNAKE_CONTINUE,
    GSP_BIND_HOME_BLOCKS_CONTINUE,
    GSP_BIND_HOME_BIRD_CONTINUE,
};

static const uint16_t s_home_record_label_bind[GAME_COUNT] = {
    GSP_BIND_HOME_MINES_RECORD_LABEL,
    GSP_BIND_HOME_SNAKE_RECORD_LABEL,
    GSP_BIND_HOME_BLOCKS_RECORD_LABEL,
    GSP_BIND_HOME_BIRD_RECORD_LABEL,
};

static const uint16_t s_home_record_value_bind[GAME_COUNT] = {
    GSP_BIND_HOME_MINES_RECORD_VALUE,
    GSP_BIND_HOME_SNAKE_RECORD_VALUE,
    GSP_BIND_HOME_BLOCKS_RECORD_VALUE,
    GSP_BIND_HOME_BIRD_RECORD_VALUE,
};

static const uint16_t s_guide_icon_bind[GAME_COUNT] = {
    GSP_BIND_ONBOARDING_MINES_GUIDE_ICON_VISIBLE,
    GSP_BIND_ONBOARDING_SNAKE_GUIDE_ICON_VISIBLE,
    GSP_BIND_ONBOARDING_BLOCKS_GUIDE_ICON_VISIBLE,
    GSP_BIND_ONBOARDING_BIRD_GUIDE_ICON_VISIBLE,
};

static const uint16_t s_mine_flag_bind[CG_MINES_TOTAL] = {
    GSP_BIND_MINE_FLAG_0_VISIBLE, GSP_BIND_MINE_FLAG_1_VISIBLE,
    GSP_BIND_MINE_FLAG_2_VISIBLE, GSP_BIND_MINE_FLAG_3_VISIBLE,
    GSP_BIND_MINE_FLAG_4_VISIBLE, GSP_BIND_MINE_FLAG_5_VISIBLE,
    GSP_BIND_MINE_FLAG_6_VISIBLE, GSP_BIND_MINE_FLAG_7_VISIBLE,
    GSP_BIND_MINE_FLAG_8_VISIBLE, GSP_BIND_MINE_FLAG_9_VISIBLE,
};

static const uint32_t s_mine_flag_object[CG_MINES_TOTAL] = {
    GSP_OBJ_KEY_MINE_FLAG_0, GSP_OBJ_KEY_MINE_FLAG_1,
    GSP_OBJ_KEY_MINE_FLAG_2, GSP_OBJ_KEY_MINE_FLAG_3,
    GSP_OBJ_KEY_MINE_FLAG_4, GSP_OBJ_KEY_MINE_FLAG_5,
    GSP_OBJ_KEY_MINE_FLAG_6, GSP_OBJ_KEY_MINE_FLAG_7,
    GSP_OBJ_KEY_MINE_FLAG_8, GSP_OBJ_KEY_MINE_FLAG_9,
};

static const uint16_t s_mine_hit_bind[CG_MINES_TOTAL] = {
    GSP_BIND_MINE_HIT_0_VISIBLE, GSP_BIND_MINE_HIT_1_VISIBLE,
    GSP_BIND_MINE_HIT_2_VISIBLE, GSP_BIND_MINE_HIT_3_VISIBLE,
    GSP_BIND_MINE_HIT_4_VISIBLE, GSP_BIND_MINE_HIT_5_VISIBLE,
    GSP_BIND_MINE_HIT_6_VISIBLE, GSP_BIND_MINE_HIT_7_VISIBLE,
    GSP_BIND_MINE_HIT_8_VISIBLE, GSP_BIND_MINE_HIT_9_VISIBLE,
};

static const uint32_t s_mine_hit_object[CG_MINES_TOTAL] = {
    GSP_OBJ_KEY_MINE_HIT_0, GSP_OBJ_KEY_MINE_HIT_1,
    GSP_OBJ_KEY_MINE_HIT_2, GSP_OBJ_KEY_MINE_HIT_3,
    GSP_OBJ_KEY_MINE_HIT_4, GSP_OBJ_KEY_MINE_HIT_5,
    GSP_OBJ_KEY_MINE_HIT_6, GSP_OBJ_KEY_MINE_HIT_7,
    GSP_OBJ_KEY_MINE_HIT_8, GSP_OBJ_KEY_MINE_HIT_9,
};

static const uint16_t s_bird_gem_bind[CG_BIRD_PIPE_COUNT] = {
    GSP_BIND_BIRD_GEM_0_VISIBLE,
    GSP_BIND_BIRD_GEM_1_VISIBLE,
    GSP_BIND_BIRD_GEM_2_VISIBLE,
};

static const uint32_t s_bird_gem_object[CG_BIRD_PIPE_COUNT] = {
    GSP_OBJ_KEY_BIRD_GEM_0,
    GSP_OBJ_KEY_BIRD_GEM_1,
    GSP_OBJ_KEY_BIRD_GEM_2,
};

static uint32_t next_seed(void)
{
    s_app.seed_cursor =
        (s_app.seed_cursor != 0U ? s_app.seed_cursor : 1U) *
        UINT32_C(1664525) + UINT32_C(1013904223);
    return s_app.seed_cursor;
}

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int round_float(float value)
{
    return value >= 0.0f ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static cg_phase_t active_phase(void)
{
    switch (s_app.active_game) {
    case CG_GAME_MINES: return s_app.mines.phase;
    case CG_GAME_SNAKE: return s_app.snake.phase;
    case CG_GAME_BLOCKS: return s_app.blocks.phase;
    case CG_GAME_BIRD: return s_app.bird.phase;
    default: return CG_PHASE_READY;
    }
}

static void set_active_phase(cg_phase_t phase)
{
    switch (s_app.active_game) {
    case CG_GAME_MINES: s_app.mines.phase = phase; break;
    case CG_GAME_SNAKE: s_app.snake.phase = phase; break;
    case CG_GAME_BLOCKS: s_app.blocks.phase = phase; break;
    case CG_GAME_BIRD: s_app.bird.phase = phase; break;
    default: break;
    }
}

static void clear_pointer(void)
{
    memset(&s_app.pointer, 0, sizeof(s_app.pointer));
}

static void init_game(cg_game_id_t game)
{
    switch (game) {
    case CG_GAME_MINES:
        cg_mines_init(&s_app.mines, next_seed());
        s_app.mines_acc_ms = 0;
        break;
    case CG_GAME_SNAKE:
        cg_snake_init(&s_app.snake);
        s_app.snake_acc_ms = 0;
        break;
    case CG_GAME_BLOCKS:
        cg_blocks_init(&s_app.blocks, next_seed());
        break;
    case CG_GAME_BIRD:
        cg_bird_init(&s_app.bird, next_seed());
        s_app.bird_render_acc_ms = 0;
        s_app.last_bird_input_ms = UINT32_MAX - BIRD_REPEAT_GUARD_MS;
        break;
    default:
        break;
    }
}

static void load_preferences(void)
{
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    for (size_t game = 0; game < GAME_COUNT; ++game) {
        uint32_t value = 0;
        if (nvs_get_u32(handle, s_record_keys[game], &value) == ESP_OK &&
                value > 0U) {
            s_app.records.has[game] = true;
            s_app.records.value[game] = value;
        }
    }
    (void)nvs_get_u8(handle, NVS_ONBOARD_KEY, &s_app.onboarded_mask);
    nvs_close(handle);
#else
    s_app.records = s_host_records;
    s_app.onboarded_mask = s_host_onboarded_mask;
#endif
}

static void save_preferences(void)
{
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    for (size_t game = 0; game < GAME_COUNT; ++game) {
        if (s_app.records.has[game]) {
            (void)nvs_set_u32(
                handle, s_record_keys[game], s_app.records.value[game]);
        } else {
            (void)nvs_erase_key(handle, s_record_keys[game]);
        }
    }
    (void)nvs_set_u8(handle, NVS_ONBOARD_KEY, s_app.onboarded_mask);
    (void)nvs_commit(handle);
    nvs_close(handle);
#else
    s_host_records = s_app.records;
    s_host_onboarded_mask = s_app.onboarded_mask;
#endif
}

static void publish_canvas_snapshot(void)
{
    cg_render_snapshot_t next = {
        .game = s_app.active_game,
        .mines = s_app.mines,
        .snake = s_app.snake,
        .blocks = s_app.blocks,
        .bird = s_app.bird,
    };
    (void)atomic_fetch_add_explicit(
        &s_canvas.sequence, 1U, memory_order_acq_rel);
    s_canvas.snapshot = next;
    atomic_thread_fence(memory_order_release);
    (void)atomic_fetch_add_explicit(
        &s_canvas.sequence, 1U, memory_order_release);
}

static cg_render_snapshot_t read_canvas_snapshot(void)
{
    for (;;) {
        const unsigned int before = atomic_load_explicit(
            &s_canvas.sequence, memory_order_acquire);
        if ((before & 1U) != 0U) {
            continue;
        }
        const cg_render_snapshot_t snapshot = s_canvas.snapshot;
        atomic_thread_fence(memory_order_acquire);
        const unsigned int after = atomic_load_explicit(
            &s_canvas.sequence, memory_order_acquire);
        if (before == after && (after & 1U) == 0U) {
            return snapshot;
        }
    }
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(
        ((uint16_t)(red & 0xf8U) << 8) |
        ((uint16_t)(green & 0xfcU) << 3) |
        ((uint16_t)blue >> 3));
}

static void put_pixel(
    const esp_gsp_canvas_surface_t *surface, int x, int y, uint16_t color)
{
    if (x < (int)surface->x || y < (int)surface->y ||
            x >= (int)surface->x + (int)surface->width ||
            y >= (int)surface->y + (int)surface->height) {
        return;
    }
    const size_t local_x = (size_t)(x - (int)surface->x);
    const size_t local_y = (size_t)(y - (int)surface->y);
    uint8_t *row = (uint8_t *)surface->pixels +
        local_y * surface->stride_bytes;
    if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
        ((uint16_t *)row)[local_x] = color;
    } else {
        uint8_t *pixel = row + local_x * 3U;
        pixel[0] = (uint8_t)(((color >> 11) & 0x1fU) * 255U / 31U);
        pixel[1] = (uint8_t)(((color >> 5) & 0x3fU) * 255U / 63U);
        pixel[2] = (uint8_t)((color & 0x1fU) * 255U / 31U);
    }
}

static uint16_t blend_rgb565(
    uint16_t foreground, uint16_t background, uint8_t alpha)
{
    if (alpha == 0U) return background;
    if (alpha == UINT8_MAX) return foreground;
    const uint32_t inverse = UINT8_MAX - alpha;
    const uint32_t foreground_r = ((foreground >> 11) & 0x1fU) * 255U / 31U;
    const uint32_t foreground_g = ((foreground >> 5) & 0x3fU) * 255U / 63U;
    const uint32_t foreground_b = (foreground & 0x1fU) * 255U / 31U;
    const uint32_t background_r = ((background >> 11) & 0x1fU) * 255U / 31U;
    const uint32_t background_g = ((background >> 5) & 0x3fU) * 255U / 63U;
    const uint32_t background_b = (background & 0x1fU) * 255U / 31U;
    return rgb565(
        (uint8_t)((foreground_r * alpha + background_r * inverse) / 255U),
        (uint8_t)((foreground_g * alpha + background_g * inverse) / 255U),
        (uint8_t)((foreground_b * alpha + background_b * inverse) / 255U));
}

static void fill_surface(
    const esp_gsp_canvas_surface_t *surface, uint16_t color)
{
    for (uint16_t y = 0; y < surface->height; ++y) {
        uint8_t *row = (uint8_t *)surface->pixels +
            (size_t)y * surface->stride_bytes;
        if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
            uint16_t *pixels = (uint16_t *)row;
            for (uint16_t x = 0; x < surface->width; ++x) {
                pixels[x] = color;
            }
        } else {
            const uint8_t red =
                (uint8_t)(((color >> 11) & 0x1fU) * 255U / 31U);
            const uint8_t green =
                (uint8_t)(((color >> 5) & 0x3fU) * 255U / 63U);
            const uint8_t blue =
                (uint8_t)((color & 0x1fU) * 255U / 31U);
            for (uint16_t x = 0; x < surface->width; ++x) {
                row[(size_t)x * 3U] = red;
                row[(size_t)x * 3U + 1U] = green;
                row[(size_t)x * 3U + 2U] = blue;
            }
        }
    }
}

static void draw_rect(
    const esp_gsp_canvas_surface_t *surface,
    int x, int y, int width, int height, uint16_t color)
{
    int left = x > (int)surface->x ? x : (int)surface->x;
    int top = y > (int)surface->y ? y : (int)surface->y;
    int right = x + width;
    int bottom = y + height;
    const int surface_right = (int)surface->x + (int)surface->width;
    const int surface_bottom = (int)surface->y + (int)surface->height;
    if (right > surface_right) right = surface_right;
    if (bottom > surface_bottom) bottom = surface_bottom;
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            put_pixel(surface, px, py, color);
        }
    }
}

static void draw_rounded_rect(
    const esp_gsp_canvas_surface_t *surface,
    int x, int y, int width, int height, int radius, uint16_t color)
{
    if (radius <= 0) {
        draw_rect(surface, x, y, width, height, color);
        return;
    }
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    draw_rect(surface, x + radius, y, width - radius * 2, height, color);
    draw_rect(surface, x, y + radius, radius, height - radius * 2, color);
    draw_rect(
        surface, x + width - radius, y + radius,
        radius, height - radius * 2, color);
    const int radius_squared = radius * radius;
    for (int py = 0; py < radius; ++py) {
        for (int px = 0; px < radius; ++px) {
            const int dx = radius - px - 1;
            const int dy = radius - py - 1;
            if (dx * dx + dy * dy > radius_squared) continue;
            put_pixel(surface, x + px, y + py, color);
            put_pixel(surface, x + width - px - 1, y + py, color);
            put_pixel(surface, x + px, y + height - py - 1, color);
            put_pixel(
                surface, x + width - px - 1,
                y + height - py - 1, color);
        }
    }
}

static void blit_scaled_art(
    const esp_gsp_canvas_surface_t *surface,
    const uint16_t *colors, const uint8_t *alpha,
    int source_width, int source_height,
    int x, int y, int width, int height, bool flip_y)
{
    if (width <= 0 || height <= 0) return;
    int left = x > (int)surface->x ? x : (int)surface->x;
    int top = y > (int)surface->y ? y : (int)surface->y;
    int right = x + width;
    int bottom = y + height;
    const int surface_right = (int)surface->x + (int)surface->width;
    const int surface_bottom = (int)surface->y + (int)surface->height;
    if (right > surface_right) right = surface_right;
    if (bottom > surface_bottom) bottom = surface_bottom;
    if (left >= right || top >= bottom) return;
    for (int target_y = top; target_y < bottom; ++target_y) {
        const int py = target_y - y;
        int source_y = py * source_height / height;
        if (flip_y) source_y = source_height - source_y - 1;
        uint8_t *row = (uint8_t *)surface->pixels +
            (size_t)(target_y - (int)surface->y) *
                surface->stride_bytes;
        for (int target_x = left; target_x < right; ++target_x) {
            const int px = target_x - x;
            const int source_x = px * source_width / width;
            const size_t source_index =
                (size_t)source_y * (size_t)source_width +
                (size_t)source_x;
            const uint8_t opacity =
                alpha == NULL ? UINT8_MAX : alpha[source_index];
            if (opacity == 0U) continue;
            const size_t local_x =
                (size_t)(target_x - (int)surface->x);
            const uint16_t foreground = colors[source_index];
            if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
                uint16_t *pixels = (uint16_t *)row;
                pixels[local_x] = opacity >= 248U
                    ? foreground
                    : blend_rgb565(
                        foreground, pixels[local_x], opacity);
            } else {
                uint8_t *pixel = row + local_x * 3U;
                const uint8_t foreground_r = (uint8_t)(
                    ((foreground >> 11) & 0x1fU) * 255U / 31U);
                const uint8_t foreground_g = (uint8_t)(
                    ((foreground >> 5) & 0x3fU) * 255U / 63U);
                const uint8_t foreground_b = (uint8_t)(
                    (foreground & 0x1fU) * 255U / 31U);
                if (opacity >= 248U) {
                    pixel[0] = foreground_r;
                    pixel[1] = foreground_g;
                    pixel[2] = foreground_b;
                } else {
                    const uint32_t inverse = UINT8_MAX - opacity;
                    pixel[0] = (uint8_t)(
                        (foreground_r * opacity +
                         pixel[0] * inverse) / UINT8_MAX);
                    pixel[1] = (uint8_t)(
                        (foreground_g * opacity +
                         pixel[1] * inverse) / UINT8_MAX);
                    pixel[2] = (uint8_t)(
                        (foreground_b * opacity +
                         pixel[2] * inverse) / UINT8_MAX);
                }
            }
        }
    }
}

static const uint8_t *font5x7(char character)
{
    static const uint8_t digits[10][7] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    };
    static const uint8_t letters[26][7] = {
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f },
        { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e },
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    };
    if (character >= '0' && character <= '9') {
        return digits[character - '0'];
    }
    if (character < 'A' || character > 'Z') return NULL;
    return letters[character - 'A'];
}

static int text5x7_width(const char *text, int scale)
{
    return text == NULL ? 0 : (int)strlen(text) * 6 * scale - scale;
}

static void draw_text5x7(
    const esp_gsp_canvas_surface_t *surface,
    int x, int y, const char *text, int scale, uint16_t color)
{
    if (text == NULL || scale <= 0) return;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        const uint8_t *rows = font5x7(text[index]);
        if (rows != NULL) {
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((rows[row] & (1U << (4 - column))) != 0U) {
                        draw_rect(
                            surface,
                            x + (int)index * 6 * scale + column * scale,
                            y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }
}

static void draw_mines(
    const esp_gsp_canvas_surface_t *surface, const cg_mines_t *mines)
{
    const int left = 22;
    const int pitch = 54;
    const uint16_t board = rgb565(14, 14, 15);
    const uint16_t covered_border = rgb565(144, 211, 154);
    const uint16_t covered = rgb565(58, 101, 64);
    const uint16_t covered_highlight = rgb565(89, 125, 94);
    const uint16_t covered_shadow = rgb565(42, 73, 46);
    const uint16_t open_border = rgb565(46, 47, 48);
    const uint16_t open = rgb565(17, 17, 18);
    const uint16_t open_highlight = rgb565(24, 24, 25);
    const uint16_t danger = rgb565(232, 54, 45);
    static const uint16_t number_colors[9] = {
        0,
        0x755f, /* #77a9ff */
        0x76b3, /* #74d69a */
        0xfc6e, /* #ff8e75 */
        0xbcff, /* #bd9cff */
        0xfe8d, /* #ffd06a */
        0x76dc, /* #75dbe7 */
        0xef7d, /* gray-1 */
        0xef7d,
    };
    draw_rounded_rect(surface, 20, -2, 432, 432, 20, board);
    for (int index = 0; index < CG_MINES_CELLS; ++index) {
        const int column = index % CG_MINES_SIZE;
        const int row = index / CG_MINES_SIZE;
        const int x = left + column * pitch;
        const int y = row * pitch;
        const bool revealed = mines->revealed[index];
        const bool hit = revealed && mines->mines[index] &&
            mines->hit_index == index;
        const uint16_t border =
            hit ? rgb565(249, 250, 251) :
            revealed ? open_border : covered_border;
        const uint16_t fill = hit ? danger : revealed ? open : covered;
        draw_rounded_rect(surface, x, y + 1, 50, 49, 7,
                          rgb565(7, 8, 8));
        draw_rounded_rect(surface, x, y, 50, 50, 7, border);
        draw_rounded_rect(surface, x + 2, y + 2, 46, 46, 5, fill);
        if (!revealed) {
            draw_rect(
                surface, x + 6, y + 2, 38, 2, covered_highlight);
            draw_rect(
                surface, x + 6, y + 45, 38, 3, covered_shadow);
        } else if (!hit) {
            draw_rect(
                surface, x + 6, y + 2, 38, 1, open_highlight);
        }
        if (mines->revealed[index] && !mines->mines[index]) {
            const unsigned int count =
                cg_mines_neighbor_count(mines, index);
            if (count > 0U) {
                char label[2] = { (char)('0' + count), '\0' };
                draw_text5x7(
                    surface, x + 18, y + 15,
                    label, 3, rgb565(0, 0, 0));
                draw_text5x7(
                    surface, x + 18, y + 14,
                    label, 3, number_colors[count]);
            }
        }
    }
}

static void draw_snake(
    const esp_gsp_canvas_surface_t *surface, const cg_snake_t *snake)
{
    const uint16_t board = rgb565(16, 20, 17);
    const uint16_t grid = rgb565(26, 29, 27);
    const uint16_t body_border = rgb565(138, 209, 148);
    const uint16_t body = rgb565(77, 143, 86);
    const uint16_t body_highlight = rgb565(109, 163, 116);
    const uint16_t body_shadow = rgb565(7, 10, 8);
    const int radius = 18;
    const int radius_squared = radius * radius;
    const uint16_t outside = rgb565(5, 5, 5);
    fill_surface(surface, board);
    for (int x = 0; x < CANVAS_W; x += 30) {
        draw_rect(surface, x, 0, 1, CANVAS_H, grid);
    }
    for (int y = 0; y < CANVAS_H; y += 30) {
        draw_rect(surface, 0, y, CANVAS_W, 1, grid);
    }
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            const int dx = radius - x - 1;
            const int dy = radius - y - 1;
            if (dx * dx + dy * dy <= radius_squared) continue;
            put_pixel(surface, x, y, outside);
            put_pixel(surface, CANVAS_W - x - 1, y, outside);
            put_pixel(surface, x, CANVAS_H - y - 1, outside);
            put_pixel(
                surface, CANVAS_W - x - 1,
                CANVAS_H - y - 1, outside);
        }
    }
    for (uint16_t index = 1; index < snake->length; ++index) {
        const cg_point_t point = snake->snake[index];
        if (point.x >= 0 && point.x < CG_SNAKE_GRID &&
                point.y >= 0 && point.y < CG_SNAKE_GRID) {
            const int left =
                (point.x * CANVAS_W + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
            const int right =
                ((point.x + 1) * CANVAS_W + CG_SNAKE_GRID / 2) /
                CG_SNAKE_GRID;
            const int top =
                (point.y * CANVAS_H + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
            const int bottom =
                ((point.y + 1) * CANVAS_H + CG_SNAKE_GRID / 2) /
                CG_SNAKE_GRID;
            draw_rounded_rect(
                surface, left + 3, top + 5,
                right - left - 6, bottom - top - 6,
                8, body_shadow);
            draw_rounded_rect(
                surface, left + 3, top + 3,
                right - left - 6, bottom - top - 6,
                8, body_border);
            draw_rounded_rect(
                surface, left + 4, top + 4,
                right - left - 8, bottom - top - 8,
                7, body);
            draw_rect(
                surface, left + 8, top + 4,
                right - left - 16, 2, body_highlight);
        }
    }
    if (snake->has_crash_cell) {
        const int x = snake->crash_cell.x < 0 ? 0 :
            snake->crash_cell.x >= CG_SNAKE_GRID ? CG_SNAKE_GRID - 1 :
            snake->crash_cell.x;
        const int y = snake->crash_cell.y < 0 ? 0 :
            snake->crash_cell.y >= CG_SNAKE_GRID ? CG_SNAKE_GRID - 1 :
            snake->crash_cell.y;
        const int left =
            (x * CANVAS_W + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
        const int right =
            ((x + 1) * CANVAS_W + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
        const int top =
            (y * CANVAS_H + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
        const int bottom =
            ((y + 1) * CANVAS_H + CG_SNAKE_GRID / 2) / CG_SNAKE_GRID;
        draw_rounded_rect(
            surface, left + 1, top + 1,
            right - left - 2, bottom - top - 2,
            6, rgb565(232, 54, 45));
    }
}

static uint16_t block_color(cg_block_kind_t kind)
{
    static const uint16_t colors[CG_BLOCK_KIND_COUNT] = {
        0x07ff, 0xffe0, 0x801f, 0xfd20, 0x045f, 0x07e0, 0xf800,
    };
    return kind < CG_BLOCK_KIND_COUNT ? colors[kind] : 0;
}

static void draw_block_cell(
    const esp_gsp_canvas_surface_t *surface,
    int x, int y, int width, int height, uint16_t color, bool ghost)
{
    const uint16_t fill = ghost ? rgb565(48, 49, 52) : color;
    draw_rounded_rect(surface, x, y, width, height, 5, fill);
    draw_rounded_rect(
        surface, x + 2, y + 2, width - 4, 3, 2,
        ghost ? rgb565(78, 79, 82) : rgb565(246, 242, 232));
}

static void draw_block_grid_cell(
    const esp_gsp_canvas_surface_t *surface,
    cg_point_t point, uint16_t color, bool ghost)
{
    const int grid_x = 11;
    const float cell = 30.5714f;
    const int left = grid_x + round_float((float)point.x * cell) + 2;
    const int right =
        grid_x + round_float((float)(point.x + 1) * cell) - 2;
    const int top = round_float((float)point.y * cell) + 2;
    const int bottom = round_float((float)(point.y + 1) * cell) - 2;
    draw_block_cell(
        surface, left, top, right - left, bottom - top, color, ghost);
}

static void draw_blocks(
    const esp_gsp_canvas_surface_t *surface, const cg_blocks_t *blocks)
{
    const int well_width = 388;
    const int panel_x = 396;
    const uint16_t well = rgb565(12, 13, 14);
    const uint16_t grid = rgb565(34, 35, 38);
    const uint16_t panel = rgb565(31, 31, 33);
    const uint16_t panel_card = rgb565(18, 18, 20);
    const uint16_t muted = rgb565(145, 145, 155);
    const uint16_t ink = rgb565(246, 242, 232);
    draw_rounded_rect(surface, 0, 0, well_width, CANVAS_H, 18, well);
    for (int column = 0; column <= CG_BLOCK_COLUMNS; ++column) {
        const int x = 11 + round_float((float)column * 30.5714f);
        draw_rect(surface, x, 0, 1, CANVAS_H, grid);
    }
    for (int row = 0; row <= CG_BLOCK_ROWS; ++row) {
        const int y = round_float((float)row * 30.5714f);
        draw_rect(surface, 11, y, 367, 1, grid);
    }

    if (blocks->phase != CG_PHASE_READY &&
            blocks->phase != CG_PHASE_OVER) {
        cg_point_t ghost[CG_BLOCK_PIECE_CELLS];
        cg_blocks_ghost_cells(blocks, ghost);
        for (size_t index = 0; index < CG_BLOCK_PIECE_CELLS; ++index) {
            draw_block_grid_cell(
                surface, ghost[index],
                block_color(blocks->piece.kind), true);
        }
    }
    for (int row = 0; row < CG_BLOCK_ROWS; ++row) {
        for (int column = 0; column < CG_BLOCK_COLUMNS; ++column) {
            const uint8_t kind =
                blocks->fixed[row * CG_BLOCK_COLUMNS + column];
            if (kind != CG_BLOCK_EMPTY) {
                draw_block_grid_cell(
                    surface, (cg_point_t){ column, row },
                    block_color((cg_block_kind_t)kind), false);
            }
        }
    }
    cg_point_t active[CG_BLOCK_PIECE_CELLS];
    (void)cg_blocks_piece_cells(&blocks->piece, active);
    for (size_t index = 0; index < CG_BLOCK_PIECE_CELLS; ++index) {
        draw_block_grid_cell(
            surface, active[index],
            block_color(blocks->piece.kind), false);
    }

    draw_rounded_rect(
        surface, panel_x, 0, CANVAS_W - panel_x, CANVAS_H, 18, panel);
    draw_text5x7(
        surface,
        panel_x + (76 - text5x7_width("NEXT", 1)) / 2,
        9, "NEXT", 1, muted);
    draw_rounded_rect(surface, panel_x + 6, 27, 64, 70, 10, panel_card);
    const cg_block_kind_t next = cg_blocks_next_kind(blocks);
    cg_block_piece_t preview = { .kind = next, .rotation = 0, .x = 0, .y = 0 };
    cg_point_t preview_cells[CG_BLOCK_PIECE_CELLS];
    (void)cg_blocks_piece_cells(&preview, preview_cells);
    int min_x = preview_cells[0].x;
    int max_x = preview_cells[0].x;
    int min_y = preview_cells[0].y;
    int max_y = preview_cells[0].y;
    for (size_t index = 1; index < CG_BLOCK_PIECE_CELLS; ++index) {
        if (preview_cells[index].x < min_x) min_x = preview_cells[index].x;
        if (preview_cells[index].x > max_x) max_x = preview_cells[index].x;
        if (preview_cells[index].y < min_y) min_y = preview_cells[index].y;
        if (preview_cells[index].y > max_y) max_y = preview_cells[index].y;
    }
    const int preview_x =
        panel_x + 6 + (64 - ((max_x - min_x + 1) * 12 - 1)) / 2;
    const int preview_y =
        27 + (70 - ((max_y - min_y + 1) * 12 - 1)) / 2;
    for (size_t index = 0; index < CG_BLOCK_PIECE_CELLS; ++index) {
        draw_rounded_rect(
            surface,
            preview_x + (preview_cells[index].x - min_x) * 12,
            preview_y + (preview_cells[index].y - min_y) * 12,
            11, 11, 3, block_color(next));
    }

    draw_text5x7(
        surface,
        panel_x + (76 - text5x7_width("LINES", 1)) / 2,
        108, "LINES", 1, muted);
    char line_text[8];
    snprintf(
        line_text, sizeof(line_text), "%02u",
        (unsigned)blocks->lines);
    draw_text5x7(
        surface,
        panel_x + (76 - text5x7_width(line_text, 2)) / 2,
        123, line_text, 2, ink);

    static const char *const primary[3] = { "DRAG", "TAP", "FLICK" };
    static const char *const secondary[3] = { "MOVE", "ROTATE", "DROP" };
    for (int index = 0; index < 3; ++index) {
        const int y = 178 + index * 79;
        draw_rounded_rect(
            surface, panel_x + 6, y, 64, 72, 8, panel_card);
        draw_text5x7(
            surface,
            panel_x + 6 +
                (64 - text5x7_width(primary[index], 1)) / 2,
            y + 21, primary[index], 1, ink);
        draw_text5x7(
            surface,
            panel_x + 6 +
                (64 - text5x7_width(secondary[index], 1)) / 2,
            y + 42, secondary[index], 1, muted);
    }
}

static int bird_render_y(float value)
{
    return round_float(value * 420.0f / CG_BIRD_FIELD);
}

static int positive_modulo(int value, int divisor)
{
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

static void draw_bird_background(
    const esp_gsp_canvas_surface_t *surface, const cg_bird_t *bird)
{
    const int world_shift =
        -round_float(bird->world_distance * 0.18f);
    const uint16_t edge = rgb565(4, 7, 14);
    for (uint16_t local_y = 0; local_y < surface->height; ++local_y) {
        const int y = (int)surface->y + local_y;
        uint8_t *row = (uint8_t *)surface->pixels +
            (size_t)local_y * surface->stride_bytes;
        if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
            uint16_t *pixels = (uint16_t *)row;
            if (y < 4 || y >= 424) {
                for (uint16_t x = 0; x < surface->width; ++x) {
                    pixels[x] = edge;
                }
                continue;
            }
            const int source_y =
                (y - 4) * CG_BIRD_BG_ART_H / 420;
            int source_x = positive_modulo(
                (int)surface->x - world_shift, CG_BIRD_BG_ART_W);
            size_t written = 0;
            while (written < surface->width) {
                size_t count =
                    (size_t)CG_BIRD_BG_ART_W - (size_t)source_x;
                const size_t remaining =
                    (size_t)surface->width - written;
                if (count > remaining) count = remaining;
                memcpy(
                    &pixels[written],
                    &cg_bird_bg_art[
                        source_y * CG_BIRD_BG_ART_W + source_x],
                    count * sizeof(uint16_t));
                written += count;
                source_x = 0;
            }
            continue;
        }
        for (uint16_t local_x = 0;
             local_x < surface->width; ++local_x) {
            uint16_t color = edge;
            if (y >= 4 && y < 424) {
                const int image_x = positive_modulo(
                    (int)surface->x + local_x - world_shift,
                    CG_BIRD_BG_ART_W);
                const int source_y =
                    (y - 4) * CG_BIRD_BG_ART_H / 420;
                color = cg_bird_bg_art[
                    source_y * CG_BIRD_BG_ART_W + image_x];
            }
            uint8_t *pixel = row + (size_t)local_x * 3U;
            pixel[0] = (uint8_t)(
                ((color >> 11) & 0x1fU) * 255U / 31U);
            pixel[1] = (uint8_t)(
                ((color >> 5) & 0x3fU) * 255U / 63U);
            pixel[2] = (uint8_t)(
                (color & 0x1fU) * 255U / 31U);
        }
    }
}

static void draw_bird(
    const esp_gsp_canvas_surface_t *surface, const cg_bird_t *bird)
{
    draw_bird_background(surface, bird);
    for (size_t index = 0; index < CG_BIRD_PIPE_COUNT; ++index) {
        const cg_bird_pipe_t *current = &bird->pipes[index];
        const int x = round_float(current->x);
        const int top = bird_render_y(current->gap_y - current->gap_half);
        const int bottom =
            bird_render_y(current->gap_y + current->gap_half);
        blit_scaled_art(
            surface,
            cg_bird_pipe_body_art, cg_bird_pipe_body_alpha,
            CG_BIRD_PIPE_BODY_ART_W, CG_BIRD_PIPE_BODY_ART_H,
            x, 0, 60, top, false);
        blit_scaled_art(
            surface,
            cg_bird_pipe_cap_art, cg_bird_pipe_cap_alpha,
            CG_BIRD_PIPE_CAP_ART_W, CG_BIRD_PIPE_CAP_ART_H,
            x - 8, top - 27, 76, 28, false);
        blit_scaled_art(
            surface,
            cg_bird_pipe_body_art, cg_bird_pipe_body_alpha,
            CG_BIRD_PIPE_BODY_ART_W, CG_BIRD_PIPE_BODY_ART_H,
            x, bottom, 60, CANVAS_H - bottom, true);
        blit_scaled_art(
            surface,
            cg_bird_pipe_cap_art, cg_bird_pipe_cap_alpha,
            CG_BIRD_PIPE_CAP_ART_W, CG_BIRD_PIPE_CAP_ART_H,
            x - 8, bottom - 1, 76, 28, true);
    }
}

static void canvas_draw(
    const esp_gsp_canvas_surface_t *surface, void *user_ctx)
{
    (void)user_ctx;
    const cg_render_snapshot_t snapshot = read_canvas_snapshot();
    if (snapshot.game == CG_GAME_BIRD) {
        draw_bird(surface, &snapshot.bird);
        return;
    }
    fill_surface(surface, rgb565(5, 5, 5));
    switch (snapshot.game) {
    case CG_GAME_MINES: draw_mines(surface, &snapshot.mines); break;
    case CG_GAME_SNAKE: draw_snake(surface, &snapshot.snake); break;
    case CG_GAME_BLOCKS: draw_blocks(surface, &snapshot.blocks); break;
    case CG_GAME_BIRD: break;
    default: break;
    }
}

static void invalidate_snake_cell(
    esp_gsp_handle_t ui, int x, int y)
{
    const gsp_rect_t dirty = {
        .x1 = (x * CANVAS_W) / CG_SNAKE_GRID,
        .y1 = (y * CANVAS_H) / CG_SNAKE_GRID,
        .x2 = ((x + 1) * CANVAS_W + CG_SNAKE_GRID - 1) /
            CG_SNAKE_GRID,
        .y2 = ((y + 1) * CANVAS_H + CG_SNAKE_GRID - 1) /
            CG_SNAKE_GRID,
    };
    (void)esp_gsp_canvas_invalidate_dirty(
        ui, GSP_BIND_GAME_CANVAS, dirty);
}

static void block_dynamic_cells(
    const cg_blocks_t *blocks, uint8_t cells[CG_BLOCK_CELLS])
{
    memset(cells, 0, CG_BLOCK_CELLS);
    if (blocks->phase != CG_PHASE_READY &&
            blocks->phase != CG_PHASE_OVER) {
        cg_point_t ghost[CG_BLOCK_PIECE_CELLS];
        cg_blocks_ghost_cells(blocks, ghost);
        for (size_t index = 0;
             index < CG_BLOCK_PIECE_CELLS; ++index) {
            const cg_point_t point = ghost[index];
            if (point.x >= 0 && point.x < CG_BLOCK_COLUMNS &&
                    point.y >= 0 && point.y < CG_BLOCK_ROWS) {
                cells[point.y * CG_BLOCK_COLUMNS + point.x] = 1U;
            }
        }
    }
    cg_point_t active[CG_BLOCK_PIECE_CELLS];
    (void)cg_blocks_piece_cells(&blocks->piece, active);
    for (size_t index = 0; index < CG_BLOCK_PIECE_CELLS; ++index) {
        const cg_point_t point = active[index];
        if (point.x >= 0 && point.x < CG_BLOCK_COLUMNS &&
                point.y >= 0 && point.y < CG_BLOCK_ROWS) {
            cells[point.y * CG_BLOCK_COLUMNS + point.x] =
                (uint8_t)(2U + blocks->piece.kind);
        }
    }
}

static void invalidate_block_cell(
    esp_gsp_handle_t ui, int x, int y)
{
    const int right =
        12 + round_float((float)(x + 1) * BLOCK_COLUMN_PX);
    const int bottom =
        1 + round_float((float)(y + 1) * BLOCK_COLUMN_PX);
    const gsp_rect_t dirty = {
        .x1 = 11 + round_float((float)x * BLOCK_COLUMN_PX),
        .y1 = round_float((float)y * BLOCK_COLUMN_PX),
        .x2 = right < CANVAS_W ? right : CANVAS_W,
        .y2 = bottom < CANVAS_H ? bottom : CANVAS_H,
    };
    (void)esp_gsp_canvas_invalidate_dirty(
        ui, GSP_BIND_GAME_CANVAS, dirty);
}

static void invalidate_canvas(esp_gsp_handle_t ui, bool full)
{
    const cg_render_snapshot_t previous = read_canvas_snapshot();
    publish_canvas_snapshot();
    if (full) {
        (void)esp_gsp_canvas_invalidate(ui, GSP_BIND_GAME_CANVAS);
        return;
    }
    if (s_app.active_game == CG_GAME_SNAKE &&
            previous.game == CG_GAME_SNAKE) {
        bool previous_cells[CG_SNAKE_CAPACITY] = { false };
        bool current_cells[CG_SNAKE_CAPACITY] = { false };
        for (uint16_t index = 1;
             index < previous.snake.length; ++index) {
            const cg_point_t point = previous.snake.snake[index];
            if (point.x >= 0 && point.x < CG_SNAKE_GRID &&
                    point.y >= 0 && point.y < CG_SNAKE_GRID) {
                previous_cells[
                    point.y * CG_SNAKE_GRID + point.x] = true;
            }
        }
        for (uint16_t index = 1;
             index < s_app.snake.length; ++index) {
            const cg_point_t point = s_app.snake.snake[index];
            if (point.x >= 0 && point.x < CG_SNAKE_GRID &&
                    point.y >= 0 && point.y < CG_SNAKE_GRID) {
                current_cells[
                    point.y * CG_SNAKE_GRID + point.x] = true;
            }
        }
        int changed = 0;
        for (int index = 0; index < CG_SNAKE_CAPACITY; ++index) {
            changed += previous_cells[index] != current_cells[index];
        }
        const bool crash_changed =
            previous.snake.has_crash_cell !=
                s_app.snake.has_crash_cell ||
            (s_app.snake.has_crash_cell &&
             (previous.snake.crash_cell.x !=
                  s_app.snake.crash_cell.x ||
              previous.snake.crash_cell.y !=
                  s_app.snake.crash_cell.y));
        if (changed > 8) {
            (void)esp_gsp_canvas_invalidate(
                ui, GSP_BIND_GAME_CANVAS);
            return;
        }
        for (int y = 0; y < CG_SNAKE_GRID; ++y) {
            for (int x = 0; x < CG_SNAKE_GRID; ++x) {
                const int index = y * CG_SNAKE_GRID + x;
                if (previous_cells[index] != current_cells[index]) {
                    invalidate_snake_cell(ui, x, y);
                }
            }
        }
        if (crash_changed && s_app.snake.has_crash_cell) {
            invalidate_snake_cell(
                ui,
                s_app.snake.crash_cell.x < 0 ? 0 :
                    s_app.snake.crash_cell.x >= CG_SNAKE_GRID
                        ? CG_SNAKE_GRID - 1 :
                        s_app.snake.crash_cell.x,
                s_app.snake.crash_cell.y < 0 ? 0 :
                    s_app.snake.crash_cell.y >= CG_SNAKE_GRID
                        ? CG_SNAKE_GRID - 1 :
                        s_app.snake.crash_cell.y);
        }
        return;
    }
    if (s_app.active_game == CG_GAME_BLOCKS &&
            previous.game == CG_GAME_BLOCKS) {
        if (memcmp(
                previous.blocks.fixed, s_app.blocks.fixed,
                sizeof(s_app.blocks.fixed)) != 0 ||
                previous.blocks.piece_index !=
                    s_app.blocks.piece_index ||
                previous.blocks.lines != s_app.blocks.lines) {
            (void)esp_gsp_canvas_invalidate(
                ui, GSP_BIND_GAME_CANVAS);
            return;
        }
        uint8_t previous_cells[CG_BLOCK_CELLS];
        uint8_t current_cells[CG_BLOCK_CELLS];
        block_dynamic_cells(&previous.blocks, previous_cells);
        block_dynamic_cells(&s_app.blocks, current_cells);
        for (int y = 0; y < CG_BLOCK_ROWS; ++y) {
            for (int x = 0; x < CG_BLOCK_COLUMNS; ++x) {
                const int index = y * CG_BLOCK_COLUMNS + x;
                if (previous_cells[index] != current_cells[index]) {
                    invalidate_block_cell(ui, x, y);
                }
            }
        }
        return;
    }
    const gsp_rect_t dirty = {
        .x1 = 0, .y1 = 0, .x2 = CANVAS_W, .y2 = CANVAS_H,
    };
    (void)esp_gsp_canvas_invalidate_dirty(
        ui, GSP_BIND_GAME_CANVAS, dirty);
}

static void set_sprite(
    esp_gsp_handle_t ui, uint32_t object, uint16_t bind,
    bool visible, int x, int y)
{
    if (visible) {
        (void)esp_gsp_component_set_position(ui, object, x, y);
    }
    (void)esp_gsp_set_visible(ui, bind, visible);
}

static void render_home(esp_gsp_handle_t ui)
{
    char text[24];
    for (int game = 0; game < GAME_COUNT; ++game) {
        const bool paused =
            (game == CG_GAME_MINES && s_app.mines.phase == CG_PHASE_PAUSED) ||
            (game == CG_GAME_SNAKE && s_app.snake.phase == CG_PHASE_PAUSED) ||
            (game == CG_GAME_BLOCKS && s_app.blocks.phase == CG_PHASE_PAUSED) ||
            (game == CG_GAME_BIRD && s_app.bird.phase == CG_PHASE_PAUSED);
        (void)esp_gsp_set_text(
            ui, s_home_continue_bind[game], paused ? "CONTINUE" : "");
        (void)esp_gsp_set_text(
            ui, s_home_record_label_bind[game],
            s_app.records.has[game] ? "BEST" : "NO BEST");
        if (!s_app.records.has[game]) {
            (void)esp_gsp_set_text(
                ui, s_home_record_value_bind[game], "PLAY");
        } else if (game == CG_GAME_MINES) {
            const uint32_t value = s_app.records.value[game];
            snprintf(
                text, sizeof(text), "%02u:%02u",
                (unsigned)(value / 60U), (unsigned)(value % 60U));
            (void)esp_gsp_set_text(
                ui, s_home_record_value_bind[game], text);
        } else {
            snprintf(
                text, sizeof(text), "%02u",
                (unsigned)s_app.records.value[game]);
            (void)esp_gsp_set_text(
                ui, s_home_record_value_bind[game], text);
        }
    }
}

static void render_onboarding(esp_gsp_handle_t ui)
{
    static const char *const title[GAME_COUNT] = {
        "READ THE FIELD", "TURN · EAT · GROW",
        "MOVE · ROTATE · DROP", "FIND THE RHYTHM",
    };
    static const char *const lead[GAME_COUNT] = {
        "Open every safe tile. Keep ten mines covered.",
        "Read one move ahead. The trail never reverses.",
        "Drag sideways, tap to rotate, flick down to drop.",
        "Small, repeated taps make the cleanest line.",
    };
    static const char *const cue[GAME_COUNT] = {
        "TAP", "SWIPE TO TURN", "DRAG", "TAP · TAP · TAP",
    };
    static const char *const detail[GAME_COUNT] = {
        "OPEN A SAFE TILE", "EAT · GROW · NEVER REVERSE",
        "MOVE SIDEWAYS", "THREAD THE GREEN GAPS",
    };
    static const char *const steps[GAME_COUNT][3] = {
        {
            "Tap a tile to reveal it",
            "Hold for 360ms to place a flag",
            "Use the numbers to secure the field",
        },
        {
            "Swipe once to choose your start",
            "Queue up to two legal turns",
            "Eat fruit and protect your next lane",
        },
        {
            "Drag sideways to place",
            "Tap once to rotate",
            "Flick down to hard drop",
        },
        {
            "Tap repeatedly to climb",
            "Release briefly to descend",
            "Collect gems and pass gates to score",
        },
    };
    const int game = s_app.active_game;
    (void)esp_gsp_set_text(ui, GSP_BIND_ONBOARDING_TITLE, title[game]);
    (void)esp_gsp_set_text(ui, GSP_BIND_ONBOARDING_LEAD, lead[game]);
    (void)esp_gsp_set_text(ui, GSP_BIND_ONBOARDING_CUE, cue[game]);
    (void)esp_gsp_set_text(
        ui, GSP_BIND_ONBOARDING_CUE_DETAIL, detail[game]);
    (void)esp_gsp_set_text(
        ui, GSP_BIND_ONBOARDING_STEP_1, steps[game][0]);
    (void)esp_gsp_set_text(
        ui, GSP_BIND_ONBOARDING_STEP_2, steps[game][1]);
    (void)esp_gsp_set_text(
        ui, GSP_BIND_ONBOARDING_STEP_3, steps[game][2]);
    for (int index = 0; index < GAME_COUNT; ++index) {
        (void)esp_gsp_set_visible(
            ui, s_guide_icon_bind[index], index == game);
    }
}

static void render_snake_sprites(esp_gsp_handle_t ui)
{
    if (s_app.active_game == CG_GAME_SNAKE &&
            s_app.stage == CG_STAGE_GAME) {
        const cg_point_t head = s_app.snake.snake[0];
        const int head_center_x =
            ((head.x * 2 + 1) * CANVAS_W + CG_SNAKE_GRID) /
            (CG_SNAKE_GRID * 2);
        const int head_center_y =
            ((head.y * 2 + 1) * CANVAS_H + CG_SNAKE_GRID) /
            (CG_SNAKE_GRID * 2);
        const int food_center_x =
            ((s_app.snake.food.x * 2 + 1) * CANVAS_W + CG_SNAKE_GRID) /
            (CG_SNAKE_GRID * 2);
        const int food_center_y =
            ((s_app.snake.food.y * 2 + 1) * CANVAS_H + CG_SNAKE_GRID) /
            (CG_SNAKE_GRID * 2);
        set_sprite(
            ui, GSP_OBJ_KEY_SNAKE_HEAD, GSP_BIND_SNAKE_HEAD_VISIBLE,
            true, CANVAS_X + head_center_x - 18,
            CANVAS_Y + head_center_y - 18);
        set_sprite(
            ui, GSP_OBJ_KEY_SNAKE_FOOD, GSP_BIND_SNAKE_FOOD_VISIBLE,
            true, CANVAS_X + food_center_x - 23,
            CANVAS_Y + food_center_y - 23);
    } else {
        set_sprite(
            ui, GSP_OBJ_KEY_SNAKE_HEAD, GSP_BIND_SNAKE_HEAD_VISIBLE,
            false, 0, 0);
        set_sprite(
            ui, GSP_OBJ_KEY_SNAKE_FOOD, GSP_BIND_SNAKE_FOOD_VISIBLE,
            false, 0, 0);
    }
}

static void render_bird_sprites(esp_gsp_handle_t ui)
{
    if (s_app.active_game == CG_GAME_BIRD &&
            s_app.stage == CG_STAGE_GAME) {
        set_sprite(
            ui, GSP_OBJ_KEY_BIRD_PLAYER, GSP_BIND_BIRD_PLAYER_VISIBLE,
            true, CANVAS_X + round_float(CG_BIRD_X) - 57,
            CANVAS_Y + bird_render_y(s_app.bird.bird_y) - 38);
        for (size_t index = 0; index < CG_BIRD_PIPE_COUNT; ++index) {
            const cg_bird_pipe_t *pipe = &s_app.bird.pipes[index];
            const float gem_center =
                pipe->x + CG_BIRD_PIPE_WIDTH / 2.0f;
            const bool visible = !pipe->gem.collected &&
                gem_center > -26.0f &&
                gem_center < CANVAS_W + 26.0f;
            set_sprite(
                ui, s_bird_gem_object[index],
                s_bird_gem_bind[index],
                visible,
                CANVAS_X + round_float(gem_center) - 26,
                CANVAS_Y + bird_render_y(pipe->gem.y) - 26);
        }
    } else {
        set_sprite(
            ui, GSP_OBJ_KEY_BIRD_PLAYER, GSP_BIND_BIRD_PLAYER_VISIBLE,
            false, 0, 0);
        (void)esp_gsp_set_visible(
            ui, GSP_BIND_BIRD_GEM_0_VISIBLE, false);
        (void)esp_gsp_set_visible(
            ui, GSP_BIND_BIRD_GEM_1_VISIBLE, false);
        (void)esp_gsp_set_visible(
            ui, GSP_BIND_BIRD_GEM_2_VISIBLE, false);
    }
}

static void render_game_sprites(esp_gsp_handle_t ui)
{
    int flag_slot = 0;
    int mine_slot = 0;
    if (s_app.active_game == CG_GAME_MINES) {
        for (int index = 0; index < CG_MINES_CELLS; ++index) {
            const int column = index % CG_MINES_SIZE;
            const int row = index / CG_MINES_SIZE;
            const int x = CANVAS_X + 22 + column * 54 + 9;
            const int y = CANVAS_Y + row * 54 + 9;
            if (s_app.mines.flagged[index] &&
                    !s_app.mines.revealed[index] &&
                    flag_slot < CG_MINES_TOTAL) {
                set_sprite(
                    ui, s_mine_flag_object[flag_slot],
                    s_mine_flag_bind[flag_slot], true, x, y);
                ++flag_slot;
            }
            if (s_app.mines.revealed[index] && s_app.mines.mines[index] &&
                    mine_slot < CG_MINES_TOTAL) {
                set_sprite(
                    ui, s_mine_hit_object[mine_slot],
                    s_mine_hit_bind[mine_slot], true, x, y);
                ++mine_slot;
            }
        }
    }
    while (flag_slot < CG_MINES_TOTAL) {
        set_sprite(
            ui, s_mine_flag_object[flag_slot],
            s_mine_flag_bind[flag_slot], false, 0, 0);
        ++flag_slot;
    }
    while (mine_slot < CG_MINES_TOTAL) {
        set_sprite(
            ui, s_mine_hit_object[mine_slot],
            s_mine_hit_bind[mine_slot], false, 0, 0);
        ++mine_slot;
    }

    render_snake_sprites(ui);
    render_bird_sprites(ui);
}

static void render_hud(esp_gsp_handle_t ui)
{
    char text[64];
    switch (s_app.active_game) {
    case CG_GAME_MINES:
        snprintf(
            text, sizeof(text), "OPEN: %02u/54 · FLAGS: %02u/10",
            (unsigned)(s_app.mines.revealed_count -
                (s_app.mines.hit_index >= 0 ? s_app.mines.mine_count : 0U)),
            (unsigned)s_app.mines.flagged_count);
        break;
    case CG_GAME_SNAKE:
        snprintf(
            text, sizeof(text), "SCORE: %02u · LENGTH: %02u",
            (unsigned)s_app.snake.score, (unsigned)s_app.snake.length);
        break;
    case CG_GAME_BLOCKS:
        snprintf(
            text, sizeof(text), "SCORE: %03u · LINES: %02u",
            (unsigned)s_app.blocks.score, (unsigned)s_app.blocks.lines);
        break;
    case CG_GAME_BIRD:
        snprintf(
            text, sizeof(text), "SCORE: %02u",
            (unsigned)s_app.bird.score);
        break;
    default:
        text[0] = '\0';
        break;
    }
    (void)esp_gsp_set_text(ui, GSP_BIND_GAME_HUD_LABEL, text);
}

static const char *impact_label(void)
{
    switch (s_app.active_game) {
    case CG_GAME_MINES:
        return s_app.mines.won ? "FIELD CLEAR" : "MINE FOUND";
    case CG_GAME_SNAKE:
        return s_app.snake.reason == CG_REASON_SNAKE_SELF
            ? "COLLISION LOCKED · SELF HIT"
            : "COLLISION LOCKED · EDGE HIT";
    case CG_GAME_BLOCKS:
        return "COLLISION LOCKED · SPAWN BLOCKED";
    case CG_GAME_BIRD:
        return s_app.bird.reason == CG_REASON_BIRD_PIPE
            ? "COLLISION LOCKED · GATE HIT"
            : "COLLISION LOCKED · EDGE HIT";
    default:
        return "RUN COMPLETE";
    }
}

static void format_score(
    cg_game_id_t game, uint32_t value, char *buffer, size_t size)
{
    if (game == CG_GAME_MINES) {
        snprintf(
            buffer, size, "%02u:%02u",
            (unsigned)(value / 60U), (unsigned)(value % 60U));
    } else {
        snprintf(buffer, size, "%02u", (unsigned)value);
    }
}

static void render_result(esp_gsp_handle_t ui)
{
    const char *title = "";
    const char *reason = "";
    const char *score_label =
        s_app.active_game == CG_GAME_MINES ? "TIME" : "SCORE";
    const char *labels[3] = { "", "", "" };
    char values[3][24] = { "", "", "" };
    uint32_t score = 0;
    switch (s_app.active_game) {
    case CG_GAME_MINES:
        title = s_app.mines.won ? "FIELD CLEAR" : "MINE FOUND";
        reason = s_app.mines.won
            ? "All safe tiles secured" : "A mine ended the run";
        score = s_app.mines.elapsed;
        labels[0] = "MOVES";
        labels[1] = "FLAGS";
        labels[2] = "OPEN";
        snprintf(values[0], sizeof(values[0]), "%u",
                 (unsigned)s_app.mines.moves);
        snprintf(values[1], sizeof(values[1]), "%u/10",
                 (unsigned)s_app.mines.flagged_count);
        snprintf(
            values[2], sizeof(values[2]), "%u",
            (unsigned)(s_app.mines.revealed_count -
                (s_app.mines.hit_index >= 0 ? s_app.mines.mine_count : 0U)));
        break;
    case CG_GAME_SNAKE:
        title = "TRAIL ENDED";
        reason = s_app.snake.reason == CG_REASON_SNAKE_SELF
            ? "Crossed your own trail" : "Reached the arena edge";
        score = s_app.snake.score;
        labels[0] = "LENGTH";
        labels[1] = "TURNS";
        labels[2] = "SCORE";
        snprintf(values[0], sizeof(values[0]), "%u",
                 (unsigned)s_app.snake.length);
        snprintf(values[1], sizeof(values[1]), "%u",
                 (unsigned)s_app.snake.moves);
        snprintf(values[2], sizeof(values[2]), "%u",
                 (unsigned)s_app.snake.score);
        break;
    case CG_GAME_BLOCKS:
        title = "STACK FULL";
        reason = "No room for the next piece";
        score = s_app.blocks.score;
        labels[0] = "LINES";
        labels[1] = "COMBO";
        labels[2] = "PIECES";
        snprintf(values[0], sizeof(values[0]), "%u",
                 (unsigned)s_app.blocks.lines);
        snprintf(values[1], sizeof(values[1]), "×%u",
                 (unsigned)s_app.blocks.max_combo);
        snprintf(values[2], sizeof(values[2]), "%u",
                 (unsigned)s_app.blocks.piece_index);
        break;
    case CG_GAME_BIRD:
        title = "FLIGHT ENDED";
        reason = s_app.bird.reason == CG_REASON_BIRD_PIPE
            ? "A gate ended the flight" :
            s_app.bird.reason == CG_REASON_BIRD_UPPER_EDGE
                ? "Reached the upper edge" : "Reached the lower edge";
        score = s_app.bird.score;
        labels[0] = "FLAPS";
        labels[1] = "GATES";
        labels[2] = "GEMS";
        snprintf(values[0], sizeof(values[0]), "%u",
                 (unsigned)s_app.bird.inputs);
        snprintf(values[1], sizeof(values[1]), "%u",
                 (unsigned)s_app.bird.gates_passed);
        snprintf(values[2], sizeof(values[2]), "%u",
                 (unsigned)s_app.bird.gems_collected);
        break;
    default:
        break;
    }
    char text[32];
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_TITLE, title);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_REASON, reason);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_SCORE_LABEL, score_label);
    format_score(s_app.active_game, score, text, sizeof(text));
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_SCORE, text);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_1_LABEL, labels[0]);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_1_VALUE, values[0]);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_2_LABEL, labels[1]);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_2_VALUE, values[1]);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_3_LABEL, labels[2]);
    (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_METRIC_3_VALUE, values[2]);
    (void)esp_gsp_set_text(
        ui, GSP_BIND_RESULT_BEST_LABEL,
        s_app.new_best ? "PREVIOUS BEST" :
        s_app.active_game == CG_GAME_MINES ? "BEST TIME" : "HIGH SCORE");
    if ((s_app.new_best && s_app.previous_best == 0U) ||
            (!s_app.new_best &&
             !s_app.records.has[s_app.active_game])) {
        (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_BEST, "—");
    } else {
        const uint32_t best = s_app.new_best
            ? s_app.previous_best
            : s_app.records.value[s_app.active_game];
        format_score(s_app.active_game, best, text, sizeof(text));
        (void)esp_gsp_set_text(ui, GSP_BIND_RESULT_BEST, text);
    }
}

static void render(esp_gsp_handle_t ui, bool full_canvas)
{
    const bool home = s_app.stage == CG_STAGE_HOME;
    const bool onboarding = s_app.stage == CG_STAGE_ONBOARDING;
    const bool game = s_app.stage == CG_STAGE_GAME;
    const cg_phase_t phase = active_phase();
    const bool paused = game && phase == CG_PHASE_PAUSED;
    const bool result =
        game && phase == CG_PHASE_OVER && s_app.result_ready;
    if (game && full_canvas) {
        publish_canvas_snapshot();
        (void)esp_gsp_canvas_invalidate(
            ui, GSP_BIND_GAME_CANVAS);
        (void)esp_gsp_flush(ui, 1000U);
    }
    (void)esp_gsp_set_visible(ui, GSP_BIND_HOME_VISIBLE, home);
    (void)esp_gsp_set_visible(
        ui, GSP_BIND_ONBOARDING_VISIBLE, onboarding);
    (void)esp_gsp_set_visible(ui, GSP_BIND_GAME_VISIBLE, game);
    (void)esp_gsp_set_visible(
        ui, GSP_BIND_PAUSE_OVERLAY_VISIBLE, paused);
    (void)esp_gsp_set_visible(
        ui, GSP_BIND_RESUME_GO_VISIBLE, game && s_app.resume_guard);
    (void)esp_gsp_set_visible(
        ui, GSP_BIND_GAME_IMPACT_VISIBLE, game && s_app.impact);
    (void)esp_gsp_set_visible(ui, GSP_BIND_RESULT_VISIBLE, result);
    (void)esp_gsp_set_visible(
        ui, GSP_BIND_GAME_READY_CUE_VISIBLE,
        game && phase == CG_PHASE_READY && !s_app.impact);
    if (home) render_home(ui);
    if (onboarding) render_onboarding(ui);
    if (game) {
        static const char *const ready_cue[GAME_COUNT] = {
            "TAP TO OPEN · FIRST TILE SAFE", "SWIPE TO START",
            "DRAG · TAP · FLICK", "TAP TO FLAP",
        };
        render_hud(ui);
        render_game_sprites(ui);
        (void)esp_gsp_set_text(
            ui, GSP_BIND_GAME_READY_CUE,
            ready_cue[s_app.active_game]);
        (void)esp_gsp_set_text(
            ui, GSP_BIND_GAME_IMPACT_LABEL, impact_label());
        if (paused) {
            (void)esp_gsp_set_text(
                ui, GSP_BIND_PAUSE_TITLE,
                s_app.restart_confirm ? "RESTART?" : "PAUSED");
            (void)esp_gsp_set_text(
                ui, GSP_BIND_PAUSE_DESCRIPTION,
                s_app.restart_confirm
                    ? "This run will be cleared"
                    : "Your position is safe");
            (void)esp_gsp_set_visible(
                ui, GSP_BIND_PAUSE_RESUME_VISIBLE,
                !s_app.restart_confirm);
            (void)esp_gsp_set_visible(
                ui, GSP_BIND_PAUSE_RESTART_VISIBLE,
                !s_app.restart_confirm);
            (void)esp_gsp_set_visible(
                ui, GSP_BIND_PAUSE_MENU_VISIBLE,
                !s_app.restart_confirm);
            (void)esp_gsp_set_visible(
                ui, GSP_BIND_PAUSE_CONFIRM_RESTART_VISIBLE,
                s_app.restart_confirm);
            (void)esp_gsp_set_visible(
                ui, GSP_BIND_PAUSE_CANCEL_RESTART_VISIBLE,
                s_app.restart_confirm);
        }
        if (result) render_result(ui);
        if (!full_canvas) {
            invalidate_canvas(ui, false);
        }
    }
}

static bool record_improved(void)
{
    const bool has = s_app.records.has[s_app.active_game];
    const uint32_t previous = s_app.records.value[s_app.active_game];
    switch (s_app.active_game) {
    case CG_GAME_MINES:
        return cg_mines_record_improved(
            has, (uint16_t)previous, &s_app.mines);
    case CG_GAME_SNAKE:
        return cg_score_record_improved(
            has, previous, s_app.snake.phase, s_app.snake.score);
    case CG_GAME_BLOCKS:
        return cg_score_record_improved(
            has, previous, s_app.blocks.phase, s_app.blocks.score);
    case CG_GAME_BIRD:
        return cg_score_record_improved(
            has, previous, s_app.bird.phase, s_app.bird.score);
    default:
        return false;
    }
}

static uint32_t run_score(void)
{
    switch (s_app.active_game) {
    case CG_GAME_MINES: return s_app.mines.elapsed;
    case CG_GAME_SNAKE: return s_app.snake.score;
    case CG_GAME_BLOCKS: return s_app.blocks.score;
    case CG_GAME_BIRD: return s_app.bird.score;
    default: return 0;
    }
}

static void handle_game_over(void)
{
    if (active_phase() != CG_PHASE_OVER || s_app.impact ||
            s_app.result_ready) {
        return;
    }
    s_app.previous_best = s_app.records.has[s_app.active_game]
        ? s_app.records.value[s_app.active_game] : 0U;
    s_app.new_best = record_improved();
    if (s_app.new_best) {
        s_app.records.has[s_app.active_game] = true;
        s_app.records.value[s_app.active_game] = run_score();
        save_preferences();
    }
    s_app.impact = true;
    s_app.impact_until_ms = s_app.elapsed_ms + IMPACT_HOLD_MS;
    clear_pointer();
    emit_feedback(18U, CG_EVENT_CRASH);
}

static void reset_active_game(void)
{
    clear_pointer();
    init_game(s_app.active_game);
    s_app.resume_guard = false;
    s_app.impact = false;
    s_app.result_ready = false;
    s_app.restart_confirm = false;
    s_app.new_best = false;
}

static void open_home(void)
{
    clear_pointer();
    s_app.stage = CG_STAGE_HOME;
    s_app.resume_guard = false;
    s_app.impact = false;
    s_app.restart_confirm = false;
    s_app.new_best = false;
}

static void pause_active_game(void)
{
    if (active_phase() == CG_PHASE_PLAYING) {
        set_active_phase(CG_PHASE_PAUSED);
        clear_pointer();
        s_app.resume_guard = false;
        s_app.restart_confirm = false;
        emit_feedback(10U, CG_EVENT_STEP);
    }
}

static void resume_active_game(void)
{
    if (active_phase() != CG_PHASE_PAUSED) return;
    switch (s_app.active_game) {
    case CG_GAME_MINES:
        (void)cg_mines_start(&s_app.mines);
        break;
    case CG_GAME_SNAKE:
        (void)cg_snake_start(&s_app.snake);
        s_app.snake_acc_ms = 0;
        break;
    case CG_GAME_BLOCKS:
        (void)cg_blocks_start(&s_app.blocks);
        break;
    case CG_GAME_BIRD:
        (void)cg_bird_start(&s_app.bird);
        s_app.bird_render_acc_ms = 0;
        s_app.last_bird_input_ms =
            s_app.elapsed_ms - BIRD_REPEAT_GUARD_MS;
        break;
    default:
        break;
    }
    s_app.restart_confirm = false;
    s_app.resume_guard = s_app.active_game != CG_GAME_MINES;
    if (s_app.resume_guard) {
        s_app.resume_until_ms = s_app.elapsed_ms + RESUME_GUARD_MS;
    }
    clear_pointer();
}

static bool app_back(esp_gsp_handle_t ui, int64_t timestamp_us)
{
    (void)timestamp_us;
    if (s_app.stage == CG_STAGE_HOME) {
        return false;
    }
    if (s_app.stage == CG_STAGE_ONBOARDING) {
        open_home();
        render(ui, false);
        return true;
    }
    if (active_phase() == CG_PHASE_PLAYING || s_app.resume_guard) {
        pause_active_game();
        render(ui, false);
        return true;
    }
    if (active_phase() == CG_PHASE_OVER) {
        reset_active_game();
    }
    open_home();
    render(ui, false);
    return true;
}

static void open_game(esp_gsp_handle_t ui, cg_game_id_t game)
{
    s_app.active_game = game;
    if (active_phase() == CG_PHASE_OVER) {
        reset_active_game();
    }
    s_app.resume_guard = false;
    s_app.impact = false;
    s_app.result_ready = false;
    s_app.restart_confirm = false;
    s_app.new_best = false;
    clear_pointer();
    if ((s_app.onboarded_mask & (uint8_t)(1U << game)) == 0U &&
            active_phase() == CG_PHASE_READY) {
        s_app.stage = CG_STAGE_ONBOARDING;
    } else {
        s_app.stage = CG_STAGE_GAME;
    }
    render(ui, true);
}

static void on_call(esp_gsp_handle_t ui, uint16_t action_id)
{
    if (action_id == GSP_ACT_ID_OPEN_MINES) {
        open_game(ui, CG_GAME_MINES);
    } else if (action_id == GSP_ACT_ID_OPEN_SNAKE) {
        open_game(ui, CG_GAME_SNAKE);
    } else if (action_id == GSP_ACT_ID_OPEN_BLOCKS) {
        open_game(ui, CG_GAME_BLOCKS);
    } else if (action_id == GSP_ACT_ID_OPEN_BIRD) {
        open_game(ui, CG_GAME_BIRD);
    } else if (action_id == GSP_ACT_ID_ONBOARDING_BACK) {
        open_home();
        render(ui, false);
    } else if (action_id == GSP_ACT_ID_ONBOARDING_PLAY) {
        s_app.onboarded_mask |= (uint8_t)(1U << s_app.active_game);
        save_preferences();
        reset_active_game();
        s_app.stage = CG_STAGE_GAME;
        render(ui, true);
    } else if (action_id == GSP_ACT_ID_GAME_BACK) {
        (void)app_back(ui, 0);
    } else if (action_id == GSP_ACT_ID_GAME_PAUSE) {
        pause_active_game();
        render(ui, false);
    } else if (action_id == GSP_ACT_ID_PAUSE_RESUME) {
        resume_active_game();
        render(ui, false);
    } else if (action_id == GSP_ACT_ID_PAUSE_RESTART) {
        if (active_phase() == CG_PHASE_PAUSED) {
            s_app.restart_confirm = true;
            s_app.restart_until_ms =
                s_app.elapsed_ms + RESTART_CONFIRM_MS;
            render(ui, false);
        }
    } else if (action_id == GSP_ACT_ID_PAUSE_MENU) {
        open_home();
        render(ui, false);
    } else if (action_id == GSP_ACT_ID_PAUSE_CONFIRM_RESTART) {
        reset_active_game();
        render(ui, true);
    } else if (action_id == GSP_ACT_ID_PAUSE_CANCEL_RESTART) {
        s_app.restart_confirm = false;
        render(ui, false);
    } else if (action_id == GSP_ACT_ID_RESULT_RETRY) {
        reset_active_game();
        render(ui, true);
    } else if (action_id == GSP_ACT_ID_RESULT_MENU) {
        reset_active_game();
        open_home();
        render(ui, false);
    }
}

static bool dominant_axis(
    int delta_x, int delta_y, int minimum, gesture_axis_t *axis)
{
    const int horizontal = abs_int(delta_x);
    const int vertical = abs_int(delta_y);
    if ((horizontal > vertical ? horizontal : vertical) < minimum) {
        return false;
    }
    if (horizontal * 4 >= vertical * 5) {
        *axis = AXIS_X;
        return true;
    }
    if (vertical * 4 >= horizontal * 5) {
        *axis = AXIS_Y;
        return true;
    }
    return false;
}

static cg_direction_t direction_from_delta(int delta_x, int delta_y)
{
    if (abs_int(delta_x) > abs_int(delta_y)) {
        return delta_x > 0 ? CG_DIR_RIGHT : CG_DIR_LEFT;
    }
    return delta_y > 0 ? CG_DIR_DOWN : CG_DIR_UP;
}

static void steer_snake(
    esp_gsp_handle_t ui, int delta_x, int delta_y)
{
    if (s_app.resume_guard &&
            s_app.snake.direction_queue_length >= 1U) {
        return;
    }
    if (cg_snake_set_direction(
            &s_app.snake, direction_from_delta(delta_x, delta_y))) {
        emit_feedback(8U, CG_EVENT_TURN);
        render(ui, false);
    }
}

static int mine_index_at(int x, int y)
{
    const int local_x = x - (CANVAS_X + 22);
    const int local_y = y - CANVAS_Y;
    if (local_x < 0 || local_y < 0 ||
            local_x >= 428 || local_y >= 428) {
        return -1;
    }
    if (local_x % 54 >= 50 || local_y % 54 >= 50) {
        return -1;
    }
    return (local_y / 54) * CG_MINES_SIZE + local_x / 54;
}

static void start_pointer(int x, int y)
{
    clear_pointer();
    s_app.pointer.active = true;
    s_app.pointer.x = x;
    s_app.pointer.y = y;
    s_app.pointer.last_x = x;
    s_app.pointer.last_y = y;
    s_app.pointer.started_ms = s_app.elapsed_ms;
}

static void pointer_down(esp_gsp_handle_t ui, int x, int y)
{
    if (s_app.stage != CG_STAGE_GAME ||
            y < CANVAS_Y || y >= INPUT_BOTTOM ||
            active_phase() == CG_PHASE_PAUSED ||
            active_phase() == CG_PHASE_OVER) {
        return;
    }
    if (s_app.pointer.active) return;
    start_pointer(x, y);
    if (s_app.active_game == CG_GAME_MINES) {
        s_app.pointer.kind = POINTER_MINE;
        s_app.pointer.mine_index = mine_index_at(x, y);
        if (s_app.pointer.mine_index < 0) clear_pointer();
    } else if (s_app.active_game == CG_GAME_SNAKE) {
        s_app.pointer.kind = POINTER_SNAKE;
    } else if (s_app.active_game == CG_GAME_BLOCKS) {
        s_app.pointer.kind = POINTER_BLOCKS;
    } else {
        s_app.pointer.kind = POINTER_BIRD;
        if (s_app.resume_guard ||
                s_app.elapsed_ms - s_app.last_bird_input_ms <
                    BIRD_REPEAT_GUARD_MS) {
            clear_pointer();
            return;
        }
        s_app.last_bird_input_ms = s_app.elapsed_ms;
        if (cg_bird_flap(&s_app.bird)) {
            emit_feedback(6U, CG_EVENT_FLAP);
            render(ui, false);
            handle_game_over();
        }
    }
}

static void pointer_move(esp_gsp_handle_t ui, int x, int y)
{
    cg_pointer_t *pointer = &s_app.pointer;
    if (!pointer->active) return;
    const int delta_x = x - pointer->x;
    const int delta_y = y - pointer->y;
    const int distance =
        abs_int(delta_x) > abs_int(delta_y)
            ? abs_int(delta_x) : abs_int(delta_y);
    if (distance > pointer->max_distance) {
        pointer->max_distance = distance;
    }
    if (delta_y > pointer->max_downward) {
        pointer->max_downward = delta_y;
    }
    if (pointer->kind == POINTER_MINE) {
        if (distance >= TAP_SLOP_PX) pointer->cancelled = true;
        return;
    }
    if (pointer->kind == POINTER_SNAKE && !pointer->applied) {
        gesture_axis_t axis;
        if (distance >= SNAKE_COMMIT_PX &&
                dominant_axis(
                    delta_x, delta_y, SNAKE_COMMIT_PX, &axis)) {
            (void)axis;
            pointer->applied = true;
            steer_snake(ui, delta_x, delta_y);
        }
        return;
    }
    if (pointer->kind != POINTER_BLOCKS) return;
    if (pointer->axis == AXIS_NONE) {
        (void)dominant_axis(
            delta_x, delta_y, AXIS_LOCK_PX, &pointer->axis);
    }
    if (pointer->axis == AXIS_X) {
        const int step = (int)((float)(x - pointer->last_x) /
                               BLOCK_COLUMN_PX);
        if (step != 0) {
            pointer->applied = true;
            pointer->last_x += round_float((float)step * BLOCK_COLUMN_PX);
            if (cg_blocks_move(&s_app.blocks, step)) {
                emit_feedback(5U, CG_EVENT_MOVE);
                render(ui, false);
            }
        }
    } else if (pointer->axis == AXIS_Y && delta_y > 0 &&
            !s_app.resume_guard &&
            s_app.elapsed_ms - pointer->started_ms > BLOCK_FLICK_MS) {
        const int step = (int)((float)(y - pointer->last_y) /
                               BLOCK_COLUMN_PX);
        if (step > 0) {
            bool changed = false;
            for (int index = 0; index < step; ++index) {
                changed = cg_blocks_soft_drop(&s_app.blocks) || changed;
            }
            pointer->applied = true;
            pointer->soft_dropped = true;
            pointer->last_y += round_float((float)step * BLOCK_COLUMN_PX);
            if (changed) {
                render(ui, false);
                handle_game_over();
            }
        }
    }
}

static void pointer_up(esp_gsp_handle_t ui, int x, int y)
{
    cg_pointer_t pointer = s_app.pointer;
    clear_pointer();
    if (!pointer.active) return;
    const int delta_x = x - pointer.x;
    const int delta_y = y - pointer.y;
    const int distance =
        abs_int(delta_x) > abs_int(delta_y)
            ? abs_int(delta_x) : abs_int(delta_y);
    if (y < CANVAS_Y || y >= INPUT_BOTTOM) return;
    if (pointer.kind == POINTER_MINE) {
        if (!pointer.cancelled && !pointer.long_pressed &&
                mine_index_at(x, y) == pointer.mine_index) {
            if (cg_mines_reveal(
                    &s_app.mines, pointer.mine_index)) {
                emit_feedback(7U, CG_EVENT_REVEAL);
                render(ui, false);
                handle_game_over();
            }
        }
    } else if (pointer.kind == POINTER_SNAKE && !pointer.applied) {
        gesture_axis_t axis;
        if (distance >= SNAKE_COMMIT_PX &&
                dominant_axis(
                    delta_x, delta_y, SNAKE_COMMIT_PX, &axis)) {
            (void)axis;
            steer_snake(ui, delta_x, delta_y);
        }
    } else if (pointer.kind == POINTER_BLOCKS) {
        gesture_axis_t axis = pointer.axis;
        if (axis == AXIS_NONE) {
            (void)dominant_axis(
                delta_x, delta_y, AXIS_LOCK_PX, &axis);
        }
        const uint32_t elapsed = s_app.elapsed_ms - pointer.started_ms;
        const int downward_distance =
            delta_y > pointer.max_downward
                ? delta_y : pointer.max_downward;
        const bool downward =
            !pointer.soft_dropped &&
            (downward_distance >= BLOCK_DROP_PX ||
             (downward_distance >= BLOCK_FLICK_PX &&
              elapsed <= BLOCK_FLICK_MS));
        if (axis == AXIS_Y && downward) {
            if (!s_app.resume_guard &&
                    cg_blocks_hard_drop(&s_app.blocks)) {
                emit_feedback(12U, CG_EVENT_HARD_DROP);
                render(ui, false);
                handle_game_over();
            }
        } else if (axis == AXIS_NONE && !pointer.applied &&
                distance < TAP_SLOP_PX &&
                pointer.max_distance < TAP_SLOP_PX) {
            if (cg_blocks_rotate(&s_app.blocks)) {
                emit_feedback(7U, CG_EVENT_ROTATE);
                render(ui, false);
            }
        }
    }
    if (active_phase() == CG_PHASE_OVER) {
        render(ui, false);
    }
}

static void on_pointer(
    esp_gsp_handle_t ui, const mosaic_event_t *event)
{
    const int x = event->data.pointer.x;
    const int y = event->data.pointer.y;
    if (event->data.pointer.pressed) {
        if (s_app.pointer.active) {
            pointer_move(ui, x, y);
        } else {
            pointer_down(ui, x, y);
        }
    } else {
        pointer_up(ui, x, y);
    }
}

static uint32_t timer_delta_ms(int64_t timestamp_us)
{
    uint64_t elapsed_us = 16000U;
    if (s_app.last_tick_us > 0 && timestamp_us > s_app.last_tick_us) {
        elapsed_us = (uint64_t)(timestamp_us - s_app.last_tick_us) +
            s_app.tick_remainder_us;
    }
    if (elapsed_us > 120000U) {
        elapsed_us = 120000U;
    }
    const uint32_t delta_ms = (uint32_t)(elapsed_us / 1000U);
    s_app.tick_remainder_us = (uint32_t)(elapsed_us % 1000U);
    s_app.last_tick_us = timestamp_us;
    return delta_ms;
}

static bool blocks_visual_changed(
    const cg_blocks_t *before, const cg_blocks_t *after)
{
    return before->phase != after->phase ||
        before->piece.kind != after->piece.kind ||
        before->piece.rotation != after->piece.rotation ||
        before->piece.x != after->piece.x ||
        before->piece.y != after->piece.y ||
        before->piece_index != after->piece_index ||
        before->score != after->score ||
        before->lines != after->lines;
}

static void timer_step(esp_gsp_handle_t ui, int64_t timestamp_us)
{
    const uint32_t delta_ms = timer_delta_ms(timestamp_us);
    s_app.elapsed_ms += delta_ms;
    bool redraw = false;
    bool snake_frame = false;
    if (s_app.restart_confirm &&
            deadline_reached(
                s_app.elapsed_ms, s_app.restart_until_ms)) {
        s_app.restart_confirm = false;
        redraw = true;
    }
    if (s_app.resume_guard &&
            deadline_reached(
                s_app.elapsed_ms, s_app.resume_until_ms)) {
        s_app.resume_guard = false;
        redraw = true;
        emit_feedback(8U, CG_EVENT_START);
    }
    if (s_app.impact &&
            deadline_reached(
                s_app.elapsed_ms, s_app.impact_until_ms)) {
        s_app.impact = false;
        s_app.result_ready = true;
        redraw = true;
    }
    if (s_app.pointer.active &&
            s_app.pointer.kind == POINTER_MINE &&
            !s_app.pointer.cancelled &&
            !s_app.pointer.long_pressed &&
            s_app.elapsed_ms - s_app.pointer.started_ms >=
                MINE_HOLD_MS &&
            !s_app.mines.revealed[s_app.pointer.mine_index]) {
        bool changed;
        if (s_app.mines.first_index < 0) {
            changed = false;
        } else {
            changed = cg_mines_toggle_flag(
                &s_app.mines, s_app.pointer.mine_index);
        }
        s_app.pointer.long_pressed = true;
        if (changed) {
            emit_feedback(12U, CG_EVENT_FLAG);
            render(ui, false);
            handle_game_over();
        }
    }
    if (s_app.stage == CG_STAGE_GAME && !s_app.resume_guard &&
            active_phase() == CG_PHASE_PLAYING) {
        switch (s_app.active_game) {
        case CG_GAME_MINES:
            s_app.mines_acc_ms += delta_ms;
            while (s_app.mines_acc_ms >= 1000U) {
                s_app.mines_acc_ms -= 1000U;
                cg_mines_step_clock(&s_app.mines);
                redraw = true;
            }
            break;
        case CG_GAME_SNAKE:
            s_app.snake_acc_ms += delta_ms;
            if (s_app.snake.phase == CG_PHASE_PLAYING &&
                    s_app.snake_acc_ms >=
                        cg_snake_step_ms(&s_app.snake)) {
                s_app.snake_acc_ms %=
                    cg_snake_step_ms(&s_app.snake);
                (void)cg_snake_step(&s_app.snake);
                if (s_app.snake.phase == CG_PHASE_OVER ||
                        s_app.snake.last_event == CG_EVENT_EAT) {
                    redraw = true;
                } else {
                    snake_frame = true;
                }
            }
            break;
        case CG_GAME_BLOCKS: {
            const cg_blocks_t before = s_app.blocks;
            (void)cg_blocks_step(&s_app.blocks, delta_ms);
            redraw = blocks_visual_changed(
                &before, &s_app.blocks);
            break;
        }
        case CG_GAME_BIRD: {
            const uint32_t previous_event_id = s_app.bird.event_id;
            (void)cg_bird_step(&s_app.bird, delta_ms);
            render_bird_sprites(ui);
            s_app.bird_render_acc_ms += delta_ms;
            if (s_app.bird_render_acc_ms >= CG_BIRD_STEP_MS ||
                    s_app.bird.phase == CG_PHASE_OVER ||
                    s_app.bird.event_id != previous_event_id) {
                s_app.bird_render_acc_ms %= CG_BIRD_STEP_MS;
                redraw = true;
            }
            break;
        }
        default:
            break;
        }
        handle_game_over();
    }
    if (redraw) {
        render(ui, false);
    } else if (snake_frame) {
        render_snake_sprites(ui);
        invalidate_canvas(ui, false);
    }
}

static void classic_games_started(esp_gsp_handle_t ui)
{
    if (s_initialized) {
        clear_pointer();
        publish_canvas_snapshot();
        (void)ui;
        return;
    }
    memset(&s_app, 0, sizeof(s_app));
    atomic_init(&s_canvas.sequence, 0U);
    memset(&s_canvas.snapshot, 0, sizeof(s_canvas.snapshot));
    s_app.stage = CG_STAGE_HOME;
    s_app.active_game = CG_GAME_MINES;
    s_app.seed_cursor = UINT32_C(0x4347414d);
    s_app.last_bird_input_ms = UINT32_MAX - BIRD_REPEAT_GUARD_MS;
    load_preferences();
    for (int game = 0; game < GAME_COUNT; ++game) {
        init_game((cg_game_id_t)game);
    }
    s_initialized = true;
    publish_canvas_snapshot();
    (void)ui;
}

static void classic_games_stopping(esp_gsp_handle_t ui)
{
    pause_active_game();
    save_preferences();
    (void)esp_gsp_canvas_stop(ui, GSP_BIND_GAME_CANVAS);
    (void)esp_gsp_flush(ui, 1000U);
}

static void classic_games_event(
    esp_gsp_handle_t ui, const struct mosaic_event *raw_event)
{
    const mosaic_event_t *event = raw_event;
    if (event == NULL) return;
    switch (event->type) {
    case MOSAIC_EVENT_START:
        s_app.last_tick_us = event->timestamp_us;
        s_app.tick_remainder_us = 0U;
        (void)esp_gsp_canvas_set_draw_cb(
            ui, GSP_BIND_GAME_CANVAS, canvas_draw, &s_canvas);
        render(ui, true);
        break;
    case MOSAIC_EVENT_SCENE_CHANGED:
        render(ui, true);
        break;
    case MOSAIC_EVENT_TIMER:
        timer_step(ui, event->timestamp_us);
        break;
    case MOSAIC_EVENT_POINTER:
        on_pointer(ui, event);
        break;
    case MOSAIC_EVENT_UI_CALL:
        on_call(ui, event->data.call.action_id);
        break;
    case MOSAIC_EVENT_STOP:
    case MOSAIC_EVENT_MODEL_CHANGED:
    default:
        break;
    }
}

const mosaic_app_descriptor_t mosaic_classic_games_app = {
    .id = CLASSIC_GAMES_APP_ID,
    .launch_action = GSP_ACT_ID_APP_CLASSIC_GAMES,
    .back_action = MOSAIC_APP_SHELL_BACK_ACTION,
    .name = "classic_games",
    .title = "Classic Games",
    .directory = &gsp_obj_directory_classic_games,
    .disable_swipe = true,
    .root_header_in_stack = true,
    .dynamic_image_slots = 1,
    .on_started = classic_games_started,
    .on_stopping = classic_games_stopping,
    .on_event = classic_games_event,
    .on_back = app_back,
};

