---
name: dsr-pr
description: Use quando uma branch do DSR Network Lab foi revisada e está pronta para pull request, ou quando pedirem para abrir, atualizar ou descrever um PR deste repo.
argument-hint: "[branch, padrão: a atual]"
---

# DSR: abrir o pull request (estágio 5)

## 1. Prove na árvore commitada
`git status --short` deve estar vazio (ou os restos explicados — `.sagyou-mcp*` não entram). Rode, via agente `dsr-test-runner`:
```bash
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 -R "_unit$" --output-on-failure
```
Vermelho para aqui: corrija (com teste) ou relate com honestidade. Nunca afirme uma checagem que não rodou. As suítes `*_integration` rodam no GitHub Actions: depois de abrir o PR, acompanhe com `gh pr checks <n> --watch`. Se não existe workflow de CI, diga no PR que elas não rodaram.

## 2. Push e abertura
`git push -u origin <branch>`, depois `gh pr create --base main --title "<título convencional>" --body-file <arquivo>`. Preencha:
- **Summary:** o que muda no laboratório, e links da spec e do plano.
- **Protocolo:** se muda o fio; quais versões subiram (framing, build ID, payload). Sem mudança: "Sem mudança no fio".
- **Segurança:** impacto no modelo de confiança, ou "sem impacto".
- **Testes:** os comandos que rodaram, contagens, e o que ficou para CI/manual (LAN, captura no jogo).
- **Documentação:** README (contagens, status), `docs/NEXT_STEPS.md`, guias atualizados — e o que **não** foi validado.
- **Decisões e pendências:** do registro de progresso, todas as linhas.
- **Causa raiz:** em fix.

## 3. Entregue a um humano
- Agentes nunca fazem merge, tag ou release.
- Após o merge, apague `.dsr/work/<slug>/`. O histórico do git é o registro.

Relate a URL do PR, o estado dos checks e o passo humano pendente (review/merge, teste manual).
