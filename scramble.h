#pragma once

#include <stdint.h>
#include <string.h>
#include <math.h>

// Adapted from bas3line/ascii's scramble.ts (MIT; THIRD_PARTY_NOTICES.txt).
// https://ascii.rest/scramble/ — stagger, jitter, hold and glyph hash.
namespace scramble {
class Animation {
 public:
  static constexpr int cols = 20;

  void begin(const char* text = "LOADING", uint32_t now = 0, bool repeat = true) {
    place(text, target);
    memcpy(source, target, sizeof(source));
    startedAt = now;
    repeating = repeat;
    configure();
    offset = period - .7f;  // Start with a readable label, as in the reference.
  }

  void setText(const char* text, uint32_t now, bool repeat = false) {
    char next[cols+1];
    place(text, next);
    if (!strcmp(next, target) && repeat == repeating) return;
    // Continue from exactly what is currently visible, even mid-transition.
    line(now, source);
    memcpy(target, next, sizeof(target));
    startedAt = now;
    repeating = repeat;
    configure();
    offset = 0;
  }

  void line(uint32_t nowMs, char (&output)[cols+1]) const {
    const float elapsed = uint32_t(nowMs - startedAt) * .001f + offset;
    const float now = repeating ? fmodf(elapsed, period) : fminf(elapsed, period);
    const bool laterCycle = repeating && elapsed >= period;
    const uint32_t tick = uint32_t(now * 20);
    // Copy first: output may alias source when setText snapshots the frame.
    char old[cols+1];
    memcpy(old, laterCycle ? target : source, sizeof(old));
    memset(output, ' ', cols); output[cols] = 0;
    const char* glyphs = "!<>-_\\/[]{}=+*^?#%&$@0123456789abcdefxyz";
    for (int c = lo; c < hi; ++c) {
      const char a = old[c], b = target[c];
      if (now < out[c]) output[c] = a;
      else if (now >= settle[c]) output[c] = b;
      else if (a == ' ' && b == ' ' && now < out[c]+.12f) output[c]=' ';
      else {
        uint32_t h = (uint32_t(c) ^ (tick * 0x27d4eb2du)) * 0x9e3779b1u;
        h = (h ^ (h >> 15)) * 0x85ebca77u;
        output[c] = glyphs[(h ^ (h >> 13)) % strlen(glyphs)];
      }
    }
  }

 private:
  char source[cols+1] = {}, target[cols+1] = {};
  float out[cols] = {}, settle[cols] = {};
  float period = 1, offset = 0;
  int lo = 0, hi = 0;
  bool repeating = true;
  uint32_t startedAt = 0, state = 77;

  static void place(const char* text, char (&result)[cols+1]) {
    unsigned length = 0;
    if (text) while (length < cols && text[length]) ++length;
    memset(result, ' ', cols); result[cols] = 0;
    const unsigned start = (cols-length)/2;
    for (unsigned i=0; i<length; ++i)
      result[start+i] = text[i] >= 32 && text[i] <= 126 ? text[i] : '?';
  }
  void configure() {
    state = 77; lo = cols; hi = 0;
    for (int c=0; c<cols; ++c) if (source[c]!=' ' || target[c]!=' ') {
      if (c<lo) lo=c;
      hi=c+1;
    }
    period = .32f;
    for (int c=lo; c<hi; ++c) {
      out[c]=(c-lo)*.022f+random()*.06f;
      settle[c]=.32f+(c-lo)*.05f+random()*.14f;
      if (settle[c]>period) period=settle[c];
    }
    period += 1.9f;
  }
  float random() {
    state += 0x6d2b79f5u;
    uint32_t t = (state ^ (state>>15)) * (1u | state);
    t = (t + ((t ^ (t>>7)) * (61u | t))) ^ t;
    return (t ^ (t>>14)) / 4294967296.0f;
  }
};
}
