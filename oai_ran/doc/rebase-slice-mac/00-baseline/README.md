# Baseline capture (N2-handover)

- **Repository commit**: 5e5d6783c934cee217ad1c5ebd03eb46e31b1ea0
- **Build command**: `./build_oai -w USRP --gNB --nrUE`
- **Result**: :x: CMake aborted because `asn1c` is missing. Package installation via `./build_oai -I` failed under the execution environment (APT proxy returns HTTP 403).
- **Next steps**: resolve toolchain availability (provide `asn1c` binary or run dependency install on an allowed network) before capturing runtime scheduler logs.

Artifacts:
- `n2-build.log` – terminal transcript of the failed build attempt.
- `n2-deps.log` – dependency installation attempt showing HTTP 403 errors from the proxy.
- `toolchain.txt` – compiler and dependency version survey (commands failing due to missing binaries are recorded verbatim).


Pending tests once dependencies are available:
- `cmake_targets/autotests/run_tests.bash -t regression`
- `nr-softmodem -O <config>` (loopback/RFsim)
