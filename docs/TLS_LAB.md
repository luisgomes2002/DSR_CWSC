# Protected channel and compatibility – milestone 0.3

This guide records milestone 0.3. For the current version 0.4, use the
[UDP/TLS guide](UDP_LAB.md), with `build-v04` executables. The current build requires
`dsr-network-lab-0.4` in HELLO; The examples below are historical.

The lab now offers TLS 1.2 with OpenSSL 3 and explicit validation of the
host certified by SHA-256 fingerprint. The pin must be obtained
independently, over a trusted channel, before connecting. Does not exist
automatic acceptance of the first certificate received.

The new executable is `build-v03\dsr_net.exe`. Host and clients need to be
of the new version: the protocol went to **3**, with laboratory build
`dsr-network-lab-0.3`. This identifier indicates message compatibility;
It is not a certificate of integrity, DRM, license or hash of a DSR executable.

The entities are still fictitious. There was no execution, modification or integration
to Dark Souls, account testing or Steam/non-genuine compatibility assessment.

## Dependencies and reasons for choice

On this machine, Git for Windows already provides:

```text
C:\Program Files\Git\usr\bin\openssl.exe
C:\Program Files\Git\mingw64\bin\libssl-3-x64.dll
C:\Program Files\Git\mingw64\bin\libcrypto-3-x64.dll
```

The CLI is used to generate the certificate. The application uses the public ABI of the
OpenSSL 3 x64, loading the DLLs from an absolute trusted folder, without fetching
in working directory or PATH. Have not been downloaded, copied or installed
components. On another PC, these libraries need to be available;
optionally enter `--tls-library 'C:\folder\trusted'` on host or client.
The CLI accepts proper path via `--openssl` in the identity generator.
Distributing an application with third-party libraries requires handling their
distribution requirements; This release does not package the Git DLLs.

The initial attempt with Schannel returned `SEC_E_NO_CREDENTIALS` in this
environment. The complete cause was not diagnosed nor a workaround adopted.
that installed certificates or persisted keys in the Windows profile.
The final implementation uses OpenSSL on both sides and has been tested against
clients/server independent of the Python `ssl` library.

## 1. Generate the host's private identity

On the host terminal:

```powershell
cd 'C:\Users\luisg\OneDrive\Documents\dsr-network'
$dsrIdentity = Join-Path $env:LOCALAPPDATA 'DSRNetworkLab\host-v03'
$env:DSR_TLS_PFX_PASSWORD = 'pfx-file-unique-password'
python .\tools\create_tls_identity.py --output $dsrIdentity
```

Choose your own password of 12 to 128 ASCII characters, different from passwords
of accounts and the room. The example above is disposable. The variable remains
only in that terminal and its child processes, without persistence in the system.
Also protect access to the terminal and private directory.

The generator requires a **new** folder, without overwriting files. If she already
exists, reuse the existing identity, or choose another folder name. The script
creates only files in that folder; does not install certificates on Windows.
In the automated investigation, only temporary identities were used
test, removed when finalizing the suite. No permanent identity of the
username was automatically generated.

Generated files:

- `host.pfx`: certificate with private key, protected by the PFX password.
- `host.key.pem`: private key also encrypted, necessary only for generation/testing.
- `host.crt.pem`: public certificate.

The certificate is self-signed, RSA 3072 and valid for 30 days. The generator prints
`TLS_PIN`, a SHA-256 hash of the DER certificate. **Never share PFX, key
private or PFX password with guests.** Share only the pin and separately
the room password. Avoid saving private identities in synchronized folders;
the current project lives on OneDrive. `.gitignore` does not prevent cloud synchronization.

## 2. Start Protected Host

In the same terminal, keeping `DSR_TLS_PFX_PASSWORD` and `$dsrIdentity`:

```powershell
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
.\build-v03\dsr_net.exe host --port 27032 --max-players 4 --tls-pfx "$dsrIdentity\host.pfx"
```

Port 27032 avoids conflict with previous labs. The host shows
`SERVER_READY` and `TLS_PIN`. Check if the pin matches the one issued by the
generator. If the PFX file/password, certificate or libraries are invalid,
the host fails before opening the port.

## 3. Connect protected clients

In another PowerShell:

```powershell
cd 'C:\Users\luisg\OneDrive\Documents\dsr-network'
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
$dsrPin = 'COLE_AQUI_OS_64_DIGITOS_HEXADECIMAIS_DO_TLS_PIN'
.\build-v03\dsr_net.exe join --port 27032 --name Piero --tls-pin $dsrPin
```

Replace the pin text with the current value, without colons or spaces. Capital letters
and lowercase letters are accepted. Do not run with the placeholder. Open another client
in the same way, with another name.

The previous commands are still available:

```text
/move 10 0 5
/players
/who
/send 2 private message
/quit
```

The room password only enters a TLS record after verifying the certificate,
validity and exact match of the pin. There is no fallback for clear text
when TLS, libraries, certificate or pin fail.

The client does not use the CA chain, DNS/SAN name, or online revocation as
trust model: **uses the exact certificate previously authorized by the
pin**, plus temporal validity. This allows the laboratory by IP, including
when different from the local SAN of the certificate. By renewing the identity, the
pin changes and must be shared again via a trusted channel.

## 4. Rejection tests

- Change the room password only on a client terminal: it must receive `ERROR AUTH` within TLS.
- Use 64 zeros in the pin: should fail with incorrect fingerprint, without sending the password.
- Try connecting old client: it should not be admitted. The protocol/build is checked before entity creation.
- Try client without `--tls-pin` against TLS port: it should not enter; There is no automatic negotiation for plain text.
- Change the host's PFX password: it should not issue `SERVER_READY`.

Incorrect room password is different from incorrect PFX file password. THE
The client never needs the PFX file or password.

## LAN and security limits

The default remains `127.0.0.1`. Non-TLS mode remains available
**loopback only**, with explicit warning. The old `--allow-insecure-lan`
has been removed. Outside of loopback, the application requires `--lan` **and** TLS:

```text
host: --address IP_LOCAL_DO_HOST --lan --tls-pfx FILE
join: --address IP_LOCAL_DO_HOST --lan --tls-pin HASH
```

These examples are options, not complete commands. Also use the same port
and password on both sides. No firewall rules are created automatically.
**LAN and Internet were not tested in this delivery**, only processes in
loopback. Do not forward ports to the Internet at this stage.

TLS protects the channel, but does not resolve weak password, malicious host, machine
compromised, protection of keys on disk, authorization per user, abuse
of resources, NAT/CGNAT or game synchronization. The encrypted PFX does not replace
appropriate file permissions. There is no security audit or guarantee
zero risk. There are limits on queues, connections and time; this does not prove
resistance to denial of service. Shutdown currently closes TCP after
application messages; does not implement full exchange of `close_notify`.

Keep the OpenSSL runtime updated from its trusted source. Do not download DLLs
from unknown websites. The ABI accepts OpenSSL 3, but this does not evaluate
automatically check whether a specific version is up to date or bug-free.

## Protocol 3

The application content continues to be ASCII lines, with LF and TAB fields;
it did not become a binary gameplay protocol. TLS encapsulates these lines.

```text
HELLO<TAB>3<TAB>name<TAB>password<TAB>dsr-network-lab-0.3
WELCOME<TAB>3<TAB>room<TAB>id<TAB>capacity
```

`ERROR VERSION` indicates incompatible version; `ERROR BUILD`, laboratory build
incompatible. These are declared compatibility checks, not authentication
of binary. Entity, chat and room messages maintain their previous fields.

The host uses a non-blocking loop for plaintext or TLS, chosen
explicitly at startup, without detection/downgrading by traffic.
The keys and certificate are loaded into memory by the OpenSSL runtime,
without using a persistent Windows certificate/key store.

## Organization and testing

- `net/tls.hpp/.cpp`: channel facade, credentials and pin format validation.
- `net/openssl_transport.hpp/.cpp`: TLS adapter, explicit runtime loading,
reading PFX into memory, BIOs and non-blocking encrypted records.
- `tools/create_tls_identity.py`, `tools/localhost.cnf`: explicit identity generation.
- `tests/tls_integration.py`: interoperability and loopback rejection tests.

```powershell
cmake -S . -B build-v03 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v03 --parallel 2
ctest --test-dir build-v03 --verbose
```

There are 8 unitary cases, 14 network scenarios, 11 entity scenarios and 12 TLS scenarios.
TLS scenarios include four clients, native client, wrong password and build,
non-TLS traffic, incorrect pin with no application data sent, wrong PFX password,
DER hash checked, encrypted record tampered with, proxy capture without password/
movement in clear text and handshake timeout. The capture is local, made
about created connections exclusively by the test.

CMake logs TLS tests only if it finds the OpenSSL CLI; if not
found, issues a warning. Enter `-DDSR_OPENSSL_EXECUTABLE=PATH` to
enable them. In this case, passing the other three suites does not validate TLS.

References: [OpenSSL handshake](https://docs.openssl.org/3.0/man3/SSL_do_handshake/),
[TLS versions](https://docs.openssl.org/3.0/man3/SSL_CTX_set_min_proto_version/),
[PKCS12_parse](https://docs.openssl.org/3.0/man3/PKCS12_parse/).
