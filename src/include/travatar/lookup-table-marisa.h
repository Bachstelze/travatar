#ifndef LOOKUP_TABLE_MARISA_H__
#define LOOKUP_TABLE_MARISA_H__

#include <travatar/lookup-table.h>
#include <marisa/marisa.h>
#include <vector>

namespace travatar {

class HyperNode;

// A table that allows rules to be looked up in a hash table
class LookupTableMarisa : public LookupTable {
public:
    LookupTableMarisa() { }
    virtual ~LookupTableMarisa();

    virtual LookupState * GetInitialState() const {
        return new LookupState;
    }

    static LookupTableMarisa * ReadFromFile(std::string & filename);
    static LookupTableMarisa * ReadFromRuleTable(std::istream & in);

    // Find rules associated with a particular source pattern
    virtual const std::vector<TranslationRule*> * FindRules(const LookupState & state) const;

protected:

    // Match a single node
    virtual LookupState * MatchNode(const HyperNode & node, const LookupState & state) const;

    // Match the start of an edge
    virtual LookupState * MatchStart(const HyperNode & node, const LookupState & state) const;
    
    // Match the end of an edge
    virtual LookupState * MatchEnd(const HyperNode & node, const LookupState & state) const;

    LookupState * MatchState(const std::string & next, const LookupState & state) const;

    // void AddRule(TranslationRule * rule) {
    //     rules_[rule->GetSrcStr()].push_back(rule);
    // }

   typedef std::vector< std::vector<TranslationRule*> > RuleSet; 
   const RuleSet & GetRules() const { return rules_; }
   RuleSet & GetRules() { return rules_; }
   const marisa::Trie & GetTrie() const { return trie_; }
   marisa::Trie & GetTrie() { return trie_; }

protected:
    marisa::Trie trie_;
    RuleSet rules_;

};

}

#endif
