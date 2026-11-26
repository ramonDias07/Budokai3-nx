# Compilar e instalar

> Este guia é para compilar do código-fonte. Quem só quer jogar pode usar o `B3deko-Installer.exe` da release, que não precisa de devkitPro, Python nem compilador (veja o [README](../README.md#instalar)).

O build para Switch parte da edição **USA/Japan para Xbox 360**. O extrator lê `DBZ3/yae3_xenon.xex` dentro da ISO e verifica SHA-256 `b40bba40cfd6c90cb269ebf5020818924f43109bcc86827a3cc37124c052a26b`. Depois salva o executável como `assets/game_root/default.xex` e extrai os arquivos `DBZ3/us/`. O menu da coletânea e os arquivos de Budokai 1 ficam no disco.

## Jeito fácil (Windows): `install.bat`

1. Instale o [devkitPro](https://github.com/devkitPro/installer/releases) (`devkitProUpdater-*.exe`) e marque **Switch Development**. Mantenha a pasta `C:\devkitPro`.
2. Baixe **Source code (zip)** da release, extraia numa pasta com uns 20 GB livres e dê dois cliques em `install.bat`.
3. O instalador confere as ferramentas. Se faltar Git, Python, CMake ou Ninja, ele oferece instalar pelo `winget`. Ele baixa o LLVM-MinGW e o DXC nas versões usadas pelas builds publicadas e confere o SHA-256 de cada um.
4. Escolha o seu `.iso` USA/Japan na janela que abrir e espere. A primeira vez leva de uma a duas horas.
5. Ele baixa a tradução PT-BR de TheRalf (pacote `ptbr.b3tr`, cerca de 80 MB) e a coloca em `B3deko/traducao/`. Para não incluir, rode `install.bat -NoTranslation`.
6. No fim, ele abre `out\sd\switch\`. Copie a pasta `B3deko` para `/switch/` no cartão e siga a [etapa 3](#3-copiar-para-o-switch).

Se algo falhar, a mensagem aparece em vermelho e os detalhes ficam em `out\install.log`. Rodar de novo continua de onde parou. O resto deste guia descreve os mesmos passos à mão, para Linux e para quem já tem as ferramentas.

## 1. Preparar as ferramentas

- Git, CMake 3.25 ou superior, Ninja e Python 3.11 ou superior com o módulo `xxhash`.
- Clang para as ferramentas de PC. No Windows, use LLVM-MinGW. Abra o terminal com o compilador no `PATH`.
- DirectXShaderCompiler para o seu sistema. No Windows, use o pacote que contém `inc/`, `lib/x64/` e `bin/x64/`. No Linux, use a distribuição com `include/dxc/` e `lib/libdxcompiler.so`. [DXC](https://github.com/microsoft/DirectXShaderCompiler/releases)
- devkitPro com devkitA64, libnx, ferramentas Switch e portlibs necessárias ao SDK. [devkitPro](https://devkitpro.org/wiki/Getting_Started)

Na raiz do repositório, prepare o SDK e as bibliotecas. Isso grava a cópia em `sdk/` e aplica os patches locais.

```sh
python tools/prepare_sdk.py
cmake -S sdk -B out/sdk-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_D3D12=OFF -DREXGLUE_USE_VULKAN=ON
cmake --build out/sdk-host --target rexglue
```

Use o `rexglue` encontrado em `sdk/out/` para seu sistema. Essa versão tem a opção `REXGLUE_DUMP_IMAGE` usada pelo gerador de shaders. No Windows, mantenha as bibliotecas do host ao lado do executável.
Se o CMake pedir `PYTHON_EXECUTABLE`, acrescente `-DPYTHON_EXECUTABLE=/caminho/do/python` à configuração do SDK.

Crie as ferramentas gráficas. Use o caminho absoluto do pacote DXC em `DXC_ROOT`.

```sh
cmake -S tools/xenos -B out/xenos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DDXC_ROOT=/caminho/do/dxc
cmake --build out/xenos --target XenosRecomp
cmake -S sdk/thirdparty/SPIRV-Cross -B out/spirv-cross -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF
cmake --build out/spirv-cross --target spirv-cross
cmake -S tools/vendor/uam -B out/uam -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/uam --target uam_tool
```

## 2. Gerar o pacote a partir da ISO

Informe os caminhos dos executáveis produzidos na etapa anterior. No Windows use os nomes com `.exe`. `--devkitpro` aponta para a pasta que contém `devkitA64`, `libnx` e `tools`.

```sh
python tools/build.py "/caminho/Budokai HD Collection (USA, Japan).iso" \
  --rexglue /caminho/rexglue --xenosrecomp out/xenos/XenosRecomp \
  --spirv-cross out/spirv-cross/spirv-cross --uam out/uam/uam \
  --devkitpro /opt/devkitpro
```

O script extrai o jogo, roda o codegen, gera a biblioteca NFSP e o cache Deko, compila para ARM64 e monta o pacote. A saída usa o ícone padrão do libnx. É possível adicionar um JPEG próprio de 256 × 256 com `--icon`.

## 3. Copiar para o Switch

Copie a pasta inteira `out/sd/switch/B3deko/` para `/switch/` no cartão. Abra o Homebrew Menu em modo aplicativo, segurando `R` ao iniciar um jogo, e selecione `B3.nro`.

```text
/switch/B3deko/
  B3.nro
  budokai3.toml
  b3_shaders.nfsp
  shadercache_dksh.bin
  game_root/default.xex
  game_root/us/...
```

O port cria os saves e logs na primeira execução. Preserve `budokai3/` ao atualizar. `assets/`, `app/generated/` e `out/` são resultados locais e ficam fora do Git.
