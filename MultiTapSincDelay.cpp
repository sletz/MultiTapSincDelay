#include <cmath>
#include <cstddef>  // Pour size_t
#include <iostream>
#include <limits>     // Pour numeric_limits
#include <stdexcept>  // Pour std::invalid_argument
#include <vector>

// Définir M_PI si non disponible (nécessaire sous Windows avec certains
// compilateurs)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class MultiTapSincDelay {
   public:
    /**
     * Constructeur.
     * @param max_delay_samples Taille maximale du buffer de délai en
     * échantillons.
     * @param initial_K Valeur initiale du paramètre K (nombre de paires de taps
     * auxiliaires).
     */
    MultiTapSincDelay(size_t max_delay_samples, int initial_K = 1, double sample_rate = 44100.0)
        : m_max_delay_samples(max_delay_samples),
          m_buffer(max_delay_samples, 0.0),  // Initialise le buffer avec des zéros
          m_writeIndex(0)
    {
        if (max_delay_samples == 0) {
            throw std::invalid_argument("Max delay samples must be greater than 0.");
        }
        setK(initial_K);  // Utilise le setter pour valider K
                          // Initialiser les délais à des valeurs par défaut sûres
        setTau1(1.0);
        setTau2(2.0);
        setAlpha(0.0);
    }

    /**
     * Définit le paramètre K (nombre de paires de taps auxiliaires).
     * K=0 signifie 2 taps au total, K=1 signifie 4 taps, etc.
     */
    void setK(int newK)
    {
        if (newK < 0) {
            throw std::invalid_argument("K cannot be negative.");
        }
        m_K = newK;
    }

    /**
     * Définit le premier délai (tau1) en échantillons.
     */
    void setTau1(double newTau1)
    {
        // Permet un délai de 0 jusqu'à la taille max moins une marge pour
        // l'interpolation
        if (newTau1 < 0.0 || newTau1 >= static_cast<double>(m_max_delay_samples) - 1.0) {
            throw std::out_of_range("Tau1 must be between 0.0 and max_delay_samples - 1.0");
        }
        m_tau1 = newTau1;
    }

    /**
     * Définit le second délai (tau2) en échantillons.
     */
    void setTau2(double newTau2)
    {
        if (newTau2 < 0.0 || newTau2 >= static_cast<double>(m_max_delay_samples) - 1.0) {
            throw std::out_of_range("Tau2 must be between 0.0 and max_delay_samples - 1.0");
        }
        m_tau2 = newTau2;
    }

    /**
     * Définit le facteur d'interpolation alpha (0=tau1, 1=tau2).
     */
    void setAlpha(double newAlpha)
    {
        if (newAlpha < 0.0 || newAlpha > 1.0) {
            throw std::invalid_argument("Alpha must be between 0.0 and 1.0.");
        }
        m_alpha = newAlpha;
    }

    /**
     * Traite un échantillon audio.
     * @param inputSample L'échantillon d'entrée.
     * @return L'échantillon de sortie traité.
     */
    double process(double inputSample)
    {
        // 1. Écrire l'échantillon d'entrée dans le buffer
        m_buffer[m_writeIndex] = inputSample;

        double output = 0.0;
        double delta  = m_tau2 - m_tau1;

        // Utiliser une petite tolérance pour comparer les flottants
        const double epsilon = std::numeric_limits<double>::epsilon() * 100;

        // 2. Cas spécial : délai fixe si tau1 est (presque) égal à tau2
        if (std::abs(delta) < epsilon) {
            double targetReadIndex = static_cast<double>(m_writeIndex) - m_tau1;
            output                 = readInterpolated(targetReadIndex);
        }
        // 3. Cas général : délai variable avec interpolation sinc multi-tap
        else {
            double tau = (1.0 - m_alpha) * m_tau1 + m_alpha * m_tau2;
            double delta_safe =
                (std::abs(delta) < epsilon) ? 1.0 : delta;  // Éviter division par zéro
                                                            // (théoriquement couvert par le if)
            double outputSum = 0.0;
            int    num_taps  = 2 * m_K + 2;

            for (int k = 0; k < num_taps; ++k) {
                // Calculer la position du tap tk (Equation 17)
                double tk = 0.0;
                if (k <= m_K) {
                    tk = m_tau1 - (static_cast<double>(m_K) - static_cast<double>(k)) * delta;
                } else {
                    tk = m_tau2 + (static_cast<double>(k) - static_cast<double>(m_K) - 1.0) * delta;
                }

                // Calculer le gain du tap hk (Equation 19)
                double arg_k = (tk - tau) / delta_safe;
                double hk    = sinc(arg_k);

                // Calculer l'index de lecture cible
                double targetReadIndex = static_cast<double>(m_writeIndex) - tk;

                // Lire la valeur interpolée du buffer
                double readSample = readInterpolated(targetReadIndex);

                // Ajouter la contribution du tap à la somme
                outputSum += readSample * hk;
            }
            output = outputSum;
        }

        // 4. Incrémenter l'index d'écriture (avec wrap-around)
        m_writeIndex = (m_writeIndex + 1) % m_max_delay_samples;

        return output;
    }

   private:
    /**
     * Calcule la fonction sinus cardinal normalisée sinc(x) = sin(pi*x)/(pi*x).
     */
    double sinc(double x)
    {
        if (std::abs(x) < std::numeric_limits<double>::epsilon()) {
            return 1.0;
        }
        double pi_x = M_PI * x;
        return std::sin(pi_x) / pi_x;
    }

    /**
     * Lit une valeur dans le buffer de délai avec interpolation linéaire.
     * Gère le wrap-around des indices.
     * @param readIndex L'index de lecture (potentiellement fractionnaire) relatif
     * à l'index d'écriture courant.
     */
    double readInterpolated(double readIndex)
    {
        // Assurer que l'index est positif avant le modulo pour éviter les problèmes
        // avec fmod sur nombres négatifs
        double wrappedReadIndex = readIndex;
        while (wrappedReadIndex < 0.0) {
            wrappedReadIndex += static_cast<double>(m_max_delay_samples);
        }
        // Appliquer le modulo pour le wrap-around final
        wrappedReadIndex = std::fmod(wrappedReadIndex, static_cast<double>(m_max_delay_samples));

        // Interpolation linéaire
        size_t index0 = static_cast<size_t>(std::floor(wrappedReadIndex));
        size_t index1 = (index0 + 1) % m_max_delay_samples;  // Gère le wrap-around pour index1
        double frac   = wrappedReadIndex - std::floor(wrappedReadIndex);

        double sample0 = m_buffer[index0];
        double sample1 = m_buffer[index1];

        return sample0 * (1.0 - frac) + sample1 * frac;
    }

    // Membres de la classe
    size_t              m_max_delay_samples;
    std::vector<double> m_buffer;
    size_t              m_writeIndex;
    int                 m_K;
    double              m_tau1;
    double              m_tau2;
    double              m_alpha;
    double              m_sampleRate;
};

// --- Exemple d'utilisation ---
int main()
{
    const size_t bufferSize = 4096;  // Taille du buffer de délai
    const int    K          = 2;     // Nombre de paires auxiliaires (total 6 taps)
    const double sampleRate = 44100.0;

    MultiTapSincDelay delay(bufferSize, K, sampleRate);

    // Définir les délais (en échantillons)
    delay.setTau1(100.5);  // Délai initial
    delay.setTau2(500.7);  // Délai final

    // Simuler une transition de alpha sur quelques échantillons
    int                 numSamplesToProcess = 1000;
    std::vector<double> inputSignal(numSamplesToProcess);
    std::vector<double> outputSignal(numSamplesToProcess);

    // Créer un signal d'entrée simple (ex: impulsion)
    inputSignal[0] = 1.0;

    std::cout << "Processing " << numSamplesToProcess << " samples..." << std::endl;

    for (int i = 0; i < numSamplesToProcess; ++i) {
        // Faire varier alpha linéairement de 0 à 1 sur la durée
        double currentAlpha = static_cast<double>(i) / static_cast<double>(numSamplesToProcess - 1);
        delay.setAlpha(currentAlpha);

        outputSignal[i] = delay.process(inputSignal[i]);

        // Afficher les valeurs
        std::cout << "Sample " << i << ": Input=" << inputSignal[i]
                  << ", Output=" << outputSignal[i] << ", Alpha=" << currentAlpha << std::endl;
    }

    std::cout << "Processing finished." << std::endl;
    return 0;
}
