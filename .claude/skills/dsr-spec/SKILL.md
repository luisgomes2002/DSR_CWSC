---
name: dsr-spec
description: Use quando pedirem uma feature nova ou mudança de comportamento no DSR Network Lab (protocolo, transporte, sala, métricas, leitor do jogo, ferramentas) e ainda não existe spec aprovada em docs/specs/, inclusive pedidos "rápidos" ou do tipo "só constrói".
argument-hint: "[o que você quer construir]"
---

# DSR: escrever a spec (estágio 1)

Pedido: $ARGUMENTS

Estágio 1 do fluxo em `docs/workflow.md`. Saída: spec aprovada em `docs/specs/`. **Sem código ou plano até um humano aprovar.**

## 1. Veja o que já existe
- `git fetch -q && git branch -a` e `gh pr list`: alguém já está construindo isto ou algo de que depende? Se sim, avise e pare até o humano decidir.
- Leia `CLAUDE.md`, a seção relevante de `docs/NEXT_STEPS.md` (em que fase do roadmap isto está? a fase anterior foi validada?) e os guias da área (`docs/MOVEMENT_PROTOCOL.md`, `UDP_LAB.md`, `PLAYER_READER.md`, `ROOM_FAULT_LAB.md`…).
- O roadmap não autoriza fases futuras de antemão. Se o pedido pula uma fase (ex.: integração com o jogo antes da LAN), diga e pergunte.
- Crie a branch antes de escrever: `git switch -c feature/<slug> origin/main`. Se a árvore tem mudanças que não são suas, não as toque: pergunte.

## 2. Entrevista
Uma pergunta por mensagem, em linguagem simples, ancorada num cenário real ("quantos clientes, em que rede, o que você quer ver no terminal"). Cubra: o problema, como é o sucesso (medível: latência, taxa, recuperação em N s), o que está fora, os detalhes que importam (mensagens de saída exatas, limites, bordas: perda, desconexão, versão diferente) e o risco (segurança, jogo). Se disserem "você decide", proponha algo concreto e marque como decisão sua.

## 3. Escreva `docs/specs/AAAA-MM-DD-<slug>.md`
```markdown
# <Nome da feature>

Status: draft
Branch: feature/<slug>
Fase do roadmap: <seção de docs/NEXT_STEPS.md>

## Por quê
## Objetivos
<!-- critérios mensuráveis -->
## Fora do escopo
## Decisões (de <nome>)
## Decisões do construtor (o agente decidiu; o humano pode mudar)
## Comportamento
<!-- regras observáveis, linhas de saída exatas (ex.: UDP_ACTIVE), bordas: perda, duplicação, timeout, desconexão, entrada inválida, versão incompatível -->
## Protocolo e compatibilidade
<!-- frames/payloads novos ou alterados, limites, qual versão sobe (framing 3, build ID, payload 1) — ou "sem mudança no fio". Se quebra: ⚠️ Breaking wire change — host e clientes precisam do novo executável. -->
## Segurança
<!-- impacto no modelo de confiança do CLAUDE.md, entradas não confiáveis novas, segredos — ou "sem impacto" com justificativa -->
## Jogo
<!-- só se toca o leitor/integração: confirmar somente leitura, ações manuais do humano, relatórios novos — ou "não toca o jogo" -->
## Como validar
<!-- testes unitários, suítes de integração (CI), benchmark, captura manual -->
## Perguntas em aberto
<!-- precisa estar vazio antes da aprovação -->
```

## 4. Portão
Mostre o caminho da spec e um resumo de 5 linhas, e pergunte: "Aprova a spec?". Espere. Quando o humano disser sim, troque para `Status: approved by <nome> on <AAAA-MM-DD>`, commite `docs(<slug>): spec` em `feature/<slug>` e dê push se pediram.

Uma feature minúscula (nada no fio, nada de segurança) pode mostrar spec e plano na mesma mensagem, mas ainda precisa de dois sins explícitos. Depois, **siga direto para a skill `dsr-plan`** com esta spec.

## Bandeiras vermelhas: volte ao portão
| Pensamento | Realidade |
| --- | --- |
| "É só um campo a mais no pacote" | É mudança no fio: versão, doc do protocolo e teste de rejeição. |
| "Para testar, afrouxo o pin/TLS" | Não. Use os alvos de teste existentes. |
| "Aproveito e conecto o leitor ao laboratório" | Escopo e fase não aprovados. Pergunte. |
| "Escrevo `approved` eu mesmo" | Só um humano aprova. |
