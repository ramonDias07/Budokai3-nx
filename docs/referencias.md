# Referências e proveniência

## Projetos usados

- **DBZ Budokai 3 HD Collection** de NovaPowers. A recompilação para PC orientou o manifesto e a identificação do executável USA/Japan. A licença MIT original acompanha este repositório. [Projeto e arquivos base](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/main/baserom.md)
- **ReXGlue SDK NX**. O patch do runtime aplica-se sobre o commit `f961303fe3c6e3a90905b379ff4bf3de9a35a757`. Ele reúne o transporte dos quadros, a adaptação Deko, a captura de diagnóstico e os ajustes de áudio do B3. O ReXGlue deriva do Xenia. [Commit do SDK NX](https://github.com/nx-mod/rexglue-sdk-nx/commit/f961303fe3c6e3a90905b379ff4bf3de9a35a757)
- **Need for Speed: Most Wanted para Switch**. A integração `app/src/nfsmw_*` reutiliza o formato de shaders e rotinas do renderizador usadas em conjunto com o SDK NX. O B3 seleciona e corrige os próprios passes e mantém o processador de comandos Xenos. [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)
- **LoS1-nx e sa2-nx**. `tools/extract_iso.py` é a base da extração XDVDFS e `tools/deko3d_glsl.py` adapta GLSL para Deko3D. O tradutor Xenos em `tools/xenos/` carrega as extensões reNut/banjo-nx que a cadeia do LoS1-nx usa. [LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx) · [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) · [XenosRecomp](https://github.com/hedge-dev/XenosRecomp)
- **uam, Mesa/nouveau e SwitchWakerHD**. A cópia vendorizada conserva a licença e as notas originais das adaptações de uam 1.1.0. [uam](https://github.com/devkitPro/uam) · [Notas do compilador](../tools/vendor/uam/PATCHES.md)

## Como projetos semelhantes pedem o jogo

| Projeto | Entrada e instalação |
| --- | --- |
| [Castlevania Lords of Shadow (LoS1-nx)](https://github.com/FurryLilManChild/LoS1-nx) | Dois dumps ISO do Xbox 360, extraídos para `game_root` e `game_root_disc2`. O README informa o hash do XEX, as ferramentas e o que copiar para o SD. |
| [Shaolin Monks NX](https://github.com/Thorhax/ShaolinMonks-NX) | ISO do Xbox original ou arquivos extraídos ao lado do NRO. O port confere o hash de `default.xbe`. |
| [Guitar Hero III (gh3-nx)](https://github.com/guillermedev75/gh3-nx) | Dump Xbox 360 extraído com `default.xex` e `DATA/`. A pessoa gera o pacote em seu computador. O NRO confere o hash do executável na instalação. |
| Budokai3-nx | O extrator recebe uma ISO USA/Japan e usa o XEX e os dados `us/` do Budokai 3. O build gera C++, shaders e NRO localmente. |
