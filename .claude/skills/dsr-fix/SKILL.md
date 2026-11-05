---
name: dsr-fix
description: Use quando algo no DSR Network Lab está quebrado ou se comporta errado (teste CTest falhando, crash, handshake que não fecha, posição stale, métrica errada, leitor do jogo com valor errado), antes de propor ou fazer qualquer correção.
argument-hint: "[o que está quebrado]"
---

# DSR: corrigir um bug

Bug: $ARGUMENTS

**Sem correção sem causa raiz, e sem causa raiz sem evidência.** Detalhes em `docs/workflow.md` → Disciplina de bug.

## 1. É mesmo um fix?
Se corrigir exige mudar o formato do fio, afrouxar uma regra de segurança do `CLAUDE.md` ou criar comportamento que ninguém especificou, é feature: vá para `/dsr-spec`. Senão, crie a branch `fix/<slug>` a partir de `origin/main`.

## 2. Reproduza e reúna evidência
- **Passos exatos, esperado vs. obtido**, e se acontece sempre ou é intermitente (rede e timing: rode várias vezes, anote a taxa).
- Mudanças recentes: `git log --oneline -15` e o diff da área.
- Saída do host e dos clientes (`SERVER_READY`, `UDP_ACTIVE`, erros do OpenSSL), relatórios JSON de métricas, `ctest -R '^<suíte>$' --output-on-failure -V`.
- Leitor do jogo: use as capturas existentes em `docs/reports/` ou o fixture sintético. Se precisar de captura nova, prepare o comando e peça ao humano para executá-la; o agente não abre o jogo.

## 3. Causa raiz
Enuncie uma hipótese: "X é a causa porque <evidência>". Teste com a menor verificação possível, uma variável por vez. Se errou, nova hipótese a partir da nova evidência; não empilhe correções. Desconfie de "aumentar o timeout": só se a evidência mostrar que o limite estava errado, não que algo lento esconde um bug.

## 4. Corrija, teste primeiro
1. Escreva o teste que falha **por causa do bug**, na camada mais baixa que o mostra (preferencialmente `*_unit`), e veja-o falhar.
2. Faça a correção mínima na origem, não onde o sintoma aparece. Veja o teste passar e rode as suítes unitárias inteiras. Se o bug só aparece em `*_integration`, escreva/ajuste o teste lá e diga que ele precisa rodar no CI.

**Três correções tentadas e ainda quebrado: pare.** O desenho provavelmente está errado; conte ao humano o que aprendeu em vez de tentar a quarta.

## 5. Termine
Commite `fix(<escopo>): <o quê>`. O corpo do PR ganha a seção "Causa raiz": causa, evidência e o teste que a prova. Depois **siga direto para a skill `dsr-review`**, que segue para `dsr-pr`.
