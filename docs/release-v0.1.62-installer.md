Release do **Budokai3-nx** com o mesmo código do **B3deko 0.1.62** da [v0.1.62](https://github.com/ramonDias07/Budokai3-nx/releases/tag/v0.1.62), agora com o `B3.nro` já compilado e um **instalador de um arquivo** para Windows. O instalador gera, no seu PC e a partir do seu próprio disco, tudo o que deriva do jogo (arquivos do jogo e shaders). Nenhum arquivo do jogo é distribuído. Não precisa de Python, devkitPro nem compilador.

## Instalação

Só Windows 10/11 x64.

1. Baixe `B3deko-0.1.62.zip` e extraia.
2. Rode `B3deko-Installer.exe` e escolha o seu `.iso` **USA/Japan de Xbox 360** do *Dragon Ball Z Budokai HD Collection*.
3. Espere alguns minutos (cerca de 3 no PC de teste; são necessários uns 3 GB livres).
4. Copie a pasta `switch` criada em `sd\` para a raiz do cartão SD.
5. Abra o **B3deko** pelo Homebrew Menu em modo aplicativo.

Nada é enviado pela internet. O Windows SmartScreen pode avisar porque o `.exe` não é assinado; o código está em `tools/b3_installer.py` e pode ser refeito com `tools/build_installer.ps1`. Ao atualizar, preserve a pasta `budokai3/` (saves) e apague ou renomeie o `budokai3.toml` antigo.

O `budokai3.toml` da release vem com a tradução PT-BR de **TheRalf** ligada. Para o inglês: menu rápido (`+` e `−` juntos) → **LANGUAGE** → **ENGLISH**, salve e reinicie o jogo.

## O que há no zip

| Arquivo | Para quê |
| --- | --- |
| `B3deko-Installer.exe` | Gera a pasta do SD a partir do seu `.iso`. |
| `B3.nro` | O port (B3deko 0.1.62), copiado para o SD pelo instalador. |
| `budokai3.toml` | Configuração padrão da release. |
| `traducao/ptbr.b3tr` | Tradução PT-BR de TheRalf. |
| `LEIA-ME.txt` | Passos resumidos em português e inglês. |
| `licenses/` | Licenças das ferramentas embutidas no instalador. |

Os arquivos do instalador foram conferidos byte a byte contra os arquivos do SD testados no console: pacote de shaders com 135 entradas e cache com 144 entradas idênticos.

## Reportar bugs

Podem aparecer bugs. Abra o menu rápido (`+` e `−` juntos) → **LOGS AND DIAGNOSTICS** → ligue **LOG TO FILE** → salve e reinicie o jogo → repita o problema (para defeitos de imagem, aperte também `L3 + R3` uma vez no quadro com defeito) → abra uma [issue](https://github.com/ramonDias07/Budokai3-nx/issues) anexando `/switch/B3deko/logs/` (todos os arquivos), `/switch/B3deko/budokai3.toml`, a captura `b3_deko_diag` se você fez uma e, se o console travou ou fechou o jogo, os arquivos mais recentes de `/atmosphere/crash_reports/` e `/atmosphere/fatal_reports/`. Depois, desligue **LOG TO FILE** de novo.

## Teste no console

As versões até a 0.1.61 foram testadas no Switch físico, incluindo a legenda, os shaders de efeito, a tradução PT-BR e o pacote de desempenho que a v0.1.56 trazia sem teste. A 0.1.62 só acrescenta a opção de log e ainda não foi testada.

## Defeito conhecido

Em alguns diálogos em quadrinhos do Dragon Universe, logo depois de uma luta que termina com um especial de disputa de força, o fundo fica congelado no último quadro da luta, amarelado, e o retrato do personagem não aparece. Os textos seguem normais e o jogo continua. A causa ainda não foi provada. Se acontecer com você, ligue **LOG TO FILE**, repita a cena e anexe o log numa issue.

## Verificação (SHA-256)

| Arquivo | SHA-256 |
| --- | --- |
| `B3deko-0.1.62.zip` | `db9722454716fd62ea95041c5587beafbae4eef994f21f4e7c7df073ed622c2b` |
| `B3deko-Installer.exe` | `b48e9fcee19f91c74e2bd21d05e14dd01e49cba43cdd0307b5a9e780b10c3c3f` |
| `B3.nro` | `8b6cd6869f0019d1c45fa64e5f108de7a3238254274cbe3bab322e3fc5662db1` |