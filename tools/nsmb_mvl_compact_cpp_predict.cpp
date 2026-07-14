#include "NsmbImitationAI.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

const char* SkipWhitespace(const char* p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

bool FindValue(const std::string& text, const std::string& key, const char*& value)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t pos = text.find(needle);
    if (pos == std::string::npos)
        return false;
    const std::size_t colon = text.find(':', pos + needle.size());
    if (colon == std::string::npos)
        return false;
    value = SkipWhitespace(text.c_str() + colon + 1);
    return true;
}

bool ParseIntField(const std::string& text, const std::string& key, int& out)
{
    const char* p = nullptr;
    if (!FindValue(text, key, p))
        return false;
    char* end = nullptr;
    out = static_cast<int>(std::strtol(p, &end, 10));
    return end != p;
}

bool ParseNumberArrayField(const std::string& text, const std::string& key, std::vector<double>& out)
{
    const char* p = nullptr;
    if (!FindValue(text, key, p))
        return false;
    p = SkipWhitespace(p);
    if (*p != '[')
        return false;
    p++;
    out.clear();
    for (;;)
    {
        p = SkipWhitespace(p);
        if (*p == ']')
            return true;
        char* end = nullptr;
        const double value = std::strtod(p, &end);
        if (end == p)
            return false;
        out.push_back(value);
        p = SkipWhitespace(end);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == ']')
            return true;
        return false;
    }
}

bool ParseIntArrayField(const std::string& text, const std::string& key, std::vector<int>& out)
{
    std::vector<double> values;
    if (!ParseNumberArrayField(text, key, values))
        return false;
    out.clear();
    out.reserve(values.size());
    for (double value : values)
        out.push_back(static_cast<int>(value));
    return true;
}

int ParseIntArg(const char* value)
{
    if (value == nullptr)
        return 0;
    return std::atoi(value);
}

std::string JoinInts(const std::vector<int>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); i++)
    {
        if (i != 0)
            out << ";";
        out << values[i];
    }
    return out.str();
}

void PrintUsage()
{
    std::cerr
        << "usage: nsmb_mvl_compact_cpp_predict <runtime-model.json> <fixture.jsonl> <output.csv>"
        << " [--limit N]\n";
}

}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        PrintUsage();
        return 2;
    }

    const std::string modelPath = argv[1];
    const std::string fixturePath = argv[2];
    const std::string outputPath = argv[3];
    int limit = 0;
    for (int i = 4; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc)
            limit = ParseIntArg(argv[++i]);
        else
        {
            PrintUsage();
            return 2;
        }
    }

    NsmbImitationAI::TorchCompactPolicyModel torchModel;
    NsmbImitationAI::CompactActionPolicyModel model;
    std::string error;
    bool useTorchModel = NsmbImitationAI::LoadTorchCompactPolicyModel(modelPath, torchModel, error);
    if (!useTorchModel && !NsmbImitationAI::LoadCompactActionPolicyModel(modelPath, model, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    std::ifstream fixture(fixturePath, std::ios::in);
    if (!fixture)
    {
        std::cerr << "failed to open fixture: " << fixturePath << "\n";
        return 1;
    }
    std::ofstream output(outputPath, std::ios::out);
    if (!output)
    {
        std::cerr << "failed to open output: " << outputPath << "\n";
        return 1;
    }

    output << "frame,cpp_held,python_held,target_held,cpp_actions,python_actions,target_actions,match_python,match_target";
    const auto& heads = useTorchModel ? torchModel.Heads : model.Heads;
    for (const auto& head : heads)
        output << ",confidence_" << head.Name;
    output << "\n";

    std::string line;
    std::size_t rows = 0;
    std::size_t pythonMatches = 0;
    std::size_t targetMatches = 0;
    while (std::getline(fixture, line))
    {
        if (line.empty())
            continue;
        int frame = 0;
        int pythonHeld = 0;
        int targetHeld = 0;
        std::vector<double> features;
        std::vector<int> pythonPred;
        std::vector<int> target;
        if (!ParseIntField(line, "frame", frame) ||
            !ParseNumberArrayField(line, "features", features) ||
            !ParseIntArrayField(line, "pythonPred", pythonPred))
        {
            std::cerr << "invalid fixture row near line " << (rows + 1) << "\n";
            return 1;
        }
        ParseIntField(line, "pythonHeld", pythonHeld);
        ParseIntField(line, "targetHeld", targetHeld);
        ParseIntArrayField(line, "target", target);

        const auto prediction = useTorchModel
            ? NsmbImitationAI::PredictTorchCompactPolicy(torchModel, features)
            : NsmbImitationAI::PredictCompactActionPolicy(model, features);
        const bool matchPython = prediction.Actions == pythonPred && static_cast<int>(prediction.Held) == pythonHeld;
        const bool matchTarget = prediction.Actions == target && static_cast<int>(prediction.Held) == targetHeld;
        pythonMatches += matchPython ? 1 : 0;
        targetMatches += matchTarget ? 1 : 0;

        output << frame << ","
            << prediction.Held << ","
            << pythonHeld << ","
            << targetHeld << ","
            << JoinInts(prediction.Actions) << ","
            << JoinInts(pythonPred) << ","
            << JoinInts(target) << ","
            << (matchPython ? 1 : 0) << ","
            << (matchTarget ? 1 : 0);
        output << std::fixed << std::setprecision(9);
        for (double confidence : prediction.Confidences)
            output << "," << confidence;
        output << "\n";

        rows++;
        if (limit > 0 && static_cast<int>(rows) >= limit)
            break;
    }

    std::cout << "rows=" << rows
        << " python_exact=" << (rows == 0 ? 0.0 : static_cast<double>(pythonMatches) / static_cast<double>(rows))
        << " target_exact=" << (rows == 0 ? 0.0 : static_cast<double>(targetMatches) / static_cast<double>(rows))
        << " output=" << outputPath << "\n";
    return pythonMatches == rows ? 0 : 1;
}
