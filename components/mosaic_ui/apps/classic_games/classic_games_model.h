/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CG_PHASE_READY = 0,
    CG_PHASE_PLAYING,
    CG_PHASE_PAUSED,
    CG_PHASE_OVER,
} cg_phase_t;

typedef enum {
    CG_EVENT_READY = 0,
    CG_EVENT_START,
    CG_EVENT_REVEAL,
    CG_EVENT_FLAG,
    CG_EVENT_TURN,
    CG_EVENT_STEP,
    CG_EVENT_EAT,
    CG_EVENT_CRASH,
    CG_EVENT_MOVE,
    CG_EVENT_ROTATE,
    CG_EVENT_BLOCKED,
    CG_EVENT_SOFT_DROP,
    CG_EVENT_FALL,
    CG_EVENT_LOCK,
    CG_EVENT_CLEAR,
    CG_EVENT_HARD_DROP,
    CG_EVENT_FLAP,
    CG_EVENT_PASS,
    CG_EVENT_GEM,
} cg_event_t;

typedef enum {
    CG_REASON_NONE = 0,
    CG_REASON_MINES_CLEARED,
    CG_REASON_MINE_HIT,
    CG_REASON_SNAKE_WALL,
    CG_REASON_SNAKE_SELF,
    CG_REASON_BLOCKS_SPAWN_BLOCKED,
    CG_REASON_BIRD_UPPER_EDGE,
    CG_REASON_BIRD_LOWER_EDGE,
    CG_REASON_BIRD_PIPE,
} cg_reason_t;

/* Mines ------------------------------------------------------------------ */

#define CG_MINES_SIZE 8
#define CG_MINES_CELLS (CG_MINES_SIZE * CG_MINES_SIZE)
#define CG_MINES_TOTAL 10
#define CG_MINES_MAX_ELAPSED 999

typedef struct {
    cg_phase_t phase;
    cg_event_t last_event;
    cg_reason_t reason;
    uint32_t seed;
    bool mines[CG_MINES_CELLS];
    bool revealed[CG_MINES_CELLS];
    bool flagged[CG_MINES_CELLS];
    int8_t first_index;
    int8_t hit_index;
    uint16_t elapsed;
    uint16_t moves;
    uint8_t mine_count;
    uint8_t revealed_count;
    uint8_t flagged_count;
    bool flag_mode;
    bool won;
} cg_mines_t;

void cg_mines_init(cg_mines_t *state, uint32_t seed);
bool cg_mines_start(cg_mines_t *state);
bool cg_mines_toggle_flag_mode(cg_mines_t *state);
bool cg_mines_toggle_flag(cg_mines_t *state, int index);
bool cg_mines_reveal(cg_mines_t *state, int index);
void cg_mines_step_clock(cg_mines_t *state);
uint8_t cg_mines_neighbor_count(const cg_mines_t *state, int index);
size_t cg_mines_neighbor_indexes(int index, uint8_t out_indexes[8]);
bool cg_mines_is_basic_logic_solvable(const cg_mines_t *state);
bool cg_mines_record_improved(bool has_record,
                              uint16_t previous_elapsed,
                              const cg_mines_t *state);

/* Snake ------------------------------------------------------------------ */

#define CG_SNAKE_GRID 14
#define CG_SNAKE_CAPACITY (CG_SNAKE_GRID * CG_SNAKE_GRID)
#define CG_SNAKE_INITIAL_LENGTH 3
#define CG_SNAKE_STEP_MS 285
#define CG_SNAKE_MIN_STEP_MS 160
#define CG_SNAKE_TURN_BUFFER 2

typedef struct {
    int8_t x;
    int8_t y;
} cg_point_t;

typedef enum {
    CG_DIR_UP = 0,
    CG_DIR_RIGHT,
    CG_DIR_DOWN,
    CG_DIR_LEFT,
} cg_direction_t;

typedef struct {
    cg_phase_t phase;
    cg_event_t last_event;
    cg_reason_t reason;
    cg_point_t snake[CG_SNAKE_CAPACITY];
    uint16_t length;
    cg_direction_t direction;
    cg_direction_t direction_queue[CG_SNAKE_TURN_BUFFER];
    uint8_t direction_queue_length;
    cg_point_t food;
    uint16_t food_index;
    uint32_t score;
    uint32_t moves;
    uint32_t event_id;
    cg_point_t last_food;
    cg_point_t crash_cell;
    bool has_last_food;
    bool has_crash_cell;
} cg_snake_t;

void cg_snake_init(cg_snake_t *state);
bool cg_snake_start(cg_snake_t *state);
bool cg_snake_set_direction(cg_snake_t *state, cg_direction_t direction);
bool cg_snake_step(cg_snake_t *state);
uint16_t cg_snake_step_ms(const cg_snake_t *state);
bool cg_score_record_improved(bool has_record,
                              uint32_t previous_score,
                              cg_phase_t phase,
                              uint32_t score);

/* Blocks ----------------------------------------------------------------- */

#define CG_BLOCK_COLUMNS 12
#define CG_BLOCK_ROWS 14
#define CG_BLOCK_CELLS (CG_BLOCK_COLUMNS * CG_BLOCK_ROWS)
#define CG_BLOCK_STEP_MS 32
#define CG_BLOCK_LOCK_MS 320
#define CG_BLOCK_LOCK_RESET_MAX 8
#define CG_BLOCK_PIECE_CELLS 4

typedef enum {
    CG_BLOCK_I = 0,
    CG_BLOCK_O,
    CG_BLOCK_T,
    CG_BLOCK_L,
    CG_BLOCK_J,
    CG_BLOCK_S,
    CG_BLOCK_Z,
    CG_BLOCK_KIND_COUNT,
    CG_BLOCK_EMPTY = 0xff,
} cg_block_kind_t;

typedef struct {
    cg_block_kind_t kind;
    uint8_t rotation;
    int8_t x;
    int8_t y;
} cg_block_piece_t;

typedef struct {
    cg_phase_t phase;
    cg_event_t last_event;
    cg_reason_t reason;
    uint32_t seed;
    uint8_t fixed[CG_BLOCK_CELLS];
    cg_block_piece_t piece;
    uint32_t piece_index;
    uint32_t score;
    uint16_t lines;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t fall_ms;
    uint16_t lock_ms;
    uint8_t lock_resets;
    uint32_t moves;
    uint32_t event_id;
    uint8_t cleared_rows[CG_BLOCK_ROWS];
    uint8_t cleared_row_count;
} cg_blocks_t;

void cg_blocks_init(cg_blocks_t *state, uint32_t seed);
bool cg_blocks_start(cg_blocks_t *state);
cg_block_kind_t cg_blocks_kind_at(uint32_t seed, uint32_t index);
cg_block_kind_t cg_blocks_next_kind(const cg_blocks_t *state);
size_t cg_blocks_piece_cells(const cg_block_piece_t *piece,
                             cg_point_t out_cells[CG_BLOCK_PIECE_CELLS]);
bool cg_blocks_move(cg_blocks_t *state, int delta);
bool cg_blocks_rotate(cg_blocks_t *state);
bool cg_blocks_soft_drop(cg_blocks_t *state);
bool cg_blocks_hard_drop(cg_blocks_t *state);
bool cg_blocks_step(cg_blocks_t *state, uint32_t delta_ms);
uint16_t cg_blocks_fall_ms(const cg_blocks_t *state);
void cg_blocks_ghost_cells(const cg_blocks_t *state,
                           cg_point_t out_cells[CG_BLOCK_PIECE_CELLS]);

/* Bird ------------------------------------------------------------------- */

#define CG_BIRD_FIELD 376.0f
#define CG_BIRD_STEP_MS 32
#define CG_BIRD_MAX_DELTA_MS 60
#define CG_BIRD_X 68.0f
#define CG_BIRD_GRAVITY 620.0f
#define CG_BIRD_FLAP_VELOCITY -190.0f
#define CG_BIRD_PIPE_WIDTH 60.0f
#define CG_BIRD_PIPE_CAP_OVERHANG 7.0f
#define CG_BIRD_PIPE_COUNT 3

typedef struct {
    uint32_t id;
    float y;
    int8_t offset_y;
    bool collected;
} cg_bird_gem_t;

typedef struct {
    uint32_t id;
    float x;
    float gap_y;
    float gap_half;
    bool passed;
    cg_bird_gem_t gem;
} cg_bird_pipe_t;

typedef struct {
    cg_phase_t phase;
    cg_event_t last_event;
    cg_reason_t reason;
    uint32_t seed;
    float bird_y;
    float velocity;
    cg_bird_pipe_t pipes[CG_BIRD_PIPE_COUNT];
    uint32_t next_pipe_id;
    uint32_t gap_index;
    uint32_t score;
    uint32_t gates_passed;
    uint32_t gems_collected;
    float world_distance;
    uint32_t inputs;
    uint32_t event_id;
    int32_t hit_pipe_id;
} cg_bird_t;

void cg_bird_init(cg_bird_t *state, uint32_t seed);
bool cg_bird_start(cg_bird_t *state);
bool cg_bird_flap(cg_bird_t *state);
bool cg_bird_step(cg_bird_t *state, uint32_t delta_ms);
float cg_bird_speed(const cg_bird_t *state);

#ifdef __cplusplus
}
#endif
