Quarta release do **Budokai3-nx**, correspondente ao **B3deko 0.1.56**. Parte da [v0.1.50](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.50) e reúne as versões 0.1.51 a 0.1.56. As novidades são um instalador de um clique para Windows, a tradução PT-BR opcional, a legenda do menu rápido, os shaders de efeito que faltavam nos especiais e um pacote de desempenho.

## Instalação de um clique (Windows)

1. Instale o [devkitPro](https://github.com/devkitPro/installer/releases) (`devkitProUpdater-*.exe`) e marque **Switch Development**. Mantenha a pasta `C:\devkitPro`.
2. Baixe **Source code (zip)** em **Assets**, extraia numa pasta com uns 20 GB livres e dê dois cliques em `install.bat`.
3. O instalador confere Git, Python, CMake e Ninja e oferece instalar pelo `winget` o que faltar. Ele baixa o LLVM-MinGW e o DXC nas versões usadas nas builds publicadas e confere o SHA-256 de cada um.
4. Escolha o seu `.iso` **USA/Japan de Xbox 360** na janela que abrir. A primeira vez leva de uma a duas horas.
5. No fim, ele abre `out\sd\switch\`. Copie a pasta `B3deko` para `/switch/` no cartão.

Se algo falhar, a mensagem aparece em vermelho e os detalhes ficam em `out\install.log`. Rodar de novo continua de onde parou. Os passos à mão, para Linux e para quem já tem as ferramentas, estão no [guia de compilação](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.56/docs/build.md).

Ao atualizar, copie `B3.nro`, `budokai3.toml`, `b3_shaders.nfsp` e `shadercache_dksh.bin`. Se já houver um `budokai3.toml` no cartão, apague ou renomeie o arquivo para receber os novos padrões. Preserve a pasta `budokai3/`, que guarda os saves e dados.

## Tradução PT-BR incluída

A tradução é de **TheRalf**, feita para o *Dragon Ball Z: Budokai 3* de PS2 (patch v1.2). Ele a fez à moda antiga, redesenhando à mão diálogos, menus, cápsulas e gráficos, e levou muito tempo. **O mérito da tradução é todo dele.** O port só converte esse trabalho para a versão HD.

O pacote `ptbr.b3tr` vai anexado a esta release e o `install.bat` o coloca sozinho na pasta do jogo: não é preciso a ISO de PS2 nem o patch. No Switch, abra o menu rápido (`+` e `−` juntos), mude **LANGUAGE** para **PORTUGUESE (BR)**, escolha SAVE AND CLOSE e reinicie o jogo. Com a tradução desligada no início, o jogo fica igual e não paga nenhum custo extra. Quem compila à mão copia o `ptbr.b3tr` para `/switch/B3deko/traducao/`. [Como o pacote é gerado](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.56/docs/traducao-ptbr.md)

Os textos que a tradução original não mudou, cerca de 795, continuam em inglês. Também ficam sem tradução os retratos, os vídeos, o áudio e 4 texturas exclusivas do HD.

## Ainda não testado no console

As versões 0.1.51 a 0.1.56 ainda não foram testadas no Switch físico. As versões até a 0.1.49 foram testadas. Se a 0.1.56 mostrar erro de textura ou de efeito, desligue `nfsmw_dk_subida_directa` ou `nfsmw_dk_compartidas_push` no `budokai3.toml` e conte numa [issue](https://github.com/ramonDias07/Budokai3-nx/issues). Se as telas de menu travarem com a tradução ligada, use `b3_traducao_ptbr_max_growth_mb = 8`.

## O que mudou desde a v0.1.50

### 0.1.51

- Legenda **(+)+(-) : Quick Menu** no menu principal do jogo, abaixo do "BACK : Help" e no estilo do jogo. A opção MAIN MENU LEGEND fica no menu avançado.
- Os dois vertex shaders de efeito que faltavam, `7E3AB645941B0B1F` e `BFC8F848616208F3`, entraram no pacote. Eles desenham o feixe e o fogo dos especiais.
- O registro dos arquivos abertos pelo jogo passou a sair em inglês. Os caches antigos do Vulkan foram removidos.

### 0.1.52

- Tradução PT-BR opcional (veja acima). O pacote B3TR tem 1.810 entradas e a camada virtual do `data_usi.afs` só fica ativa com o PT ligado no início do jogo.

### 0.1.53

- O detector da legenda passou a reconhecer a dica "BACK : Help".
- O cache pré-compilado `shadercache_dksh.bin` ganhou os dois vertex shaders da 0.1.51. Antes o console falhava ao convertê-los e descartava os desenhos do feixe.
- Tradução: os botões do PS2 foram trocados pelos do Xbox 360 (A/B/X/Y, LB/RB/LT/RT, START/BACK e direcional) e o "(PS2)" saiu das mensagens de memory card.

### 0.1.54

- A legenda ficou logo abaixo da dica "BACK : Help", alinhada à direita.
- Tradução: "Sim / Não" no quadro de sair do modo história sem salvar.

### 0.1.55 (diagnóstico)

- A captura `L3 + R3` passou a gravar cada desenho de efeito dos especiais.

### 0.1.56: pacote de desempenho

- Os logs periódicos estão desligados por padrão (`b3_log_periodico = false`) e o perfil de GPU também (`nfsmw_dk_perfil_gpu_s = 0`).
- A busca de shader não copia mais o microcódigo e as constantes compartilhadas são atualizadas por push (`nfsmw_dk_compartidas_push`).
- As texturas sobem direto (`nfsmw_dk_subida_directa`). O resultado foi conferido com a rota antiga nas primeiras texturas.
- O CRC32 da tradução usa a instrução de hardware. O bloco de 96 MB da captura é liberado depois de cada captura.
- **Instalador de um clique** (`install.bat`, veja acima). O uam passou a compilar no Windows com LLVM-MinGW.

## Créditos

Este port é um projeto independente. O que cada projeto contribuiu está em [docs/referencias.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.56/docs/referencias.md) e [CONTRIBUTORS.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.56/CONTRIBUTORS.md).

- **TheRalf**: a tradução PT-BR do Budokai 3 de PS2, base da tradução opcional desta release.
- Os créditos anteriores continuam valendo.
- **Ramon (ramonDias07)**: manutenção, testes no Switch físico e validação.
- **OpenAI Codex** e **Claude (Anthropic)**, com os modelos Claude Opus, Sonnet e Haiku via Claude Code: implementação, depuração e documentação, em colaboração com Ramon.

## English

One-click Windows installer (`install.bat`) and a Brazilian Portuguese translation. The translation is the PS2 fan translation by **TheRalf**, made by hand and image by image, and all credit goes to him. It is converted for the HD version and attached to this release as `ptbr.b3tr`; the installer adds it automatically (switch LANGUAGE in the quick menu and restart). This release also adds an on-screen quick-menu legend, the two missing special-effect shaders and a performance pack. Versions 0.1.51–0.1.56 are not tested on hardware yet. You still need your own Xbox 360 *Budokai HD Collection (USA, Japan)* ISO. Apart from the translated screens, no game data is distributed.
