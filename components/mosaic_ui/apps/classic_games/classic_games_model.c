/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "classic_games_model.h"

#include <string.h>

#define CG_MINES_LAYOUT_ATTEMPTS 96
static uint32_t cg_lcg_next(uint32_t *value)
{
    *value = (*value != 0u ? *value : 1u) * 1664525u + 1013904223u;
    return *value;
}

static bool cg_valid_mine_index(int index)
{
    return index >= 0 && index < CG_MINES_CELLS;
}

size_t cg_mines_neighbor_indexes(int index, uint8_t out_indexes[8])
{
    if (!cg_valid_mine_index(index) || out_indexes == NULL) {
        return 0;
    }

    const int row = index / CG_MINES_SIZE;
    const int column = index % CG_MINES_SIZE;
    size_t count = 0;
    for (int row_delta = -1; row_delta <= 1; ++row_delta) {
        for (int column_delta = -1; column_delta <= 1; ++column_delta) {
            if (row_delta == 0 && column_delta == 0) {
                continue;
            }
            const int next_row = row + row_delta;
            const int next_column = column + column_delta;
            if (next_row >= 0 && next_row < CG_MINES_SIZE &&
                next_column >= 0 && next_column < CG_MINES_SIZE) {
                out_indexes[count++] =
                    (uint8_t)(next_row * CG_MINES_SIZE + next_column);
            }
        }
    }
    return count;
}

uint8_t cg_mines_neighbor_count(const cg_mines_t *state, int index)
{
    if (state == NULL) {
        return 0;
    }

    uint8_t neighbors[8];
    const size_t neighbor_count =
        cg_mines_neighbor_indexes(index, neighbors);
    uint8_t count = 0;
    for (size_t i = 0; i < neighbor_count; ++i) {
        if (state->mines[neighbors[i]]) {
            ++count;
        }
    }
    return count;
}

static void cg_mines_reveal_region(const bool mines[CG_MINES_CELLS],
                                   const bool flagged[CG_MINES_CELLS],
                                   bool revealed[CG_MINES_CELLS],
                                   int start)
{
    uint8_t queue[CG_MINES_CELLS];
    bool queued[CG_MINES_CELLS] = { false };
    size_t read_index = 0;
    size_t write_index = 0;

    if (!cg_valid_mine_index(start)) {
        return;
    }
    queue[write_index++] = (uint8_t)start;
    queued[start] = true;

    while (read_index < write_index) {
        const uint8_t index = queue[read_index++];
        if (revealed[index] || flagged[index] || mines[index]) {
            continue;
        }
        revealed[index] = true;
        uint8_t neighbors[8];
        const size_t neighbor_count =
            cg_mines_neighbor_indexes(index, neighbors);
        uint8_t adjacent_mines = 0;
        for (size_t i = 0; i < neighbor_count; ++i) {
            if (mines[neighbors[i]]) {
                ++adjacent_mines;
            }
        }
        if (adjacent_mines != 0) {
            continue;
        }
        for (size_t i = 0; i < neighbor_count; ++i) {
            const uint8_t neighbor = neighbors[i];
            if (!queued[neighbor] && !revealed[neighbor] &&
                !flagged[neighbor] && !mines[neighbor]) {
                queued[neighbor] = true;
                queue[write_index++] = neighbor;
            }
        }
    }
}

static uint8_t cg_count_true(const bool values[CG_MINES_CELLS])
{
    uint8_t count = 0;
    for (size_t i = 0; i < CG_MINES_CELLS; ++i) {
        if (values[i]) {
            ++count;
        }
    }
    return count;
}

static bool cg_mines_layout_solvable(const bool mines[CG_MINES_CELLS],
                                     int first_index)
{
    bool revealed[CG_MINES_CELLS] = { false };
    bool flagged[CG_MINES_CELLS] = { false };
    cg_mines_reveal_region(mines, flagged, revealed, first_index);

    bool changed = true;
    while (changed) {
        changed = false;
        for (int index = 0; index < CG_MINES_CELLS; ++index) {
            if (!revealed[index]) {
                continue;
            }
            uint8_t neighbors[8];
            const size_t neighbor_count =
                cg_mines_neighbor_indexes(index, neighbors);
            uint8_t hidden[8];
            size_t hidden_count = 0;
            uint8_t adjacent_mines = 0;
            uint8_t nearby_flags = 0;
            for (size_t i = 0; i < neighbor_count; ++i) {
                const uint8_t neighbor = neighbors[i];
                if (mines[neighbor]) {
                    ++adjacent_mines;
                }
                if (flagged[neighbor]) {
                    ++nearby_flags;
                } else if (!revealed[neighbor]) {
                    hidden[hidden_count++] = neighbor;
                }
            }
            if (hidden_count == 0) {
                continue;
            }
            const uint8_t remaining_mines =
                (uint8_t)(adjacent_mines - nearby_flags);
            if (remaining_mines == 0) {
                const uint8_t before = cg_count_true(revealed);
                for (size_t i = 0; i < hidden_count; ++i) {
                    cg_mines_reveal_region(mines, flagged, revealed,
                                           hidden[i]);
                }
                changed = changed || before != cg_count_true(revealed);
            } else if (remaining_mines == hidden_count) {
                for (size_t i = 0; i < hidden_count; ++i) {
                    if (!flagged[hidden[i]]) {
                        flagged[hidden[i]] = true;
                        changed = true;
                    }
                }
            }
        }
    }
    return cg_count_true(revealed) == CG_MINES_CELLS - CG_MINES_TOTAL;
}

static bool cg_mines_has_opening_clue(
    const bool mines[CG_MINES_CELLS],
    const bool protected_cells[CG_MINES_CELLS])
{
    for (int index = 0; index < CG_MINES_CELLS; ++index) {
        if (!protected_cells[index]) {
            continue;
        }
        uint8_t neighbors[8];
        const size_t count = cg_mines_neighbor_indexes(index, neighbors);
        for (size_t i = 0; i < count; ++i) {
            if (mines[neighbors[i]]) {
                return true;
            }
        }
    }
    return false;
}

static void cg_mines_fallback(bool mines[CG_MINES_CELLS], int first_index)
{
    const int first_row = first_index / CG_MINES_SIZE;
    const int first_column = first_index % CG_MINES_SIZE;
    const int row_start = first_row < CG_MINES_SIZE / 2
        ? CG_MINES_SIZE - 2
        : 0;
    const int column_start = first_column < CG_MINES_SIZE / 2
        ? CG_MINES_SIZE - 5
        : 0;
    memset(mines, 0, sizeof(bool) * CG_MINES_CELLS);
    for (int row = row_start; row < row_start + 2; ++row) {
        for (int column = column_start; column < column_start + 5;
             ++column) {
            mines[row * CG_MINES_SIZE + column] = true;
        }
    }
}

static void cg_mines_generate(cg_mines_t *state, int first_index)
{
    bool protected_cells[CG_MINES_CELLS] = { false };
    protected_cells[first_index] = true;
    uint8_t neighbors[8];
    const size_t neighbor_count =
        cg_mines_neighbor_indexes(first_index, neighbors);
    for (size_t i = 0; i < neighbor_count; ++i) {
        protected_cells[neighbors[i]] = true;
    }

    for (uint32_t attempt = 0; attempt < CG_MINES_LAYOUT_ATTEMPTS;
         ++attempt) {
        uint8_t candidates[CG_MINES_CELLS];
        size_t candidate_count = 0;
        for (uint8_t index = 0; index < CG_MINES_CELLS; ++index) {
            if (!protected_cells[index]) {
                candidates[candidate_count++] = index;
            }
        }
        uint32_t random_state =
            state->seed + attempt * UINT32_C(2654435761);
        for (size_t i = candidate_count - 1; i > 0; --i) {
            const uint32_t random_value = cg_lcg_next(&random_state);
            const size_t swap_index =
                (size_t)(((uint64_t)random_value * (i + 1)) >> 32);
            const uint8_t temporary = candidates[i];
            candidates[i] = candidates[swap_index];
            candidates[swap_index] = temporary;
        }
        memset(state->mines, 0, sizeof(state->mines));
        for (size_t i = 0; i < CG_MINES_TOTAL; ++i) {
            state->mines[candidates[i]] = true;
        }
        if (cg_mines_has_opening_clue(state->mines, protected_cells) &&
            cg_mines_layout_solvable(state->mines, first_index)) {
            state->mine_count = CG_MINES_TOTAL;
            return;
        }
    }
    cg_mines_fallback(state->mines, first_index);
    state->mine_count = CG_MINES_TOTAL;
}

void cg_mines_init(cg_mines_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->phase = CG_PHASE_READY;
    state->last_event = CG_EVENT_READY;
    state->seed = seed;
    state->first_index = -1;
    state->hit_index = -1;
}

bool cg_mines_start(cg_mines_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_PLAYING) {
        return false;
    }
    state->phase = CG_PHASE_PLAYING;
    state->last_event = CG_EVENT_START;
    state->reason = CG_REASON_NONE;
    return true;
}

bool cg_mines_toggle_flag_mode(cg_mines_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_OVER ||
        state->first_index < 0) {
        return false;
    }
    state->flag_mode = !state->flag_mode;
    return true;
}

bool cg_mines_toggle_flag(cg_mines_t *state, int index)
{
    if (state == NULL || !cg_valid_mine_index(index) ||
        state->phase == CG_PHASE_OVER || state->first_index < 0 ||
        state->revealed[index]) {
        return false;
    }
    if (state->flagged[index]) {
        state->flagged[index] = false;
        --state->flagged_count;
    } else {
        if (state->flagged_count >= CG_MINES_TOTAL) {
            return false;
        }
        state->flagged[index] = true;
        ++state->flagged_count;
    }
    ++state->moves;
    state->last_event = CG_EVENT_FLAG;
    return true;
}

static void cg_mines_lose(cg_mines_t *state, int index)
{
    state->phase = CG_PHASE_OVER;
    state->hit_index = (int8_t)index;
    state->won = false;
    state->reason = CG_REASON_MINE_HIT;
    for (int cell = 0; cell < CG_MINES_CELLS; ++cell) {
        if (state->mines[cell] && !state->revealed[cell]) {
            state->revealed[cell] = true;
            ++state->revealed_count;
        }
    }
    ++state->moves;
    state->last_event = CG_EVENT_CRASH;
}

static bool cg_mines_reveal_indexes(cg_mines_t *state,
                                    const uint8_t *indexes,
                                    size_t index_count)
{
    bool changed = false;
    for (size_t i = 0; i < index_count; ++i) {
        const uint8_t index = indexes[i];
        if (state->flagged[index] || state->revealed[index]) {
            continue;
        }
        if (state->mines[index]) {
            cg_mines_lose(state, index);
            return true;
        }
        const uint8_t before = cg_count_true(state->revealed);
        cg_mines_reveal_region(state->mines, state->flagged,
                               state->revealed, index);
        changed = changed || before != cg_count_true(state->revealed);
    }
    if (!changed) {
        return false;
    }
    state->revealed_count = cg_count_true(state->revealed);
    state->won =
        state->revealed_count == CG_MINES_CELLS - CG_MINES_TOTAL;
    if (state->won) {
        state->phase = CG_PHASE_OVER;
        state->reason = CG_REASON_MINES_CLEARED;
        if (state->elapsed == 0) {
            state->elapsed = 1;
        }
    } else {
        state->phase = CG_PHASE_PLAYING;
        state->reason = CG_REASON_NONE;
    }
    ++state->moves;
    state->last_event = CG_EVENT_REVEAL;
    return true;
}

bool cg_mines_reveal(cg_mines_t *state, int index)
{
    if (state == NULL || !cg_valid_mine_index(index) ||
        state->phase == CG_PHASE_OVER || state->flagged[index]) {
        return false;
    }
    if (state->mine_count == 0) {
        cg_mines_generate(state, index);
        state->phase = CG_PHASE_PLAYING;
        state->reason = CG_REASON_NONE;
        state->first_index = (int8_t)index;
        state->revealed[index] = true;
        uint8_t neighbors[8];
        const size_t neighbor_count =
            cg_mines_neighbor_indexes(index, neighbors);
        for (size_t i = 0; i < neighbor_count; ++i) {
            state->revealed[neighbors[i]] = true;
        }
        state->revealed_count = (uint8_t)(neighbor_count + 1);
        ++state->moves;
        state->last_event = CG_EVENT_REVEAL;
        return true;
    }
    if (state->revealed[index]) {
        const uint8_t adjacent_mines =
            cg_mines_neighbor_count(state, index);
        if (adjacent_mines == 0) {
            return false;
        }
        uint8_t neighbors[8];
        const size_t neighbor_count =
            cg_mines_neighbor_indexes(index, neighbors);
        uint8_t nearby_flags = 0;
        for (size_t i = 0; i < neighbor_count; ++i) {
            if (state->flagged[neighbors[i]]) {
                ++nearby_flags;
            }
        }
        if (nearby_flags != adjacent_mines) {
            return false;
        }
        return cg_mines_reveal_indexes(state, neighbors, neighbor_count);
    }
    const uint8_t only_index = (uint8_t)index;
    return cg_mines_reveal_indexes(state, &only_index, 1);
}

void cg_mines_step_clock(cg_mines_t *state)
{
    if (state != NULL && state->phase == CG_PHASE_PLAYING &&
        state->first_index >= 0 && state->elapsed < CG_MINES_MAX_ELAPSED) {
        ++state->elapsed;
    }
}

bool cg_mines_is_basic_logic_solvable(const cg_mines_t *state)
{
    return state != NULL && state->first_index >= 0 &&
        state->mine_count == CG_MINES_TOTAL &&
        cg_mines_layout_solvable(state->mines, state->first_index);
}

bool cg_mines_record_improved(bool has_record,
                              uint16_t previous_elapsed,
                              const cg_mines_t *state)
{
    return state != NULL && state->phase == CG_PHASE_OVER && state->won &&
        (!has_record || state->elapsed < previous_elapsed);
}

/* Snake ------------------------------------------------------------------ */

static const cg_point_t s_direction_vectors[] = {
    [CG_DIR_UP] = { 0, -1 },
    [CG_DIR_RIGHT] = { 1, 0 },
    [CG_DIR_DOWN] = { 0, 1 },
    [CG_DIR_LEFT] = { -1, 0 },
};

static const cg_point_t s_food_fallbacks[] = {
    { 11, 7 }, { 11, 3 }, { 6, 3 }, { 6, 10 },
    { 11, 10 }, { 11, 5 }, { 3, 5 }, { 3, 11 },
};

static const cg_point_t s_food_offsets[] = {
    { 4, 0 }, { 0, 4 }, { -4, 0 }, { 0, -4 },
};

static bool cg_same_point(cg_point_t left, cg_point_t right)
{
    return left.x == right.x && left.y == right.y;
}

static bool cg_valid_direction(cg_direction_t direction)
{
    return direction >= CG_DIR_UP && direction <= CG_DIR_LEFT;
}

static bool cg_opposite_direction(cg_direction_t left,
                                  cg_direction_t right)
{
    return ((int)left + 2) % 4 == (int)right;
}

void cg_snake_init(cg_snake_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->phase = CG_PHASE_READY;
    state->last_event = CG_EVENT_READY;
    state->snake[0] = (cg_point_t){ 7, 7 };
    state->snake[1] = (cg_point_t){ 6, 7 };
    state->snake[2] = (cg_point_t){ 5, 7 };
    state->length = CG_SNAKE_INITIAL_LENGTH;
    state->direction = CG_DIR_RIGHT;
    state->food = s_food_fallbacks[0];
}

bool cg_snake_start(cg_snake_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_PLAYING) {
        return false;
    }
    state->phase = CG_PHASE_PLAYING;
    state->last_event = CG_EVENT_START;
    state->reason = CG_REASON_NONE;
    return true;
}

bool cg_snake_set_direction(cg_snake_t *state, cg_direction_t direction)
{
    if (state == NULL || state->phase == CG_PHASE_OVER ||
        !cg_valid_direction(direction)) {
        return false;
    }
    const cg_point_t next = s_direction_vectors[direction];
    if (state->phase == CG_PHASE_READY) {
        const cg_point_t head = state->snake[0];
        cg_snake_start(state);
        state->snake[1] =
            (cg_point_t){ head.x - next.x, head.y - next.y };
        state->snake[2] =
            (cg_point_t){ head.x - next.x * 2, head.y - next.y * 2 };
        state->direction = direction;
        state->direction_queue_length = 0;
        state->food =
            (cg_point_t){ head.x + next.x * 4, head.y + next.y * 4 };
        ++state->moves;
        ++state->event_id;
        state->last_event = CG_EVENT_TURN;
        return true;
    }

    const cg_direction_t reference =
        state->direction_queue_length > 0
        ? state->direction_queue[state->direction_queue_length - 1]
        : state->direction;
    if (direction == reference ||
        cg_opposite_direction(reference, direction) ||
        state->direction_queue_length >= CG_SNAKE_TURN_BUFFER) {
        return false;
    }
    state->direction_queue[state->direction_queue_length++] = direction;
    ++state->moves;
    ++state->event_id;
    state->last_event = CG_EVENT_TURN;
    return true;
}

static bool cg_snake_occupies(const cg_snake_t *state,
                              cg_point_t point,
                              uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        if (cg_same_point(state->snake[i], point)) {
            return true;
        }
    }
    return false;
}

static void cg_snake_choose_food(cg_snake_t *state)
{
    const cg_point_t head = state->snake[0];
    for (size_t step = 0;
         step < sizeof(s_food_offsets) / sizeof(s_food_offsets[0]);
         ++step) {
        const cg_point_t offset =
            s_food_offsets[(state->food_index + step) %
                           (sizeof(s_food_offsets) /
                            sizeof(s_food_offsets[0]))];
        const cg_point_t candidate =
            { head.x + offset.x, head.y + offset.y };
        if (candidate.x >= 0 && candidate.x < CG_SNAKE_GRID &&
            candidate.y >= 0 && candidate.y < CG_SNAKE_GRID &&
            !cg_snake_occupies(state, candidate, state->length)) {
            state->food = candidate;
            ++state->food_index;
            return;
        }
    }
    const size_t fallback_count =
        sizeof(s_food_fallbacks) / sizeof(s_food_fallbacks[0]);
    for (size_t offset = 1; offset <= fallback_count; ++offset) {
        const cg_point_t candidate =
            s_food_fallbacks[(state->food_index + offset) % fallback_count];
        if (!cg_snake_occupies(state, candidate, state->length)) {
            state->food = candidate;
            ++state->food_index;
            return;
        }
    }
    state->food = (cg_point_t){ 1, 1 };
    state->food_index = 0;
}

bool cg_snake_step(cg_snake_t *state)
{
    if (state == NULL || state->phase != CG_PHASE_PLAYING ||
        state->length == 0) {
        return false;
    }
    cg_direction_t direction = state->direction;
    if (state->direction_queue_length > 0) {
        direction = state->direction_queue[0];
        for (uint8_t i = 1; i < state->direction_queue_length; ++i) {
            state->direction_queue[i - 1] = state->direction_queue[i];
        }
        --state->direction_queue_length;
    }
    const cg_point_t vector = s_direction_vectors[direction];
    const cg_point_t head =
        { state->snake[0].x + vector.x, state->snake[0].y + vector.y };
    const bool ate = cg_same_point(head, state->food);
    const bool hits_wall =
        head.x < 0 || head.x >= CG_SNAKE_GRID ||
        head.y < 0 || head.y >= CG_SNAKE_GRID;
    const uint16_t collision_length =
        ate ? state->length : (uint16_t)(state->length - 1);
    const bool hits_self =
        cg_snake_occupies(state, head, collision_length);
    state->direction = direction;
    if (hits_wall || hits_self) {
        state->phase = CG_PHASE_OVER;
        state->reason =
            hits_wall ? CG_REASON_SNAKE_WALL : CG_REASON_SNAKE_SELF;
        state->crash_cell = head;
        state->has_crash_cell = true;
        ++state->event_id;
        state->last_event = CG_EVENT_CRASH;
        return true;
    }

    const uint16_t next_length =
        ate && state->length < CG_SNAKE_CAPACITY
        ? (uint16_t)(state->length + 1)
        : state->length;
    for (uint16_t i = next_length - 1; i > 0; --i) {
        state->snake[i] = state->snake[i - 1];
    }
    state->snake[0] = head;
    state->length = next_length;
    state->has_last_food = false;
    ++state->event_id;
    if (!ate) {
        state->last_event = CG_EVENT_STEP;
        return true;
    }
    ++state->score;
    state->last_food = head;
    state->has_last_food = true;
    state->last_event = CG_EVENT_EAT;
    cg_snake_choose_food(state);
    return true;
}

uint16_t cg_snake_step_ms(const cg_snake_t *state)
{
    if (state == NULL) {
        return CG_SNAKE_STEP_MS;
    }
    const uint32_t reduction = (state->score / 3u) * 15u;
    if (reduction >= CG_SNAKE_STEP_MS - CG_SNAKE_MIN_STEP_MS) {
        return CG_SNAKE_MIN_STEP_MS;
    }
    return (uint16_t)(CG_SNAKE_STEP_MS - reduction);
}

bool cg_score_record_improved(bool has_record,
                              uint32_t previous_score,
                              cg_phase_t phase,
                              uint32_t score)
{
    return phase == CG_PHASE_OVER && score > 0 &&
        (!has_record || score > previous_score);
}

/* Blocks ----------------------------------------------------------------- */

typedef struct {
    int8_t x;
    int8_t y;
} cg_shape_cell_t;

static const cg_shape_cell_t s_block_shapes[CG_BLOCK_KIND_COUNT][4][4] = {
    [CG_BLOCK_I] = {
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },
        { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 } },
    },
    [CG_BLOCK_O] = {
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
    },
    [CG_BLOCK_T] = {
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } },
        { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } },
        { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
        { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 } },
    },
    [CG_BLOCK_L] = {
        { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 2 } },
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 } },
        { { 1, 0 }, { 1, 1 }, { 0, 2 }, { 1, 2 } },
    },
    [CG_BLOCK_J] = {
        { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
        { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 2 } },
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 } },
        { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 1, 2 } },
    },
    [CG_BLOCK_S] = {
        { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },
        { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } },
    },
    [CG_BLOCK_Z] = {
        { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } },
        { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 } },
    },
};

static const uint8_t s_block_rotation_counts[CG_BLOCK_KIND_COUNT] = {
    [CG_BLOCK_I] = 2,
    [CG_BLOCK_O] = 1,
    [CG_BLOCK_T] = 4,
    [CG_BLOCK_L] = 4,
    [CG_BLOCK_J] = 4,
    [CG_BLOCK_S] = 2,
    [CG_BLOCK_Z] = 2,
};

static cg_block_piece_t cg_blocks_spawn(uint32_t seed, uint32_t index)
{
    return (cg_block_piece_t){
        .kind = cg_blocks_kind_at(seed, index),
        .rotation = 0,
        .x = (CG_BLOCK_COLUMNS - 4) / 2,
        .y = 0,
    };
}

cg_block_kind_t cg_blocks_kind_at(uint32_t seed, uint32_t index)
{
    cg_block_kind_t bag[CG_BLOCK_KIND_COUNT] = {
        CG_BLOCK_I, CG_BLOCK_O, CG_BLOCK_T, CG_BLOCK_L,
        CG_BLOCK_J, CG_BLOCK_S, CG_BLOCK_Z,
    };
    const uint32_t bag_index = index / CG_BLOCK_KIND_COUNT;
    uint32_t random_state =
        seed + (bag_index + 1u) * UINT32_C(2654435761);
    for (size_t i = CG_BLOCK_KIND_COUNT - 1; i > 0; --i) {
        const uint32_t random_value = cg_lcg_next(&random_state);
        const size_t swap_index =
            (size_t)(((uint64_t)random_value * (i + 1)) >> 32);
        const cg_block_kind_t temporary = bag[i];
        bag[i] = bag[swap_index];
        bag[swap_index] = temporary;
    }
    return bag[index % CG_BLOCK_KIND_COUNT];
}

size_t cg_blocks_piece_cells(const cg_block_piece_t *piece,
                             cg_point_t out_cells[CG_BLOCK_PIECE_CELLS])
{
    if (piece == NULL || out_cells == NULL ||
        piece->kind >= CG_BLOCK_KIND_COUNT) {
        return 0;
    }
    const uint8_t rotation_count = s_block_rotation_counts[piece->kind];
    const uint8_t rotation = piece->rotation % rotation_count;
    for (size_t i = 0; i < CG_BLOCK_PIECE_CELLS; ++i) {
        const cg_shape_cell_t cell =
            s_block_shapes[piece->kind][rotation][i];
        out_cells[i] =
            (cg_point_t){ piece->x + cell.x, piece->y + cell.y };
    }
    return CG_BLOCK_PIECE_CELLS;
}

static bool cg_blocks_collides(const cg_blocks_t *state,
                               const cg_block_piece_t *piece)
{
    cg_point_t cells[CG_BLOCK_PIECE_CELLS];
    if (cg_blocks_piece_cells(piece, cells) != CG_BLOCK_PIECE_CELLS) {
        return true;
    }
    for (size_t i = 0; i < CG_BLOCK_PIECE_CELLS; ++i) {
        if (cells[i].x < 0 || cells[i].x >= CG_BLOCK_COLUMNS ||
            cells[i].y < 0 || cells[i].y >= CG_BLOCK_ROWS ||
            state->fixed[cells[i].y * CG_BLOCK_COLUMNS + cells[i].x] !=
                CG_BLOCK_EMPTY) {
            return true;
        }
    }
    return false;
}

void cg_blocks_init(cg_blocks_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    memset(state->fixed, CG_BLOCK_EMPTY, sizeof(state->fixed));
    state->phase = CG_PHASE_READY;
    state->last_event = CG_EVENT_READY;
    state->seed = seed;
    state->piece = cg_blocks_spawn(seed, 0);
}

bool cg_blocks_start(cg_blocks_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_PLAYING) {
        return false;
    }
    state->phase = CG_PHASE_PLAYING;
    state->last_event = CG_EVENT_START;
    state->reason = CG_REASON_NONE;
    return true;
}

cg_block_kind_t cg_blocks_next_kind(const cg_blocks_t *state)
{
    return state == NULL
        ? CG_BLOCK_EMPTY
        : cg_blocks_kind_at(state->seed, state->piece_index + 1);
}

static bool cg_blocks_grounded(const cg_blocks_t *state,
                               const cg_block_piece_t *piece)
{
    cg_block_piece_t below = *piece;
    ++below.y;
    return cg_blocks_collides(state, &below);
}

static void cg_blocks_apply_grounded_reset(cg_blocks_t *state,
                                           bool was_grounded,
                                           bool still_grounded)
{
    const bool may_reset =
        was_grounded && still_grounded &&
        state->lock_resets < CG_BLOCK_LOCK_RESET_MAX;
    if (still_grounded) {
        if (may_reset) {
            state->lock_ms = 0;
            ++state->lock_resets;
        }
    } else {
        state->lock_ms = 0;
    }
}

bool cg_blocks_move(cg_blocks_t *state, int delta)
{
    if (state == NULL || state->phase == CG_PHASE_OVER) {
        return false;
    }
    if (state->phase == CG_PHASE_READY) {
        cg_blocks_start(state);
    }
    int steps = delta < 0 ? -delta : delta;
    if (steps > CG_BLOCK_COLUMNS) {
        steps = CG_BLOCK_COLUMNS;
    }
    const int direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
    cg_block_piece_t piece = state->piece;
    int moved = 0;
    for (int step = 0; step < steps; ++step) {
        cg_block_piece_t candidate = piece;
        candidate.x = (int8_t)(candidate.x + direction);
        if (cg_blocks_collides(state, &candidate)) {
            break;
        }
        piece = candidate;
        ++moved;
    }
    if (moved == 0) {
        return false;
    }
    const bool was_grounded = cg_blocks_grounded(state, &state->piece);
    const bool still_grounded = cg_blocks_grounded(state, &piece);
    state->piece = piece;
    cg_blocks_apply_grounded_reset(state, was_grounded, still_grounded);
    ++state->moves;
    ++state->event_id;
    state->last_event = CG_EVENT_MOVE;
    state->cleared_row_count = 0;
    return true;
}

bool cg_blocks_rotate(cg_blocks_t *state)
{
    static const int8_t kicks[][2] = {
        { 0, 0 }, { -1, 0 }, { 1, 0 }, { -2, 0 }, { 2, 0 },
        { 0, -1 }, { -1, -1 }, { 1, -1 }, { 0, -2 },
    };
    if (state == NULL || state->phase == CG_PHASE_OVER) {
        return false;
    }
    if (state->phase == CG_PHASE_READY) {
        cg_blocks_start(state);
    }
    const uint8_t rotation_count =
        s_block_rotation_counts[state->piece.kind];
    if (rotation_count == 1) {
        ++state->event_id;
        state->last_event = CG_EVENT_STEP;
        state->cleared_row_count = 0;
        return false;
    }
    cg_block_piece_t rotated = state->piece;
    rotated.rotation =
        (uint8_t)((rotated.rotation + 1) % rotation_count);
    for (size_t i = 0; i < sizeof(kicks) / sizeof(kicks[0]); ++i) {
        cg_block_piece_t shifted = rotated;
        shifted.x = (int8_t)(shifted.x + kicks[i][0]);
        shifted.y = (int8_t)(shifted.y + kicks[i][1]);
        if (cg_blocks_collides(state, &shifted)) {
            continue;
        }
        const bool was_grounded =
            cg_blocks_grounded(state, &state->piece);
        const bool still_grounded =
            cg_blocks_grounded(state, &shifted);
        state->piece = shifted;
        cg_blocks_apply_grounded_reset(state, was_grounded,
                                       still_grounded);
        ++state->moves;
        ++state->event_id;
        state->last_event = CG_EVENT_ROTATE;
        state->cleared_row_count = 0;
        return true;
    }
    ++state->event_id;
    state->last_event = CG_EVENT_BLOCKED;
    state->cleared_row_count = 0;
    return false;
}

static uint8_t cg_blocks_clear_rows(cg_blocks_t *state)
{
    bool cleared[CG_BLOCK_ROWS] = { false };
    uint8_t cleared_count = 0;
    state->cleared_row_count = 0;
    for (int y = 0; y < CG_BLOCK_ROWS; ++y) {
        uint8_t count = 0;
        for (int x = 0; x < CG_BLOCK_COLUMNS; ++x) {
            if (state->fixed[y * CG_BLOCK_COLUMNS + x] !=
                CG_BLOCK_EMPTY) {
                ++count;
            }
        }
        if (count == CG_BLOCK_COLUMNS) {
            cleared[y] = true;
            state->cleared_rows[state->cleared_row_count++] =
                (uint8_t)y;
            ++cleared_count;
        }
    }
    if (cleared_count == 0) {
        return 0;
    }
    uint8_t compacted[CG_BLOCK_CELLS];
    memset(compacted, CG_BLOCK_EMPTY, sizeof(compacted));
    for (int y = 0; y < CG_BLOCK_ROWS; ++y) {
        if (cleared[y]) {
            continue;
        }
        int drop = 0;
        for (int cleared_y = y + 1; cleared_y < CG_BLOCK_ROWS;
             ++cleared_y) {
            if (cleared[cleared_y]) {
                ++drop;
            }
        }
        for (int x = 0; x < CG_BLOCK_COLUMNS; ++x) {
            compacted[(y + drop) * CG_BLOCK_COLUMNS + x] =
                state->fixed[y * CG_BLOCK_COLUMNS + x];
        }
    }
    memcpy(state->fixed, compacted, sizeof(state->fixed));
    return cleared_count;
}

static void cg_blocks_lock(cg_blocks_t *state,
                           const cg_block_piece_t *piece)
{
    cg_point_t cells[CG_BLOCK_PIECE_CELLS];
    cg_blocks_piece_cells(piece, cells);
    for (size_t i = 0; i < CG_BLOCK_PIECE_CELLS; ++i) {
        state->fixed[cells[i].y * CG_BLOCK_COLUMNS + cells[i].x] =
            (uint8_t)piece->kind;
    }
    const uint8_t cleared = cg_blocks_clear_rows(state);
    ++state->piece_index;
    state->piece = cg_blocks_spawn(state->seed, state->piece_index);
    if (cleared > 0) {
        ++state->combo;
    } else {
        state->combo = 0;
    }
    if (state->combo > state->max_combo) {
        state->max_combo = state->combo;
    }
    state->score +=
        4u + (uint32_t)cleared * 100u *
        (state->combo > 0 ? state->combo : 1u);
    state->lines = (uint16_t)(state->lines + cleared);
    state->fall_ms = 0;
    state->lock_ms = 0;
    state->lock_resets = 0;
    ++state->event_id;
    const bool over = cg_blocks_collides(state, &state->piece);
    state->phase = over ? CG_PHASE_OVER : CG_PHASE_PLAYING;
    state->reason =
        over ? CG_REASON_BLOCKS_SPAWN_BLOCKED : CG_REASON_NONE;
    state->last_event = over
        ? CG_EVENT_CRASH
        : cleared > 0 ? CG_EVENT_CLEAR : CG_EVENT_LOCK;
}

bool cg_blocks_soft_drop(cg_blocks_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_OVER) {
        return false;
    }
    if (state->phase == CG_PHASE_READY) {
        cg_blocks_start(state);
    }
    cg_block_piece_t piece = state->piece;
    ++piece.y;
    if (cg_blocks_collides(state, &piece)) {
        const uint16_t increased = (uint16_t)(state->lock_ms + 96u);
        state->lock_ms =
            increased > CG_BLOCK_LOCK_MS ? CG_BLOCK_LOCK_MS : increased;
        return true;
    }
    state->piece = piece;
    ++state->score;
    state->fall_ms = 0;
    state->lock_ms = 0;
    ++state->moves;
    ++state->event_id;
    state->last_event = CG_EVENT_SOFT_DROP;
    state->cleared_row_count = 0;
    return true;
}

uint16_t cg_blocks_fall_ms(const cg_blocks_t *state)
{
    const uint32_t lines = state == NULL ? 0 : state->lines;
    const uint32_t reduction = lines * 24u;
    if (reduction >= 430u) {
        return 220;
    }
    return (uint16_t)(650u - reduction);
}

bool cg_blocks_step(cg_blocks_t *state, uint32_t delta_ms)
{
    if (state == NULL || state->phase != CG_PHASE_PLAYING) {
        return false;
    }
    const uint16_t elapsed =
        (uint16_t)(delta_ms > 120u ? 120u : delta_ms);
    if (cg_blocks_grounded(state, &state->piece)) {
        const uint32_t lock_ms = state->lock_ms + elapsed;
        if (lock_ms >= CG_BLOCK_LOCK_MS) {
            cg_blocks_lock(state, &state->piece);
        } else {
            state->lock_ms = (uint16_t)lock_ms;
        }
        return elapsed > 0;
    }

    uint32_t fall_ms = state->fall_ms + elapsed;
    const uint16_t interval = cg_blocks_fall_ms(state);
    bool changed = false;
    while (fall_ms >= interval) {
        cg_block_piece_t below = state->piece;
        ++below.y;
        if (cg_blocks_collides(state, &below)) {
            break;
        }
        state->piece = below;
        fall_ms -= interval;
        changed = true;
    }
    state->fall_ms = (uint16_t)fall_ms;
    state->lock_ms = 0;
    if (changed) {
        state->last_event = CG_EVENT_FALL;
        state->cleared_row_count = 0;
    }
    return elapsed > 0;
}

bool cg_blocks_hard_drop(cg_blocks_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_OVER) {
        return false;
    }
    if (state->phase == CG_PHASE_READY) {
        cg_blocks_start(state);
    }
    cg_block_piece_t piece = state->piece;
    uint32_t distance = 0;
    while (true) {
        cg_block_piece_t below = piece;
        ++below.y;
        if (cg_blocks_collides(state, &below)) {
            break;
        }
        piece = below;
        ++distance;
    }
    state->score += distance * 2u;
    ++state->moves;
    state->last_event = CG_EVENT_HARD_DROP;
    cg_blocks_lock(state, &piece);
    return true;
}

void cg_blocks_ghost_cells(const cg_blocks_t *state,
                           cg_point_t out_cells[CG_BLOCK_PIECE_CELLS])
{
    if (state == NULL || out_cells == NULL) {
        return;
    }
    cg_block_piece_t piece = state->piece;
    while (true) {
        cg_block_piece_t below = piece;
        ++below.y;
        if (cg_blocks_collides(state, &below)) {
            break;
        }
        piece = below;
    }
    cg_blocks_piece_cells(&piece, out_cells);
}

/* Bird ------------------------------------------------------------------- */

static uint32_t cg_bird_mix(uint32_t value)
{
    value *= UINT32_C(0x21f0aaad);
    value ^= value >> 15;
    value *= UINT32_C(0x735a2d97);
    return value ^ (value >> 15);
}

static double cg_bird_seeded_unit(uint32_t seed, uint32_t index)
{
    uint32_t value =
        seed ^ ((index + 1u) * UINT32_C(0x9e3779b9));
    value = cg_bird_mix(value ^ (value >> 16));
    return (double)value / 4294967296.0;
}

static int cg_floor_to_int(double value)
{
    const int truncated = (int)value;
    return value < (double)truncated ? truncated - 1 : truncated;
}

static int cg_round_to_int(double value)
{
    return cg_floor_to_int(value + 0.5);
}

static float cg_bird_gap_half(uint32_t score)
{
    const uint32_t reduction = (score / 6u) * 2u;
    return (float)(reduction >= 14u ? 64u : 78u - reduction);
}

static float cg_float_clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

static float cg_bird_fair_gap(uint32_t seed,
                              uint32_t index,
                              float previous_gap_y,
                              float gap_half)
{
    float safe_minimum = 140.0f;
    const float hitbox_minimum = gap_half + 17.0f + 8.0f;
    if (hitbox_minimum > safe_minimum) {
        safe_minimum = hitbox_minimum;
    }
    float safe_maximum = 236.0f;
    const float hitbox_maximum =
        CG_BIRD_FIELD - gap_half - 17.0f - 8.0f;
    if (hitbox_maximum < safe_maximum) {
        safe_maximum = hitbox_maximum;
    }
    const int shift = cg_round_to_int(
        (cg_bird_seeded_unit(seed, index) * 2.0 - 1.0) * 44.0);
    const float rounded_previous =
        (float)cg_round_to_int(previous_gap_y);
    return cg_float_clamp(rounded_previous + (float)shift,
                          safe_minimum, safe_maximum);
}

static cg_bird_gem_t cg_bird_make_gem(uint32_t seed,
                                      uint32_t index,
                                      uint32_t id,
                                      float gap_y,
                                      float gap_half)
{
    const int direction =
        cg_bird_seeded_unit(seed, index * 2u + 4096u) < 0.5 ? -1 : 1;
    const int desired_offset = cg_round_to_int(
        26.0 + cg_bird_seeded_unit(seed, index * 2u + 4097u) * 6.0);
    float safe_offset = gap_half - 26.0f - 6.0f;
    if (safe_offset < 0) {
        safe_offset = 0;
    }
    const int offset =
        desired_offset < (int)safe_offset
        ? desired_offset
        : (int)safe_offset;
    return (cg_bird_gem_t){
        .id = id,
        .y = gap_y + (float)(direction * offset),
        .offset_y = (int8_t)(direction * offset),
        .collected = false,
    };
}

void cg_bird_init(cg_bird_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->phase = CG_PHASE_READY;
    state->last_event = CG_EVENT_READY;
    state->seed = seed;
    state->bird_y = 184.0f;
    float previous_gap_y = 184.0f;
    for (uint32_t index = 0; index < CG_BIRD_PIPE_COUNT; ++index) {
        const float gap_half = cg_bird_gap_half(0);
        const float gap_y = index == 0
            ? 184.0f
            : cg_bird_fair_gap(seed, index, previous_gap_y, gap_half);
        state->pipes[index] = (cg_bird_pipe_t){
            .id = index,
            .x = 250.0f + (float)index * 164.0f,
            .gap_y = gap_y,
            .gap_half = gap_half,
            .passed = false,
            .gem = cg_bird_make_gem(seed, index, index,
                                    gap_y, gap_half),
        };
        previous_gap_y = gap_y;
    }
    state->next_pipe_id = 3;
    state->gap_index = 2;
    state->hit_pipe_id = -1;
}

bool cg_bird_start(cg_bird_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_PLAYING) {
        return false;
    }
    if (state->phase == CG_PHASE_PAUSED) {
        state->phase = CG_PHASE_PLAYING;
        state->last_event = CG_EVENT_START;
        state->reason = CG_REASON_NONE;
        return true;
    }
    state->phase = CG_PHASE_PLAYING;
    state->velocity = CG_BIRD_FLAP_VELOCITY;
    state->last_event = CG_EVENT_START;
    state->reason = CG_REASON_NONE;
    return true;
}

bool cg_bird_flap(cg_bird_t *state)
{
    if (state == NULL || state->phase == CG_PHASE_OVER) {
        return false;
    }
    if (state->phase == CG_PHASE_READY) {
        cg_bird_start(state);
    }
    state->velocity = CG_BIRD_FLAP_VELOCITY;
    ++state->inputs;
    ++state->event_id;
    state->last_event = CG_EVENT_FLAP;
    return true;
}

float cg_bird_speed(const cg_bird_t *state)
{
    uint32_t score = state == NULL ? 0 : state->gates_passed;
    if (score > 18u) {
        score = 18u;
    }
    const float speed = 106.0f + (float)score * (2.0f / 3.0f);
    return speed > 118.0f ? 118.0f : speed;
}

static bool cg_bird_collects_gem(float bird_y,
                                 const cg_bird_pipe_t *pipe)
{
    if (pipe->gem.collected) {
        return false;
    }
    const float bird_left = CG_BIRD_X + 3.0f;
    const float bird_right = CG_BIRD_X + 49.0f;
    const float bird_top = bird_y - 17.0f;
    const float bird_bottom = bird_y + 17.0f;
    const float gem_x = pipe->x + CG_BIRD_PIPE_WIDTH / 2.0f;
    return bird_right > gem_x - 12.0f &&
        bird_left < gem_x + 12.0f &&
        bird_bottom > pipe->gem.y - 12.0f &&
        bird_top < pipe->gem.y + 12.0f;
}

static cg_reason_t cg_bird_collision(
    float bird_y,
    const cg_bird_pipe_t pipes[CG_BIRD_PIPE_COUNT],
    int32_t *hit_pipe_id)
{
    const float bird_top = bird_y - 17.0f;
    const float bird_bottom = bird_y + 17.0f;
    *hit_pipe_id = -1;
    if (bird_top <= 3.0f) {
        return CG_REASON_BIRD_UPPER_EDGE;
    }
    if (bird_bottom >= CG_BIRD_FIELD - 3.0f) {
        return CG_REASON_BIRD_LOWER_EDGE;
    }
    const float bird_left = CG_BIRD_X + 3.0f;
    const float bird_right = CG_BIRD_X + 49.0f;
    for (size_t i = 0; i < CG_BIRD_PIPE_COUNT; ++i) {
        const cg_bird_pipe_t *pipe = &pipes[i];
        const float pipe_left = pipe->x - CG_BIRD_PIPE_CAP_OVERHANG;
        const float pipe_right =
            pipe->x + CG_BIRD_PIPE_WIDTH + CG_BIRD_PIPE_CAP_OVERHANG;
        const bool overlaps =
            bird_right > pipe_left && bird_left < pipe_right;
        if (overlaps &&
            (bird_top < pipe->gap_y - pipe->gap_half ||
             bird_bottom > pipe->gap_y + pipe->gap_half)) {
            *hit_pipe_id = (int32_t)pipe->id;
            return CG_REASON_BIRD_PIPE;
        }
    }
    return CG_REASON_NONE;
}

bool cg_bird_step(cg_bird_t *state, uint32_t delta_ms)
{
    if (state == NULL || state->phase != CG_PHASE_PLAYING) {
        return false;
    }
    if (delta_ms > CG_BIRD_MAX_DELTA_MS) {
        delta_ms = CG_BIRD_MAX_DELTA_MS;
    }
    const float delta = (float)delta_ms / 1000.0f;
    state->velocity += CG_BIRD_GRAVITY * delta;
    state->bird_y += state->velocity * delta;
    const float speed = cg_bird_speed(state);
    state->world_distance += speed * delta;
    state->last_event = CG_EVENT_STEP;
    for (size_t i = 0; i < CG_BIRD_PIPE_COUNT; ++i) {
        state->pipes[i].x -= speed * delta;
        if (!state->pipes[i].passed &&
            state->pipes[i].x + CG_BIRD_PIPE_WIDTH +
                CG_BIRD_PIPE_CAP_OVERHANG <
                CG_BIRD_X + 3.0f) {
            state->pipes[i].passed = true;
            ++state->score;
            ++state->gates_passed;
            ++state->event_id;
            state->last_event = CG_EVENT_PASS;
        }
    }

    size_t farthest_index = 0;
    for (size_t i = 1; i < CG_BIRD_PIPE_COUNT; ++i) {
        if (state->pipes[i].x > state->pipes[farthest_index].x) {
            farthest_index = i;
        }
    }
    float farthest = state->pipes[farthest_index].x;
    float previous_gap_y = state->pipes[farthest_index].gap_y;
    const uint32_t seed = state->seed;
    for (size_t i = 0; i < CG_BIRD_PIPE_COUNT; ++i) {
        if (state->pipes[i].x >
            -CG_BIRD_PIPE_WIDTH - CG_BIRD_PIPE_CAP_OVERHANG - 1.0f) {
            continue;
        }
        farthest += 164.0f;
        ++state->gap_index;
        const float gap_half = cg_bird_gap_half(state->gates_passed);
        const float gap_y =
            cg_bird_fair_gap(seed, state->gap_index,
                             previous_gap_y, gap_half);
        state->pipes[i] = (cg_bird_pipe_t){
            .id = state->next_pipe_id,
            .x = farthest,
            .gap_y = gap_y,
            .gap_half = gap_half,
            .passed = false,
            .gem = cg_bird_make_gem(
                seed, state->gap_index, state->next_pipe_id,
                gap_y, gap_half),
        };
        previous_gap_y = gap_y;
        ++state->next_pipe_id;
    }

    int32_t hit_pipe_id = -1;
    const cg_reason_t collision =
        cg_bird_collision(state->bird_y, state->pipes, &hit_pipe_id);
    if (collision != CG_REASON_NONE) {
        state->phase = CG_PHASE_OVER;
        state->reason = collision;
        state->hit_pipe_id = hit_pipe_id;
        ++state->event_id;
        state->last_event = CG_EVENT_CRASH;
        return true;
    }
    for (size_t i = 0; i < CG_BIRD_PIPE_COUNT; ++i) {
        if (cg_bird_collects_gem(state->bird_y, &state->pipes[i])) {
            state->pipes[i].gem.collected = true;
            ++state->score;
            ++state->gems_collected;
            ++state->event_id;
            state->last_event = CG_EVENT_GEM;
        }
    }
    return true;
}
