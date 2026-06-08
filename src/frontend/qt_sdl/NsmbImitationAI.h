/*
    Lightweight NSMB Mario vs Luigi imitation-policy runtime.
*/

#ifndef NSMBIMITATIONAI_H
#define NSMBIMITATIONAI_H

#include <cstdint>
#include <string>
#include <vector>

namespace NsmbImitationAI
{

struct LinearPolicyModel
{
    std::string Schema;
    std::string FeatureSchemaID;
    std::vector<std::string> FeatureNames;
    std::vector<std::string> Buttons;
    std::vector<double> Mean;
    std::vector<double> Scale;
    std::vector<double> Bias;
    std::vector<double> Weights;

    std::size_t FeatureCount() const;
    std::size_t ButtonCount() const;
    bool IsUsable() const;
};

struct Prediction
{
    std::uint16_t Held = 0;
    std::vector<double> Probabilities;
};

bool LoadLinearPolicyModel(
    const std::string& path,
    LinearPolicyModel& model,
    std::string& error);

Prediction PredictLinearPolicy(
    const LinearPolicyModel& model,
    const std::vector<double>& rawFeatures,
    double threshold);

std::uint16_t HeldFromPrediction(const std::vector<double>& probabilities, double threshold);

}

#endif
