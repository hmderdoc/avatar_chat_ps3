# PSL1GHT header shim

Stand-in headers with the PSL1GHT declarations `src/platform/ps3/platform_ps3.c`
uses, so the PS3 platform file can be *syntax-checked* on a normal PC
(`make -f Makefile.host check-ps3`). They are not the real SDK: the
signatures were written from PSL1GHT's public headers and may lag behind
them. A real PS3 build needs the ps3dev toolchain (see README.md).
