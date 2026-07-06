#!/usr/bin/env bash
# Выбираем эффект через fzf и применяем его к монитору через сокет

SELECTED_EFFECT=$(interactive-wallpaper --list-plugins | jq -r '.[]' | fzf --prompt="Select Wallpaper Effect: ")

if [ -n "$SELECTED_EFFECT" ]; then
    shader-desk-ctl "core.outputs['eDP-1'].effect = '$SELECTED_EFFECT'; core.reload()"
fi