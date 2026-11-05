---
name: dsr-build
description: Use quando um plano do DSR Network Lab em docs/plans/ está aprovado e suas tasks precisam ser implementadas, ou ao retomar um build interrompido (janela nova, contexto compactado).
argument-hint: "[docs/plans/<arquivo>.md]"
---

# DSR: construir o plano (estágio 3)

Plano: $ARGUMENTS

**Recuse começar** se o plano não diz `Status: approved by …`. Nesse caso, aponte para `/dsr-plan`.

## Preparação
1. Leia o plano e a spec uma vez. Anote as Restrições Globais e o Foco de Review: valem para toda task.
2. Vá para a branch do plano (`git switch feature/<slug>`, ou crie a partir de `origin/main`). Nunca trabalhe na `main`.
3. Registro de progresso: `.dsr/work/<slug>/progress.md` (ignorado pelo git). A primeira linha é `# plan: <caminho do plano>`. Se já existe, cada linha `Task N: complete` está feita: retome na primeira sem ela, e confie no `git log` mais que na memória.
4. Garanta o build: `cmake -S . -B build-v04 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON` se `build-v04` não existe. Releia `CLAUDE.md` se a task toca o fio, TLS/DTLS ou o leitor do jogo.

## Por task, em ordem
1. Releia o texto da task (valores exatos vivem lá, não na sua memória).
2. Escreva o teste primeiro, compile, rode e **veja-o falhar pelo motivo que o plano diz**. Teste que passa antes de o código existir está quebrado: conserte.
3. Implemente o mínimo para passar, rode o comando da task e compare com o `Esperado:`.
   - Saída difere porque **o código está errado**: ache a causa raiz antes de mudar qualquer coisa (veja `dsr-fix` §3).
   - Saída difere porque **o plano está errado**: faça a menor mudança que ainda satisfaça a spec. Acrescente `Task N: Decisão: <o quê> — <por quê> — <custo se errada>` ao registro.
4. Rode o build completo (sem warnings novos) e `ctest --test-dir build-v04 -R "_unit$" --output-on-failure`. Para rodadas longas, use o agente `dsr-test-runner`. Suítes `*_integration` ficam para o CI, salvo pedido do desenvolvedor.
5. Commite com a mensagem do plano; dê push se o desenvolvedor pediu.
6. Acrescente `Task N: complete (<sha-inicial>..<sha-final>, <comando> → <resultado>)`.

Uma task grande com interfaces claras pode ir para o agente `dsr-implementer`: passe o caminho do plano, o número da task e o do registro. Confira o resultado você mesmo (rode os testes de novo) antes de escrever a linha de conclusão.

## Só isto interrompe o build
- algo destrutivo ou irreversível (apagar evidência em `docs/reports/`, identidade TLS do usuário, reescrever histórico git);
- qualquer coisa que publique (tag, release, push para `main`), que agentes nunca fazem;
- qualquer ação no jogo: abrir, escrever memória, injetar — ou uma captura ao vivo, que é do humano;
- mudança de rede fora do laboratório (firewall, porta no roteador, serviço externo);
- uma escolha de segurança ou de formato do fio que o plano não resolve;
- um plano tão errado que todo caminho adiante é chute.

Fora isso, decida, registre uma Decisão e siga. Não pare entre tasks para perguntar "posso continuar?".

Com todas as tasks concluídas, **siga direto para a skill `dsr-review`**.
