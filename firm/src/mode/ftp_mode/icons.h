#pragma once
#include <esper-gui/graphics.h>

namespace {
  const uint8_t icn_file_data[] = {
    0b00011110,
    0b00110010,
    0b01110010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01111110,
    0b00000000
  };
  const uint8_t icn_dir_data[] = {
    0b01110000,
    0b10001000,
    0b10001111,
    0b10000001,
    0b10000001,
    0b10000001,
    0b01111110,
    0b00000000
  };
  const uint8_t icn_dir_open_data[] = {
    0b01110000,
    0b10001000,
    0b10000111,
    0b11111001,
    0b10000111,
    0b11000001,
    0b01111110,
    0b00000000
  };
}

const EGImage icn_file = {
    .format = EG_FMT_HORIZONTAL,
    .size = {8, 8},
    .data = icn_file_data
};

const EGImage icn_dir = {
    .format = EG_FMT_HORIZONTAL,
    .size = {8, 8},
    .data = icn_dir_data
};

const EGImage icn_dir_open = {
    .format = EG_FMT_HORIZONTAL,
    .size = {8, 8},
    .data = icn_dir_open_data
};