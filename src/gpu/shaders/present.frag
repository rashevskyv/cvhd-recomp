#version 460
// Present pass: the guest front buffer, plus the fps counter fed by deko3d_backend.cpp IssueSwap.
// Straight-line float math only (no integer or bit operations, no branches): Eden died with an
// uncaught exception at the first frame that drew the integer version of this shader (finding
// 2026-10-27). size.xy = the output in pixels, size.z = cells drawn; cells[2c] = rows 0-3 and
// cells[2c+1].x = row 4 of cell c's 3x5 glyph, each row a value 0-7 (bit 2 = left column). A cell is
// 16x24 px, a glyph 12x20 (4x scale), at (8,8) from the top-left.
layout(binding = 0) uniform sampler2D front;
layout(std140, binding = 0) uniform Hud { vec4 size; vec4 cells[16]; };
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
float eq(float x, float k) { return step(abs(x - k), 0.5); }  // 1.0 when the integer-valued x equals k
void main() {
  color = vec4(texture(front, uv).rgb, 1.0);
  vec2 p = floor(uv * size.xy) - vec2(8.0, 8.0);
  float cell = floor(p.x / 16.0);
  float inside = step(0.0, p.x) * step(0.0, p.y) * step(p.y, 23.5) * step(cell, size.z - 0.5);
  vec2 g = floor(vec2(p.x - cell * 16.0, p.y) / 4.0);  // glyph unit: column 0-3, row 0-5
  vec4 a = cells[0] * eq(cell, 0.0) + cells[2] * eq(cell, 1.0) + cells[4] * eq(cell, 2.0) + cells[6] * eq(cell, 3.0) +
           cells[8] * eq(cell, 4.0) + cells[10] * eq(cell, 5.0) + cells[12] * eq(cell, 6.0) + cells[14] * eq(cell, 7.0);
  vec4 b = cells[1] * eq(cell, 0.0) + cells[3] * eq(cell, 1.0) + cells[5] * eq(cell, 2.0) + cells[7] * eq(cell, 3.0) +
           cells[9] * eq(cell, 4.0) + cells[11] * eq(cell, 5.0) + cells[13] * eq(cell, 6.0) + cells[15] * eq(cell, 7.0);
  float row = dot(a, vec4(eq(g.y, 0.0), eq(g.y, 1.0), eq(g.y, 2.0), eq(g.y, 3.0))) + b.x * eq(g.y, 4.0);
  float bit = eq(g.x, 0.0) * step(4.0, row) + eq(g.x, 1.0) * mod(floor(row * 0.5), 2.0) + eq(g.x, 2.0) * mod(row, 2.0);
  color = mix(color, mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0), bit), inside);
}
