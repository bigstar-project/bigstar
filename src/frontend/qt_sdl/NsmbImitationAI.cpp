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
        !ParseStringField(text, "label_schema", parsed.LabelSchema, error) ||
        !ParseStringArrayField(text, "head_names", headNames, error) ||
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

}
