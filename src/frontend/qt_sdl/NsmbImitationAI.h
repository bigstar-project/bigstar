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

struct CompactActionHead
{
    std::string Name;
    std::vector<std::string> Classes;
    std::vector<double> Bias;
    std::vector<double> Weights;

    std::size_t ClassCount() const;
};

struct CompactActionPolicyModel
{
    std::string Schema;
    std::string LabelSchema;
    std::vector<double> Mean;
    std::vector<double> Scale;
    std::vector<CompactActionHead> Heads;

    std::size_t FeatureCount() const;
    bool IsUsable() const;
};

struct CompactActionPrediction
{
    std::uint16_t Held = 0;
    std::vector<int> Actions;
    std::vector<double> Confidences;
};

bool LoadLinearPolicyModel(
    const std::string& path,
    LinearPolicyModel& model,
    std::string& error);

bool LoadCompactActionPolicyModel(
    const std::string& path,
    CompactActionPolicyModel& model,
    std::string& error);

Prediction PredictLinearPolicy(
    const LinearPolicyModel& model,
    const std::vector<double>& rawFeatures,
    double threshold);

CompactActionPrediction PredictCompactActionPolicy(
    const CompactActionPolicyModel& model,
    const std::vector<double>& rawFeatures);

std::uint16_t HeldFromPrediction(const std::vector<double>& probabilities, double threshold);
std::uint16_t HeldFromCompactActions(
    const std::vector<CompactActionHead>& heads,
    const std::vector<int>& actions);

}

#endif
