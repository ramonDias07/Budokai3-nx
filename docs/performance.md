# Desempenho no Switch

As medições abaixo vêm dos últimos logs físicos disponíveis, da build **Deko3D 0.1.38**, em **17 de novembro de 2026**. O console foi testado com **RAM a 1.600 MHz, CPU a 1.400 MHz e GPU a 460 MHz**, conforme informado pelo responsável pelo teste. Esses clocks não são medições do log nem requisitos do port.

A saída estava em **1280 × 720**. Cada linha representa uma janela de aproximadamente dez segundos registrada pelo Deko3D. A taxa de quadros do renderizador pode diferir da cadência da simulação, que também aparece nos logs de áudio.

| Trecho da sessão | Quadros/s | GPU por quadro | Desenhos por quadro |
| --- | ---: | ---: | ---: |
| Diálogo/cena leve, 09:51:35 | 60,0 | 6,41 ms | 7 |
| Janela de combate pesada, 10:20:27 | 26,5 | 36,65 ms | 395 |
| Janela seguinte, 10:20:37 | 44,2 | 21,83 ms | 345 |
| Trecho perto do final da sessão, 10:24:29 | 59,9 | 13,53 ms | 41 |

Na janela de 26,5 quadros/s, o desenho da cena consumiu **28,64 ms** na GPU em média e as cópias consumiram **3,54 ms**. O relógio da luta marcou **23,7 a 24,3 atualizações/s** em amostras próximas. O áudio em Ritmo normal ainda avançava com piso de 0,5×. Isso motivou o ajuste de 0.1.39 nos efeitos, preservando a trilha musical em tempo real.

A nova organização de menus e avisos de 0.1.40 foi compilada. Ainda faltam medições físicas dessa revisão. Os resultados da tabela não devem ser extrapolados para toda luta, para clocks de fábrica ou para modos de TV e portátil sem um novo teste.
