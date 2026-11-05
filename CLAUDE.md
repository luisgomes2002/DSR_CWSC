# DSR Network Lab — guia para agentes

Laboratório de rede host-based (C++17, Windows x64, MinGW/CMake/CTest) para um futuro co-op privado de Dark Souls Remastered, mais um leitor externo **somente leitura** de dados do jogo (`tools/player_reader.py`). Visão geral, build e arquitetura: [README.md](README.md). Roadmap: [docs/NEXT_STEPS.md](docs/NEXT_STEPS.md). Fluxo de trabalho e skills: [docs/workflow.md](docs/workflow.md).

## Build e testes

```powershell
cmake -S . -B build-v04 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 -R "_unit$" --output-on-failure        # unitários: ok rodar localmente
```

As suítes `*_integration` sobem processos reais de host/cliente em loopback (são end-to-end). Pela preferência do desenvolvedor, **não rode localmente sem ele pedir**: elas são para CI (GitHub Actions). Capturas ao vivo do jogo são sempre manuais e feitas pelo humano.

- Arquivo `.cpp` novo entra em `dsr_network_core` no `CMakeLists.txt`; teste novo ganha `add_test` com `TIMEOUT`.
- Compila sem warnings com `-Wall -Wextra -Wpedantic -Wconversion`.
- Python: só biblioteca padrão. Dependência nova (C++ ou Python) só com aprovação.

## Regras que quebram compatibilidade

Host e clientes precisam do mesmo executável. Mudou o formato do fio, mude a versão:

- Framing TCP versão `3` (ASCII, LF, campos TAB); build ID `dsr-network-lab-0.4`; payload binário de movimento versão `1`, **68 bytes** dentro de DTLS.
- Mudar campo, ordem, tamanho ou semântica de frame/payload exige: subir a versão correspondente, atualizar `docs/MOVEMENT_PROTOCOL.md` e o README, e teste que rejeita a versão antiga.
- Limites são contrato: frame ≤ 2048 bytes antes do LF, texto ≤ 1024, fila de saída ≤ 64 KiB por conexão, nome 1–24 (`A-Za-z0-9_-`), sala 2–32 (padrão 8). Toda entrada vinda da rede é não confiável e validada antes de usar.
- O host atribui as próprias revisões e valida dono da entidade; nunca confia em sequência do cliente. Revisão aplicada nunca regride.

## Regras de segurança

- Fora de loopback: `--lan` e TLS obrigatórios. `--udp` sempre exige TLS. **Sem fallback silencioso** para texto puro ou movimento TCP após falha do canal.
- Confiança no host = pin SHA-256 explícito + validade do certificado. Sem trust-on-first-use, CA do sistema ou desligar checagem.
- Chave DTLS derivada da sessão TLS autenticada (exporter), nunca da senha da sala.
- Segredos (senha da sala, senha do PFX, chave privada) só por variável de ambiente/arquivo do usuário; nunca em log, saída, relatório ou commit (`*.pfx`, `*.key.pem`, `/secrets/` estão no `.gitignore`).
- Ganchos de falha só existem em alvos de teste (`DSR_ROOM_TESTING`, `DSR_DTLS_TESTING`). O `dsr_net` normal não ganha relay, hook nem flag de teste.
- Não abrir porta no roteador, mexer em firewall, instalar certificado no Windows, contratar servidor/relay ou serviço externo sem decisão do humano.

## Regras do jogo (leitor e integração)

- O leitor **só lê** memória de outro processo. Nunca escrever memória, injetar DLL/código, alterar executável, save ou arquivos do jogo/Seamless.
- Agentes não abrem o jogo nem o mod. Capturas ao vivo: o humano executa as ações; o agente prepara o comando e analisa o resultado.
- Evidência não se sobrescreve: cada captura vai para um arquivo novo com data/hora em `docs/reports/`.
- Não copiar código, binários ou protocolo do Seamless Co-op. Análise estática registra endereço/versão, evidência e hipótese separadamente.
- Nunca afirmar risco zero de ban nem compatibilidade com o multiplayer do jogo.

## Honestidade da documentação

README e `docs/NEXT_STEPS.md` separam **implementado / testado / proposto**. Ao mudar comportamento ou testes, atualize as contagens de casos e suítes do README. Teste em loopback não é prova de LAN, Internet, gameplay ou capacidade do jogo; diga isso.
