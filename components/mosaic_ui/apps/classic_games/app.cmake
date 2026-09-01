set(MOSAIC_APP_NAME classic_games)
set(MOSAIC_APP_MODULE_SOURCE classic_games_app.c)
set(MOSAIC_APP_MODULE_SYMBOL mosaic_classic_games_app)
set(MOSAIC_APP_BUNDLE generated/classic_games.gspb)
set(MOSAIC_APP_SCENE_DIR scene)
set(MOSAIC_APP_SCENE_JSON scene/classic_games_480.json)
set(MOSAIC_APP_GENERATOR scene/gen_scene.py)
set(MOSAIC_APP_SCENE_SOURCES
    assets/hub-mines-v13.png
    assets/hub-snake.png
    assets/hub-blocks.png
    assets/bird-arcade-player-v6.png
    assets/bird-arcade-bg-v6.png
    assets/bird-arcade-pipe-body-v6.png
    assets/bird-arcade-pipe-cap-v6.png
    assets/mine-flag.png
    assets/mine-hit.png
    assets/snake-head.png
    assets/snake-food.png
    assets/bird-arcade-gem-v6.png
    assets/game-canvas-placeholder.png
    assets/launcher-bricks.png
    scene/generate_canvas_art.py)
set(MOSAIC_APP_EXTRA_SOURCES classic_games_model.c)
set(MOSAIC_APP_LOGIC NATIVE)
set(MOSAIC_APP_TICK_MS 16)
