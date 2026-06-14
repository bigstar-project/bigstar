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

struct RuntimeLinearLayer
{
    int In = 0;
    int Out = 0;
    std::vector<double> Weight;
    std::vector<double> Bias;

    bool IsUsable() const;
};

struct RuntimeNormLayer
{
    int Size = 0;
    double Eps = 1.0e-5;
    std::vector<double> Weight;
    std::vector<double> Bias;

    bool IsUsable() const;
};

struct RuntimeBatchNormLayer
{
    int Channels = 0;
    double Eps = 1.0e-5;
    std::vector<double> Weight;
    std::vector<double> Bias;
    std::vector<double> RunningMean;
    std::vector<double> RunningVar;

    bool IsUsable() const;
};

struct RuntimeConv2DLayer
{
    int In = 0;
    int Out = 0;
    int KernelH = 0;
    int KernelW = 0;
    int Padding = 0;
    std::vector<double> Weight;
    std::vector<double> Bias;

    bool IsUsable() const;
};

struct TorchCompactPolicyModel
{
    std::string Schema;
    std::string LabelSchema;
    std::vector<std::string> HeadNames;
    std::vector<CompactActionHead> Heads;
    std::vector<double> ScalarMean;
    std::vector<double> ScalarScale;
    std::vector<double> EntityMean;
    std::vector<double> EntityScale;

    int ScalarCount = 0;
    int TerrainHeight = 0;
    int TerrainWidth = 0;
    int TerrainChannels = 0;
    int OpponentTerrainChannels = 0;
    int EntityCount = 0;
    int EntityFeatures = 0;
    int TotalFeatures = 0;

    RuntimeLinearLayer ScalarLinear0;
    RuntimeNormLayer ScalarLayerNorm1;
    RuntimeLinearLayer ScalarLinear4;

    RuntimeConv2DLayer TerrainConv0;
    RuntimeBatchNormLayer TerrainBatchNorm1;
    RuntimeConv2DLayer TerrainConv3;
    RuntimeBatchNormLayer TerrainBatchNorm4;
    RuntimeConv2DLayer TerrainConv7;
    RuntimeBatchNormLayer TerrainBatchNorm8;
    RuntimeConv2DLayer TerrainConv10;
    RuntimeBatchNormLayer TerrainBatchNorm11;
    RuntimeLinearLayer TerrainLinear15;

    RuntimeLinearLayer EntityLinear0;
    RuntimeNormLayer EntityLayerNorm1;
    RuntimeLinearLayer EntityLinear3;

    RuntimeLinearLayer FusionLinear0;
    RuntimeNormLayer FusionLayerNorm1;
    RuntimeLinearLayer FusionLinear4;

    std::size_t FeatureCount() const;
    bool IsUsable() const;
};

bool LoadLinearPolicyModel(
    const std::string& path,
    LinearPolicyModel& model,
    std::string& error);

bool LoadCompactActionPolicyModel(
    const std::string& path,
    CompactActionPolicyModel& model,
    std::string& error);

bool LoadTorchCompactPolicyModel(
    const std::string& path,
    TorchCompactPolicyModel& model,
    std::string& error);

Prediction PredictLinearPolicy(
    const LinearPolicyModel& model,
    const std::vector<double>& rawFeatures,
    double threshold);

CompactActionPrediction PredictCompactActionPolicy(
    const CompactActionPolicyModel& model,
    const std::vector<double>& rawFeatures);

CompactActionPrediction PredictTorchCompactPolicy(
    const TorchCompactPolicyModel& model,
    const std::vector<double>& rawFeatures);

std::uint16_t HeldFromPrediction(const std::vector<double>& probabilities, double threshold);
std::uint16_t HeldFromCompactActions(
    const std::vector<CompactActionHead>& heads,
    const std::vector<int>& actions);

}

#endif
