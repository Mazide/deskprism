#pragma once

// Mirror the logical image horizontally, then rotate 90 degrees CCW.
// Source (x,y) -> mirrored (size-1-x,y) -> panel (y,x).
// The reflection in the prism restores the intended handedness.
namespace optics {
constexpr int sourceIndex(int panelX, int panelY, int size, bool mirror) {
  const int sourceX = mirror ? panelY : size - 1 - panelY;
  const int sourceY = panelX;
  return sourceY * size + sourceX;
}
}
