#version 460
// Present pass: the guest front buffer, plus the text overlay fed by deko3d_backend.cpp IssueSwap (the
// fps counter, or the settings menu of nx_menu.cpp): a grid of cols x rows cells, each a 5x7 glyph in a
// 6x8-unit cell at style.x pixels per unit. Float math only but for the uniform index (Eden died on an
// integer/bit-operation version of this shader, finding 2026-10-27): a glyph's rows are two integer-
// valued floats (rows 0-3 as 20 bits, rows 4-6 as 15 bits) decoded with floor and mod.
// size.xy = the output in pixels, size.z = cols (0 = nothing to draw), size.w = rows;
// grid = (x0, y0, cell w, cell h) in pixels; style = (scale, box alpha, box on, 0);
// rowcol[r] = the text colour of row r, .a = 1 for the highlighted row;
// cells[i/2].xy or .zw = the glyph of cell i (row-major).
layout(binding = 0) uniform sampler2D front;
layout(std140, binding = 0) uniform Hud { vec4 size; vec4 grid; vec4 style; vec4 rowcol[16]; vec4 cells[384]; };
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
  color = vec4(texture(front, uv).rgb, 1.0);
  vec2 p = floor(uv * size.xy) - grid.xy;
  float col = floor(p.x / grid.z);
  float row = floor(p.y / grid.w);
  float inside = step(0.0, p.x) * step(0.0, p.y) * step(col, size.z - 0.5) * step(row, size.w - 0.5);
  col = clamp(col, 0.0, 47.0);
  row = clamp(row, 0.0, 15.0);
  int ri = int(row);
  float i = row * size.z + col;
  vec4 pair = cells[int(floor(i * 0.5))];
  float odd = mod(i, 2.0);
  float hi = mix(pair.x, pair.z, odd);
  float lo = mix(pair.y, pair.w, odd);
  // glyph unit inside the cell: gx 0-5, gy 0-7; the glyph covers gx 0-4, gy 0-6
  vec2 g = floor((p - vec2(col * grid.z, row * grid.w)) / style.x);
  float r = g.y;
  float rowbits = mix(mod(floor(hi / exp2(5.0 * (3.0 - r))), 32.0), mod(floor(lo / exp2(5.0 * (6.0 - r))), 32.0), step(3.5, r));
  float bit = mod(floor(rowbits / exp2(4.0 - g.x)), 2.0) * step(g.x, 4.5) * step(r, 6.5);
  vec4 rc = rowcol[ri];
  vec3 back = mix(vec3(0.055, 0.063, 0.094), vec3(0.235, 0.25, 0.376), rc.w);  // the box, the highlighted row
  color.rgb = mix(color.rgb, mix(color.rgb, back, style.y * style.z), inside);
  color.rgb = mix(color.rgb, rc.rgb, bit * inside);
}
