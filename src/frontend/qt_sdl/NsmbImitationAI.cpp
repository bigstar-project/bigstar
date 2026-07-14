#include "NsmbImitationAI.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace NsmbImitationAI
{

namespace
{

class JsonCursor
{
public:
    explicit JsonCursor(const std::string& text) : Text(text) {}

    bool FindKey(const std::string& key)
    {
        const std::string needle = "\"" + key + "\"";
        Pos = Text.find(needle);
        if (Pos == std::string::npos)
            return false;
        Pos += needle.size();
        SkipWhitespace();
        if (!Consume(':'))
            return false;
        SkipWhitespace();
        return true;
    }

    bool ParseString(std::string& out)
    {
        SkipWhitespace();
        if (!Consume('"'))
            return false;
        out.clear();
        while (Pos < Text.size())
        {
            const char ch = Text[Pos++];
            if (ch == '"')
                return true;
            if (ch == '\\')
            {
                if (Pos >= Text.size())
                    return false;
                const char escaped = Text[Pos++];
                switch (escaped)
                {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escaped);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    return false;
                }
            }
            else
            {
                out.push_back(ch);
            }
        }
        return false;
    }

    bool ParseStringArray(std::vector<std::string>& out)
    {
        SkipWhitespace();
        if (!Consume('['))
            return false;
        out.clear();
        SkipWhitespace();
        if (Consume(']'))
            return true;
        for (;;)
        {
            std::string value;
            if (!ParseString(value))
                return false;
            out.push_back(value);
            SkipWhitespace();
            if (Consume(']'))
                return true;
            if (!Consume(','))
                return false;
        }
    }

    bool ParseNumberArray(std::vector<double>& out)
    {
        SkipWhitespace();
        if (!Consume('['))
            return false;
        out.clear();
        SkipWhitespace();
        if (Consume(']'))
            return true;
        for (;;)
        {
            double value = 0.0;
            if (!ParseNumber(value))
                return false;
            out.push_back(value);
            SkipWhitespace();
            if (Consume(']'))
                return true;
            if (!Consume(','))
                return false;
        }
    }

    bool ParseSingleNumber(double& out)
    {
        return ParseNumber(out);
    }

    bool ParseNumberMatrix(std::vector<double>& out, std::size_t& rows, std::size_t& cols)
    {
        SkipWhitespace();
        if (!Consume('['))
            return false;
        out.clear();
        rows = 0;
        cols = 0;
        SkipWhitespace();
        if (Consume(']'))
            return true;
        for (;;)
        {
            std::vector<double> row;
            if (!ParseNumberArray(row))
                return false;
            if (rows == 0)
                cols = row.size();
            else if (row.size() != cols)
                return false;
            out.insert(out.end(), row.begin(), row.end());
            rows++;
            SkipWhitespace();
            if (Consume(']'))
                return true;
            if (!Consume(','))
                return false;
        }
    }

private:
    bool ParseNumber(double& out)
    {
        SkipWhitespace();
        if (Pos >= Text.size())
            return false;
        const char* start = Text.c_str() + Pos;
        char* end = nullptr;
        errno = 0;
        out = std::strtod(start, &end);
        if (end == start || errno == ERANGE)
            return false;
        Pos += static_cast<std::size_t>(end - start);
        return true;
    }

    bool Consume(char expected)
    {
        SkipWhitespace();
        if (Pos >= Text.size() || Text[Pos] != expected)
            return false;
        Pos++;
        return true;
    }

    void SkipWhitespace()
    {
        while (Pos < Text.size())
        {
            const char ch = Text[Pos];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                break;
            Pos++;
        }
    }

    const std::string& Text;
    std::size_t Pos = 0;
};

double Sigmoid(double x)
{
    x = std::clamp(x, -40.0, 40.0);
    return 1.0 / (1.0 + std::exp(-x));
}

std::vector<double> Softmax(const std::vector<double>& logits)
{
    std::vector<double> probs(logits.size(), 0.0);
    if (logits.empty())
        return probs;
    const double maxLogit = *std::max_element(logits.begin(), logits.end());
    double sum = 0.0;
    for (std::size_t i = 0; i < logits.size(); i++)
    {
        const double value = std::exp(std::clamp(logits[i] - maxLogit, -40.0, 40.0));
        probs[i] = value;
        sum += value;
    }
    if (sum <= 0.0)
        return probs;
    for (double& value : probs)
        value /= sum;
    return probs;
}

bool ReadFile(const std::string& path, std::string& text, std::string& error)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        error = "failed to open model: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

bool ParseStringField(const std::string& text, const std::string& key, std::string& value, std::string& error)
{
    JsonCursor cursor(text);
    if (!cursor.FindKey(key) || !cursor.ParseString(value))
    {
        error = "model is missing or has invalid string field: " + key;
        return false;
    }
    return true;
}

bool ParseStringArrayField(
    const std::string& text,
    const std::string& key,
    std::vector<std::string>& value,
    std::string& error)
{
    JsonCursor cursor(text);
    if (!cursor.FindKey(key) || !cursor.ParseStringArray(value))
    {
        error = "model is missing or has invalid string array field: " + key;
        return false;
    }
    return true;
}

bool ParseNumberArrayField(
    const std::string& text,
    const std::string& key,
    std::vector<double>& value,
    std::string& error)
{
    JsonCursor cursor(text);
    if (!cursor.FindKey(key) || !cursor.ParseNumberArray(value))
    {
        error = "model is missing or has invalid number array field: " + key;
        return false;
    }
    return true;
}

bool ParseNumberField(
    const std::string& text,
    const std::string& key,
    double& value,
    std::string& error)
{
    JsonCursor cursor(text);
    if (!cursor.FindKey(key) || !cursor.ParseSingleNumber(value))
    {
        error = "model is missing or has invalid number field: " + key;
        return false;
    }
    return true;
}

bool ParseIntField(
    const std::string& text,
    const std::string& key,
    int& value,
    std::string& error)
{
    double raw = 0.0;
    if (!ParseNumberField(text, key, raw, error))
        return false;
    value = static_cast<int>(std::llround(raw));
    return true;
}

bool ParseNumberMatrixField(
    const std::string& text,
    const std::string& key,
    std::vector<double>& value,
    std::size_t& rows,
    std::size_t& cols,
    std::string& error)
{
    JsonCursor cursor(text);
    if (!cursor.FindKey(key) || !cursor.ParseNumberMatrix(value, rows, cols))
    {
        error = "model is missing or has invalid number matrix field: " + key;
        return false;
    }
    return true;
}

}

std::size_t LinearPolicyModel::FeatureCount() const
{
    return FeatureNames.size();
}

std::size_t LinearPolicyModel::ButtonCount() const
{
    return Buttons.size();
}

bool LinearPolicyModel::IsUsable() const
{
    const std::size_t features = FeatureCount();
    const std::size_t buttons = ButtonCount();
    return features > 0 &&
        buttons > 0 &&
        Mean.size() == features &&
        Scale.size() == features &&
        Bias.size() == buttons &&
        Weights.size() == features * buttons;
}

std::size_t CompactActionHead::ClassCount() const
{
    return Classes.size();
}

std::size_t CompactActionPolicyModel::FeatureCount() const
{
    return Mean.size();
}

bool CompactActionPolicyModel::IsUsable() const
{
    const std::size_t features = FeatureCount();
    if (features == 0 || Scale.size() != features || Heads.empty())
        return false;
    for (const CompactActionHead& head : Heads)
    {
        const std::size_t classes = head.ClassCount();
        if (head.Name.empty() || classes == 0 || head.Bias.size() != classes || head.Weights.size() != features * classes)
            return false;
    }
    return true;
}

bool RuntimeLinearLayer::IsUsable() const
{
    return In > 0 &&
        Out > 0 &&
        Weight.size() == static_cast<std::size_t>(In * Out) &&
        Bias.size() == static_cast<std::size_t>(Out);
}

bool RuntimeNormLayer::IsUsable() const
{
    return Size > 0 &&
        Weight.size() == static_cast<std::size_t>(Size) &&
        Bias.size() == static_cast<std::size_t>(Size);
}

bool RuntimeBatchNormLayer::IsUsable() const
{
    return Channels > 0 &&
        Weight.size() == static_cast<std::size_t>(Channels) &&
        Bias.size() == static_cast<std::size_t>(Channels) &&
        RunningMean.size() == static_cast<std::size_t>(Channels) &&
        RunningVar.size() == static_cast<std::size_t>(Channels);
}

bool RuntimeConv2DLayer::IsUsable() const
{
    return In > 0 &&
        Out > 0 &&
        KernelH > 0 &&
        KernelW > 0 &&
        Padding >= 0 &&
        Weight.size() == static_cast<std::size_t>(Out * In * KernelH * KernelW) &&
        Bias.size() == static_cast<std::size_t>(Out);
}

std::size_t TorchCompactPolicyModel::FeatureCount() const
{
    return static_cast<std::size_t>(std::max(0, TotalFeatures));
}

bool TorchCompactPolicyModel::IsUsable() const
{
    if (Schema != "nsmb_mvl_torch_compact_policy_runtime_v1" ||
        ScalarCount <= 0 ||
        TerrainHeight <= 0 ||
        TerrainWidth <= 0 ||
        TerrainChannels <= 0 ||
        OpponentTerrainChannels <= 0 ||
        EntityCount <= 0 ||
        EntityFeatures <= 0 ||
        TotalFeatures <= 0 ||
        ScalarMean.size() != static_cast<std::size_t>(ScalarCount) ||
        ScalarScale.size() != static_cast<std::size_t>(ScalarCount) ||
        EntityMean.size() != static_cast<std::size_t>(EntityFeatures) ||
        EntityScale.size() != static_cast<std::size_t>(EntityFeatures) ||
        Heads.empty())
    {
        return false;
    }
    const int expectedTotal =
        ScalarCount +
        TerrainHeight * TerrainWidth * TerrainChannels +
        TerrainHeight * TerrainWidth * OpponentTerrainChannels +
        EntityCount * EntityFeatures;
    if (TotalFeatures != expectedTotal)
        return false;
    if (!ScalarLinear0.IsUsable() ||
        !ScalarLayerNorm1.IsUsable() ||
        !ScalarLinear4.IsUsable() ||
        !TerrainConv0.IsUsable() ||
        !TerrainBatchNorm1.IsUsable() ||
        !TerrainConv3.IsUsable() ||
        !TerrainBatchNorm4.IsUsable() ||
        !TerrainConv7.IsUsable() ||
        !TerrainBatchNorm8.IsUsable() ||
        !TerrainConv10.IsUsable() ||
        !TerrainBatchNorm11.IsUsable() ||
        !TerrainLinear15.IsUsable() ||
        !EntityLinear0.IsUsable() ||
        !EntityLayerNorm1.IsUsable() ||
        !EntityLinear3.IsUsable() ||
        !FusionLinear0.IsUsable() ||
        !FusionLayerNorm1.IsUsable() ||
        !FusionLinear4.IsUsable())
    {
        return false;
    }
    for (const CompactActionHead& head : Heads)
    {
        const std::size_t classes = head.ClassCount();
        if (head.Name.empty() ||
            classes == 0 ||
            head.Bias.size() != classes ||
            head.Weights.size() != static_cast<std::size_t>(FusionLinear4.Out) * classes)
        {
            return false;
        }
    }
    return true;
}

bool ParseRuntimeLinearLayer(
    const std::string& text,
    const std::string& prefix,
    RuntimeLinearLayer& layer,
    std::string& error)
{
    return ParseIntField(text, prefix + "_in", layer.In, error) &&
        ParseIntField(text, prefix + "_out", layer.Out, error) &&
        ParseNumberArrayField(text, prefix + "_weight", layer.Weight, error) &&
        ParseNumberArrayField(text, prefix + "_bias", layer.Bias, error);
}

bool ParseRuntimeNormLayer(
    const std::string& text,
    const std::string& prefix,
    RuntimeNormLayer& layer,
    std::string& error)
{
    return ParseIntField(text, prefix + "_size", layer.Size, error) &&
        ParseNumberField(text, prefix + "_eps", layer.Eps, error) &&
        ParseNumberArrayField(text, prefix + "_weight", layer.Weight, error) &&
        ParseNumberArrayField(text, prefix + "_bias", layer.Bias, error);
}

bool ParseRuntimeBatchNormLayer(
    const std::string& text,
    const std::string& prefix,
    RuntimeBatchNormLayer& layer,
    std::string& error)
{
    return ParseIntField(text, prefix + "_channels", layer.Channels, error) &&
        ParseNumberField(text, prefix + "_eps", layer.Eps, error) &&
        ParseNumberArrayField(text, prefix + "_weight", layer.Weight, error) &&
        ParseNumberArrayField(text, prefix + "_bias", layer.Bias, error) &&
        ParseNumberArrayField(text, prefix + "_running_mean", layer.RunningMean, error) &&
        ParseNumberArrayField(text, prefix + "_running_var", layer.RunningVar, error);
}

bool ParseRuntimeConv2DLayer(
    const std::string& text,
    const std::string& prefix,
    RuntimeConv2DLayer& layer,
    std::string& error)
{
    return ParseIntField(text, prefix + "_in", layer.In, error) &&
        ParseIntField(text, prefix + "_out", layer.Out, error) &&
        ParseIntField(text, prefix + "_kernel_h", layer.KernelH, error) &&
        ParseIntField(text, prefix + "_kernel_w", layer.KernelW, error) &&
        ParseIntField(text, prefix + "_padding", layer.Padding, error) &&
        ParseNumberArrayField(text, prefix + "_weight", layer.Weight, error) &&
        ParseNumberArrayField(text, prefix + "_bias", layer.Bias, error);
}

double SiLU(double x)
{
    return x * Sigmoid(x);
}

std::vector<double> LinearForward(const RuntimeLinearLayer& layer, const std::vector<double>& input)
{
    std::vector<double> output(static_cast<std::size_t>(std::max(0, layer.Out)), 0.0);
    if (!layer.IsUsable() || input.size() != static_cast<std::size_t>(layer.In))
        return output;
    for (int out = 0; out < layer.Out; out++)
    {
        double value = layer.Bias[static_cast<std::size_t>(out)];
        const std::size_t base = static_cast<std::size_t>(out * layer.In);
        for (int in = 0; in < layer.In; in++)
            value += input[static_cast<std::size_t>(in)] * layer.Weight[base + static_cast<std::size_t>(in)];
        output[static_cast<std::size_t>(out)] = value;
    }
    return output;
}

void ApplySiLU(std::vector<double>& values)
{
    for (double& value : values)
        value = SiLU(value);
}

void ApplyLayerNorm(const RuntimeNormLayer& layer, std::vector<double>& values)
{
    if (!layer.IsUsable() || values.size() != static_cast<std::size_t>(layer.Size))
        return;
    double mean = 0.0;
    for (double value : values)
        mean += value;
    mean /= std::max<int>(1, layer.Size);
    double variance = 0.0;
    for (double value : values)
    {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= std::max<int>(1, layer.Size);
    const double invStd = 1.0 / std::sqrt(variance + layer.Eps);
    for (int i = 0; i < layer.Size; i++)
    {
        const std::size_t index = static_cast<std::size_t>(i);
        values[index] = (values[index] - mean) * invStd * layer.Weight[index] + layer.Bias[index];
    }
}

std::vector<double> Conv2DForward(
    const RuntimeConv2DLayer& layer,
    const std::vector<double>& input,
    int height,
    int width)
{
    std::vector<double> output(static_cast<std::size_t>(std::max(0, layer.Out * height * width)), 0.0);
    if (!layer.IsUsable() || input.size() != static_cast<std::size_t>(layer.In * height * width))
        return output;
    auto inputAt = [&](int channel, int row, int col) -> double {
        return input[static_cast<std::size_t>((channel * height + row) * width + col)];
    };
    auto weightAt = [&](int out, int in, int kh, int kw) -> double {
        const int index = (((out * layer.In + in) * layer.KernelH + kh) * layer.KernelW + kw);
        return layer.Weight[static_cast<std::size_t>(index)];
    };
    for (int out = 0; out < layer.Out; out++)
    {
        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                double value = layer.Bias[static_cast<std::size_t>(out)];
                for (int in = 0; in < layer.In; in++)
                {
                    for (int kh = 0; kh < layer.KernelH; kh++)
                    {
                        const int inRow = row + kh - layer.Padding;
                        if (inRow < 0 || inRow >= height)
                            continue;
                        for (int kw = 0; kw < layer.KernelW; kw++)
                        {
                            const int inCol = col + kw - layer.Padding;
                            if (inCol < 0 || inCol >= width)
                                continue;
                            value += inputAt(in, inRow, inCol) * weightAt(out, in, kh, kw);
                        }
                    }
                }
                output[static_cast<std::size_t>((out * height + row) * width + col)] = value;
            }
        }
    }
    return output;
}

void ApplyBatchNorm2D(RuntimeBatchNormLayer const& layer, std::vector<double>& values, int height, int width)
{
    if (!layer.IsUsable() || values.size() != static_cast<std::size_t>(layer.Channels * height * width))
        return;
    for (int channel = 0; channel < layer.Channels; channel++)
    {
        const double invStd = 1.0 / std::sqrt(layer.RunningVar[static_cast<std::size_t>(channel)] + layer.Eps);
        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                const std::size_t index = static_cast<std::size_t>((channel * height + row) * width + col);
                values[index] = (values[index] - layer.RunningMean[static_cast<std::size_t>(channel)]) *
                    invStd *
                    layer.Weight[static_cast<std::size_t>(channel)] +
                    layer.Bias[static_cast<std::size_t>(channel)];
            }
        }
    }
}

std::vector<double> MaxPool2D2x2(const std::vector<double>& input, int channels, int height, int width, int& outHeight, int& outWidth)
{
    outHeight = height / 2;
    outWidth = width / 2;
    std::vector<double> output(static_cast<std::size_t>(channels * outHeight * outWidth), 0.0);
    for (int channel = 0; channel < channels; channel++)
    {
        for (int row = 0; row < outHeight; row++)
        {
            for (int col = 0; col < outWidth; col++)
            {
                double value = -1.0e100;
                for (int kh = 0; kh < 2; kh++)
                {
                    for (int kw = 0; kw < 2; kw++)
                    {
                        const int inRow = row * 2 + kh;
                        const int inCol = col * 2 + kw;
                        value = std::max(value, input[static_cast<std::size_t>((channel * height + inRow) * width + inCol)]);
                    }
                }
                output[static_cast<std::size_t>((channel * outHeight + row) * outWidth + col)] = value;
            }
        }
    }
    return output;
}

std::vector<double> AdaptiveAvgPool2D4x4(const std::vector<double>& input, int channels, int height, int width)
{
    constexpr int outHeight = 4;
    constexpr int outWidth = 4;
    std::vector<double> output(static_cast<std::size_t>(channels * outHeight * outWidth), 0.0);
    for (int channel = 0; channel < channels; channel++)
    {
        for (int outRow = 0; outRow < outHeight; outRow++)
        {
            const int rowStart = static_cast<int>(std::floor(static_cast<double>(outRow * height) / outHeight));
            const int rowEnd = static_cast<int>(std::ceil(static_cast<double>((outRow + 1) * height) / outHeight));
            for (int outCol = 0; outCol < outWidth; outCol++)
            {
                const int colStart = static_cast<int>(std::floor(static_cast<double>(outCol * width) / outWidth));
                const int colEnd = static_cast<int>(std::ceil(static_cast<double>((outCol + 1) * width) / outWidth));
                double sum = 0.0;
                int count = 0;
                for (int row = rowStart; row < rowEnd; row++)
                {
                    for (int col = colStart; col < colEnd; col++)
                    {
                        sum += input[static_cast<std::size_t>((channel * height + row) * width + col)];
                        count++;
                    }
                }
                output[static_cast<std::size_t>((channel * outHeight + outRow) * outWidth + outCol)] =
                    count > 0 ? sum / count : 0.0;
            }
        }
    }
    return output;
}

bool LoadLinearPolicyModel(
    const std::string& path,
    LinearPolicyModel& model,
    std::string& error)
{
    std::string text;
    if (!ReadFile(path, text, error))
        return false;

    LinearPolicyModel parsed {};
    if (!ParseStringField(text, "schema", parsed.Schema, error) ||
        !ParseStringField(text, "feature_schema_id", parsed.FeatureSchemaID, error) ||
        !ParseStringArrayField(text, "feature_names", parsed.FeatureNames, error) ||
        !ParseStringArrayField(text, "buttons", parsed.Buttons, error) ||
        !ParseNumberArrayField(text, "mean", parsed.Mean, error) ||
        !ParseNumberArrayField(text, "scale", parsed.Scale, error) ||
        !ParseNumberArrayField(text, "bias", parsed.Bias, error))
    {
        return false;
    }

    std::size_t weightRows = 0;
    std::size_t weightCols = 0;
    if (!ParseNumberMatrixField(text, "weights", parsed.Weights, weightRows, weightCols, error))
        return false;
    if (weightRows != parsed.FeatureNames.size() || weightCols != parsed.Buttons.size())
    {
        error = "weight matrix shape does not match feature/button names";
        return false;
    }
    if (!parsed.IsUsable())
    {
        error = "model arrays have inconsistent lengths";
        return false;
    }

    for (double& scale : parsed.Scale)
    {
        if (std::abs(scale) < 1e-12)
            scale = 1.0;
    }

    model = std::move(parsed);
    return true;
}

bool LoadCompactActionPolicyModel(
    const std::string& path,
    CompactActionPolicyModel& model,
    std::string& error)
{
    std::string text;
    if (!ReadFile(path, text, error))
        return false;

    CompactActionPolicyModel parsed {};
    std::vector<std::string> headNames;
    if (!ParseStringField(text, "schema", parsed.Schema, error) ||
        !ParseStringField(text, "input_schema", parsed.InputSchema, error) ||
        !ParseStringField(text, "scalar_schema", parsed.ScalarSchema, error) ||
        !ParseStringField(text, "label_schema", parsed.LabelSchema, error) ||
        !ParseStringArrayField(text, "head_names", headNames, error) ||
        !ParseIntField(text, "scalar", parsed.ScalarCount, error) ||
        !ParseNumberArrayField(text, "mean", parsed.Mean, error) ||
        !ParseNumberArrayField(text, "scale", parsed.Scale, error))
    {
        return false;
    }

    if (parsed.Schema != "nsmb_mvl_compact_action_policy_v1")
    {
        error = "unsupported compact action policy schema: " + parsed.Schema;
        return false;
    }
    if ((parsed.InputSchema == "nsmb_mvl_compact_observation_v2" && parsed.ScalarCount != 35) ||
        (parsed.InputSchema == "nsmb_mvl_compact_observation_v3" && parsed.ScalarCount != 47) ||
        (parsed.InputSchema != "nsmb_mvl_compact_observation_v2" &&
            parsed.InputSchema != "nsmb_mvl_compact_observation_v3"))
    {
        error = "compact policy input schema/scalar count mismatch: " + parsed.InputSchema +
            " scalar=" + std::to_string(parsed.ScalarCount);
        return false;
    }

    parsed.Heads.reserve(headNames.size());
    for (const std::string& name : headNames)
    {
        CompactActionHead head {};
        head.Name = name;
        if (!ParseStringArrayField(text, "classes_" + name, head.Classes, error) ||
            !ParseNumberArrayField(text, "bias_" + name, head.Bias, error))
        {
            return false;
        }
        std::size_t rows = 0;
        std::size_t cols = 0;
        if (!ParseNumberMatrixField(text, "weights_" + name, head.Weights, rows, cols, error))
            return false;
        if (rows != parsed.Mean.size() || cols != head.Classes.size())
        {
            error = "weight matrix shape does not match compact action head: " + name;
            return false;
        }
        parsed.Heads.push_back(std::move(head));
    }

    if (!parsed.IsUsable())
    {
        error = "compact action model arrays have inconsistent lengths";
        return false;
    }

    for (double& scale : parsed.Scale)
    {
        if (std::abs(scale) < 1e-12)
            scale = 1.0;
    }

    model = std::move(parsed);
    return true;
}

bool LoadTorchCompactPolicyModel(
    const std::string& path,
    TorchCompactPolicyModel& model,
    std::string& error)
{
    std::string text;
    if (!ReadFile(path, text, error))
        return false;

    TorchCompactPolicyModel parsed {};
    if (!ParseStringField(text, "schema", parsed.Schema, error) ||
        !ParseStringField(text, "input_schema", parsed.InputSchema, error) ||
        !ParseStringField(text, "scalar_schema", parsed.ScalarSchema, error) ||
        !ParseStringField(text, "label_schema", parsed.LabelSchema, error) ||
        !ParseStringArrayField(text, "head_names", parsed.HeadNames, error) ||
        !ParseNumberArrayField(text, "scalar_mean", parsed.ScalarMean, error) ||
        !ParseNumberArrayField(text, "scalar_scale", parsed.ScalarScale, error) ||
        !ParseNumberArrayField(text, "entity_mean", parsed.EntityMean, error) ||
        !ParseNumberArrayField(text, "entity_scale", parsed.EntityScale, error) ||
        !ParseIntField(text, "scalar", parsed.ScalarCount, error) ||
        !ParseIntField(text, "terrain_height", parsed.TerrainHeight, error) ||
        !ParseIntField(text, "terrain_width", parsed.TerrainWidth, error) ||
        !ParseIntField(text, "terrain_channels", parsed.TerrainChannels, error) ||
        !ParseIntField(text, "opponent_terrain_channels", parsed.OpponentTerrainChannels, error) ||
        !ParseIntField(text, "entity_count", parsed.EntityCount, error) ||
        !ParseIntField(text, "entity_features", parsed.EntityFeatures, error) ||
        !ParseIntField(text, "total", parsed.TotalFeatures, error))
    {
        return false;
    }

    if (parsed.Schema != "nsmb_mvl_torch_compact_policy_runtime_v1")
    {
        error = "unsupported torch compact policy schema: " + parsed.Schema;
        return false;
    }
    if ((parsed.InputSchema == "nsmb_mvl_compact_observation_v2" && parsed.ScalarCount != 35) ||
        (parsed.InputSchema == "nsmb_mvl_compact_observation_v3" && parsed.ScalarCount != 47) ||
        (parsed.InputSchema != "nsmb_mvl_compact_observation_v2" &&
            parsed.InputSchema != "nsmb_mvl_compact_observation_v3"))
    {
        error = "compact policy input schema/scalar count mismatch: " + parsed.InputSchema +
            " scalar=" + std::to_string(parsed.ScalarCount);
        return false;
    }

    if (!ParseRuntimeLinearLayer(text, "scalar_linear0", parsed.ScalarLinear0, error) ||
        !ParseRuntimeNormLayer(text, "scalar_layer_norm1", parsed.ScalarLayerNorm1, error) ||
        !ParseRuntimeLinearLayer(text, "scalar_linear4", parsed.ScalarLinear4, error) ||
        !ParseRuntimeConv2DLayer(text, "terrain_conv0", parsed.TerrainConv0, error) ||
        !ParseRuntimeBatchNormLayer(text, "terrain_batch_norm1", parsed.TerrainBatchNorm1, error) ||
        !ParseRuntimeConv2DLayer(text, "terrain_conv3", parsed.TerrainConv3, error) ||
        !ParseRuntimeBatchNormLayer(text, "terrain_batch_norm4", parsed.TerrainBatchNorm4, error) ||
        !ParseRuntimeConv2DLayer(text, "terrain_conv7", parsed.TerrainConv7, error) ||
        !ParseRuntimeBatchNormLayer(text, "terrain_batch_norm8", parsed.TerrainBatchNorm8, error) ||
        !ParseRuntimeConv2DLayer(text, "terrain_conv10", parsed.TerrainConv10, error) ||
        !ParseRuntimeBatchNormLayer(text, "terrain_batch_norm11", parsed.TerrainBatchNorm11, error) ||
        !ParseRuntimeLinearLayer(text, "terrain_linear15", parsed.TerrainLinear15, error) ||
        !ParseRuntimeLinearLayer(text, "entity_linear0", parsed.EntityLinear0, error) ||
        !ParseRuntimeNormLayer(text, "entity_layer_norm1", parsed.EntityLayerNorm1, error) ||
        !ParseRuntimeLinearLayer(text, "entity_linear3", parsed.EntityLinear3, error) ||
        !ParseRuntimeLinearLayer(text, "fusion_linear0", parsed.FusionLinear0, error) ||
        !ParseRuntimeNormLayer(text, "fusion_layer_norm1", parsed.FusionLayerNorm1, error) ||
        !ParseRuntimeLinearLayer(text, "fusion_linear4", parsed.FusionLinear4, error))
    {
        return false;
    }

    parsed.Heads.reserve(parsed.HeadNames.size());
    for (const std::string& name : parsed.HeadNames)
    {
        CompactActionHead head {};
        head.Name = name;
        int in = 0;
        int out = 0;
        if (!ParseStringArrayField(text, "classes_" + name, head.Classes, error) ||
            !ParseIntField(text, "head_" + name + "_in", in, error) ||
            !ParseIntField(text, "head_" + name + "_out", out, error) ||
            !ParseNumberArrayField(text, "head_" + name + "_weight", head.Weights, error) ||
            !ParseNumberArrayField(text, "head_" + name + "_bias", head.Bias, error))
        {
            return false;
        }
        if (in != parsed.FusionLinear4.Out || out != static_cast<int>(head.Classes.size()))
        {
            error = "torch compact action head shape mismatch: " + name;
            return false;
        }
        parsed.Heads.push_back(std::move(head));
    }

    for (double& scale : parsed.ScalarScale)
    {
        if (std::abs(scale) < 1e-12)
            scale = 1.0;
    }
    for (double& scale : parsed.EntityScale)
    {
        if (std::abs(scale) < 1e-12)
            scale = 1.0;
    }

    if (!parsed.IsUsable())
    {
        error = "torch compact model arrays have inconsistent lengths";
        return false;
    }

    model = std::move(parsed);
    return true;
}

bool Runtime::LoadModel(const std::string& path, ModelLoadErrors& errors)
{
    errors = {};
    LoadedModelType = ModelType::None;
    Linear = {};
    Compact = {};
    TorchCompact = {};

    if (LoadTorchCompactPolicyModel(path, TorchCompact, errors.TorchCompact))
    {
        LoadedModelType = ModelType::TorchCompact;
        return true;
    }
    if (LoadCompactActionPolicyModel(path, Compact, errors.Compact))
    {
        LoadedModelType = ModelType::Compact;
        return true;
    }
    if (LoadLinearPolicyModel(path, Linear, errors.Linear))
    {
        LoadedModelType = ModelType::Linear;
        return true;
    }
    return false;
}

void Runtime::ResetPlayer(int instanceID, int player)
{
    if (!ValidPlayer(instanceID, player))
        return;
    Players[instanceID][player] = {};
}

std::uint32_t Runtime::ApplyFireTapRelease(
    int instanceID,
    int player,
    std::uint32_t held,
    std::uint32_t allowedHeldMask,
    bool firePressIntent,
    const char*& phase)
{
    phase = "none";
    if (!ValidPlayer(instanceID, player))
        return held;

    constexpr std::uint32_t kHeldY = 1u << 11;
    PlayerState& state = Players[instanceID][player];
    if ((allowedHeldMask & kHeldY) == 0)
    {
        state.FireTapPressNext = false;
        state.LastHeld = held;
        state.LastHeldValid = true;
        return held;
    }

    if (state.FireTapPressNext)
    {
        held |= kHeldY;
        state.FireTapPressNext = false;
        phase = "press";
    }
    else
    {
        const bool yHeldNow = (held & kHeldY) != 0;
        const bool yHeldLast = state.LastHeldValid && (state.LastHeld & kHeldY) != 0;
        if (firePressIntent && yHeldNow && yHeldLast)
        {
            held &= ~kHeldY;
            state.FireTapPressNext = true;
            phase = "release";
        }
    }

    state.LastHeld = held;
    state.LastHeldValid = true;
    return held;
}

std::uint32_t Runtime::ApplyNeutralHold(
    int instanceID,
    int player,
    std::uint32_t frame,
    std::uint32_t held,
    int neutralHoldFrames,
    std::uint32_t allowedHeldMask,
    bool& adjusted)
{
    adjusted = false;
    if (!ValidPlayer(instanceID, player))
        return held;

    PlayerState& state = Players[instanceID][player];
    if (held != 0)
    {
        state.LastNonZeroHeldValid = true;
        state.LastNonZero = {held, frame};
        return held;
    }
    if (neutralHoldFrames <= 0 || !state.LastNonZeroHeldValid)
        return held;
    if (frame >= state.LastNonZero.Frame &&
        frame - state.LastNonZero.Frame <= static_cast<std::uint32_t>(neutralHoldFrames))
    {
        adjusted = true;
        return state.LastNonZero.Held & allowedHeldMask;
    }
    return held;
}

const Runtime::HeldRecord* Runtime::CachedHeld(int instanceID, int player) const
{
    if (!ValidPlayer(instanceID, player))
        return nullptr;
    const PlayerState& state = Players[instanceID][player];
    return state.CachedHeldValid ? &state.Cached : nullptr;
}

void Runtime::CacheHeld(int instanceID, int player, std::uint32_t frame, std::uint32_t held)
{
    if (!ValidPlayer(instanceID, player))
        return;
    PlayerState& state = Players[instanceID][player];
    state.CachedHeldValid = true;
    state.Cached = {held, frame};
}

std::uint16_t HeldFromPrediction(const std::vector<double>& probabilities, double threshold)
{
    std::uint16_t held = 0;
    const std::size_t count = std::min<std::size_t>(probabilities.size(), 16);
    for (std::size_t i = 0; i < count; i++)
    {
        if (probabilities[i] >= threshold)
            held = static_cast<std::uint16_t>(held | (1u << i));
    }
    return held;
}

std::uint16_t HeldFromCompactActions(
    const std::vector<CompactActionHead>& heads,
    const std::vector<int>& actions)
{
    std::uint16_t held = 0;
    for (std::size_t i = 0; i < heads.size() && i < actions.size(); i++)
    {
        const std::string& name = heads[i].Name;
        const int action = actions[i];
        if (name == "horizontal")
        {
            if (action == 1)
                held = static_cast<std::uint16_t>(held | (1u << 5));
            else if (action == 2)
                held = static_cast<std::uint16_t>(held | (1u << 4));
        }
        else if (name == "vertical")
        {
            if (action == 1)
                held = static_cast<std::uint16_t>(held | (1u << 6));
            else if (action == 2)
                held = static_cast<std::uint16_t>(held | (1u << 7));
        }
        else if (name == "jump")
        {
            if (action != 0)
                held = static_cast<std::uint16_t>(held | (1u << 1));
        }
        else if (name == "run" || name == "fire")
        {
            if (action != 0)
                held = static_cast<std::uint16_t>(held | (1u << 11));
        }
    }
    return held;
}

Prediction PredictLinearPolicy(
    const LinearPolicyModel& model,
    const std::vector<double>& rawFeatures,
    double threshold)
{
    Prediction prediction {};
    const std::size_t features = model.FeatureCount();
    const std::size_t buttons = model.ButtonCount();
    prediction.Probabilities.assign(buttons, 0.0);
    if (!model.IsUsable() || rawFeatures.size() != features)
        return prediction;

    for (std::size_t button = 0; button < buttons; button++)
    {
        double z = model.Bias[button];
        for (std::size_t feature = 0; feature < features; feature++)
        {
            const double x = (rawFeatures[feature] - model.Mean[feature]) / model.Scale[feature];
            z += x * model.Weights[feature * buttons + button];
        }
        prediction.Probabilities[button] = Sigmoid(z);
    }
    prediction.Held = HeldFromPrediction(prediction.Probabilities, threshold);
    return prediction;
}

CompactActionPrediction PredictCompactActionPolicy(
    const CompactActionPolicyModel& model,
    const std::vector<double>& rawFeatures)
{
    CompactActionPrediction prediction {};
    const std::size_t features = model.FeatureCount();
    if (!model.IsUsable() || rawFeatures.size() != features)
        return prediction;

    std::vector<double> x(features, 0.0);
    for (std::size_t i = 0; i < features; i++)
        x[i] = (rawFeatures[i] - model.Mean[i]) / model.Scale[i];

    prediction.Actions.reserve(model.Heads.size());
    prediction.Confidences.reserve(model.Heads.size());
    for (const CompactActionHead& head : model.Heads)
    {
        const std::size_t classes = head.ClassCount();
        std::vector<double> logits(classes, 0.0);
        for (std::size_t c = 0; c < classes; c++)
        {
            double value = head.Bias[c];
            for (std::size_t f = 0; f < features; f++)
                value += x[f] * head.Weights[f * classes + c];
            logits[c] = value;
        }
        const std::vector<double> probs = Softmax(logits);
        const auto best = std::max_element(probs.begin(), probs.end());
        const int action = best == probs.end() ? 0 : static_cast<int>(best - probs.begin());
        prediction.Actions.push_back(action);
        prediction.Confidences.push_back(best == probs.end() ? 0.0 : *best);
    }
    prediction.Held = HeldFromCompactActions(model.Heads, prediction.Actions);
    return prediction;
}

CompactActionPrediction PredictTorchCompactPolicy(
    const TorchCompactPolicyModel& model,
    const std::vector<double>& rawFeatures)
{
    CompactActionPrediction prediction {};
    if (!model.IsUsable() || rawFeatures.size() != model.FeatureCount())
        return prediction;

    std::size_t offset = 0;
    std::vector<double> scalar(static_cast<std::size_t>(model.ScalarCount), 0.0);
    for (int i = 0; i < model.ScalarCount; i++, offset++)
        scalar[static_cast<std::size_t>(i)] = (rawFeatures[offset] - model.ScalarMean[static_cast<std::size_t>(i)]) /
            model.ScalarScale[static_cast<std::size_t>(i)];

    const int height = model.TerrainHeight;
    const int width = model.TerrainWidth;
    const int selfTerrainValues = height * width * model.TerrainChannels;
    const int opponentTerrainValues = height * width * model.OpponentTerrainChannels;
    const int terrainChannels = model.TerrainChannels + model.OpponentTerrainChannels;
    std::vector<double> terrain(static_cast<std::size_t>(terrainChannels * height * width), 0.0);
    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            for (int channel = 0; channel < model.TerrainChannels; channel++)
            {
                const std::size_t rawIndex = offset + static_cast<std::size_t>((row * width + col) * model.TerrainChannels + channel);
                terrain[static_cast<std::size_t>((channel * height + row) * width + col)] = rawFeatures[rawIndex];
            }
        }
    }
    offset += static_cast<std::size_t>(selfTerrainValues);
    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            for (int channel = 0; channel < model.OpponentTerrainChannels; channel++)
            {
                const std::size_t rawIndex = offset + static_cast<std::size_t>((row * width + col) * model.OpponentTerrainChannels + channel);
                const int outChannel = model.TerrainChannels + channel;
                terrain[static_cast<std::size_t>((outChannel * height + row) * width + col)] = rawFeatures[rawIndex];
            }
        }
    }
    offset += static_cast<std::size_t>(opponentTerrainValues);

    std::vector<double> entityMean(static_cast<std::size_t>(model.EntityLinear3.Out), 0.0);
    std::vector<double> entityMax(static_cast<std::size_t>(model.EntityLinear3.Out), -1.0e4);
    int activeEntities = 0;
    for (int entity = 0; entity < model.EntityCount; entity++)
    {
        const std::size_t entityOffset = offset + static_cast<std::size_t>(entity * model.EntityFeatures);
        const bool active = rawFeatures[entityOffset] != 0.0;
        std::vector<double> entityValues(static_cast<std::size_t>(model.EntityFeatures), 0.0);
        for (int feature = 0; feature < model.EntityFeatures; feature++)
        {
            const std::size_t index = static_cast<std::size_t>(feature);
            entityValues[index] = (rawFeatures[entityOffset + index] - model.EntityMean[index]) / model.EntityScale[index];
        }
        std::vector<double> entityFeatures = LinearForward(model.EntityLinear0, entityValues);
        ApplyLayerNorm(model.EntityLayerNorm1, entityFeatures);
        ApplySiLU(entityFeatures);
        entityFeatures = LinearForward(model.EntityLinear3, entityFeatures);
        ApplySiLU(entityFeatures);
        if (!active)
            continue;
        activeEntities++;
        for (std::size_t i = 0; i < entityFeatures.size(); i++)
        {
            entityMean[i] += entityFeatures[i];
            entityMax[i] = std::max(entityMax[i], entityFeatures[i]);
        }
    }
    if (activeEntities > 0)
    {
        for (double& value : entityMean)
            value /= static_cast<double>(activeEntities);
    }
    else
    {
        std::fill(entityMax.begin(), entityMax.end(), 0.0);
    }

    std::vector<double> scalarFeatures = LinearForward(model.ScalarLinear0, scalar);
    ApplyLayerNorm(model.ScalarLayerNorm1, scalarFeatures);
    ApplySiLU(scalarFeatures);
    scalarFeatures = LinearForward(model.ScalarLinear4, scalarFeatures);
    ApplySiLU(scalarFeatures);

    std::vector<double> terrainFeatures = Conv2DForward(model.TerrainConv0, terrain, height, width);
    ApplyBatchNorm2D(model.TerrainBatchNorm1, terrainFeatures, height, width);
    ApplySiLU(terrainFeatures);
    terrainFeatures = Conv2DForward(model.TerrainConv3, terrainFeatures, height, width);
    ApplyBatchNorm2D(model.TerrainBatchNorm4, terrainFeatures, height, width);
    ApplySiLU(terrainFeatures);
    int pooledHeight = 0;
    int pooledWidth = 0;
    terrainFeatures = MaxPool2D2x2(terrainFeatures, model.TerrainConv3.Out, height, width, pooledHeight, pooledWidth);
    terrainFeatures = Conv2DForward(model.TerrainConv7, terrainFeatures, pooledHeight, pooledWidth);
    ApplyBatchNorm2D(model.TerrainBatchNorm8, terrainFeatures, pooledHeight, pooledWidth);
    ApplySiLU(terrainFeatures);
    terrainFeatures = Conv2DForward(model.TerrainConv10, terrainFeatures, pooledHeight, pooledWidth);
    ApplyBatchNorm2D(model.TerrainBatchNorm11, terrainFeatures, pooledHeight, pooledWidth);
    ApplySiLU(terrainFeatures);
    terrainFeatures = AdaptiveAvgPool2D4x4(terrainFeatures, model.TerrainConv10.Out, pooledHeight, pooledWidth);
    terrainFeatures = LinearForward(model.TerrainLinear15, terrainFeatures);
    ApplySiLU(terrainFeatures);

    std::vector<double> fused;
    fused.reserve(scalarFeatures.size() + terrainFeatures.size() + entityMean.size() + entityMax.size());
    fused.insert(fused.end(), scalarFeatures.begin(), scalarFeatures.end());
    fused.insert(fused.end(), terrainFeatures.begin(), terrainFeatures.end());
    fused.insert(fused.end(), entityMean.begin(), entityMean.end());
    fused.insert(fused.end(), entityMax.begin(), entityMax.end());
    fused = LinearForward(model.FusionLinear0, fused);
    ApplyLayerNorm(model.FusionLayerNorm1, fused);
    ApplySiLU(fused);
    fused = LinearForward(model.FusionLinear4, fused);
    ApplySiLU(fused);

    prediction.Actions.reserve(model.Heads.size());
    prediction.Confidences.reserve(model.Heads.size());
    for (const CompactActionHead& head : model.Heads)
    {
        const std::size_t classes = head.ClassCount();
        std::vector<double> logits(classes, 0.0);
        for (std::size_t c = 0; c < classes; c++)
        {
            double value = head.Bias[c];
            const std::size_t weightBase = c * fused.size();
            for (std::size_t i = 0; i < fused.size(); i++)
                value += fused[i] * head.Weights[weightBase + i];
            logits[c] = value;
        }
        const std::vector<double> probs = Softmax(logits);
        const auto best = std::max_element(probs.begin(), probs.end());
        const int action = best == probs.end() ? 0 : static_cast<int>(best - probs.begin());
        prediction.Actions.push_back(action);
        prediction.Confidences.push_back(best == probs.end() ? 0.0 : *best);
    }
    prediction.Held = HeldFromCompactActions(model.Heads, prediction.Actions);
    return prediction;
}

}
