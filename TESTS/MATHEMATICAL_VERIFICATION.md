# Mathematical Verification Record

This record separates established mathematical results from the finite C++ checks performed in this repository. A test fixture checks only the stated computation under its chosen inputs; it does not verify that assumptions fit real data.

## Factual ingestion

**Mathematical basis:** Bayesian conditioning over finite possible worlds; factorized and open-world probabilistic database semantics are discussed in the research paper sources.

**Executed check:** for prior \(P(X)=0.2\) and two conditionally independent reports with likelihoods 0.9 and 0.1, successive posteriors are \(9/13\) and \(81/85\); the posterior sums to one. A separate independent conjunction computes \(0.2\times0.4=0.08\).

**Boundary:** arithmetic only. It does not validate the prior, likelihoods, extractor, or independence of reports.

## Learning

**Mathematical basis:** sequential Bayesian model averaging under logarithmic loss has a pointwise cumulative regret bound relative to each model in the declared library.

**Executed check:** an equal-prior mixture of Bernoulli models with probabilities 0.25 and 0.75 is evaluated on 60 positive observations using log-sum-exp. The regret relative to the better model is \(\ln 2\), no greater than \(\ln(1/0.5)\).

**Boundary:** one finite sequence and two models; no evidence about natural-data generalization or library quality.

## Evolution and adaptation

**Mathematical basis:** convex weighted recombination, projection onto a bounded set, and one-sided cumulative-sum change detection.

**Executed check:** one weighted recombination is computed, the result is projected into the unit ball, and a deterministic NIST CUSUM sample signals at sample 14 with the stated parameters.

**Boundary:** the code does not implement full CMA-ES or establish optimization quality, detector guarantees under deployment distributions, or closed-loop stability.

## Reasoning

**Mathematical basis:** exact weighted model counting over a finite propositional model and a supplied structural causal mechanism.

**Executed check:** for \(H=(A\lor B)\land(\neg A\lor B)\), with independent weights \(P(A)=0.3\), \(P(B)=0.6\), exact enumeration gives \(\operatorname{WMC}(H)=0.6\), \(\operatorname{WMC}(H\land A)=0.18\), and \(P(A\mid H)=0.3\). A separate mechanism smoke fixture checks two supplied conditional values.

**Boundary:** truth-table enumeration on two variables and a supplied causal mechanism; no scalable solver or real causal identification is implemented.

## Calibration

**Mathematical basis:** strictly proper Brier and logarithmic scores; split-conformal order-statistic construction under exchangeability.

**Executed check:** expected Brier score is 0.21 for forecast 0.3 under true Bernoulli probability 0.3, versus 0.30 for forecast 0.6; log score prefers the true probability. For nine sorted conformity scores and nominal 90% coverage, \(k=\lceil(9+1)0.9\rceil=9\).

**Boundary:** expected-score arithmetic and index calculation, not empirical calibration or a coverage study.

## Output

**Mathematical basis:** expected-loss minimization with explicit resource cost and an abstention action, restricted to actions whose required certificates pass.

**Executed check:** risks 0.30 and 0.34 are compared with abstention cost 0.15; the policy abstains. When no action certificate passes, the policy also abstains.

**Boundary:** selected numbers and boolean certificate fixtures only; no real loss model or certificate verifier is implemented.

## Reproducibility and overall status

The harness is in `cpp/pillar_tests.cpp`; `README.md` gives the GCC/C++20 build command; `validation.log` records the run. The overall claim is limited to passing these deterministic checks. The checks do not verify an end-to-end production architecture, train or evaluate on any real corpus, establish performance, establish novelty, or prove intelligence.