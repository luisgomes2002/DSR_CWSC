---
name: dsr-plan
description: Use quando uma spec do DSR Network Lab em docs/specs/ está aprovada e ainda não tem plano em docs/plans/, ou quando um plano aprovado precisa mudar no meio do build.
argument-hint: "[docs/specs/<arquivo>.md]"
---

# DSR: escrever o plano (estágio 2)

Spec: $ARGUMENTS

**Recuse começar** se a spec não diz `Status: approved by …`. Nesse caso, aponte para `/dsr-spec`.

O plano é o conjunto de decisões que um implementador não toma sozinho: arquivos, nomes e assinaturas, layouts de frame/payload, limites e textos exatos da spec, e os testes que provam cada task. Não é o código.

## Escreva `docs/plans/AAAA-MM-DD-<slug>.md`
```markdown
# Plano de implementação: <Feature>

Status: draft
Spec: docs/specs/<arquivo>.md
Branch: feature/<slug>

**Objetivo:** uma frase. **Arquitetura:** 2-3 frases (quais módulos de src/ mudam).

## Restrições Globais
<!-- uma linha cada, valores exatos da spec e do CLAUDE.md: versões, limites, timeouts -->

## Foco de Review
<!-- as 5 entradas/modos de falha com mais chance de quebrar o laboratório que a spec implica mas não detalha,
     cada um com o comportamento esperado e "Testado na Task N". Sempre inclua: entrada malformada da rede
     e compatibilidade com o executável anterior. -->

### Task N: <nome>
**Arquivos:** Criar / Modificar / Teste (caminhos exatos; CMakeLists.txt se há .cpp ou teste novo)
**Interfaces:** Consome (de tasks anteriores, assinaturas exatas) / Produz (para as seguintes)
- [ ] Passo 1: escreva o teste que falha: nomes + asserções com os valores exatos da spec
- [ ] Passo 2: rode `ctest --test-dir build-v04 -R '^<suíte>$' --output-on-failure`. Esperado: FALHA porque <motivo>
- [ ] Passo 3: implemente `<assinatura>` em `<arquivo>` (uma linha sobre a abordagem se houver escolha real)
- [ ] Passo 4: rode o mesmo comando. Esperado: PASSA
- [ ] Passo 5: commit `<tipo>(<escopo>): <mensagem>`
```

Regras:
- Uma task é a menor unidade com ciclo de teste próprio que um revisor poderia rejeitar sozinha.
- Prefira provar a lógica em suíte unitária (`*_unit`). Cenário que só aparece com processos reais vai para suíte `*_integration` — escreva o teste no plano, mas marque que roda no CI/quando o desenvolvedor pedir.
- Mudança no fio: uma task sobe a versão, atualiza `docs/MOVEMENT_PROTOCOL.md` e testa rejeição da versão anterior.
- Leitor do jogo: o teste usa processo filho sintético (`tests/player_reader_fixture.py`); captura real é um passo manual do humano com arquivo novo em `docs/reports/`.
- Sem linhas que não decidem nada ("tratar bordas", "adicionar validação", "TBD").
- A última task é sempre: suítes unitárias completas, lista do que rodar no CI/manualmente, PR (estágio 5), e atualizar README (contagens de testes, status), `docs/NEXT_STEPS.md` e `CLAUDE.md` se regras mudaram.

## Checagem do construtor
Despache o agente `dsr-plan-checker` com os caminhos da spec e do plano. Classifique as perguntas:
- **Já respondida** pela spec ou repo: escreva a resposta no plano.
- **Menor** (não muda o que é construído): decida e marque "(decisão do construtor)".
- **Do humano** (muda o que é construído): pergunte numa mensagem só, a mais importante primeiro.

## Portão
Mostre o caminho do plano, a lista de tasks com uma linha cada, e suas decisões. Pergunte "Aprova o plano?" e espere. Com sim: `Status: approved by <nome> on <AAAA-MM-DD>`, commite `docs(<slug>): plan` em `feature/<slug>` e dê push se pediram. Depois **siga direto para a skill `dsr-build`** com este plano.

Se o plano mudar no meio do build: edite, volte para `Status: draft` e peça nova aprovação das tasks alteradas.
