#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Full-canvas retained scene for the Classic Games native App."""

from __future__ import annotations

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent.parent / "common"))

from font_paths import DEJAVU_SANS_BOLD  # noqa: E402
from scene_common import (  # noqa: E402
    CONTENT_H,
    CONTENT_W,
    button,
    container,
    explicit_charset,
    image,
    label,
    layer,
    scene_out_path,
    shared_prefix,
    write_scene,
)

FONT = DEJAVU_SANS_BOLD
BOLD = FONT


def packed_charset(*parts: str) -> str:
    return "".join(sorted(set("".join(parts))))


RUNTIME_CHARSET = packed_charset(
    "".join(chr(codepoint) for codepoint in range(32, 127)),
    "·×—‹Ⅱ",
)
FONT_SIZES = (14, 18, 24, 32, 44)
FONT_POLICIES = {
    size: explicit_charset(RUNTIME_CHARSET) for size in FONT_SIZES
}

BG = "#050505"
PANEL = "#181819"
PANEL_EDGE = "#3B3C3D"
INK = "#FCFCFF"
MUTED = "#91919B"
PAPER = "#F6F2E8"
PAPER_INK = "#191816"
PAPER_MUTED = "#67625A"
ACCENT = "#FF4C01"
GREEN = "#63C270"
AMBER = "#FFB020"

ASSETS = "../assets"
HOME_ART = {
    "mines": f"{ASSETS}/hub-mines-v13.png",
    "snake": f"{ASSETS}/hub-snake.png",
    "blocks": f"{ASSETS}/hub-blocks.png",
    "bird": f"{ASSETS}/bird-arcade-player-v6.png",
}
FLAG_ASSET = f"{ASSETS}/mine-flag.png"
MINE_ASSET = f"{ASSETS}/mine-hit.png"
SNAKE_HEAD_ASSET = f"{ASSETS}/snake-head.png"
SNAKE_FOOD_ASSET = f"{ASSETS}/snake-food.png"
BIRD_PLAYER_ASSET = f"{ASSETS}/bird-arcade-player-v6.png"
BIRD_GEM_ASSET = f"{ASSETS}/bird-arcade-gem-v6.png"
CANVAS_PLACEHOLDER = f"{ASSETS}/game-canvas-placeholder.png"
LAUNCHER_ASSET = f"{ASSETS}/launcher-bricks.png"

IMAGES = [
    (HOME_ART["mines"], 82, 76),
    (HOME_ART["snake"], 82, 76),
    (HOME_ART["blocks"], 82, 76),
    (HOME_ART["bird"], 82, 76),
    (FLAG_ASSET, 32, 32),
    (MINE_ASSET, 32, 32),
    (SNAKE_HEAD_ASSET, 36, 36),
    (SNAKE_FOOD_ASSET, 47, 47),
    (BIRD_PLAYER_ASSET, 114, 76),
    (BIRD_GEM_ASSET, 52, 52),
    (CANVAS_PLACEHOLDER, 472, 428),
    (LAUNCHER_ASSET, 232, 232),
]

HOME_LANES = (
    ("mines", "01", "MINES", ACCENT),
    ("snake", "02", "SNAKE", GREEN),
    ("blocks", "03", "BLOCKS", AMBER),
    ("bird", "04", "BIRD", GREEN),
)


def visible(obj: dict, bind: str, *, hidden: bool = False) -> dict:
    obj["bind"] = bind
    obj["bind_target"] = "visible"
    if hidden:
        obj["hidden"] = True
    return obj


def dynamic_label(
    parent: int,
    x: int,
    y: int,
    w: int,
    h: int,
    text: str,
    bind: str,
    *,
    size: int,
    color: str,
    align: str = "left",
    font: str | None = None,
) -> dict:
    obj = label(
        parent,
        x,
        y,
        w,
        h,
        text,
        size=size,
        color=color,
        align=align,
        bind=bind,
        name=bind,
        font=font,
        font_charset=RUNTIME_CHARSET,
    )
    obj["overflow"] = "ellipsis"
    return obj


def action_button(
    parent: int,
    x: int,
    y: int,
    w: int,
    h: int,
    text: str,
    action: str,
    *,
    bg: str,
    fg: str,
    size: int = 18,
    radius: int = 12,
    border: str | None = None,
    border_w: int = 0,
) -> dict:
    obj = button(
        parent,
        x,
        y,
        w,
        h,
        text,
        bg=bg,
        fg=fg,
        radius=radius,
        size=size,
        name=action,
        callback=action,
        border=border,
        border_w=border_w,
        align="center",
    )
    obj["font_charset"] = RUNTIME_CHARSET
    return obj


def bounded_image(
    parent: int,
    src: str,
    *,
    name: str,
    bind: str,
    x: int,
    y: int,
    size: int | None = None,
    width: int | None = None,
    height: int | None = None,
    min_x: int | None = None,
    max_x: int | None = None,
    min_y: int | None = None,
    max_y: int | None = None,
) -> dict:
    image_w = width if width is not None else size
    image_h = height if height is not None else size
    if image_w is None or image_h is None:
        raise ValueError("bounded_image requires size or width/height")
    return visible(
        {
            "type": "image",
            "parent": parent,
            "name": name,
            "x": {
                "default": x,
                "min": 4 if min_x is None else min_x,
                "max": 476 - image_w if max_x is None else max_x,
            },
            "y": {
                "default": y,
                "min": 50 if min_y is None else min_y,
                "max": 478 - image_h if max_y is None else max_y,
            },
            "w": image_w,
            "h": image_h,
            "image": src,
        },
        bind,
        hidden=True,
    )


def build_home(objs: list[dict], content: int) -> None:
    home = len(objs)
    objs.append(visible(
        layer(content, 0, 0, CONTENT_W, CONTENT_H, name="home_layer"),
        "home_visible",
    ))
    objs.append(container(home, 0, 0, CONTENT_W, CONTENT_H, bg=BG,
                          name="home_background"))
    objs.append(label(
        home, 20, 8, 300, 36, "CLASSICS", size=24, color=INK,
        name="home_title", font=BOLD, font_charset=RUNTIME_CHARSET,
    ))

    for lane_index, (game, number, title, tone) in enumerate(HOME_LANES):
        y = 48 + lane_index * 104
        action = f"open_{game}"
        objs.append(action_button(
            home, 20, y, 440, 100, "", action,
            bg=PANEL, fg=INK, radius=14, border=PANEL_EDGE, border_w=1,
        ))
        objs.append(label(
            home, 32, y + 36, 38, 28, number, size=14, color=tone,
            name=f"home_{game}_number", font=BOLD,
            font_charset=RUNTIME_CHARSET,
        ))
        objs.append(container(home, 76, y + 21, 1, 58, bg=PANEL_EDGE))
        objs.append(label(
            home, 88, y + 23, 154, 34, title, size=24, color=INK,
            name=f"home_{game}_title", font=BOLD,
            font_charset=RUNTIME_CHARSET,
        ))
        objs.append(dynamic_label(
            home, 88, y + 59, 154, 20, "", f"home_{game}_continue",
            size=14, color=tone,
        ))
        objs.append(image(
            home, HOME_ART[game], 248, y + 12, 82, 76,
            name=f"home_{game}_art",
        ))
        objs.append(container(home, 338, y + 21, 1, 58, bg=PANEL_EDGE))
        objs.append(dynamic_label(
            home, 348, y + 24, 100, 18, "NO BEST",
            f"home_{game}_record_label", size=14, color=MUTED,
            align="right",
        ))
        objs.append(dynamic_label(
            home, 348, y + 45, 100, 30, "PLAY",
            f"home_{game}_record_value", size=18, color=tone,
            align="right", font=BOLD,
        ))


def build_onboarding(objs: list[dict], content: int) -> None:
    onboarding = len(objs)
    objs.append(visible(
        layer(
            content, 0, 0, CONTENT_W, CONTENT_H, hidden=True,
            name="onboarding_layer",
        ),
        "onboarding_visible",
        hidden=True,
    ))
    objs.append(container(
        onboarding, 0, 0, CONTENT_W, 48, bg=BG,
        name="onboarding_header",
    ))
    objs.append(action_button(
        onboarding, 40, 2, 44, 44, "‹", "onboarding_back",
        bg=BG, fg=INK, size=32, radius=0,
    ))
    objs.append(label(
        onboarding, 96, 10, 288, 28, "HOW TO PLAY", size=18,
        color=INK, align="center", name="onboarding_header_title",
        font=BOLD, font_charset=RUNTIME_CHARSET,
    ))
    objs.append(container(
        onboarding, 0, 48, CONTENT_W, 432, bg=PAPER,
        name="onboarding_body",
    ))
    objs.append(label(
        onboarding, 20, 64, 440, 18, "QUICK GUIDE", size=14,
        color=ACCENT, name="onboarding_kicker", font=BOLD,
        font_charset=RUNTIME_CHARSET,
    ))
    objs.append(dynamic_label(
        onboarding, 20, 86, 440, 40, "READ THE FIELD",
        "onboarding_title", size=32, color=PAPER_INK, font=BOLD,
    ))
    objs.append(dynamic_label(
        onboarding, 20, 126, 440, 42,
        "Open every safe tile. Keep ten mines covered.",
        "onboarding_lead", size=14, color=PAPER_MUTED,
    ))

    objs.append(container(
        onboarding, 20, 174, 440, 92, bg="#E9E3D8", radius=14,
        border="#D2C9BA", border_w=1, name="onboarding_guide_card",
    ))
    for game, _, _, _ in HOME_LANES:
        objs.append(visible(
            image(
                onboarding, HOME_ART[game], 32, 184, 72, 72,
                name=f"onboarding_{game}_guide_icon",
            ),
            f"onboarding_{game}_guide_icon_visible",
            hidden=True,
        ))
    objs.append(dynamic_label(
        onboarding, 120, 190, 320, 28, "TAP",
        "onboarding_cue", size=24, color=PAPER_INK, font=BOLD,
    ))
    objs.append(dynamic_label(
        onboarding, 120, 220, 320, 24, "OPEN A SAFE TILE",
        "onboarding_cue_detail", size=14, color=PAPER_MUTED,
    ))

    for index in range(3):
        y = 274 + index * 36
        objs.append(label(
            onboarding, 24, y + 5, 32, 24, f"0{index + 1}",
            size=14, color=ACCENT, name=f"onboarding_step_{index + 1}_number",
            font=BOLD, font_charset=RUNTIME_CHARSET,
        ))
        objs.append(dynamic_label(
            onboarding, 64, y + 3, 392, 28,
            (
                "Tap a tile to reveal it",
                "Hold for 360ms to place a flag",
                "Use the numbers to secure the field",
            )[index],
            f"onboarding_step_{index + 1}",
            size=14, color=PAPER_INK, font=BOLD,
        ))
    objs.append(action_button(
        onboarding, 20, 398, 440, 58, "PLAY", "onboarding_play",
        bg=ACCENT, fg=INK, size=24, radius=14,
    ))


def build_game(objs: list[dict], content: int) -> None:
    game = len(objs)
    objs.append(visible(
        layer(
            content, 0, 0, CONTENT_W, CONTENT_H, hidden=True,
            name="game_layer",
        ),
        "game_visible",
        hidden=True,
    ))
    objs.append(container(
        game, 0, 0, CONTENT_W, 48, bg=BG, name="game_header",
    ))
    objs.append(action_button(
        game, 40, 2, 44, 44, "‹", "game_back",
        bg=BG, fg=INK, size=32, radius=0,
    ))
    objs.append(dynamic_label(
        game, 92, 8, 296, 34, "MOVES: 000 · FLAGS: 00/10",
        "game_hud_label", size=18, color=INK, align="center", font=BOLD,
    ))
    objs.append(action_button(
        game, 396, 2, 44, 44, "Ⅱ", "game_pause",
        bg=BG, fg=INK, size=24, radius=0,
    ))
    canvas = image(
        game, CANVAS_PLACEHOLDER, 4, 50, 472, 428,
        name="game_canvas", bind="game_canvas",
    )
    canvas["dynamic_image"] = True
    objs.append(canvas)

    for index in range(10):
        objs.append(bounded_image(
            game, FLAG_ASSET,
            name=f"mine_flag_{index}", bind=f"mine_flag_{index}_visible",
            x=28 + index * 38, y=94, size=32,
        ))
        objs.append(bounded_image(
            game, MINE_ASSET,
            name=f"mine_hit_{index}", bind=f"mine_hit_{index}_visible",
            x=28 + index * 38, y=132, size=32,
        ))

    objs.append(bounded_image(
        game, SNAKE_HEAD_ASSET,
        name="snake_head", bind="snake_head_visible",
        x=220, y=230, size=36,
        min_x=-36, max_x=480, min_y=-36, max_y=480,
    ))
    objs.append(bounded_image(
        game, SNAKE_FOOD_ASSET,
        name="snake_food", bind="snake_food_visible",
        x=340, y=230, size=47,
        min_x=-47, max_x=480, min_y=-47, max_y=480,
    ))
    objs.append(bounded_image(
        game, BIRD_PLAYER_ASSET,
        name="bird_player", bind="bird_player_visible",
        x=44, y=220, width=114, height=76,
        min_x=-114, max_x=480, min_y=-76, max_y=480,
    ))
    for index in range(3):
        objs.append(bounded_image(
            game, BIRD_GEM_ASSET,
            name=f"bird_gem_{index}", bind=f"bird_gem_{index}_visible",
            x=206 + index * 82, y=214, size=52,
            min_x=-52, max_x=480,
        ))

    ready = len(objs)
    objs.append(visible(
        layer(
            game, 100, 432, 280, 30, hidden=True,
            name="game_ready_cue_layer",
        ),
        "game_ready_cue_visible",
        hidden=True,
    ))
    objs.append(container(
        ready, 0, 0, 280, 30, bg=BG, radius=15, opacity=230,
        border=PANEL_EDGE, border_w=1,
    ))
    objs.append(dynamic_label(
        ready, 8, 3, 264, 24, "TAP TO START",
        "game_ready_cue", size=14, color=MUTED,
        align="center", font=BOLD,
    ))

    impact = len(objs)
    objs.append(visible(
        layer(
            game, 64, 206, 352, 66, hidden=True,
            name="game_impact_layer",
        ),
        "game_impact_visible",
        hidden=True,
    ))
    objs.append(container(
        impact, 0, 0, 352, 66, bg=BG, radius=14, opacity=235,
        border=ACCENT, border_w=2,
    ))
    objs.append(dynamic_label(
        impact, 12, 15, 328, 36, "COLLISION LOCKED",
        "game_impact_label", size=18, color=INK,
        align="center", font=BOLD,
    ))


def build_pause_overlay(objs: list[dict], content: int) -> None:
    pause = len(objs)
    objs.append(visible(
        layer(
            content, 0, 0, CONTENT_W, CONTENT_H, hidden=True,
            name="pause_overlay",
        ),
        "pause_overlay_visible",
        hidden=True,
    ))
    objs.append(container(
        pause, 0, 0, CONTENT_W, CONTENT_H, bg=BG, opacity=170,
        name="pause_scrim",
    ))
    sheet = len(objs)
    objs.append(container(
        pause, 16, 142, 448, 214, bg=PAPER, radius=18,
        name="pause_sheet",
    ))
    objs.append(dynamic_label(
        sheet, 20, 18, 408, 42, "PAUSED",
        "pause_title", size=32, color=PAPER_INK,
        align="center", font=BOLD,
    ))
    objs.append(dynamic_label(
        sheet, 20, 62, 408, 30, "Your position is safe",
        "pause_description", size=14, color=PAPER_MUTED,
        align="center",
    ))

    normal_buttons = (
        ("pause_resume", "RESUME", 12, ACCENT, INK),
        ("pause_restart", "RESTART", 156, "#E2DACE", PAPER_INK),
        ("pause_menu", "ARCADE", 300, "#E2DACE", PAPER_INK),
    )
    for action, text, x, bg, fg in normal_buttons:
        obj = action_button(
            sheet, x, 132, 136, 58, text, action,
            bg=bg, fg=fg, size=14, radius=12,
            border="#C9BFAF" if bg != ACCENT else None,
            border_w=1 if bg != ACCENT else 0,
        )
        objs.append(visible(obj, f"{action}_visible"))

    confirm = action_button(
        sheet, 38, 132, 176, 58, "RESTART",
        "pause_confirm_restart", bg=ACCENT, fg=INK,
        size=14, radius=12,
    )
    objs.append(visible(
        confirm, "pause_confirm_restart_visible", hidden=True,
    ))
    cancel = action_button(
        sheet, 234, 132, 176, 58, "KEEP RUN",
        "pause_cancel_restart", bg="#E2DACE", fg=PAPER_INK,
        size=14, radius=12, border="#C9BFAF", border_w=1,
    )
    objs.append(visible(
        cancel, "pause_cancel_restart_visible", hidden=True,
    ))


def build_resume_go(objs: list[dict], content: int) -> None:
    resume = len(objs)
    objs.append(visible(
        layer(
            content, 0, 48, CONTENT_W, 432, hidden=True,
            name="resume_go_layer",
        ),
        "resume_go_visible",
        hidden=True,
    ))
    objs.append(container(
        resume, 0, 0, CONTENT_W, 432, bg=BG, opacity=190,
        name="resume_go_scrim",
    ))
    objs.append(label(
        resume, 0, 164, CONTENT_W, 104, "GO", size=44,
        color=ACCENT, align="center", name="resume_go_label",
        font=BOLD, font_charset=RUNTIME_CHARSET,
    ))


def build_result(objs: list[dict], content: int) -> None:
    result = len(objs)
    objs.append(visible(
        layer(
            content, 0, 0, CONTENT_W, CONTENT_H, hidden=True,
            name="result_layer",
        ),
        "result_visible",
        hidden=True,
    ))
    objs.append(container(
        result, 0, 0, CONTENT_W, CONTENT_H, bg=PAPER,
        name="result_background",
    ))
    objs.append(dynamic_label(
        result, 20, 34, 440, 50, "FIELD CLEAR",
        "result_title", size=32, color=PAPER_INK,
        align="center", font=BOLD,
    ))
    objs.append(dynamic_label(
        result, 20, 86, 440, 28, "All safe tiles secured",
        "result_reason", size=14, color=PAPER_MUTED, align="center",
    ))
    objs.append(dynamic_label(
        result, 64, 128, 144, 24, "TIME",
        "result_score_label", size=14, color=PAPER_MUTED, align="right",
    ))
    objs.append(dynamic_label(
        result, 220, 116, 196, 48, "00:58",
        "result_score", size=32, color=ACCENT, font=BOLD,
    ))

    for index in range(3):
        x = 20 + index * 148
        objs.append(container(
            result, x, 184, 136, 78, bg="#E9E3D8", radius=12,
            border="#D2C9BA", border_w=1,
            name=f"result_metric_{index + 1}_card",
        ))
        objs.append(dynamic_label(
            result, x + 8, 196, 120, 18,
            ("MOVES", "FLAGS", "OPEN")[index],
            f"result_metric_{index + 1}_label",
            size=14, color=PAPER_MUTED, align="center",
        ))
        objs.append(dynamic_label(
            result, x + 8, 218, 120, 32, "0",
            f"result_metric_{index + 1}_value",
            size=24, color=PAPER_INK, align="center", font=BOLD,
        ))

    objs.append(container(
        result, 20, 278, 440, 58, bg="#E9E3D8", radius=12,
        name="result_best_card",
    ))
    objs.append(dynamic_label(
        result, 36, 294, 180, 26, "BEST TIME",
        "result_best_label", size=14, color=PAPER_MUTED,
    ))
    objs.append(dynamic_label(
        result, 232, 288, 212, 36, "00:58",
        "result_best", size=24, color=ACCENT, align="right", font=BOLD,
    ))
    objs.append(action_button(
        result, 20, 370, 284, 62, "PLAY AGAIN", "result_retry",
        bg=ACCENT, fg=INK, size=18, radius=14,
    ))
    objs.append(action_button(
        result, 316, 370, 144, 62, "ARCADE", "result_menu",
        bg=PAPER, fg=PAPER_INK, size=18, radius=14,
        border="#BEB4A5", border_w=1,
    ))


def main() -> None:
    objs, content = shared_prefix(IMAGES, FONT_POLICIES, BOLD)
    objs.append(container(
        content, 0, 0, CONTENT_W, CONTENT_H, bg=BG,
        name="classic_games_background",
    ))
    build_home(objs, content)
    build_onboarding(objs, content)
    build_game(objs, content)
    build_pause_overlay(objs, content)
    build_resume_go(objs, content)
    build_result(objs, content)

    for obj in objs:
        if obj.get("type") in ("label", "button") and obj.get("font_size"):
            obj.setdefault("font_charset", RUNTIME_CHARSET)

    output = scene_out_path(HERE, "classic_games_480.json")
    write_scene(
        output,
        "classic_games",
        objs,
        font=FONT,
    )
    # This scene's five packs fit the per-scene budget. Keeping them local
    # also makes the App bundle self-contained when launched directly by the
    # simulator or loaded independently on device.
    scene = json.loads(output.read_text(encoding="utf-8"))
    scene["font_link"] = "embedded"
    output.write_text(
        json.dumps(scene, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
