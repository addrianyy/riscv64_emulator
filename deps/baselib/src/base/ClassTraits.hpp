#pragma once

#define CLASS_NON_COPYABLE(target) \
  target(const target&) = delete;  \
  target& operator=(const target&) = delete;

#define CLASS_NON_MOVABLE(target) \
  target(target&&) = delete;      \
  target& operator=(target&&) = delete;

#define CLASS_NON_COPYABLE_NON_MOVABLE(target) \
  target(const target&) = delete;              \
  target& operator=(const target&) = delete;   \
  target(target&&) = delete;                   \
  target& operator=(target&&) = delete;
