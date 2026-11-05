---
name: dsr-review
description: Use quando uma branch do DSR Network Lab está implementada (todas as tasks do plano concluídas, ou um fix pronto) e precisa de review antes do PR, ou quando pedirem para revisar uma branch ou PR.
argument-hint: "[branch, padrão: a atual]"
---

# DSR: revisar a branch (estágio 4)

Branch: $ARGUMENTS (padrão: a atual)

## 1. Empacote a mudança
```bash
slug=<slug>; mkdir -p .dsr/work/$slug
git fetch -q origin
{ git log --oneline origin/main..HEAD; echo; git diff --stat origin/main...HEAD; echo; \
  git diff -U10 origin/main...HEAD -- . ':!docs/reports/*.jsonl' ':!docs/reports/*.json'; } \
  > .dsr/work/$slug/review.diff
```
Vai dentro do repo, nunca em `/tmp`: subagentes não leem fora do diretório de trabalho. Relatórios JSON/JSONL brutos ficam fora do pacote; se mudaram, liste-os.

## 2. Despache os revisores
Passe ao `dsr-reviewer`: o caminho do pacote, os caminhos da spec e do plano (se houver), o **Foco de Review** do plano colado literalmente e o caminho do registro de progresso, para ele pesar as linhas `Decisão:`.

Mudança pequena (menos de ~150 linhas, nada no fio nem em `src/net/`): despache com `model: sonnet`.

Em paralelo, despache o **`dsr-security-reviewer`** se o diff toca `src/net/`, `src/protocol/`, `src/session/`, `src/client/` (parsing), `src/app/options.*`, `CMakeLists.txt` (definições de teste), `tools/create_tls_identity.py`, `tools/player_reader.py` ou `tools/static_game_probe.py`. Passe o pacote, a spec, o plano e o Foco de Review.

Junte os achados de todos antes da triagem.

## 3. Triagem (você decide, não os rótulos)
Reclassifique cada achado por **o que aconteceria se alguém usasse este executável numa sala real**. Quebra silenciosa de protocolo, falha de segurança, crash com entrada da rede e qualquer escrita no processo do jogo são sempre Critical.
- **Critical / Important:** corrija tudo numa passada. Para cada um: escreva um teste que o reproduz, veja falhar, corrija, veja passar, rode as suítes unitárias. Registre `Final: fixed <achado> — <teste> RED→GREEN`.
- **Important que não vai corrigir:** registre `Final: Decisão: <achado> — <por que fica> — <custo se errada>`. Vai para o PR.
- **Minor:** registre `Final: minor (adiado): <uma linha>`. Vai para o PR em "Decisões e pendências". Não corrija minors que ninguém pediu.

Não há segunda rodada de review: os testes que falhavam e agora passam são a prova. Commite as correções (`fix(<escopo>): …`) e dê push se pediram. Depois **siga direto para a skill `dsr-pr`**.
