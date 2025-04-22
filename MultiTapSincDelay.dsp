import("stdfaust.lib");

//----------------`(de.)multiTapSincDelay`-------------
//
// Variable delay line using multi-tap sinc interpolation.
//
// This function implements a continuously variable delay line by superposing (2K+2) auxiliary delayed signals
// whose positions and gains are determined by a sinc-based interpolation method. It extends the traditional
// crossfade delay technique to significantly reduce spectral coloration artifacts, which are problematic in
// applications like Wave Field Synthesis (WFS) and auralization.
//
// Operation:
//
//   - If tau1 and tau2 are very close (|tau2 - tau1| ≈ 0), a simple fixed fractional delay is applied
//   - Otherwise, a variable delay is synthesized by:
//
//      - Computing (2K+2) taps symmetrically distributed around tau1 and tau2
//      - Applying sinc-based weighting to each tap, based on its offset from the target interpolated delay tau
//      - Summing all the weighted taps to produce the output
//
// Features:
//
//   - Smooth delay variation without introducing Doppler pitch shifts
//   - Significant reduction of comb-filter coloration compared to classical crossfading
//   - Switching between fixed and variable delay modes to ensure stability
//
// #### Usage
// ```
//  _ : multiTapSincDelay(K, MaxDelay, tau1, tau2, alpha) : _
// 
// ```
//
// Where:
//
// * `K (integer)`: number of auxiliary tap pairs (a constant numerical expression). Total number of taps = 2*K + 2
// * `MaxDelay`: maximum allowable delay in samples (buffer size)
// * `tau1`: initial delay in samples (can be fractional)
// * `tau2`: target delay in samples (can be fractional)
// * `alpha`: interpolation factor between tau1 and tau2 (0 = tau1, 1 = tau2)
//
// #### Reference
//   - T. Carpentier, "Implémentation de ligne à retard avec délai continûment variable", 2024: <https://hal.science/hal-04535030>
//------------------------------------------------------------

multiTapSincDelay(K, MaxDelay, tau1, tau2, alpha, input) = output with {

    // Total number of taps used in the sum (2K + 2).
    numTaps = 2 * K + 2;

    // Normalized Sinc function: sinc(x) = sin(pi*x)/(pi*x)
    // Handles the case where x ≈ 0 to avoid division by zero and return 1.
    sinc(x) = ba.if(abs(x) < ma.EPSILON, 1, sin(ma.PI * x) / (ma.PI * x));

    // Computes the difference between two delays: delta = tau2 - tau1
    delta(t1, t2) = t2 - t1;

    // Computes the target interpolated delay: tau = (1-alpha)*tau1 + alpha*tau2
    // This is the center of the sinc function for the hk gains.
    tau(t1, t2, a) = (1 - a) * t1 + a * t2;

    // Computes the position (delay in samples) of the i-th tap (tk),
    // according to equation (17) of the article.
    tk(i, t1, t2) = ba.if(i <= K,
                        t1 - (K - i) * delta(t1, t2),
                        t2 + (i - K - 1) * delta(t1, t2));

    // Computes the gain (hk) of the i-th tap, according to equation (19) of the paper.
    // hk = sinc((tk - tau) / delta)
    hk(i, t1, t2, a) = sinc(offset) with {
        d      = delta(t1, t2);
        denom  = ba.if(abs(d) < ma.EPSILON, 1, d);
        offset = (tk(i, t1, t2) - tau(t1, t2, a)) / denom;
    };

    // Computes the sum of delayed signals weighted by their gains.
    // This is the core of the variable multi-tap effect.
    tapSum(sig, t1, t2, a) =
        // Iterates from i = 0 to numTaps-1
        sum(i, numTaps,
            // For each tap 'i':
            // 1. Delays the signal 'sig' by tk(i) samples (fractional)
            //    using an internal delay line (de.fdelay).
            // 2. Multiplies the delayed signal by the gain hk(i).
            sig : de.fdelay(MaxDelay, tk(i, t1, t2)) * hk(i, t1, t2, a)
        );

    // Simple function to apply a fixed delay (potentially fractional).
    // Used when tau1 and tau2 are very close.
    fixedDelay(x, t) = x : de.fdelay(MaxDelay, t);

    // Determines if delays tau1 and tau2 are close enough to be
    // considered fixed (to avoid unstable calculations when delta ≈ 0).
    // isFixed equals 1 if delta ≈ 0, otherwise 0.
    isFixed = abs(delta(tau1, tau2)) < ma.EPSILON;

    // Computes the output corresponding to the fixed delay (used if isFixed=1).
    fd = fixedDelay(input, tau1);            

    // Computes the output corresponding to the variable multi-tap delay (used if isFixed=0).
    mt = tapSum(input, tau1, tau2, alpha);    

    // Selects the final output based on isFixed.
    output = ba.if(isFixed, fd, mt); 
};

// ───────── Compilation constants ─────────
MaxDelay = 4096;             // maximum buffer size (samples)

// Number of pairs of "auxiliary" taps (K=0 -> 2 taps, K=1 -> 4 taps, etc.),
// constant because numTaps is used as the limit in 'sum'.
K = 2;

// ───────── User controls ─────────
// Starting delay (in samples). Can be fractional.
tau1 = hslider("tau1 [unit:samples]", 100.5, 0, MaxDelay - 1, 0.1);

// Ending delay (in samples). Can be fractional.
tau2 = hslider("tau2 [unit:samples]", 500.7, 0, MaxDelay - 1, 0.1);

// Interpolation factor between tau1 and tau2 (0 = tau1, 1 = tau2).
//alpha = hslider("alpha", 0.0, 0, 1, 0.001);

// Interpole alpha from 0 to 1 in 1000 samples.
alpha = ba.time/1000; // 0.0 to 1.0, linear between 0 and 1000 samples

// ───────── Test signal ─────────
dirac = 1 - 1';     // input signal (dirac) for testing

process = (dirac : multiTapSincDelay(K, MaxDelay, tau1, tau2, alpha)), dirac, alpha;
