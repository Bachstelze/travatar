#ifndef TRANSLATION_RULE_HIERO_H__
#define TRANSLATION_RULE_HIERO_H__

#include <string>
#include <vector>
#include <travatar/sentence.h>
#include <travatar/dict.h>
#include <travatar/sparse-map.h>
#include <travatar/translation-rule.h>

namespace travatar {
class TranslationRuleHiero : public TranslationRule {
public: 

    TranslationRuleHiero(const std::string & src_str = "",
                    const CfgDataVector & trg_data = CfgDataVector(),
                    const SparseMap & features = SparseMap(),
                    const CfgData & src_data = Sentence()
                    ) : TranslationRule(src_str, trg_data, features),
                        src_data_(src_data) { }

    std::string ToString();

    virtual bool operator==(const TranslationRuleHiero & rhs) const {
        return
            trg_data_ == rhs.trg_data_ &&
            features_ == rhs.features_ &&
            src_data_ == rhs.src_data_;
    }

    // ACCESSOR
    CfgData & GetSrcData() { return src_data_; }

protected:
	CfgData src_data_;
};

}
#endif