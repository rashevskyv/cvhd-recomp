# SDK patches

`sdk/*.patch` apply on ReXGlue v0.10.0 (`c94f5eb`). They come from
giangra/stuntdouble-reignition (`patches/sdk`, BSD 3-Clause, the SDK's licence):

- `0001-codegen-eh-aware-function-discovery.patch`: `noreturn` functions in the manifest, code
  pointers found in data (`data_pointer_scan`) and in `lis`+`addi/ori` constants (`code_pointer_scan`),
  so callbacks the game registers at run time are recompiled instead of hitting
  "Call to invalid or unregistered function".
- `0002-fpscr-keep-host-exception-masks.patch`: `mtfsf` starts from the live MXCSR, so a context whose
  copy was never loaded does not unmask host FP exceptions.

Apply: `git -C /root/cvhd/rexglue-sdk apply patches/sdk/*.patch`, rebuild `rexglue`.
