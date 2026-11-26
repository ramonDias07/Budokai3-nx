Quinta release do **Budokai3-nx**, correspondente ao **B3deko 0.1.62**. Parte da [v0.1.56](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.56) e reúne as versões 0.1.57 a 0.1.62. As novidades são a correção do feixe dos especiais no GAME TIME, uma revisão geral do código com ganhos de CPU, ajustes no áudio e os logs desligados por padrão.

## Instalação

Igual à da v0.1.56: instale o [devkitPro](https://github.com/devkitPro/installer/releases) com **Switch Development**, baixe **Source code (zip)** em **Assets**, extraia e dê dois cliques em `install.bat`. Ele pede o seu `.iso` **USA/Japan de Xbox 360** e monta a pasta `B3deko` em `out\sd\switch\`. Os passos à mão estão no [guia de compilação](https://github.com/ramonDias07/Budokai3-nx/blob/v0.1.62/docs/build.md).

Ao atualizar, copie `B3.nro`, `budokai3.toml`, `b3_shaders.nfsp` e `shadercache_dksh.bin`. Se já houver um `budokai3.toml` no cartão, apague ou renomeie o arquivo para receber os novos padrões. Preserve a pasta `budokai3/`, que guarda os saves e dados.

A tradução PT-BR de **TheRalf** continua a mesma da v0.1.56. O `install.bat` baixa o pacote `ptbr.b3tr` daquela release.

## Logs desligados por padrão

O port não grava mais o log nem o relatório de desempenho no cartão, a não ser que você peça. Para relatar um defeito, abra o menu rápido (`+` e `−` juntos), entre em **LOGS AND DIAGNOSTICS**, ligue **LOG TO FILE**, salve e reinicie o jogo. Os arquivos saem em `/switch/B3deko/logs/`. No `budokai3.toml` a opção é `log_to_file = true`.

O arquivo `logs/rex/rex_stderr.log` continua existindo. É um arquivo único e pequeno, reescrito a cada abertura, que registra a causa quando o jogo fecha sozinho.

## Teste no console

As versões até a 0.1.61 foram testadas no Switch físico, incluindo a legenda, os shaders de efeito, a tradução PT-BR e o pacote de desempenho que a v0.1.56 trazia sem teste. A 0.1.62 só acrescenta a opção de log e ainda não foi testada.

Cada mudança de comportamento da 0.1.61 tem uma opção própria. Se algo piorar, ponha a opção em `false` no `budokai3.toml` e conte numa [issue](https://github.com/ramonDias07/Budokai3-nx/issues):

| Opção | O que faz |
| --- | --- |
| `b3_registros_em_bloco` | Constantes dos shaders escritas em bloco |
| `nfsmw_dk_memo_shaders` | Menos buscas de shader por desenho |
| `rex_reloj_sin_perdida` | Relógio do jogo sem perda na conversão |
| `audio_switch_tempo_continuo` | Emenda contínua do áudio perto da velocidade normal |
| `audio_switch_cola_por_tempo` | Atraso dos efeitos sonoros estável com o jogo mais lento |
| `audio_switch_retardo_suave` | Troca de atraso do áudio sem estalo |

## Defeito conhecido

Em alguns diálogos em quadrinhos do Dragon Universe, logo depois de uma luta que termina com um especial de disputa de força, o fundo fica congelado no último quadro da luta, amarelado, e o retrato do personagem não aparece. Os textos seguem normais e o jogo continua. A causa ainda não foi provada. Se acontecer com você, ligue **LOG TO FILE**, repita a cena e anexe o log numa issue.

## O que mudou desde a v0.1.56

### 0.1.57

- GAME TIME: o tempo compensado não vale mais do que 1,25 passo por quadro. Os efeitos dos especiais (feixe, fogo) não são mais pulados quando a cena carrega pela primeira vez. Ajuste: `b3_tempo_jogo_salto_max` (0 = comportamento antigo).

### 0.1.58 e 0.1.59

- Tentativas de usar o passo original do jogo durante os especiais e de limitar o vídeo de abertura a 30 FPS. As duas foram retiradas: o feixe ficou correto só com o limite da 0.1.57.

### 0.1.60 (diagnóstico)

- O log registra os diálogos em quadrinhos (linhas `[B3 dialogue]`) para investigar o defeito conhecido acima.

### 0.1.61: revisão geral do código

- Desempenho: constantes dos shaders escritas em bloco, menos buscas de shader por desenho, cronômetros e registros de diagnóstico só com o log periódico ligado.
- Relógio do jogo sem perda na conversão de tempo.
- Áudio no GAME TIME: emenda contínua perto da velocidade normal, atraso dos efeitos estável com o jogo mais lento e troca de atraso sem estalo.
- Tradução PT-BR: um erro de leitura do cartão volta como erro para o jogo e aparece no log, em vez de virar dados zerados.
- Salvar as configurações grava num arquivo temporário antes de trocar o `budokai3.toml`.
- Código sem uso removido, incluindo a detecção de cena especial (`b3_especial_passo_original` não existe mais).
- Experimentos desligados por padrão: `switch_nucleo_hilo_principal` e `switch_nucleo_hilo_gpu` (-1 = como antes; 0 a 2 = núcleo preferido).

### 0.1.62

- Opção `log_to_file`, desligada por padrão (veja acima). A linha LOG TO FILE do menu passou a funcionar.
