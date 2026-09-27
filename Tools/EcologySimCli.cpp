#include "ecology/PredatorPreyModel.h"
#include "core/MathSafety.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string output = argc > 1 ? argv[1] : "predator_prey_trace.csv";
    std::ofstream csv(output, std::ios::trunc);
    if (!csv)
    {
        std::cerr << "Could not open " << output << '\n';
        return 2;
    }

    longland::PredatorPreyModel model;
    longland::PredatorPreyModel counterfactual;
    model.reset();
    counterfactual.reset();
    longland::PredatorPreyControls controls;
    auto noAttackControls = controls;
    noAttackControls.attackEnabled = false;
    csv << "time_s,prey,predator,prey_health,predator_health,raw_food,filtered_food,"
           "prey_birth,prey_death,predator_birth,predator_death,attack,prey_gain,predator_gain,faults,"
           "prey_no_attack,predator_no_attack\n";

    for (int tick = 0; tick < 15000; ++tick)
    {
        const auto activity = 0.01 + 0.99 * longland::smoothStep(model.telemetry().preyPopulation);
        const auto counterfactualActivity = 0.01 + 0.99 * longland::smoothStep(counterfactual.telemetry().preyPopulation);
        model.step(activity, controls, 0.02);
        counterfactual.step(counterfactualActivity, noAttackControls, 0.02);
        const auto& s = model.telemetry();
        const auto& c = counterfactual.telemetry();
        csv << s.timeSeconds << ',' << s.preyPopulation << ',' << s.predatorPopulation << ','
            << s.preyHealth << ',' << s.predatorHealth << ',' << s.rawPredatorFood << ','
            << s.filteredPredatorFood << ',' << s.preyBirthRate << ',' << s.preyDeathRate << ','
            << s.predatorBirthRate << ',' << s.predatorDeathRate << ',' << s.attackInfluence << ','
            << s.preyGain << ',' << s.predatorGain << ',' << s.numericalFaults << ','
            << c.preyPopulation << ',' << c.predatorPopulation << '\n';
    }
    std::cout << "Wrote deterministic Phase 2 telemetry to " << output << '\n';
    return 0;
}
