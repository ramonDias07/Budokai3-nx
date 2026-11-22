# Tradução PT-BR (opcional)

O port pode usar a tradução de fãs em português do Brasil feita para o **Budokai 3 Collector's Edition de PS2** (patch PT-BR v1.2). **Nenhum dado traduzido é distribuído** com o port nem faz parte deste repositório: a ferramenta `tools/traducao_ptbr.py` gera tudo no PC a partir dos arquivos do próprio usuário e produz um único arquivo, `ptbr.b3tr`, que o port carrega quando existe no cartão SD.

**Créditos:** a tradução é de **TheRalf**. Ele a fez à moda antiga, redesenhando à mão diálogos, menus, cápsulas e gráficos, e levou muito tempo. O mérito da tradução é todo dele; este port só converte o trabalho para a versão HD.

## O que é preciso

- A ISO limpa de PS2 **DragonBall Z - Budokai 3 - Collector's Edition (Europe) (En,Ja,Fr,De,Es,It)** (SLES_533.46).
- O pacote da tradução `DBZ Budokai 3 - Collectors Edition (PT-BR) (V1.2).7z` (traz um `.xdelta`). O pacote da versão Greatest Hits não serve, porque pede a ISO americana Greatest Hits.
- O `game_root` do port já extraído (`data_usi.afs`, `data_spn.afs` e `data_cmn.afs` em `us/`). O pacote fica preso a esses arquivos: o cabeçalho guarda o tamanho e o CRC32 do início de cada um.
- Ferramentas (os caminhos padrão apontam para `usogeral`; todos podem ser trocados por argumentos, veja `--help`):
  - Python 3 com **numpy**, **Pillow** e **zstandard** (sem zstandard o pacote sai sem compressão, bem maior);
  - **7-Zip** (`7zr.exe`) e **xdelta3**, para montar a ISO traduzida;
  - **realesrgan-ncnn-vulkan** (release oficial 20220424 do Real-ESRGAN; precisa de GPU com Vulkan). Sem ele, `--no-upscale` usa só Lanczos;
  - na primeira execução, **clang** (llvm-mingw) com o libmspack do rexglue, para compilar o descompressor LZX `xmemd`.

## Uso

```
python tools/traducao_ptbr.py                      tudo, do patch ao pacote
python tools/traducao_ptbr.py --only menus pack    só algumas etapas (reaproveitam as saídas anteriores)
python tools/traducao_ptbr.py --verify-pack        confere <work>/saida/ptbr.b3tr
python tools/traducao_ptbr.py --clean-iso <iso> --translation-7z <7z> --game-root <game_root> --work <pasta>
```

Pode rodar de novo quantas vezes quiser: cada etapa apaga e refaz as próprias saídas. As imagens ampliadas ficam em cache (`<work>/cache/realesrgan/`), então só a primeira execução usa a GPU. Tempo típico: cerca de 30 minutos (a busca dos botões do PS2 nas imagens de diálogo leva a maior parte; com `--keep-ps2-buttons`, 7 a 8 minutos).

### No cartão SD

Copie o pacote para:

```
switch/B3deko/traducao/ptbr.b3tr
```

A ferramenta também deixa uma cópia em `B3deko/traducao/ptbr.b3tr` (pasta de entrega do port). Sem esse arquivo o jogo fica em inglês, como antes.

## Etapas

1. **patch**: se a ISO traduzida não existir em `<work>/ce/`, extrai o `.xdelta` do `.7z`, aplica sobre a ISO limpa, confere o SHA-256 (`d801e50865b0df46b4d66b0da184121a421156616cbb02ce9e5c382e9b87b2bc`) e apaga o `.xdelta`. A ISO limpa nunca é alterada.
2. **dialogue**: compara `USR/DATA_SPN.AFS` traduzido com o limpo. A tradução ocupa o espaço do espanhol e é toda em imagens: cada fala é uma textura com os acentos já desenhados. Cada textura de diálogo (`#AMT` de PS2, 4 ou 8 bits com paleta) vira `#AZT` do 360 (DDS DXT3), com a entrada do 360 de mesmo tamanho como molde. Saída: `<work>/saida/entradas/NNNN.azt`, `index.json`, `report.txt`, `folhas/folha_NN.png`.
3. **menus**: monta as entradas de menu (base: `data_spn.afs` do 360, porque os tradutores trabalharam sobre o espanhol e o layout espanhol é o que combina com as imagens traduzidas):
   - os 52 contêineres `#AMB` (menus, resultados da luta, avisos): só as texturas que a tradução mudou são trocadas; animações e demais blocos ficam idênticos;
   - as texturas que o 360 redesenhou em cerca de 1,5x: ampliadas com Real-ESRGAN (`realesr-animevideov3` x2, cor e alfa em separado) e reduzidas com Lanczos para o tamanho exato do 360; se a ampliação se afastar demais da imagem original, cai para Lanczos (listado no relatório);
   - as texturas que os tradutores redimensionaram onde o 360 manteve o tamanho do PS2: ficam no tamanho traduzido (o jogo lê o tamanho do cabeçalho da textura);
   - texturas de 16 bits (A4R4G4B4), DXT1 e DXT5;
   - as três animações `#ACA` (621, 2040, 2044) que no 360 mudam por idioma: vêm do `data_spn.afs`, porque posicionam as texturas no layout espanhol que a tradução usa;
   - contêineres que o 360 reordenou (por exemplo 2041) são casados pela imagem, não pelo índice.
4. **signs** (junto com menus): as placas dos cenários traduzidas em `DATA_CMN` (entradas 3787, 3822 e 3848 do `data_cmn.afs`, mesmo tamanho).
   Saída: `<work>/saida/menus/`, `saida/cmn/`, `menus.json`, `report_menus.txt`, `folhas/menus_NN.png` (PS2 PT-BR | gerado | original 360 em inglês).
5. **botões e memory card** (dentro de dialogue e menus; `--keep-ps2-buttons` desliga): a tradução é de PS2 e mostra os botões do PS2. A ferramenta troca cada botão do PS2 pelo botão do 360 tirado das texturas do próprio jogo (`data_usi.afs`, lidas na hora; nada é desenhado à mão nem guardado no repositório) e tira o "PS2" dos avisos de memory card. Detalhes em `tools/ptbr/botoes.py` e `tools/ptbr/memoria.py`. Saída: `saida/botoes.json`, `saida/report_botoes.txt`, `folhas/botoes_NN.png` (antes | depois | original 360 em inglês).
6. **pack**: junta tudo no formato B3TR v1 (descrito em `tools/ptbr/b3tr.py`; cada entrada comprimida com zstd nível 19) em `<work>/saida/ptbr.b3tr` e confere o pacote logo em seguida (`--verify-pack`: descomprime cada entrada, confere o CRC32, confere que cada uma é um `#AZT`/`#AMB`/`#ACA` válido do 360 e compara a tabela de arquivos com o `game_root`).

## O que é traduzido

- Diálogos e textos do modo história (cerca de 1.650 entradas).
- Menus, telas de ajuda, resultados e avisos de luta (`#AMB` e texturas de menu), inclusive as que o 360 redesenhou em HD.
- Placas dos cenários (Cápsula, Hotel, Corporação Cápsula).

## Botões: PS2 para 360

Mapeamento usado, conferido nos próprios dados (o 360 trocou "× Aceptar / △ Cancelar" por "A Aceptar / Y Cancelar" no `#0`, e as legendas de `#2033`/`#2035` mostram LB/RB/LT/RT/START/BACK onde o PS2 tinha L1/R1/L2/R2/START/SELECT):

| PS2 | 360 (imagem usada) | Switch (padrão do port) |
|---|---|---|
| × | A | A |
| ○ | B | B |
| □ | X | X |
| △ | Y | Y |
| L1 / R1 | LB / RB | L / R |
| L2 / R2 | LT / RT | ZL / ZR |
| START | START | + |
| SELECT | BACK | − |
| direcional | direcional do 360, com a mesma seta acesa | direcional |

- Com o padrão do port (`input_xbox_layout=false`) cada botão do Switch vale o botão do 360 de mesma letra, então a letra mostrada é o botão a apertar no Switch.
- Regra de posição: onde uma imagem mostrar os botões na posição do controle, vale a posição do Switch: A à direita, B embaixo, X em cima, Y à esquerda (como o port já faz no Dragon Rush). Nas imagens traduzidas não há nenhum desenho assim (os botões aparecem soltos no texto), então nenhuma precisou ser reposicionada.
- Com **BOTÕES POR POSIÇÃO** (`input_xbox_layout=true`) as letras das imagens deixam de bater com as letras do Switch, igual ao resto do jogo HD (que já mostra os botões do 360).
- Onde a troca é feita:
  - legendas de controle (`#2035` texturas 3 a 61, `#2033` textura 24, `#0` bloco 8 texturas 49 e 50): a parte dos botões vem da textura do 360 em `data_spn.afs`; o texto traduzido fica;
  - botões que os tradutores desenharam no meio do texto (descrições de cápsulas, tutoriais, telas de ajuda, listas de comandos `#2`, `#3`, `#17`): o botão do PS2 é apagado (fundo transparente ou a cor em volta) e o do 360 entra na altura do botão do PS2, centralizado. Os direcionais só são trocados quando batem com os desenhos que os tradutores usaram (`botoes.DPAD_EXEMPLARS`); assim letras de fontes com contorno não são confundidas com direcionais.
  - Os botões escritos por extenso seguem o inglês do 360 ("PRESSIONE START" continua igual).
- Na ilustração da tela de ajuda `#1980` textura 6 (□ □ □ □ ○ sobre o combo) entra a ilustração do próprio 360, que usa os discos P/E como o resto do jogo HD (`botoes.ART_ROWS`).
- Ficam para revisão manual: os direcionais pequenos de `#1980` texturas 6 e 7 ("com A + ✥✥"; desenho diferente dos outros, sobre fundo opaco) e os botões minúsculos do cartão de exemplo da textura 11.

## Memory card

- Os avisos de memory card (`#1995`, `#2025`, legendas de salvar/carregar `#1999` e o aviso do E.S.C. em `#1980`) diziam "memory card (PS2)" e "nem reinicie o PS2". O " (PS2)" e o " o PS2" são cortados da imagem e a linha é fechada (e centralizada de novo se era centralizada).
- "memória do Switch" no lugar não foi possível: as fontes dessas imagens não têm "w" (nem outra letra que sirva) no mesmo estilo, então a troca seria desenho novo. Fica só a remoção.
- "Slot do MEMORY CARD 1/2" e "Cápsulas no MC 1/2" ficam como estão (o 360 diz o mesmo).

## Aviso do modo história só do 360

- O aviso em caixa vermelha ao sair do modo história sem salvar ("All unsaved progress will be lost. End your game?", `data_usi` #2706 textura 0) não existe no PS2, então a tradução não tem. A ferramenta monta "Todo o progresso sem salvar vai ser perdido. / Encerrar a partida?" com as letras da mesma fonte do 360, recortadas dos outros avisos em inglês de #2705/#2706 (`tools/ptbr/hdmsg.py`). A fonte não tem letras acentuadas, por isso o texto foi escolhido sem acentos. Saída em `saida/hd/`.
- A mesma entrada tem a caixa "Yes / No" (`#2706` textura 3), que vira "Sim / Nao" com as letras S, i, m, N, a, o da mesma fonte. O "Nao" fica sem til: nenhuma mensagem do 360 (inglês, espanhol, francês) tem til nessa fonte e nada é desenhado à mão. A ferramenta confere que as outras texturas da entrada #2706 saem idênticas byte a byte.

## O que não é traduzido

- Retratos e cenários de tela cheia (a arte HD do 360 é melhor; alguns são outra imagem no 360): Corp. Cápsula, Casa do Kame e duas molduras de menu (`menus.KEEP_360`), além dos retratos e cenários de `DATA_CMN`.
- Áudio (ADX) e o vídeo de abertura (`opening.sfd`).
- As texturas que só existem no 360 (entradas 2705 em diante: avisos de dispositivo de armazenamento, perfil, salvamento) ficam em inglês, menos o aviso de sair do modo história e o "Sim / Nao" dele (acima).
- Textos que o 360 reescreveu ou acrescentou sem equivalente no PS2 ficam como no `data_spn.afs` (espanhol); textos do PS2 que o 360 removeu são ignorados. Onde o 360 só mudou a redação (por exemplo, avisos de memory card), entra a tradução do texto do PS2. A lista exata fica no `report_menus.txt`.
- As entradas que a tradução não mudou continuam vindo do `data_usi.afs` em inglês.

## Conferências feitas pela ferramenta

- Ida e volta dos gravadores de `#AZT` e `#AMB`: as entradas do 360 voltam byte a byte iguais.
- Erro da imagem gerada contra a imagem do PS2, medido igual ao da conversão feita pela própria Bandai nas mesmas entradas em espanhol.
- Casamento de cada textura do 360 com a do PS2 pela imagem (espanhol do PS2 contra espanhol do 360).
- Folhas com PS2 PT-BR | gerado | original 360 em inglês (`folha_NN`, `menus_NN`, `botoes_NN`).
- A entrada 4 de `data_usi`/`data_cmn` nunca entra no pacote (o jogo a carrega num buffer de tamanho fixo).
- Verificação completa do pacote ao final.
