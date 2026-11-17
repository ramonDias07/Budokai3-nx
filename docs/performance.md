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
