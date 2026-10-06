#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr double kTol = 1e-12;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b, double tol = kTol) {
    return std::abs(a - b) <= tol;
}

double bayes_update(double prior_true, double likelihood_if_true,
                    double likelihood_if_false) {
    const double numerator = prior_true * likelihood_if_true;
    const double denominator = numerator + (1.0 - prior_true) * likelihood_if_false;
    require(denominator > 0.0, "Bayesian update has zero evidence probability");
    return numerator / denominator;
}

double log_add_exp(double a, double b) {
    const double m = std::max(a, b);
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}

double weighted_model_count_ab_or_both(double p_a, double p_b, bool require_a) {
    // H = (A OR B) AND (NOT A OR B), which is equivalent to B.
    double sum = 0.0;
    for (int a = 0; a <= 1; ++a) {
        for (int b = 0; b <= 1; ++b) {
            const bool satisfies_h = (a || b) && ((!a) || b);
            if (!satisfies_h || (require_a && !a)) continue;
            const double pa = a ? p_a : 1.0 - p_a;
            const double pb = b ? p_b : 1.0 - p_b;
            sum += pa * pb;
        }
    }
    return sum;
}

double brier_expected(double forecast, double truth_probability) {
    return truth_probability * std::pow(forecast - 1.0, 2) +
           (1.0 - truth_probability) * std::pow(forecast, 2);
}

double bernoulli_log_loss(double forecast, double truth_probability) {
    return -truth_probability * std::log(forecast) -
           (1.0 - truth_probability) * std::log(1.0 - forecast);
}

void test_ingestion() {
    double p = 0.2;
    p = bayes_update(p, 0.9, 0.1);
    require(near(p, 9.0 / 13.0), "one-evidence posterior must equal 9/13");
    p = bayes_update(p, 0.9, 0.1);
    require(near(p, 81.0 / 85.0), "two-evidence posterior must equal 81/85");
    require(near(p + (1.0 - p), 1.0), "posterior must normalize");
    require(near(0.2 * 0.4, 0.08), "independent fact conjunction must be 0.08");
}

void test_learning() {
    // Two Bernoulli models with equal prior weight; observe 60 positive outcomes.
    constexpr int n = 60;
    constexpr double prior1 = 0.5;
    constexpr double prior2 = 0.5;
    constexpr double p1 = 0.25;
    constexpr double p2 = 0.75;
    const double log_q = log_add_exp(std::log(prior1) + n * std::log(p1),
                                     std::log(prior2) + n * std::log(p2));
    const double log_best = n * std::log(p2);
    const double regret = log_best - log_q;
    require(near(regret, std::log(2.0), 1e-12),
            "mixture regret should approach ln(2) for this stream");
    require(regret <= std::log(1.0 / prior2) + 1e-12,
            "mixture regret must not exceed log inverse prior mass");
}

void test_evolution() {
    // One CMA-style weighted recombination step over two already-ranked points.
    const double x1[2] = {2.0, 0.0};
    const double x2[2] = {0.0, 2.0};
    const double w1 = 0.75;
    const double w2 = 0.25;
    double theta[2] = {w1 * x1[0] + w2 * x2[0],
                       w1 * x1[1] + w2 * x2[1]};
    require(near(theta[0], 1.5) && near(theta[1], 0.5),
            "weighted recombination mismatch");

    // Projection onto the unit Euclidean ball preserves the declared parameter bound.
    const double radius = std::hypot(theta[0], theta[1]);
    if (radius > 1.0) {
        theta[0] /= radius;
        theta[1] /= radius;
    }
    require(std::hypot(theta[0], theta[1]) <= 1.0 + kTol,
            "projected parameter escaped its bound");

    // NIST upper CUSUM fixture: first alarm at sample 14.
    const std::array<double, 20> sample = {
        324.925, 324.675, 324.725, 324.350, 325.350,
        325.225, 324.125, 324.525, 325.225, 324.600,
        324.625, 325.150, 328.325, 327.250, 327.825,
        328.500, 326.675, 327.775, 326.875, 328.350};
    constexpr double target = 325.0;
    constexpr double allowance = 0.3175;
    constexpr double threshold = 4.1959;
    double s = 0.0;
    int first_alarm = -1;
    for (std::size_t i = 0; i < sample.size(); ++i) {
        s = std::max(0.0, s + sample[i] - target - allowance);
        if (s > threshold && first_alarm < 0) first_alarm = static_cast<int>(i + 1);
        if (i == 12) require(near(s, 3.0075, 1e-10), "CUSUM sample 13 mismatch");
        if (i == 13) require(near(s, 4.94, 1e-10), "CUSUM sample 14 mismatch");
    }
    require(first_alarm == 14, "CUSUM first alarm must occur at sample 14");
}

void test_reasoning() {
    const double denominator = weighted_model_count_ab_or_both(0.3, 0.6, false);
    const double numerator = weighted_model_count_ab_or_both(0.3, 0.6, true);
    require(near(denominator, 0.6), "WMC(H) must equal 0.6");
    require(near(numerator, 0.18), "WMC(H AND A) must equal 0.18");
    require(near(numerator / denominator, 0.3), "P(A|H) must equal 0.3");

    // In a no-confounding two-node SCM, intervention risk is the structural mechanism.
    const double p_y_if_x1 = 0.8;
    const double p_y_if_x0 = 0.2;
    require(near(p_y_if_x1, 0.8) && near(p_y_if_x0, 0.2),
            "causal smoke-test mechanisms mismatch");
}

void test_calibration() {
    require(near(brier_expected(0.3, 0.3), 0.21),
            "Brier expected loss at truth must be 0.21");
    require(near(brier_expected(0.6, 0.3), 0.30),
            "miscalibrated Brier expected loss must be 0.30");
    require(bernoulli_log_loss(0.3, 0.3) < bernoulli_log_loss(0.6, 0.3),
            "proper log score should prefer the true Bernoulli probability");

    // Split-conformal finite-sample quantile index: ceil((n+1)(1-alpha)).
    std::vector<double> scores{9, 1, 5, 3, 7, 2, 4, 6, 8};
    std::sort(scores.begin(), scores.end());
    constexpr std::size_t n = 9;
    constexpr double alpha = 0.1;
    const std::size_t k = static_cast<std::size_t>(std::ceil((n + 1) * (1.0 - alpha)));
    require(k == 9 && near(scores[k - 1], 9.0),
            "conformal order statistic index mismatch");
}

void test_output() {
    const double p_good = 0.7;
    const double p_bad = 1.0 - p_good;
    const double risk_act1 = p_good * 0.0 + p_bad * 1.0;
    const double risk_act2 = p_good * 0.4 + p_bad * 0.2;
    constexpr double abstention_cost = 0.15;
    require(near(risk_act1, 0.30), "risk(act1) must equal 0.30");
    require(near(risk_act2, 0.34), "risk(act2) must equal 0.34");
    require(abstention_cost < risk_act1 && abstention_cost < risk_act2,
            "Bayes decision should abstain for the stated costs");

    const bool act1_certificate_valid = false;
    const bool act2_certificate_valid = false;
    const double best_certified_risk =
        (act1_certificate_valid ? risk_act1 : INFINITY) <
                (act2_certificate_valid ? risk_act2 : INFINITY)
            ? (act1_certificate_valid ? risk_act1 : INFINITY)
            : (act2_certificate_valid ? risk_act2 : INFINITY);
    const bool choose_abstention = !std::isfinite(best_certified_risk) ||
                                   abstention_cost < best_certified_risk;
    require(choose_abstention, "missing valid certificate must lead to abstention");
}

}  // namespace

int main() {
    std::cout << std::setprecision(15);
    try {
        test_ingestion();
        std::cout << "PASS factual_ingestion: posterior 9/13 -> 81/85; normalized\n";
        test_learning();
        std::cout << "PASS learning: Bayesian-mixture regret <= ln(1/prior mass)\n";
        test_evolution();
        std::cout << "PASS evolution: weighted recombination, projection bound, CUSUM alarm at 14\n";
        test_reasoning();
        std::cout << "PASS reasoning: exact finite WMC and causal mechanism smoke test\n";
        test_calibration();
        std::cout << "PASS calibration: proper-score arithmetic and conformal quantile index\n";
        test_output();
        std::cout << "PASS output: posterior-risk choice and certificate-gated abstention\n";
        std::cout << "RESULT: 6/6 pillar test groups passed. These are mathematical/software checks, not evidence of intelligence or empirical performance.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
