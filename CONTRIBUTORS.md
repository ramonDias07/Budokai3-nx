# Contribuidores

- **Ramon (ramonDias07)** — manutenção do projeto, testes em Switch físico e validação das releases.
- **OpenAI Codex** — implementação e depuração do port, ferramentas de compilação e documentação em colaboração com Ramon.
- **Claude (Anthropic)** — modelos Claude Opus, Sonnet e Haiku, via Claude Code: implementação, depuração e documentação das versões 0.1.41 a 0.1.56, em colaboração com Ramon.

## Tradução PT-BR

- **TheRalf** — tradução de fãs em português do Brasil do *Dragon Ball Z: Budokai 3* de PS2 (patch v1.2), feita à mão, imagem por imagem. A tradução opcional do port é gerada a partir desse trabalho, no PC de quem joga.

## Projetos cujo trabalho foi usado

As contribuições dos projetos usados como base estão em [Referências](docs/referencias.md). Em especial:

- **Budokai-NX (Unknown, MIT)** — técnica de delta time para a velocidade real da luta (hooks de sub_82088F70, sub_820FC260 e sub_820AB818 e instrumentação do relógio de luta). As notas de reutilização de buffers do projeto foram estudadas para a 0.1.41.
- **NovaPowers (DBZ-Budokai-3-HD-Collection)** — referência e pesquisa de endereços do jogo usadas no ajuste de velocidade e nas sombras dos lutadores.
