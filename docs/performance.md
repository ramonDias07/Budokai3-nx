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

A partir da 0.1.42 o alvo é manter uma velocidade estável com os **clocks padrão do Switch**. Os números abaixo vêm de logs de luta da **0.1.44** no modo portátil, com **CPU a 1.020 MHz, GPU a 307 MHz e RAM a 1.331 MHz**, sem o modo oficial de GPU de 460 MHz, exceto onde indicado. A 0.1.46 ainda não foi medida: a versão não foi testada no console.

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

Em testes com o perfil NO OVERCLOCK (CPU 1.020 MHz, RAM 1.331 MHz) havia chiado no áudio, que sumia com RAM a 1.600 MHz. A 0.1.46 acrescenta uma rampa anti-estalo e um colchão de áudio maior nesse perfil. Isso ainda precisa de teste no console.