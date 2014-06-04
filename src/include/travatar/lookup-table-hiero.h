// TODO: Delete this? To be superseded by lookup-table-fsm

#ifndef LOOKUP_TABLE_HIERO_H__
#define LOOKUP_TABLE_HIERO_H__

#include <vector>
#include <boost/shared_ptr.hpp>
#include <travatar/sparse-map.h>
#include <travatar/translation-rule-hiero.h>
#include <travatar/dict.h>
#include <travatar/hyper-graph.h>
#include <travatar/graph-transformer.h>
#include <generic-string.h>
#include <deque>

typedef std::deque<std::pair<int,int> > HieroRuleSpans;

namespace travatar {

class LookupNodeHiero {
typedef std::map<GenericString<WordId>, LookupNodeHiero*> NodeMap;
public:
	LookupNodeHiero() { 
		lookup_map = NodeMap(); 
	}

	virtual ~LookupNodeHiero() { 
		BOOST_FOREACH(NodeMap::value_type &it, lookup_map) {
			delete it.second++;
		}
		BOOST_FOREACH(TranslationRuleHiero* rule, rules) {
			delete rule;
		}
	}
	
	virtual void AddEntry(GenericString<WordId> & key, LookupNodeHiero* rule);
	virtual LookupNodeHiero* FindNode(GenericString<WordId> & key) const;
	virtual void AddRule(TranslationRuleHiero* rule);
	virtual std::string ToString() const;
	std::vector<TranslationRuleHiero*> & GetTranslationRules() { return rules; }
	
protected:
	NodeMap lookup_map;
	std::vector<TranslationRuleHiero*> rules;
private:
	std::string ToString(int indent) const;
};	

class LookupTableHiero : public GraphTransformer {
typedef std::deque<std::pair<int,int> > HieroRuleSpans;
public:
	LookupTableHiero() {
		root_node = new LookupNodeHiero;
        Sentence trg_sent(2); trg_sent[0] = -1; trg_sent[1] = -2;
		SparseMap features = Dict::ParseFeatures("glue=0.5");
		glue_rule = new TranslationRuleHiero(
            "x0 x1",
            CfgDataVector(GlobalVars::trg_factors, CfgData(trg_sent)),
            features,
            trg_sent
        );
        delete_unknown = false;
        span_length = 20;
	}

	virtual ~LookupTableHiero() { 
		delete root_node;
		delete glue_rule;
	}
	
	static LookupTableHiero * ReadFromRuleTable(std::istream & in);

	static TranslationRuleHiero * BuildRule(travatar::TranslationRuleHiero * rule, std::vector<std::string> & source, 
			std::vector<std::string> & target, SparseMap features);

	virtual HyperGraph * BuildHyperGraph(const Sentence & input) const;
	virtual HyperGraph * TransformGraph(const HyperGraph & graph) const;

	virtual void AddRule(TranslationRuleHiero* rule);
	
	TranslationRuleHiero* GetGlueRule() { return glue_rule; }

	virtual std::string ToString() const;

	virtual std::vector<std::pair<TranslationRuleHiero*, HieroRuleSpans* > > FindRules(const Sentence & input) const;

	TranslationRuleHiero* GetUnknownRule(WordId unknown_word) const;

	void SetSpanLimit(int length) { span_length = length; }
	void SetDeleteUnknown(bool del) { delete_unknown = del; }
	int GetSpanLimit() const { return span_length; } 
	bool GetDeleteUnknown() const { return delete_unknown; }
protected:
	LookupNodeHiero* root_node;
	TranslationRuleHiero* glue_rule;
 	int span_length;
 	bool delete_unknown;
private:
	void AddGlueRule(int start, int end, HyperGraph* ret, std::map<std::pair<int,int>, HyperNode*>* node_map, 
			std::vector<std::pair<int,int> >* span_temp, std::set<GenericString<WordId> >* edge_set) const;

	void AddRule(int position, LookupNodeHiero* target_node, TranslationRuleHiero* rule);
	std::vector<std::pair<TranslationRuleHiero*, HieroRuleSpans* > > FindRules(LookupNodeHiero* node, const Sentence & input, const int start, int depth) const;
	HyperNode* FindNode(std::map<std::pair<int,int>, HyperNode*>* map_ptr, const int span_begin, const int span_end) const;

	GenericString<WordId> TransformSpanToKey(const int xbegin, const int xend, const std::vector<std::pair<int,int> > & tail_spans) const;

	HyperEdge* TransformRuleIntoEdge(std::map<std::pair<int,int>, HyperNode*>* map, const int head_first, 
			const int head_second, const std::vector<std::pair<int,int> > & tail_spans, TranslationRuleHiero* rule) const;
};



}

#endif