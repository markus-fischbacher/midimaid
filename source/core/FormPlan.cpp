#include "core/FormPlan.h"

#include "core/PatternValidation.h"

#include <array>

namespace mm::core {

namespace {

struct Layout {
    std::vector<uint32_t> bars;
    std::vector<PhraseRole> melodic; ///< roles in Melodic Techno
    std::vector<PhraseRole> other;   ///< roles in all other styles
    int weightPeak;
    int weightMelodic;
    int weightHard;
};

using R = PhraseRole;

const std::vector<Layout>& layoutsFor(uint32_t lengthBars) {
    static const std::vector<Layout> eight{
        {{8}, {R::Main}, {R::Main}, 40, 30, 60},
        {{4, 4}, {R::Main, R::Answer}, {R::Main, R::Variation}, 60, 70, 40},
    };
    static const std::vector<Layout> sixteen{
        {{16}, {R::Main}, {R::Main}, 10, 10, 20},
        {{8, 8}, {R::Main, R::Answer}, {R::Main, R::Variation}, 25, 30, 20},
        {{8, 4, 4}, {R::Main, R::Answer, R::Breakdown}, {R::Main, R::Variation, R::Build}, 10, 10, 10},
        {{4, 4, 8}, {R::Main, R::Answer, R::Main}, {R::Main, R::Variation, R::Main}, 10, 10, 10},
        {{4, 8, 4}, {R::Main, R::Answer, R::Breakdown}, {R::Main, R::Variation, R::Build}, 5, 10, 10},
        {{4, 4, 4, 4},
         {R::Main, R::Answer, R::Main, R::Breakdown},
         {R::Main, R::Variation, R::Main, R::Build},
         40,
         30,
         30},
    };
    static const std::vector<Layout> none;
    return lengthBars == 8 ? eight : lengthBars == 16 ? sixteen : none;
}

int weightOf(const Layout& layout, const std::string& styleId) {
    if (styleId == "melodic_techno") {
        return layout.weightMelodic;
    }
    return styleId == "hard_industrial" ? layout.weightHard : layout.weightPeak;
}

Phrase mainPhrase(uint32_t lengthBars) {
    Phrase phrase;
    phrase.startBar = 0;
    phrase.lengthBars = lengthBars;
    phrase.role = PhraseRole::Main;
    return phrase;
}

} // namespace

std::vector<Phrase> generateFormPlan(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng) {
    const auto& layouts = layoutsFor(lengthBars);
    if (layouts.empty()) {
        return {mainPhrase(lengthBars)};
    }
    std::vector<uint32_t> weights;
    for (const Layout& layout : layouts) {
        weights.push_back(static_cast<uint32_t>(weightOf(layout, style.id)));
    }
    const size_t index = rng.weightedIndex(weights);
    const Layout& layout = layouts[index < layouts.size() ? index : 0];
    const auto& roles = style.id == "melodic_techno" ? layout.melodic : layout.other;
    std::vector<Phrase> phrases;
    uint32_t start = 0;
    for (size_t i = 0; i < layout.bars.size(); ++i) {
        Phrase phrase;
        phrase.startBar = start;
        phrase.lengthBars = layout.bars[i];
        phrase.role = roles[i];
        phrase.kickGridId = phraseKickGrid(phrase.role);
        phrases.push_back(phrase);
        start += layout.bars[i];
    }
    return phrases;
}

bool isValidFormPlan(uint32_t lengthBars, const std::vector<Phrase>& phrases) {
    Pattern pattern = makeEmptyPattern(lengthBars, "");
    pattern.phrases = phrases;
    for (const std::string& issue : validatePattern(pattern)) {
        if (issue.rfind("phrases", 0) == 0) {
            return false;
        }
    }
    return true;
}

int phraseEnergyDelta(PhraseRole role) {
    switch (role) {
    case PhraseRole::Build:
        return 20;
    case PhraseRole::Breakdown:
        return -30;
    default:
        return 0;
    }
}

int phraseCreativityDelta(PhraseRole role) {
    return role == PhraseRole::Variation ? 30 : 0;
}

std::optional<std::string> phraseKickGrid(PhraseRole role) {
    if (role == PhraseRole::Breakdown) {
        return std::string("halftime");
    }
    return std::nullopt;
}

} // namespace mm::core
