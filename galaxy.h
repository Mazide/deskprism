#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace galaxy {
// Procedural 3D particle galaxy. Positions are generated once, then rotated
// and projected with perspective; no network, textures or prerecorded frames.
class Animation {
 public:
  static constexpr int size = 240;
  static constexpr int starCount = 2600;

  void begin(uint32_t seed = 1) {
    randomState = seed ? seed : 1;
    for (int i = 0; i < starCount; ++i) {
      Star& star = stars[i];
      const bool bulge = i < 450;
      const float radius = bulge ? 0.22f * powf(unit(), 0.65f) : 0.12f + 0.88f * sqrtf(unit());
      const bool diffuse = i >= 450 && i < 850;
      const float angle = (bulge || diffuse) ? unit() * tau : (i % 3) * tau / 3 + radius * 6.8f + gaussian() * (0.12f + 0.20f * radius);
      star.x = radius * cosf(angle);
      star.y = radius * sinf(angle);
      star.z = gaussian() * (bulge ? 0.065f : 0.018f) * (1 - radius * 0.5f);
      const float intensity = 0.28f + 0.72f * unit();
      // Golden bulge, icy blue arms and a violet outer edge.
      if (bulge) {
        star.r = uint8_t(180 * intensity);
        star.g = uint8_t(145 * intensity);
        star.b = uint8_t(85 * intensity);
      } else {
        star.r = uint8_t((60 + 65 * radius) * intensity);
        star.g = uint8_t((145 - 65 * radius) * intensity);
        star.b = uint8_t(230 * intensity);
      }
      const float phase = unit() * tau;
      star.phaseCos = cosf(phase);
      star.phaseSin = sinf(phase);
      star.glow = i % 19 == 0;
    }
    for (auto& star : background) {
      star.x = 8 + unit() * 224;
      star.y = 8 + unit() * 224;
      star.phase = unit() * tau;
    }
    for (int y = -24; y <= 24; ++y)
      for (int x = -24; x <= 24; ++x)
        halo[(y+24)*49+x+24] = uint8_t(42 * expf(-(x*x + y*y) / 140.0f));
  }

  void render(uint32_t elapsedMs, uint16_t* pixels) const {
    memset(pixels, 0, size * size * sizeof(uint16_t));
    const float t = elapsedMs * 0.001f;
    // Arm angle increases outwards, so rotate the opposite way:
    // the outer ends trail behind the orbiting stars.
    const float angle = -t * 0.20f;
    const float c = cosf(angle), s = sinf(angle);
    const float tilt = 0.97f + 0.16f * sinf(t * 0.35f);
    const float ct = cosf(tilt), st = sinf(tilt);
    const float yaw = 0.20f * sinf(t * 0.22f);
    const float cy = cosf(yaw), sy = sinf(yaw);
    for (const auto& star : background) {
      const int light = int(24 + 12 * sinf(t + star.phase));
      add(pixels, int(star.x), int(star.y), light / 2, light * 3 / 4, light);
    }
    for (int y = -24; y <= 24; ++y) {
      for (int x = -24; x <= 24; ++x) {
        const unsigned light = halo[(y+24)*49+x+24];
        add(pixels, 120+x, 120+y, light, light * 4 / 5, light / 2);
      }
    }
    for (const auto& star : stars) {
      const float rx = star.x * c - star.y * s;
      const float ry = star.x * s + star.y * c;
      const float tiltedY = ry * ct - star.z * st;
      const float tiltedZ = ry * st + star.z * ct;
      const float x = rx * cy + tiltedZ * sy;
      const float z = -rx * sy + tiltedZ * cy;
      const float scale = 92 / (1.0f + z * 0.24f);
      const float px = 120 + x * scale, py = 120 + tiltedY * scale;
      // Slow twinkle without a trigonometric call for every particle.
      const float shimmer = 0.88f + 0.12f * (c * star.phaseCos + s * star.phaseSin);
      point(pixels, px, py, star.r * shimmer, star.g * shimmer, star.b * shimmer);
      if (star.glow) {
        point(pixels, px-1, py, star.r*.16f, star.g*.16f, star.b*.16f);
        point(pixels, px+1, py, star.r*.16f, star.g*.16f, star.b*.16f);
        point(pixels, px, py-1, star.r*.16f, star.g*.16f, star.b*.16f);
        point(pixels, px, py+1, star.r*.16f, star.g*.16f, star.b*.16f);
      }
    }
  }

 private:
  static constexpr float tau = 6.2831853f;
  struct Star { float x, y, z, phaseCos, phaseSin; uint8_t r, g, b; bool glow; };
  struct Background { float x, y, phase; };
  Star stars[starCount] = {};
  Background background[55] = {};
  uint8_t halo[49 * 49] = {};
  uint32_t randomState = 1;

  float unit() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (randomState >> 8) / 16777216.0f;
  }
  float gaussian() { return unit() + unit() + unit() + unit() - 2; }
  static unsigned limit(unsigned value, unsigned max) { return value < max ? value : max; }
  static void add(uint16_t* pixels, int x, int y, unsigned r, unsigned g, unsigned b) {
    if (x < 0 || x >= size || y < 0 || y >= size) return;
    uint16_t& p = pixels[y*size+x];
    p = (limit((p>>11) + (r>>3), 31) << 11) |
        (limit(((p>>5)&63) + (g>>2), 63) << 5) | limit((p&31) + (b>>3), 31);
  }
  static void point(uint16_t* pixels, float x, float y, float r, float g, float b) {
    const int ix = int(floorf(x)), iy = int(floorf(y));
    const float fx = x-ix, fy = y-iy;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        const float weight = (dx ? fx : 1-fx) * (dy ? fy : 1-fy);
        add(pixels, ix+dx, iy+dy, unsigned(r*weight), unsigned(g*weight), unsigned(b*weight));
      }
  }
};
}
