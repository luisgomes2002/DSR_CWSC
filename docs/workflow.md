# Fluxo de trabalho com agentes

Adaptado do fluxo do Sagyou. Regras do projeto: [CLAUDE.md](../CLAUDE.md).

| Estágio | Skill | Saída | Portão humano |
| --- | --- | --- | --- |
| 1. Spec | `/dsr-spec` | `docs/specs/AAAA-MM-DD-<slug>.md` | Sim: "Aprova a spec?" |
| 2. Plano | `/dsr-plan` | `docs/plans/AAAA-MM-DD-<slug>.md` | Sim: "Aprova o plano?" |
| 3. Build | `/dsr-build` | commits em `feature/<slug>` | Não |
| 4. Review | `/dsr-review` | correções + registro | Não |
| 5. PR | `/dsr-pr` | PR aberto | Merge é humano |
| Bug | `/dsr-fix` | `fix/<slug>` → review → PR | Não (salvo se virar feature) |

## Agentes (`.claude/agents/`)

| Agente | Quando | Escreve? |
| --- | --- | --- |
| `dsr-plan-checker` | `/dsr-plan`, antes da aprovação | Não |
| `dsr-implementer` | `/dsr-build`, task grande com interfaces claras | Sim (só a task) |
| `dsr-test-runner` | rodadas de build/ctest longas | Não |
| `dsr-reviewer` | `/dsr-review`, sempre | Não |
| `dsr-security-reviewer` | `/dsr-review`, se o diff toca `src/net/`, `src/protocol/`, `src/session/`, `src/app/options.*`, `tools/create_tls_identity.py`, `tools/player_reader.py`/`static_game_probe.py` ou ganchos de teste | Não |

Revisores só reportam; quem os chamou corrige.

## Regras gerais

- Nunca commitar na `main`, nunca fazer merge, tag ou release. Push só quando o desenvolvedor pedir.
- Registro de progresso em `.dsr/work/<slug>/progress.md` (ignorado pelo git).
- Teste primeiro: veja o teste falhar pelo motivo certo antes de implementar.
- Testes locais: unitários (`ctest -R "_unit$"`). Suítes `*_integration` (processos reais) rodam no GitHub Actions ou quando o desenvolvedor pedir explicitamente. Nunca afirme uma checagem que não rodou.
- Mudança no formato do fio sobe a versão (framing, build ID ou payload) — ver CLAUDE.md.
- **Disciplina de bug:** sem correção sem causa raiz, sem causa raiz sem evidência. Três tentativas falhas: pare e conte o que aprendeu.
- Commits convencionais: `feat(<escopo>)`, `fix(<escopo>)`, `test(<escopo>)`, `docs(<escopo>)`. Escopos: `net`, `protocol`, `session`, `client`, `world`, `ui`, `app`, `reader`, `tools`, `build`.
