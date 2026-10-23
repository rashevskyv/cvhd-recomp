# Codegen flags gotcha (from the Guardian Heroes port)

The flags skip_lr, skip_msr, ctr/xer/reserved/cr/non_argument/non_volatile_as_local must sit inside the [entrypoint] table of cvhd_manifest.toml. At the top level they are ignored. Codegen also reuses old output when its stamp matches: use --ignore-stamp. Verify the generated code changed before measuring. cvhd_manifest.toml has none of these flags set today: try them one by one once the picture is right (the GPU side is the current blocker, not the CPU).
