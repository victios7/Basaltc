# Windows packaging

GitHub Actions (`windows-latest`) builds an MSI with **WiX Toolset v3**.

Payload expected:

```
payload/
  bin/basaltc.exe  basm.exe  pe_tool.exe
  lib/basalt_rt.o
  share/examples/  languages/
```

Today Basalt’s primary host is Linux; the Windows job packages PE32+ toolchain
bits (`pe_tool`) and any Windows binaries produced by the Linux cross job.
When native Windows `basaltc.exe` is available, place it in `payload/bin/`.
