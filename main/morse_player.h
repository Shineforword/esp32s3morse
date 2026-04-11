#pragma once

#include "led_strip.h"

#include <stdbool.h>

void morse_player_init(led_strip_handle_t strip);
bool morse_player_preview_symbol(char sym);
bool morse_player_submit_play(const char *pattern);
