# Desempenho no Switch

As medições vêm de duas sessões no Switch físico com o **B3deko 0.1.40**, em **17 de novembro de 2026**. O console usava **RAM a 1.600 MHz, CPU a 1.400 MHz e GPU a 460 MHz**, conforme informado pelo responsável pelos testes. A saída do jogo era **1280 × 720**. Os logs não medem os clocks e esses valores não são requisitos do port.

| Recorte dos logs | Janelas de aproximadamente 10 s | FPS médio | Mediana | Metade central das janelas |
| --- | ---: | ---: | ---: | ---: |
| Sessões completas, incluindo menus e cenas leves | 279 | 48,6 | 47,2 | 43,8–56,6 |
| Janelas com pelo menos 200 desenhos por quadro | 193 | 45,9 | 46,2 | 42,2–49,4 |

O segundo recorte seleciona janelas com mais trabalho de renderização pelo número de desenhos. A taxa é a do renderizador Deko3D. A cadência da simulação pode cair em momentos diferentes.

| Horário | FPS | GPU por quadro | Desenhos por quadro |
| --- | ---: | ---: | ---: |
| 13:20:27, início da sessão | 60,2 | 5,13 ms | 3 |
| 14:00:39, menor média em uma janela | 30,8 | 31,65 ms | 373 |
| 14:19:12 | 47,2 | 20,44 ms | 361 |
| 14:19:22 | 48,7 | 19,81 ms | 348 |
| 14:19:32 | 46,2 | 20,87 ms | 406 |

Na janela de 30,8 FPS, o desenho da cena consumiu **19,91 ms** e as cópias consumiram **6,10 ms** de GPU por quadro. Nas cenas mais pesadas, vozes ou efeitos ainda podem adiantar em relação à animação com a trilha no modo **Ritmo normal**.

## B3deko 0.1.44 e 0.1.46

A partir da 0.1.42 o alvo é manter uma velocidade estável com os **clocks padrão do Switch**. Os números abaixo vêm de logs de luta da **0.1.44** no modo portátil, com **CPU a 1.020 MHz, GPU a 307 MHz e RAM a 1.331 MHz**, sem o modo oficial de GPU de 460 MHz, exceto onde indicado. A 0.1.46 não foi medida; veja a seção da 0.1.49 mais abaixo.

| Teto (GAME TIME) | Condições | FPS | GPU por quadro |
| --- | --- | ---: | ---: |
| 30 FPS | clocks padrão | 30,0 | - |
| 35 FPS | clocks padrão | 35,0 | - |
| 40 FPS | clocks padrão | 40,0 (mediana; mínimo de janela 32,4) | 21,5 ms |
| 45 FPS | perfil DEFAULT, interna 720, clocks padrão | 43,8 (mediana) | 13,5 ms (mediana) |

Linha de base da **0.1.43**, com o modo oficial de GPU a **460 MHz**:

| Teto (GAME TIME) | Condições | FPS | GPU por quadro |
| --- | --- | ---: | ---: |
| 40 FPS | interna 720 | 39,9 | 17,5 ms |
| 45 FPS | interna 720 | 45,0 | 14,9 ms |
| 45 FPS, mapa do Dragon Universe | - | - | 13 a 16 ms (0.1.41: 21 ms) |

O tempo de GPU por quadro depende do clock da GPU, de modo que valores medidos a 307 MHz e a 460 MHz não se comparam diretamente. O tempo de jogo em tempo real (delta time) separa a velocidade da simulação da taxa de apresentação; veja [Referências](referencias.md).

Em testes com o perfil NO OVERCLOCK (CPU 1.020 MHz, RAM 1.331 MHz) havia chiado no áudio, que sumia com RAM a 1.600 MHz. A 0.1.46 acrescentou uma rampa anti-estalo e um colchão de áudio maior nesse perfil. A causa real do chiado com a CPU de fábrica foi corrigida na 0.1.49 (veja abaixo).

## B3deko 0.1.49

Logs de luta no modo portátil, perfil PERFORMANCE, **CPU 1.020 MHz (padrão)** e GPU boost no modo oficial (**GPU 460,8 MHz + RAM 1.600 MHz**, `0x92220007`). Esta versão foi testada no Switch físico.

| Teto (GAME TIME) | Janelas de 10 s | FPS médio | GPU por quadro | Desenhos por quadro |
| --- | ---: | ---: | ---: | ---: |
| 40 FPS | 86 | 39,8 | 20,0 ms (máximo 23,1 ms) | cerca de 318 |

Referência da 0.1.46: teto 40 a 307 MHz deu 40,0 FPS com GPU de 20 a 23 ms; teto 45 com boost deu 44 a 45 FPS com GPU de 16 a 22 ms.

### Áudio

Pedidos de áudio sem dados (faltas) por janela de 10 s, com a CPU a 1.020 MHz:

| Versão | Faltas por 10 s |
| --- | ---: |
| 0.1.43 | cerca de 106 |
| 0.1.44 | cerca de 92 |
| 0.1.46 | 54 a 250 |
| 0.1.48 | 60 a 260 |
| 0.1.49 | 1,3 em média (129 janelas), 0 amostras saturadas |

Com a CPU a 1.428 MHz o áudio já saía limpo (0 faltas) nas versões antigas. A causa nas versões anteriores à 0.1.49: os dois mixers do jogo (música e efeitos), com cerca de 2,8 ms cada, eram chamados em fila por um único Audio Worker e passavam dos 5,33 ms de cada quadro de áudio com a CPU de fábrica. A 0.1.49 usa um Audio Worker por cliente e os mixers rodam em paralelo.

A 0.1.50, a saída 1440P e a resolução interna 1080P do dock não foram medidas nem testadas no console.
