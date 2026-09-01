#!/bin/sh
# Build the WebAssembly module into docs/, which GitHub Pages serves.
# Requires an emsdk environment (source emsdk_env.sh first).
set -e
cd "$(dirname "$0")/.."

mkdir -p docs
EMCC="${EMCC:-emcc}"
"$EMCC" -O2 -std=c++20 \
    src/raster.cpp src/doc.cpp src/app.cpp src/wasm_main.cpp \
    -sALLOW_MEMORY_GROWTH=1 \
    -sSINGLE_FILE=1 \
    -sMODULARIZE=1 -sEXPORT_NAME=createCamplan \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAPF32 \
    -sEXPORTED_FUNCTIONS=_cp_init,_cp_resize,_cp_dirty,_cp_render,_cp_mouse_down,_cp_mouse_move,_cp_mouse_up,_cp_wheel,_cp_key,_cp_set_mode,_cp_get_mode,_cp_set_theme,_cp_zoom_fit,_cp_set_marker,_cp_get_marker,_cp_undo,_cp_redo,_cp_camera_at,_cp_sel_number,_cp_sel_dir,_cp_sel_fov,_cp_sel_range,_cp_sel_set_number,_cp_sel_set_dir,_cp_sel_set_fov,_cp_sel_set_range,_cp_delete_selected,_cp_select_number,_cp_camera_count,_cp_camera_number_at,_cp_set_background,_cp_set_background_pixels,_cp_clear_background,_cp_bg_bytes,_cp_bg_size,_cp_save,_cp_save_size,_cp_load,_cp_export_render,_cp_export_w,_cp_export_h,_cp_alloc,_cp_free \
    -o docs/camplan.js

# A browser happily keeps yesterday's camplan.js under today's index.html,
# and the mismatch surfaces as 'M._cp_xxx is not a function'.  A new build is
# a new URL, so it can never come out of the cache.
V="$(date -u '+%Y%m%d%H%M%S')"
sed "s/src=\"camplan.js\"/src=\"camplan.js?v=$V\"/" web/index.html > docs/index.html
echo "built docs/camplan.js + docs/index.html (v=$V)"
