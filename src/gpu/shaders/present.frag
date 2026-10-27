#version 460
// Present pass: the guest front buffer, plus the fps counter fed by deko3d_backend.cpp IssueSwap:
// up to 8 glyphs of a 3x5 font, 4 bits per cell in text.x (0-9 digits, 10 '.', 11 blank);
// text.y/z = the output size in pixels, text.w = cells drawn (black backdrop). Positioned from uv,
// which is right side up (the picture is), not from gl_FragCoord.
layout(binding = 0) uniform sampler2D front;
layout(std140, binding = 0) uniform Hud { uvec4 text; };
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
const uint glyphs[11] = uint[11](0x7B6Fu, 0x2C97u, 0x73E7u, 0x73CFu, 0x5BC9u, 0x79CFu, 0x79EFu, 0x7249u,
                                 0x7BEFu, 0x7BCFu, 0x0002u);
void main() {
  color = vec4(texture(front, uv).rgb, 1.0);
  ivec2 p = ivec2(uv * vec2(text.yz)) - ivec2(8, 8);  // 4x scale: a cell is 16x24 px, a glyph 12x20
  int cell = p.x >> 4;
  if (p.x < 0 || p.y < 0 || p.y >= 24 || cell >= int(text.w)) return;
  color = vec4(0.0, 0.0, 0.0, 1.0);
  uint code = (text.x >> uint(cell * 4)) & 15u;
  ivec2 g = ivec2((p.x & 15) >> 2, p.y >> 2);
  if (code > 10u || g.x >= 3 || g.y >= 5) return;
  if (((glyphs[code] >> uint(14 - g.y * 3 - g.x)) & 1u) != 0u) color = vec4(1.0);
}
