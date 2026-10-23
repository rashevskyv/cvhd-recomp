# Backend fixes from the Hard Corps port (apply to the deko3d backend)

1. CompareOp off by one in src/gpu/deko3d_backend.cpp: Xenos kNever = 0, DkCompareOp_Never = 1, so return DkCompareOp(uint32_t(f) + 1). LEQUAL was EQUAL.
2. Resolve() must clear depth when rb_copy_control.depth_clear_enable is set.
Commits in D:\git\dev\hcu-switch: 7b4c2af, 793fc06, 458c237; full patch: hcu-backend-fixes.patch (relative to the cvhd tree).
