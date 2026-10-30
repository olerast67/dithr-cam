idf_component_register(SRCS "fx_core.c" "fx_preview.c" "fx_neon.c" "fx_y2k.c" "fx_melt.c"
                            "fx_ghost.c" "fx_mosh.c" "fx_dmg.c" "fx_ink.c" "png_write.c" "font5x7.c"
                            "fx_tools.c" "fx_bluenoise.c" "fx_tide.c" "fx_pulse.c"
                            "fx_shard.c" "fx_holo.c" "fx_loop.c" "fx_topo.c" "fx_preview2.c"
                       INCLUDE_DIRS "include")
# Эффекты считают попиксельно: -O2 даёт заметный выигрыш против -Os.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
