# Canal protegido e compatibilidade — marco 0.3

Este guia registra o marco 0.3. Para a versao atual 0.4, use o
[guia UDP/TLS](UDP_LAB.md), com executaveis `build-v04`. A build atual exige
`dsr-network-lab-0.4` no HELLO; os exemplos abaixo ficam como historico.

O laboratório agora oferece TLS 1.2 com OpenSSL 3 e validação explícita do
certificado do host por impressão digital SHA-256. O pin deve ser obtido
independentemente, por um canal confiável, antes de conectar. Não existe
aceitação automática do primeiro certificado recebido.

O executável novo é `build-v03\dsr_net.exe`. Host e clientes precisam ser
da versão nova: o protocolo passou para **3**, com build de laboratório
`dsr-network-lab-0.3`. Esse identificador indica compatibilidade de mensagens;
não é atestado de integridade, DRM, licença nem hash de um executável do DSR.

As entidades ainda são fictícias. Não houve execução, modificação ou integração
ao Dark Souls, teste de contas ou avaliação de compatibilidade Steam/não original.

## Dependências e motivo da escolha

Nesta máquina, o Git para Windows já fornece:

```text
C:\Program Files\Git\usr\bin\openssl.exe
C:\Program Files\Git\mingw64\bin\libssl-3-x64.dll
C:\Program Files\Git\mingw64\bin\libcrypto-3-x64.dll
```

O CLI é usado para gerar o certificado. A aplicação usa a ABI pública do
OpenSSL 3 x64, carregando as DLLs de uma pasta absoluta confiável, sem buscar
em diretório de trabalho ou PATH. Não foram baixados, copiados ou instalados
componentes. Em outro PC, essas bibliotecas precisam estar disponíveis;
opcionalmente informe `--tls-library 'C:\pasta\confiavel'` em host ou cliente.
O CLI aceita caminho próprio por `--openssl` no gerador de identidade.
Distribuir uma aplicação com bibliotecas de terceiros exige tratar seus
requisitos de distribuição; esta entrega não empacota as DLLs do Git.

A tentativa inicial com Schannel retornou `SEC_E_NO_CREDENTIALS` neste
ambiente. Não foi diagnosticada a causa completa nem adotado um contorno
que instalasse certificados ou persistisse chaves no perfil Windows.
A implementação final usa OpenSSL nos dois lados e foi testada contra
clientes/servidor independentes da biblioteca `ssl` do Python.

## 1. Gerar a identidade privada do host

No terminal do host:

```powershell
cd 'C:\Users\luisg\OneDrive\Documentos\dsr-network'
$dsrIdentity = Join-Path $env:LOCALAPPDATA 'DSRNetworkLab\host-v03'
$env:DSR_TLS_PFX_PASSWORD = 'senha-exclusiva-do-arquivo-pfx'
python .\tools\create_tls_identity.py --output $dsrIdentity
```

Escolha uma senha própria de 12 a 128 caracteres ASCII, diferente de senhas
de contas e da sala. O exemplo acima é descartável. A variável permanece
somente naquele terminal e seus processos filhos, sem persistência no sistema.
Proteja também o acesso ao terminal e ao diretório privado.

O gerador exige uma pasta **nova**, sem sobrescrever arquivos. Se ela já
existir, reuse a identidade existente ou escolha outro nome de pasta. O script
cria somente arquivos nessa pasta; não instala certificados no Windows.
Na investigação automatizada, foram usadas apenas identidades temporárias
de teste, removidas ao finalizar a suíte. Nenhuma identidade permanente do
usuário foi gerada automaticamente.

Arquivos gerados:

- `host.pfx`: certificado com chave privada, protegido pela senha PFX.
- `host.key.pem`: chave privada também cifrada, necessária apenas para geração/testes.
- `host.crt.pem`: certificado público.

O certificado é autoassinado, RSA 3072 e válido por 30 dias. O gerador imprime
`TLS_PIN`, um hash SHA-256 do certificado DER. **Nunca compartilhe PFX, chave
privada ou senha PFX com convidados.** Compartilhe apenas o pin e, separadamente,
a senha da sala. Evite guardar identidade privada em pastas sincronizadas;
o projeto atual fica no OneDrive. `.gitignore` não impede sincronização em nuvem.

## 2. Iniciar o host protegido

No mesmo terminal, mantendo `DSR_TLS_PFX_PASSWORD` e `$dsrIdentity`:

```powershell
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
.\build-v03\dsr_net.exe host --port 27032 --max-players 4 --tls-pfx "$dsrIdentity\host.pfx"
```

A porta 27032 evita conflito com laboratórios anteriores. O host mostra
`SERVER_READY` e `TLS_PIN`. Confira se o pin corresponde ao emitido pelo
gerador. Se o arquivo/senha PFX, certificado ou bibliotecas forem inválidos,
o host falha antes de abrir a porta.

## 3. Conectar os clientes protegidos

Em outro PowerShell:

```powershell
cd 'C:\Users\luisg\OneDrive\Documentos\dsr-network'
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
$dsrPin = 'COLE_AQUI_OS_64_DIGITOS_HEXADECIMAIS_DO_TLS_PIN'
.\build-v03\dsr_net.exe join --port 27032 --name Piero --tls-pin $dsrPin
```

Troque o texto do pin pelo valor real, sem dois-pontos ou espaços. Maiúsculas
e minúsculas são aceitas. Não execute com o placeholder. Abra outro cliente
da mesma forma, com outro nome.

Os comandos anteriores continuam disponíveis:

```text
/move 10 0 5
/players
/who
/send 2 mensagem privada
/quit
```

A senha da sala só entra em um registro TLS após verificar certificado,
validade e correspondência exata do pin. Não há fallback para texto claro
quando TLS, bibliotecas, certificado ou pin falham.

O cliente não usa a cadeia de CAs, nome DNS/SAN ou revogação online como
modelo de confiança: **usa o certificado exato previamente autorizado pelo
pin**, mais a validade temporal. Isso permite o laboratório por IP, inclusive
quando diferente do SAN local do certificado. Ao renovar a identidade, o
pin muda e deve ser compartilhado novamente por canal confiável.

## 4. Testes de rejeição

- Mude a senha da sala somente no terminal de um cliente: deve receber `ERROR AUTH` dentro de TLS.
- Use 64 zeros no pin: deve falhar com impressão digital incorreta, sem enviar a senha.
- Tente conectar cliente antigo: não deve ser admitido. O protocolo/build é verificado antes da criação de entidade.
- Tente cliente sem `--tls-pin` contra porta TLS: não deve entrar; não existe negociação automática para texto claro.
- Mude a senha PFX do host: ele não deve emitir `SERVER_READY`.

Senha incorreta da sala é diferente de senha incorreta do arquivo PFX. O
cliente nunca precisa do arquivo nem da senha PFX.

## LAN e limites de segurança

O padrão continua sendo `127.0.0.1`. O modo sem TLS permanece disponível
**somente em loopback**, com aviso explícito. O antigo `--allow-insecure-lan`
foi removido. Fora de loopback, a aplicação exige `--lan` **e** TLS:

```text
host: --address IP_LOCAL_DO_HOST --lan --tls-pfx ARQUIVO
join: --address IP_LOCAL_DO_HOST --lan --tls-pin HASH
```

Esses exemplos são opções, não comandos completos. Use também a mesma porta
e senha nos dois lados. Nenhuma regra de firewall é criada automaticamente.
**LAN e Internet não foram testadas nesta entrega**, apenas processos em
loopback. Não encaminhe portas para a Internet nesta fase.

TLS protege o canal, mas não resolve senha fraca, host malicioso, máquina
comprometida, proteção das chaves em disco, autorização por usuário, abuso
de recursos, NAT/CGNAT ou sincronização do jogo. O PFX cifrado não substitui
permissões de arquivos adequadas. Não há auditoria de segurança nem garantia
de risco zero. Há limites de filas, conexões e tempo; isso não comprova
resistência a negação de serviço. O encerramento atualmente fecha TCP depois
das mensagens da aplicação; não implementa troca completa de `close_notify`.

Mantenha o runtime OpenSSL atualizado por sua origem confiável. Não baixe DLLs
avulsas de sites desconhecidos. A ABI aceita OpenSSL 3, mas isso não avalia
automaticamente se uma versão específica está atualizada ou livre de falhas.

## Protocolo 3

O conteúdo da aplicação continua sendo linhas ASCII, com LF e campos TAB;
não virou um protocolo binário de gameplay. TLS encapsula essas linhas.

```text
HELLO<TAB>3<TAB>nome<TAB>senha<TAB>dsr-network-lab-0.3
WELCOME<TAB>3<TAB>sala<TAB>id<TAB>capacidade
```

`ERROR VERSION` indica versão incompatível; `ERROR BUILD`, build de laboratório
incompatível. São verificações de compatibilidade declarada, não autenticação
de binário. Mensagens de entidades, chat e sala mantêm seus campos anteriores.

O host usa um loop não bloqueante para texto simples ou TLS, escolhido
explicitamente na inicialização, sem detectar/downgradear pelo tráfego.
As chaves e o certificado são carregados em memória pelo runtime OpenSSL,
sem usar um repositório persistente de certificados/chaves do Windows.

## Organização e testes

- `net/tls.hpp/.cpp`: fachada do canal, credenciais e validação do formato do pin.
- `net/openssl_transport.hpp/.cpp`: adaptador TLS, carregamento explícito do runtime,
  leitura do PFX em memória, BIOs e registros cifrados não bloqueantes.
- `tools/create_tls_identity.py`, `tools/localhost.cnf`: geração explícita de identidade.
- `tests/tls_integration.py`: interoperabilidade e testes de rejeição em loopback.

```powershell
cmake -S . -B build-v03 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v03 --parallel 2
ctest --test-dir build-v03 --verbose
```

Há 8 casos unitários, 14 cenários de rede, 11 de entidades e 12 de TLS.
Os cenários TLS incluem quatro clientes, cliente nativo, senha e build errados,
tráfego não TLS, pin incorreto sem dados de aplicação enviados, senha PFX errada,
hash DER conferido, registro cifrado adulterado, captura por proxy sem senha/
movimento em texto claro e timeout de handshake. A captura é local, feita
sobre conexões criadas exclusivamente pelo teste.

O CMake registra os testes TLS somente se encontrar o CLI OpenSSL; se não
encontrar, emite um aviso. Informe `-DDSR_OPENSSL_EXECUTABLE=CAMINHO` para
habilitá-los. Nesse caso, passar nas outras três suítes não valida TLS.

Referências: [OpenSSL handshake](https://docs.openssl.org/3.0/man3/SSL_do_handshake/),
[versões TLS](https://docs.openssl.org/3.0/man3/SSL_CTX_set_min_proto_version/),
[PKCS12_parse](https://docs.openssl.org/3.0/man3/PKCS12_parse/).
