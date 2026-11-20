Segunda release do **Budokai3-nx**, correspondente ao **B3deko 0.1.46**. Parte da [v0.1.40](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.40) e reúne as versões 0.1.41 a 0.1.46: velocidade real da luta, teto de FPS, perfis de desempenho, menu rápido reorganizado, resolução interna e de saída ao vivo, ampliadores e correções de sombra, efeito de luta e áudio.

As versões 0.1.42, 0.1.43 e 0.1.44 foram testadas no Switch físico. **O shader da 0.1.45 e as correções da 0.1.46 ainda não foram testados no console** (lista [mais abaixo](#ainda-não-testado-no-console)).

## Feedback do teste no Switch

- **0.1.42:** 40 FPS lisos, sem overclock e com o áudio em sincronia. Foi escolhida como a versão mais jogável até então.
- **0.1.43:** o tempo de jogo também no mapa do Dragon Universe "funcionou muito bem".
- **0.1.44:** os tetos de 30, 35, 40, 45, 50, 55 e 60 FPS se aplicam ao vivo, sem reiniciar.

## Desempenho atual

Medições da 0.1.44 em luta, no modo portátil, com os clocks padrão do Switch (**CPU 1.020 MHz, GPU 307 MHz, RAM 1.331 MHz**), exceto onde indicado. Os valores vêm dos logs do jogo; veja os detalhes em [docs/performance.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.46/docs/performance.md).

| Teto (GAME TIME) | Resolução interna | Clocks | FPS | GPU por quadro |
| --- | --- | --- | ---: | ---: |
| 30 FPS | - | padrão | 30,0 | - |
| 35 FPS | - | padrão | 35,0 | - |
| 40 FPS | - | padrão | 40,0 (mediana; mínimo de janela 32,4) | 21,5 ms |
| 45 FPS, perfil DEFAULT | 720 | padrão | 43,8 (mediana) | 13,5 ms (mediana) |

Referência da 0.1.43, com o modo oficial de GPU a **460 MHz** ligado:

| Teto (GAME TIME) | Resolução interna | FPS | GPU por quadro |
| --- | --- | ---: | ---: |
| 40 FPS | 720 | 39,9 | 17,5 ms |
| 45 FPS | 720 | 45,0 | 14,9 ms |
| Mapa do Dragon Universe, 45 FPS | - | - | 13 a 16 ms (era 21 ms na 0.1.41) |

Os milissegundos de GPU dependem do clock: a 460 MHz a mesma carga leva menos tempo do que a 307 MHz, então os valores das duas tabelas não são comparáveis diretamente. Com a **0.1.40**, o limite foi medido com RAM a 1.600 MHz, CPU a 1.400 MHz e GPU a 460 MHz (média de 45,9 FPS nas janelas pesadas). A partir da 0.1.42 o alvo passou a ser rodar a velocidade estável também com os clocks padrão.

## O que mudou desde a v0.1.40

### 0.1.41

- **Cena 3D da luta com escala própria** (código deste port): o menu oferecia 1280×720, 960×540 e 640×360 para o passe principal da luta, com cor e Z24S8 em imagens separadas devolvidas às superfícies de 720p antes do HUD. Foi substituída na 0.1.42 pela resolução interna global.
- **Reaproveitamento de geometria entre fotogramas.** O renderizador Deko3D herdado do SDK NX e do trabalho de StevensND no nfsmw-nx já guardava vértices e índices que se repetem entre quadros (cache com hash do conteúdo e fences, "geometria entre fotogramas"). A 0.1.41 estendeu esse mesmo cache aos buffers contíguos de *fetch* manual de vértices do Xenos (até 512 KiB, conferidos pelo hash do conteúdo completo). Estudamos as notas técnicas públicas do **Budokai-NX** (alexyt2021es) sobre reaproveitar buffers imutáveis de vértices e índices mantidos vivos até a GPU terminar; a implementação é código deste port no backend Deko3D, não uma cópia do outro projeto.
- Aba **RENDERIZACAO DEKO** nos ajustes avançados (L + R + Direita), com compressões, limpezas, envio parcial e tamanho do buffer de envio.

### 0.1.42

- **Velocidade real da luta (delta time)**, **portada do Budokai-NX v0.4** (Unknown, licença MIT) com apoio na pesquisa de endereços do DBZ-Budokai-3-HD-Collection de NovaPowers (MIT). A luta da 0.1.41 rodava a cerca de 44 atualizações por segundo, ou 74% da velocidade original. A adaptação para este port usa o passo `0x8238A60C`, uma cópia instrumentada de `sub_820FC260` (3 injeções) e só atua nas fases de luta 9 e 15.
- **Teto de FPS na luta** (código deste port): 30 FPS pelo intervalo de apresentação e 40, 45 e 50 por espera monotônica, só na luta.
- **Resolução interna global** (código deste port): alvos EDRAM, resolves, desfoques e saída escalados; alvos pequenos e resolves lidos pela CPU ficam sem escala. O caminho antigo da cena 3D da luta foi removido. Saída de 1080 ou 720 no dock.
- **Sombras dos lutadores:** constantes de *fetch* inválidas (tipo 0) passam a ser aceitas como textura, no mesmo comportamento da opção `gpu_allow_invalid_fetch_constants` do Xenia para PC.

### 0.1.43

- **GAME TIME único** (código deste port): um só controle define a velocidade e o teto de FPS na luta e também no mapa do Dragon Universe.
- **Perfis DEFAULT, PERFORMANCE e NO OVERCLOCK**, com ajustes por perfil e **perfil DOCK automático** (720 interna, 1080 de saída ao encaixar o console).
- **Menu rápido rolável com letreiro de ajuda** e **RESTORE DEFAULTS** com confirmação. Os ajustes avançados (L + R + Direita) foram reorganizados em categorias, seguindo a organização do menu do port de Castlevania Lords of Shadow do responsável pelo projeto.
- **Resolução interna e de saída ao vivo** e **ampliador** LINEAR, **CAS** e **FSR 1 (EASU)**. CAS e FSR 1 seguem o algoritmo público do AMD FidelityFX; o EASU foi escrito do zero para este port em GLSL. A nitidez do CAS é ajustável.
- **Handover de cor** e **pular resolve repetido**: otimizações de renderização vindas dos ports irmãos (handover de cor do sa2-nx). Ficaram desligadas por padrão depois da 0.1.44.
- **Vsync relaxado** parcial e **modo oficial de GPU a 460 MHz** no portátil, seguindo a abordagem dos ports ds2-nx e sa2-nx.
- **Resolução interna AUTO** (experimental).

### 0.1.44

- Teto preso em 30 FPS: o wrapper da fila de buffers da janela não restaurava o intervalo de troca original. Corrigido.
- **NO OVERCLOCK** fechava o jogo: a thread do GPU boost usava `std::thread` (ENOSYS no Horizon). Passou a usar `pthread`, e o boost saiu desse perfil.
- A resolução exibida pelo monitor de hardware do SaltyNX é republicada a cada troca de cadeia de apresentação.
- **GAME TIME** com 30, 35, 40, 45, 50, 55 e 60 FPS.
- Bloom desligado nos três perfis. Handover de cor e pular resolve repetido desligados por padrão (os contadores mostravam zero uso no Budokai 3).
- Resolução AUTO com histerese: espera 1 s para descer e 3 s para subir.

### 0.1.45

- O vertex shader de efeito de luta `4D5BDBCCEDEB1BB5` foi traduzido. Ele é uma variante de um shader já conhecido sem uma saída que o pixel shader não lê. O efeito estava invisível antes. Para esta correção, atualize **também** o `b3_shaders.nfsp`.

### 0.1.46

- **GPU boost:** o botão não fica mais preso por uma corrida entre ligar e desligar.
- **Piscada da sombra dos lutadores:** as capturas L3 + R3 mostraram *z-fighting* em listras no desenho da projeção da sombra. Foi acrescentado um pequeno viés de profundidade só nesse desenho, com a nova opção **SHADOW DEPTH BIAS**.
- **Áudio:** rampa anti-estalo quando o jogo perde um quadro de áudio e colchão de áudio maior no perfil NO OVERCLOCK (o chiado aparecia com CPU 1.020 MHz e RAM 1.331 MHz e sumia com RAM a 1.600 MHz).
- **Novos padrões, a partir da configuração do responsável:** perfil PERFORMANCE ativo, GAME TIME 40, bloom e sombras projetadas desligados, vibração ligada (0,25), contador de FPS ligado e GPU boost ligado nos perfis DEFAULT e PERFORMANCE (só no portátil). O NO OVERCLOCK passa a usar 40 FPS, interna 540, saída 720 e sem boost.

## Ainda não testado no console

- Correção do GPU boost preso (0.1.46).
- Viés de profundidade na sombra dos lutadores (0.1.46).
- Rampa anti-estalo e colchão de áudio maior no NO OVERCLOCK (0.1.46).
- Efeito de luta com o shader `4D5BDBCC` (0.1.45).
- Os novos padrões e o perfil NO OVERCLOCK a 40 FPS.

## Créditos

Este port é um projeto independente. O renderizador Deko3D do Budokai 3 e o código do port não foram copiados de outro Budokai 3 para Switch. Abaixo está o que cada projeto contribuiu, conforme os comentários do código e [docs/referencias.md](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.46/docs/referencias.md).

- **[Budokai-NX](https://gitlab.com/alexyt2021es/Budokai-NX)** (alexyt2021es, Unknown, MIT): a técnica de **delta time** da luta, portada para este port na 0.1.42 (hooks de `sub_82088F70`, `sub_820FC260` e `sub_820AB818`). As [notas técnicas de reutilização de buffers](https://gitlab.com/alexyt2021es/Budokai-NX/-/blob/main/switch/PERFORMANCE-04851.md) foram estudadas para a 0.1.41; a implementação é própria.
- **[DBZ-Budokai-3-HD-Collection](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)** (NovaPowers, MIT): recompilação para PC, manifesto e pesquisa de endereços do jogo, usados também no ajuste de velocidade e nas sombras.
- **[nfsmw-nx](https://github.com/StevensND/nfsmw-nx)** (StevensND) e **[ReXGlue SDK NX](https://github.com/nx-mod/rexglue-sdk-nx)** (nx-mod, sobre o ReXGlue e o Xenia): runtime, renderizador Deko3D e cache de geometria entre fotogramas que serviram de base.
- **MarathonRecomp-NX**: valores de áudio, memória e threads do Horizon indicados nos comentários do SDK.
- **[sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx)** e **[LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx)**: extrator XDVDFS, conversão de GLSL para Deko3D, tradução de shaders reNut/banjo-nx e a ideia do handover de cor. O menu avançado segue a organização do port de Castlevania Lords of Shadow (LoS1-nx).
- **ds2-nx** (VasilkovND, github.com/VasilkovND/ds2-nx): referência do cache e dos envios parciais e do modo oficial de GPU.
- **[SaltyNX](https://github.com/masagrator/SaltyNX)** e Status-Monitor-Overlay (masagrator): bloco de leitura de clocks e resolução usado pelo monitor de hardware.
- **AMD FidelityFX** (CAS e FSR 1 EASU): algoritmos de referência do ampliador; a implementação em GLSL é deste port.
- **[XenosRecomp](https://github.com/hedge-dev/XenosRecomp)**, **[uam](https://github.com/devkitPro/uam)**, **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)**, **deko3d** e **libnx** (devkitPro): ferramentas e bibliotecas de tradução de shaders e de execução no Switch.
- **Ramon (ramonDias07)**: manutenção, testes no Switch físico e validação.
- **OpenAI Codex** e **Claude (Anthropic)**, com os modelos Claude Opus, Sonnet e Haiku via Claude Code: implementação, depuração e documentação, em colaboração com Ramon. O Claude atuou nas versões 0.1.41 a 0.1.46.

## Como instalar

Baixe **Source code (zip)** ou **Source code (tar.gz)** em **Assets** e siga o [guia de compilação](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.46/docs/build.md). Use seu próprio dump da edição **USA/Japan de Xbox 360** para gerar o `B3.nro` e os arquivos auxiliares. O pacote para o cartão fica em `out/sd/switch/B3deko/`.

O `budokai3.toml` gerado já traz os padrões do perfil **PERFORMANCE**. Se você já tem um `budokai3.toml` no cartão, apague ou renomeie o arquivo para receber os novos padrões, ou acrescente `b3_perfil = "performance"`. Ao atualizar, preserve a pasta `budokai3/` (saves e dados).

Issues e pull requests de otimização são bem-vindos. Em comparações de desempenho, informe a cena, o perfil, os clocks e os resultados antes e depois.
