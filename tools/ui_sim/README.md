# Luxor UI simulator

Runs the real `main/ui.c` with LVGL 8.3.11 on a PC (Linux, WSL or macOS), with no watch connected.

```bash
git clone --depth 1 --branch v8.3.11 https://github.com/lvgl/lvgl.git ~/lvgl
cd tools/ui_sim
make LVGL_DIR=~/lvgl -j8          # build ./luxor_sim
make LVGL_DIR=~/lvgl check        # audit every screen x every theme
make LVGL_DIR=~/lvgl gallery      # gallery_ivory.png + gallery_noir.png (needs python3-pil)
./luxor_sim render <theme> <app> <battery%> out.raw [x y]   # one frame; x y = show that control pressed
```

`check` fails when any label's text is wider than its box (clipped text), or uses a character missing from its font (a tofu box). Run it after every UI change, next to `make -C tests`.

App numbers: 0 home, 1 display, 2 settings, 3 music, 4 calc, 5 clock, 6 maze, 7 steps, 8 time, 9 motion, 10 about. Themes 0–5 in Settings order.
