# Test Notes

## Build and run

Requirements: GCC 13+ or another conforming C++20 compiler. The project also provides a CMake/CTest path.

Direct build:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  TESTS/cpp/pillar_tests.cpp -o /tmp/xai-pillar-tests
/tmp/xai-pillar-tests
```

CMake and CTest:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The harness uses fixed, deterministic inputs. The recorded output is in [`validation.log`](validation.log), and the pillar-by-pillar evidence boundaries are in [`MATHEMATICAL_VERIFICATION.md`](MATHEMATICAL_VERIFICATION.md).

## Coverage

The six groups check selected examples: Bayesian posterior updates and normalization; a finite Bayesian model-mixture log-loss inequality case; weighted recombination, projection into a declared bound, and a CUSUM sample; exact finite weighted model counting plus a causal-mechanism smoke fixture; proper-score arithmetic and a conformal order-statistic index; and risk minimization with a certificate-gated abstention path.

## Limits

A passing test means only that the tested code produced the expected results for those fixtures. It is not a proof of the full architecture, suitability of the assumptions, correct inference on arbitrary inputs, real-data calibration, learning quality, performance, novelty, or intelligence. The causal fixture checks supplied mechanism values; it does not estimate a causal effect. The adaptation fixture does not implement a complete covariance-matrix adaptation strategy. The harness does not train on a corpus, run a performance benchmark, fine-tune, or deploy a model.

No training or evaluation on a 1 GB dataset has been conducted. Dataset sufficiency must be established for a named task and evaluation population, not inferred from byte size.

## Reproducibility record

The previous recorded run used GCC 13.3, C++20, optimization, and `-Wall -Wextra -Werror`. Rebuild locally to verify the current source on your toolchain; the existing log is a historical result, not a claim about every environment.