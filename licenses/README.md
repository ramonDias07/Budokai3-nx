# Licences of bundled components

`B3deko-Installer.exe` is a single file that carries the helper programs below. They run only on your PC, to turn your own game disc into the files the Switch port needs. The licence text of each component is in this folder. The port's own code is GPL-3.0 (see `../LICENSE`).

| Component | What it does in the installer | Licence | File |
| --- | --- | --- | --- |
| ReXGlue (rexglue.exe, librexruntime.dll) | Decrypts and decompresses the game executable from your disc (no code is generated or kept). | BSD-3-Clause | `rexglue-BSD.txt` |
| XenosRecomp | Converts the Xbox 360 shaders found in your game to HLSL. | MIT | `XenosRecomp-MIT.txt` |
| NovaPowers recompilation base | Origin of the recompilation set-up the port builds on. | MIT | `novapowers-MIT.txt` |
| DirectX Shader Compiler (dxcompiler.dll, v1.8.2407) | Used by XenosRecomp to compile HLSL to SPIR-V. | University of Illinois/NCSA (with LLVM components) | `DirectXShaderCompiler-LICENSE.txt` (full text, tag v1.8.2407) and `DirectXShaderCompiler-NOTICE.txt` |
| SPIRV-Cross (spirv-cross.exe, Vulkan SDK 1.4.304.0) | Converts SPIR-V to GLSL for the Switch GPU. | Apache-2.0 | `SPIRV-Cross-Apache-2.0.txt` |
| uam (uam.exe) | Compiles GLSL to the Switch GPU shader binary (DKSH). | zlib-style licence plus Mesa (MIT) parts | `uam-zlib-and-Mesa-MIT.txt` |
| LLVM-MinGW runtime (libc++.dll, libunwind.dll) | C++ runtime needed by the helper programs. | Apache-2.0 with LLVM exceptions | `LLVM-MinGW-runtime-Apache-2.0-LLVM-exception.txt` |
| winpthreads (libwinpthread-1.dll) | Threading runtime from MinGW-w64, needed by the helper programs. | MIT-style | `winpthreads-MinGW-w64.txt` |
| Python 3.13 | Runs the installer's own scripts (embedded; no Python install is needed). | PSF License | `Python-PSF.txt` |
| xxhash (Python module 4.0.2) | Hashing used when building the shader cache. | BSD-2-Clause | `xxhash-python-BSD-2-Clause.txt` |
| PyInstaller 6.22.3 (bootloader and runtime) | Packs everything into the single .exe. | GPL-2.0 with the Bootloader Exception | `PyInstaller-GPL-2.0-bootloader-exception.txt` |
| PyInstaller hooks contrib | Packaging hooks used while building the .exe. | GPL-2.0 / Apache-2.0 | `PyInstaller-hooks-contrib-GPL-2.0-Apache-2.0.txt` |


The installer's source is `tools/b3_installer.py` and it is rebuilt with `tools/build_installer.ps1`. No game file or shader derived from a game is published by this project.
