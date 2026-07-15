#include "NsmbImitationAI.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

int Failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
    Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestModelRuntime()
{
    NsmbImitationAI::Runtime runtime;
    CHECK(!runtime.IsEnabled());
    CHECK(!runtime.HasModel());
    CHECK(runtime.LoadedType() == NsmbImitationAI::ModelType::None);

    runtime.SetEnabled(true);
    CHECK(runtime.IsEnabled());
    runtime.Disable();
    CHECK(!runtime.IsEnabled());

    const auto disabled = runtime.InitializeModel(false, "ignored.json");
    CHECK(!disabled.RequestedEnabled);
    CHECK(!disabled.ModelPathEmpty);
    CHECK(!disabled.Loaded);
    CHECK(!runtime.IsEnabled());

    const auto empty = runtime.InitializeModel(true, "");
    CHECK(empty.RequestedEnabled);
    CHECK(empty.ModelPathEmpty);
    CHECK(!empty.Loaded);
    CHECK(!runtime.IsEnabled());

    const auto missing = runtime.InitializeModel(
        true, "missing-nsmb-imitation-model.json");
    CHECK(missing.RequestedEnabled);
    CHECK(!missing.ModelPathEmpty);
    CHECK(!missing.Loaded);
    CHECK(!missing.Errors.TorchCompact.empty());
    CHECK(!missing.Errors.Compact.empty());
    CHECK(!missing.Errors.Linear.empty());
    CHECK(!runtime.IsEnabled());
    CHECK(runtime.DescribeModel().Type == NsmbImitationAI::ModelType::None);

    NsmbImitationAI::ModelLoadErrors errors;
    CHECK(!runtime.LoadModel("missing-nsmb-imitation-model.json", errors));
    CHECK(!runtime.HasModel());
    CHECK(!errors.TorchCompact.empty());
    CHECK(!errors.Compact.empty());
    CHECK(!errors.Linear.empty());

    const char* modelPath = "nsmb-imitation-ai-test-model.json";
    {
        std::ofstream modelFile(modelPath, std::ios::out | std::ios::trunc);
        modelFile << R"({
  "schema": "linear-test",
  "feature_schema_id": "feature-test",
  "feature_names": ["x"],
  "buttons": ["A"],
  "mean": [0],
  "scale": [1],
  "bias": [0],
  "weights": [[0]]
})";
    }
    const auto loaded = runtime.InitializeModel(true, modelPath);
    CHECK(loaded.RequestedEnabled);
    CHECK(!loaded.ModelPathEmpty);
    CHECK(loaded.Loaded);
    CHECK(runtime.IsEnabled());
    const auto description = runtime.DescribeModel();
    CHECK(description.Type == NsmbImitationAI::ModelType::Linear);
    CHECK(description.FeatureCount == 1);
    CHECK(description.OutputCount == 1);
    CHECK(description.Schema == "linear-test");
    CHECK(description.DetailSchema == "feature-test");
    std::remove(modelPath);

    CHECK(!runtime.HasFeatureCoverage());
    runtime.RecordFeatureCoverage(12, 3);
    CHECK(runtime.HasFeatureCoverage());
    CHECK(runtime.FeatureCountFilled() == 12);
    CHECK(runtime.FeatureCountMissing() == 3);
}

void TestPlayerRuntime()
{
    NsmbImitationAI::Runtime runtime;
    constexpr std::uint32_t kHeldY = 1u << 11;
    const char* phase = nullptr;

    CHECK(runtime.ApplyFireTapRelease(-1, 0, kHeldY, kHeldY, true, phase) == kHeldY);
    CHECK(std::string(phase) == "none");
    CHECK(runtime.ApplyFireTapRelease(0, 0, kHeldY, kHeldY, true, phase) == kHeldY);
    CHECK(std::string(phase) == "none");
    CHECK(runtime.ApplyFireTapRelease(0, 0, kHeldY, kHeldY, true, phase) == 0u);
    CHECK(std::string(phase) == "release");
    CHECK(runtime.ApplyFireTapRelease(0, 0, 0, kHeldY, false, phase) == kHeldY);
    CHECK(std::string(phase) == "press");

    runtime.ResetPlayer(0, 0);
    CHECK(runtime.ApplyFireTapRelease(0, 0, kHeldY, kHeldY, true, phase) == kHeldY);
    CHECK(std::string(phase) == "none");
    CHECK(runtime.ApplyFireTapRelease(0, 0, kHeldY, 0, true, phase) == kHeldY);
    CHECK(std::string(phase) == "none");

    CHECK(runtime.CachedHeld(0, 1) == nullptr);
    runtime.CacheHeld(0, 1, 42, 0x123);
    const auto* cached = runtime.CachedHeld(0, 1);
    CHECK(cached != nullptr);
    CHECK(cached && cached->Frame == 42u);
    CHECK(cached && cached->Held == 0x123u);
    runtime.CacheHeld(16, 1, 99, 0x456);
    CHECK(runtime.CachedHeld(16, 1) == nullptr);

    bool adjusted = true;
    CHECK(runtime.ApplyNeutralHold(0, 1, 100, 0x21, 2, 0x0F, adjusted) == 0x21u);
    CHECK(!adjusted);
    CHECK(runtime.ApplyNeutralHold(0, 1, 101, 0, 2, 0x0F, adjusted) == 0x01u);
    CHECK(adjusted);
    CHECK(runtime.ApplyNeutralHold(0, 1, 102, 0, 2, 0x0F, adjusted) == 0x01u);
    CHECK(adjusted);
    CHECK(runtime.ApplyNeutralHold(0, 1, 103, 0, 2, 0x0F, adjusted) == 0u);
    CHECK(!adjusted);

    runtime.ResetPlayer(0, 1);
    CHECK(runtime.CachedHeld(0, 1) == nullptr);
    CHECK(runtime.ApplyNeutralHold(0, 1, 104, 0, 2, 0x0F, adjusted) == 0u);
    CHECK(!adjusted);
}

} // namespace

int main()
{
    TestModelRuntime();
    TestPlayerRuntime();
    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb imitation AI tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb imitation AI tests passed\n");
    return 0;
}
