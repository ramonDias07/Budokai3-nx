# Budokai3-nx — Dragon Ball Z: Budokai 3 HD para Nintendo Switch

Port homebrew não oficial de **Dragon Ball Z: Budokai 3 HD** para Nintendo Switch, baseado na edição Xbox 360 de *Dragon Ball Z: Budokai HD Collection*. O ReXGlue recompila o código PowerPC do jogo para ARM64. O port processa os comandos gráficos Xenos e renderiza com Deko3D.

[Release v0.1.56](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.56) · [Como compilar](docs/build.md) · [Capturas de gameplay](#capturas-de-gameplay) · [English overview](#english-overview)

Para começar, baixe **Source code** na release. No Windows, instale o devkitPro e dê dois cliques em `install.bat`: ele confere as ferramentas, pede o seu dump **USA/Japan de Xbox 360**, gera o `B3.nro` e monta a pasta para o SD. O [guia de compilação](docs/build.md) explica o jeito fácil e os passos à mão.

## Estado do port

Destaques da **v0.1.56** (B3deko 0.1.51 a 0.1.56):

- **Instalador de um clique para Windows** (`install.bat`): confere e instala as ferramentas, pede a ISO e gera a pasta para o SD.
- **Tradução PT-BR opcional**, a tradução de fãs de **TheRalf** para o Budokai 3 de PS2, convertida no seu PC a partir dos seus discos e ligada pela opção LANGUAGE do menu rápido. [Como gerar](docs/traducao-ptbr.md)
- **Legenda do menu rápido** no menu principal do jogo: `(+)+(-) : Quick Menu`.
- **Efeitos de especiais:** os dois shaders de efeito que faltavam (feixe e fogo) estão no pacote e no cache pré-compilado.
- **Pacote de desempenho** (0.1.56): logs periódicos e perfil de GPU desligados por padrão, busca de shader sem cópia, constantes por push e subida direta de texturas.

Continuam da v0.1.50: 40 FPS nas lutas com a CPU de fábrica, áudio limpo, perfis DEFAULT, PERFORMANCE e NO OVERCLOCK, GPU boost oficial, conquistas com ícones e opções experimentais de dock.

| Área | Estado |
| --- | --- |
| Switch físico | Testadas no console até a 0.1.49 (boost, conquistas com ícones, áudio limpo). **Ainda não testadas no console:** as novidades da 0.1.51 à 0.1.56 (legenda, shaders de efeito, tradução PT-BR e pacote de desempenho), o cartão de detalhe da conquista e as opções de dock. Se a 0.1.56 mostrar erro de textura ou de efeito, desligue `nfsmw_dk_subida_directa` ou `nfsmw_dk_compartidas_push` no `budokai3.toml`. |
| Desempenho | Luta a 40 FPS, modo portátil, CPU 1.020 MHz, boost GPU 460 MHz + RAM 1.600 MHz: **39,8 FPS** de média, GPU de **20,0 ms** por quadro. Faltas de áudio por 10 s com a CPU de fábrica: 54 a 260 (0.1.46 e 0.1.48) contra **1,3** na 0.1.49. [Medições e método](docs/performance.md) |

## O que você precisa

- Um dump `.iso` próprio do disco **USA/Japan para Xbox 360**. O arquivo de entrada é `DBZ3/yae3_xenon.xex` com SHA-256 `b40bba40cfd6c90cb269ebf5020818924f43109bcc86827a3cc37124c052a26b`.
- Nintendo Switch com acesso ao Homebrew Menu em modo aplicativo e um cartão SD.
- Python, ReXGlue e as ferramentas de compilação explicadas no [guia de build](docs/build.md).

O extrator copia os arquivos do Budokai 3 para `assets/game_root`. A recompilação e os shaders são gerados localmente a partir do seu dump. A entrega final fica em `out/sd/switch/B3deko/`, pronta para copiar como pasta para o SD.

## O que há no código

| Pasta | Conteúdo |
| --- | --- |
| `app/` | Manifesto, ajustes da recompilação USA/Japan, integração gráfica, sincronização de efeitos, menus e configuração padrão. |
| `patches/` | Alterações do SDK e do FFmpeg, aplicadas a versões fixas das dependências. |
| `tools/` | Extração do disco, reconstrução local dos shaders, preparação do SDK e empacotamento. |
| `docs/` | Guia de compilação, requisitos do dump, referências e medições no Switch. |

## No Switch

| Comando | Menu |
| --- | --- |
| `+` e `−` | Perfil, opções de cena e áudio. |
| `L + R + →` | Ajustes avançados. `L` e `R` trocam de aba. |
| `L + R + ←` | Conquistas. `A` abre o detalhe, `B` volta. |

Pressione `B` para fechar. Em **Ritmo normal** a trilha mantém seu próprio ritmo, enquanto vozes e efeitos acompanham o combate.

O **Dragon Rush** tem um layout ABXY próprio para o Switch. Com a configuração padrão, cada botão responde ao comando indicado no centro da tela.

## Capturas de gameplay

Capturas feitas no Switch com a build 0.1.38, mostrando o layout de Dragon Rush mantido na 0.1.40.

![Dragon Rush com Goku Super Saiyajin 4 e o layout ABXY do Switch](docs/screenshots/dragon-rush.jpg)

![Ataque durante a sequência do Dragon Rush](docs/screenshots/combate.jpg)

Menu principal com a tradução PT-BR opcional (tradução de TheRalf), capturado no Switch em 22/11/2026.

![Menu principal traduzido para o português](docs/screenshots/traducao-ptbr-menu.jpg)

## Contribuições

Sugestões e pull requests de otimização são bem-vindos. Se você testar no console, conte o que funcionou e abra uma issue caso encontre um problema. Inclua a versão do port, a cena, o perfil gráfico, os clocks do console e, se possível, o trecho relevante do log. Para propor uma otimização, inclua medições antes e depois na mesma cena. [Issues](https://github.com/ramonDias07/Budokai3-nx/issues) · [Contribuidores](CONTRIBUTORS.md)

Se o projeto foi útil, deixe uma estrela no GitHub para acompanhar o repositório.

## Créditos e referências

### Jogo original

- **Dragon Ball Z: Budokai 3** foi desenvolvido pela **Dimps** e publicado pela **Bandai** no Japão e pela **Atari** na América do Norte e na Europa (PlayStation 2, 2004).
- **Dragon Ball Z: Budokai HD Collection** (Xbox 360 e PlayStation 3, 2012), a edição usada como base deste port, foi publicada pela **Namco Bandai Games**, hoje **Bandai Namco Entertainment**.
- **Dragon Ball** foi criado por **Akira Toriyama**. O mangá é publicado pela **Shueisha** e a série animada é produzida pela **Toei Animation**. ©BIRD STUDIO/SHUEISHA, TOEI ANIMATION.

O jogo, os personagens, a arte, a música, as vozes e o design pertencem a essas empresas e às equipes que os criaram. Se você gosta deste port, apoie as versões oficiais.

### Port e ferramentas

- **NovaPowers** pelo port original para PC e pela base de recompilação. [DBZ-Budokai-3-HD-Collection](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)
- **Tom Clay, colaboradores do Xenia e nx-mod** pelo recompilador, runtime e suporte ao Switch. [ReXGlue](https://github.com/rexglue/rexglue-sdk) · [ReXGlue SDK NX](https://github.com/nx-mod/rexglue-sdk-nx)
- **StevensND** pelo trabalho no renderizador Deko/NFSMW e pelas rotinas reutilizadas de shaders, desenho e cache. [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)
- **FurryLilManChild**, **ChanseyIsTheBest**, **masterspike52** e colaboradores pelos projetos LoS1-nx, SA2-nx e reNut. O extrator XDVDFS e a adaptação SPIR-V para GLSL/Deko vieram da árvore do LoS1-nx. [LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx) · [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx)
- **hedge-dev**, **devkitPro** e **Khronos** pelas ferramentas usadas na tradução de shaders. [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) · [uam](https://github.com/devkitPro/uam) · [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)

### Tradução PT-BR

- **TheRalf** pela tradução de fãs em português do Brasil do *Dragon Ball Z: Budokai 3* de PS2 (patch v1.2). Ela foi feita à moda antiga, redesenhando à mão diálogos, menus, cápsulas e gráficos, e levou muito tempo. O mérito da tradução é todo dele. O port só converte esse trabalho para a versão HD, no PC de quem joga, e não distribui a tradução nem os arquivos do jogo.

Veja de onde veio cada parte e como outros ports tratam instalação e dados originais em [Referências](docs/referencias.md).

## English overview

**Budokai3-nx** is an unofficial Nintendo Switch homebrew port of **Dragon Ball Z: Budokai 3 HD** from the Xbox 360 collection. It uses ReXGlue for static recompilation and Deko3D for rendering. Version **0.1.56** builds on 0.1.50 (40 FPS fights at stock CPU clocks, clean audio, official GPU boost, achievements with in-game icons) and adds a one-click Windows installer (`install.bat`), an optional Brazilian Portuguese translation (the PS2 fan translation by **TheRalf**, all credit to him; converted locally from your own discs, never distributed), an on-screen quick-menu legend, the two missing special-effect shaders and a performance pack. Versions up to 0.1.49 were tested on a physical Switch; the 0.1.51–0.1.56 changes were not yet.

Download **Source code** from the release and build with your own **USA/Japan Xbox 360 ISO**. On Windows, install devkitPro and double-click `install.bat`: it checks the tools, asks for your ISO and builds the NRO and its SD card folder locally. Hardware test reports and performance contributions are welcome in English or Portuguese. [Release](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.56) · [Build guide in Portuguese](docs/build.md) · [Gameplay screenshots](#capturas-de-gameplay) · [Issues](https://github.com/ramonDias07/Budokai3-nx/issues)

**Original game and legal notice.** *Dragon Ball Z: Budokai 3* was developed by **Dimps** and published by **Bandai** and **Atari**. *Dragon Ball Z: Budokai HD Collection* was published by **Namco Bandai Games**, now **Bandai Namco Entertainment**. *Dragon Ball* was created by **Akira Toriyama**. ©BIRD STUDIO/SHUEISHA, TOEI ANIMATION. This is a non-commercial fan project. It is **not affiliated with or endorsed by** Bandai Namco, Dimps, Atari, Shueisha, Toei Animation, Bird Studio, Microsoft or Nintendo. This repository contains no game code or assets. You must own the original disc and make your own dump. Rights holders with a concern can open an issue, and the material will be reviewed or removed promptly.

## Aviso legal

Este é um projeto de fãs, não oficial e sem fins lucrativos. **Não tem afiliação, patrocínio nem aprovação** da Bandai Namco Entertainment, Dimps, Atari, Shueisha, Toei Animation, Bird Studio, Microsoft ou Nintendo.

- *Dragon Ball*, *Dragon Ball Z* e *Budokai* são marcas e obras protegidas dos respectivos titulares. ©BIRD STUDIO/SHUEISHA, TOEI ANIMATION. O jogo é © Bandai Namco Entertainment Inc.
- *Xbox 360* é marca da Microsoft Corporation e *Nintendo Switch* é marca da Nintendo. Os nomes aparecem aqui só para indicar compatibilidade.
- Este repositório **não distribui** o jogo, seus executáveis, arquivos de dados, áudio, vídeo, texturas, código recompilado nem chaves. Para compilar, é preciso um disco original adquirido legalmente e um dump feito pela própria pessoa. Não peça nem compartilhe ISOs ou NROs que contenham dados do jogo.
- As capturas em `docs/screenshots/` servem só para documentar o funcionamento do port. As imagens do jogo continuam pertencendo aos titulares.
- Titulares de direitos com alguma objeção podem abrir uma [issue](https://github.com/ramonDias07/Budokai3-nx/issues). O material será revisto ou removido prontamente.

## Licenças

O código deste port segue a GPL-3.0. Os componentes de terceiros mantêm suas licenças em [`licenses/`](licenses/) e nas próprias árvores. A GPL-3.0 vale apenas para o código deste repositório e não cobre o jogo original nem os dados extraídos dele. O dump, os executáveis do jogo, a recompilação gerada, os caches derivados, os arquivos de arte extraídos, os saves e os NROs gerados ficam no computador de quem compila.
