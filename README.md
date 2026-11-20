# Budokai3-nx — Dragon Ball Z: Budokai 3 HD para Nintendo Switch

Port homebrew não oficial de **Dragon Ball Z: Budokai 3 HD** para Nintendo Switch, baseado na edição Xbox 360 de *Dragon Ball Z: Budokai HD Collection*. O ReXGlue recompila o código PowerPC do jogo para ARM64. O port processa os comandos gráficos Xenos e renderiza com Deko3D.

[Release v0.1.46](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.46) · [Como compilar](docs/build.md) · [Capturas de gameplay](#capturas-de-gameplay) · [English overview](#english-overview)

Para começar, baixe **Source code** na release e siga o guia de compilação com seu próprio dump **USA/Japan de Xbox 360**. As ferramentas geram o `B3.nro` e montam a pasta para o SD.

## Estado do port

| Área | Estado |
| --- | --- |
| Switch físico | A [release 0.1.46](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.46) parte da 0.1.40, testada no console com lutas, campanha, menus e avisos de conquista. As versões 0.1.42 a 0.1.44 também foram testadas; o shader da 0.1.45 e as correções da 0.1.46 ainda não. |
| Ponto em aberto | Em cenas mais pesadas, alguns efeitos de áudio ainda podem adiantar com a trilha no modo Ritmo normal. |
| Desempenho | **45,9 FPS** de média nas janelas com mais desenhos. A metade central ficou entre **42,2 e 49,4 FPS**. O Switch dos testes usou **RAM 1.600 MHz, CPU 1.400 MHz e GPU 460 MHz**. [Medições e método](docs/performance.md) |

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
| `L + R + ←` | Conquistas. |

Pressione `B` para fechar. Em **Ritmo normal** a trilha mantém seu próprio ritmo, enquanto vozes e efeitos acompanham o combate.

O **Dragon Rush** tem um layout ABXY próprio para o Switch. Com a configuração padrão, cada botão responde ao comando indicado no centro da tela.

## Capturas de gameplay

Capturas feitas no Switch com a build 0.1.38, mostrando o layout de Dragon Rush mantido na 0.1.40.

![Dragon Rush com Goku Super Saiyajin 4 e o layout ABXY do Switch](docs/screenshots/dragon-rush.jpg)

![Ataque durante a sequência do Dragon Rush](docs/screenshots/combate.jpg)

## Contribuições

Sugestões e pull requests de otimização são bem-vindos. Se você testar no console, conte o que funcionou e abra uma issue caso encontre um problema. Inclua a versão do port, a cena, o perfil gráfico, os clocks do console e, se possível, o trecho relevante do log. Para propor uma otimização, inclua medições antes e depois na mesma cena. [Issues](https://github.com/ramonDias07/Budokai3-nx/issues) · [Contribuidores](CONTRIBUTORS.md)

Se o projeto foi útil, deixe uma estrela no GitHub para acompanhar o repositório.

## Créditos e referências

- **NovaPowers** pelo port original para PC e pela base de recompilação. [DBZ-Budokai-3-HD-Collection](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)
- **Tom Clay, colaboradores do Xenia e nx-mod** pelo recompilador, runtime e suporte ao Switch. [ReXGlue](https://github.com/rexglue/rexglue-sdk) · [ReXGlue SDK NX](https://github.com/nx-mod/rexglue-sdk-nx)
- **StevensND** pelo trabalho no renderizador Deko/NFSMW e pelas rotinas reutilizadas de shaders, desenho e cache. [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)
- **FurryLilManChild**, **ChanseyIsTheBest**, **masterspike52** e colaboradores pelos projetos LoS1-nx, SA2-nx e reNut. O extrator XDVDFS e a adaptação SPIR-V para GLSL/Deko vieram da árvore do LoS1-nx. [LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx) · [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx)
- **hedge-dev**, **devkitPro** e **Khronos** pelas ferramentas usadas na tradução de shaders. [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) · [uam](https://github.com/devkitPro/uam) · [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)

Veja de onde veio cada parte e como outros ports tratam instalação e dados originais em [Referências](docs/referencias.md).

## English overview

**Budokai3-nx** is an unofficial Nintendo Switch homebrew port of **Dragon Ball Z: Budokai 3 HD** from the Xbox 360 collection. It uses ReXGlue for static recompilation and Deko3D for rendering. Version **0.1.46** builds on **0.1.40**, which was tested on physical Switch hardware, and features a Dragon Rush prompt adapted to the Switch ABXY layout.

Download **Source code** from the release and build with your own **USA/Japan Xbox 360 ISO**. The build tools generate the NRO and its SD card folder locally. Hardware test reports and performance contributions are welcome in English or Portuguese. [Release](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.46) · [Build guide in Portuguese](docs/build.md) · [Gameplay screenshots](#capturas-de-gameplay) · [Issues](https://github.com/ramonDias07/Budokai3-nx/issues)

## Licenças e dados do jogo

O código deste port segue a GPL-3.0. Os componentes de terceiros conservam suas licenças em [`licenses/`](licenses/) e nas próprias árvores. O repositório distribui código, configurações, ferramentas e capturas para documentação. O dump, executáveis do jogo, recompilação gerada, caches derivados, arquivos de arte extraídos, saves e NROs gerados ficam no computador de quem compila. *Dragon Ball* e *Budokai* pertencem aos respectivos titulares. Este projeto é independente.
