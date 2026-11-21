Terceira release do **Budokai3-nx**, correspondente ao **B3deko 0.1.50**. Parte da [v0.1.46](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.46) e reúne as versões 0.1.47 a 0.1.50: GPU boost que desliga de verdade, boost oficial com RAM a 1.600 MHz, conquistas com ícones, opções experimentais para o dock e a correção do chiado de áudio com a CPU de fábrica.

O destaque é o **áudio limpo com os clocks de fábrica do Switch**: o chiado nas caudas agudas de explosões e magias tinha causa identificada e foi corrigido na 0.1.49.

## Feedback do teste no Switch

Testado no Switch físico por Ramon (ramonDias07):

- **0.1.47:** o boost agora volta ao clock original e acompanha o perfil ("o clock boost desliga conforme o perfil"). O menu de conquistas com ícones apareceu correto.
- **0.1.48:** o áudio "melhorou muito", mas ainda chiava nas caudas agudas dos efeitos.
- **0.1.49:** a correção de áudio "deu certo".

## Ainda não testado no console

- Cartão de detalhe da conquista (0.1.50).
- Resolução interna **1080P** e saída **1440P** no dock (experimentais).
- Os novos padrões do perfil NO OVERCLOCK (GAME TIME 35, interna AUTO) não foram avaliados de forma específica.

## Desempenho

Medições da **0.1.49** em luta, no modo portátil, perfil PERFORMANCE, **CPU 1.020 MHz (padrão)**, GPU boost a 460 MHz e RAM a 1.600 MHz (modo oficial). Os valores vêm dos logs do jogo; veja [docs/performance.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.50/docs/performance.md).

| Teto (GAME TIME) | FPS médio | GPU por quadro | Desenhos por quadro |
| --- | ---: | ---: | ---: |
| 40 FPS | 39,8 (86 janelas de 10 s) | 20,0 ms (máximo 23,1 ms) | cerca de 318 |

Faltas de áudio por janela de 10 s com a CPU a 1.020 MHz:

| Versão | Pedidos de áudio sem dados por 10 s |
| --- | ---: |
| 0.1.43 | cerca de 106 |
| 0.1.44 | cerca de 92 |
| 0.1.46 | 54 a 250 |
| 0.1.48 | 60 a 260 |
| **0.1.49** | **1,3 em média (129 janelas), 0 amostras saturadas** |

Com a CPU a 1.428 MHz o áudio já saía limpo (0 faltas) nas versões antigas, e por isso parecia bom "antes": o problema só aparecia com a CPU de fábrica.

Referência da 0.1.46: teto de 40 FPS a 307 MHz de GPU deu 40,0 FPS com GPU de 20 a 23 ms; teto de 45 FPS com boost deu 44 a 45 FPS com GPU de 16 a 22 ms. Os milissegundos de GPU dependem do clock e não se comparam diretamente entre 307 e 460 MHz.

## O que mudou desde a v0.1.46

### 0.1.47

- **GPU boost volta ao normal de verdade.** A etapa que aplicava o boost nunca guardava a configuração original do console, então a restauração não fazia nada e a GPU ficava em 460 MHz mesmo com o boost desligado. Agora o clock original é restaurado ao desligar, pelo menu ou ao trocar de perfil.
- **Boost oficial nos perfis DEFAULT e PERFORMANCE:** usa a configuração portátil oficial **GPU 460,8 MHz + RAM 1.600 MHz** (`0x92220007`), a mesma usada por jogos comerciais, sem overclock. Nova opção **BOOST WITH RAM 1600** nos ajustes avançados, aplicada ao vivo.
- **GAME TIME** em ordem: 30, 35, 40, 45, 50, 55 e 60.
- **Dock, experimental:** resolução interna **1080P** (escala 1,5) e saída **1440P** (2560x1440). O HDMI do Switch 1 entrega no máximo 1080p, então a saída 1440 funciona como supersampling (mais nítida e mais pesada) e volta sozinha a 1080 se o sistema recusar. Referência: [TechRadar](https://techradar.com/news/is-nintendo-switch-4k-an-overview-of-resolution-on-the-portable-console).
- **Conquistas** (`L + R + ←`) com os ícones do próprio jogo, lidos em tempo de execução do XEX do usuário (nada é distribuído). Nome e descrição pequena no menu e no aviso de desbloqueio; as bloqueadas aparecem em cinza. O carregamento dos ícones reaproveita a abordagem do overlay de conquistas para PC do ReXGlue (`sdk/src/ui/overlay/achievement_icon_cache.cpp`, de Rien Gupta, licença BSD 3-Clause). O rasterizador do Switch (desenho por CPU do overlay Deko3D em 2x) é código deste port.

### 0.1.48

- **Diagnóstico do áudio:** o callback de áudio do jogo (`0x822A9B88`, que chama `sub_822A9968`) acorda uma thread de mixagem do jogo e espera por ela. Com a CPU de fábrica (1.020 MHz) cada callback levava cerca de 2,7 ms.
- As threads de mixagem do jogo passam a rodar com prioridade `0x2D` em tempo de execução, detectadas pelos eventos sinalizados pelo Audio Worker.
- **Ocultação de quadros de efeito atrasados** (repete o último quadro, atenuado) em todos os perfis.
- **Novos padrões do NO OVERCLOCK:** GAME TIME 35 e interna AUTO. **PERFORMANCE:** interna AUTO. O AUTO foi conferido nos logs (cerca de 60 trocas entre 720 e 540 conforme a carga da GPU).

### 0.1.49

- **A correção de verdade do áudio.** Os dois mixers do jogo (música e efeitos) eram chamados um depois do outro por um único Audio Worker: 2 x 2,8 ms passam dos 5,33 ms de cada quadro de áudio. Faltavam quadros e as caudas agudas de explosões e magias soavam como "caixa estourada". Agora há **um Audio Worker por cliente** e os dois mixers rodam em paralelo, em núcleos diferentes. A seção de cópia e envio continua serializada, porque o jogo usa um único buffer estático compartilhado (`0x8251F940`).
- A cvar `audio_parallel_client_callbacks = false` volta ao modo antigo.
- As mensagens de log passam a ser em inglês.
- O esquema de prioridades de thread (guest `0x3B`, áudio `0x2B`, apresentação `0x2C`) segue o do MarathonRecomp-NX e já estava no SDK.

### 0.1.50

- **Cartão de detalhe da conquista:** `A` sobre uma conquista abre um cartão maior, com ícone grande, nome em fonte grande e a descrição. `B` volta à lista e `B` de novo fecha o menu.

## Créditos

Este port é um projeto independente. O que cada projeto contribuiu está em [docs/referencias.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.50/docs/referencias.md) e [CONTRIBUTORS.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.50/CONTRIBUTORS.md). Novidades desta release:

- **ReXGlue** (Rien Gupta, BSD 3-Clause): a abordagem de carregamento de ícones de conquista do overlay de PC.
- **MarathonRecomp-NX:** esquema de prioridades de thread do Horizon.
- **TechRadar:** limite de saída de vídeo usado nas opções de dock.
- Os créditos anteriores (Budokai-NX, NovaPowers, nfsmw-nx, ReXGlue SDK NX, sa2-nx, LoS1-nx, ds2-nx, SaltyNX, AMD FidelityFX, XenosRecomp, uam, SPIRV-Cross, deko3d e libnx) continuam valendo.
- **Ramon (ramonDias07)**: manutenção, testes no Switch físico e validação.
- **OpenAI Codex** e **Claude (Anthropic)**, com os modelos Claude Opus, Sonnet e Haiku via Claude Code: implementação, depuração e documentação, em colaboração com Ramon.

## Como instalar

Baixe **Source code (zip)** ou **Source code (tar.gz)** em **Assets** e siga o [guia de compilação](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.50/docs/build.md). Use seu próprio dump da edição **USA/Japan de Xbox 360** para gerar o `B3.nro` e os arquivos auxiliares. O pacote para o cartão fica em `out/sd/switch/B3deko/`.

Ao atualizar, copie `B3.nro` e `budokai3.toml` (o `b3_shaders.nfsp` da 0.1.45 continua valendo). Se você já tem um `budokai3.toml` no cartão, apague ou renomeie o arquivo para receber os novos padrões. Preserve a pasta `budokai3/` (saves e dados).

Issues e pull requests de otimização são bem-vindos. Em comparações de desempenho, informe a cena, o perfil, os clocks e os resultados antes e depois.
