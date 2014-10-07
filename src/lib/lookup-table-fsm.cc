#include <travatar/translation-rule-hiero.h>
#include <travatar/dict.h>
#include <travatar/global-debug.h>
#include <travatar/string-util.h>
#include <travatar/hyper-graph.h>
#include <travatar/lookup-table-fsm.h>
#include <travatar/sentence.h>
#include <travatar/input-file-stream.h>
#include <boost/foreach.hpp>
#include <sstream>

using namespace travatar;
using namespace std;
using namespace boost;

///////////////////////////////////
///     LOOK UP TABLE FSM        //
///////////////////////////////////
LookupTableFSM::LookupTableFSM() : rule_fsms_(),
                   delete_unknown_(false),
                   trg_factors_(1),
                   root_symbol_(HieroHeadLabels(vector<WordId>(1,Dict::WID("S")))),
                   save_src_str_(false) { }

LookupTableFSM::~LookupTableFSM() {
    BOOST_FOREACH(RuleFSM* rule_fsm, rule_fsms_) {
        if(rule_fsm != NULL)
            delete rule_fsm;
    }
}

RuleFSM * RuleFSM::ReadFromRuleTable(istream & in) {
    string line;
    RuleFSM * ret = new RuleFSM;
    UnaryMap & unaries = ret->unaries_;
    while(getline(in, line)) {
        vector<string> columns = Tokenize(line, " ||| ");;
        if(columns.size() < 3)
            THROW_ERROR("Wrong number of columns in rule table, expected at least 3 but got "<<columns.size()<<": " << endl << line);
        CfgData src_data = Dict::ParseAnnotatedWords(columns[0]);
        TranslationRuleHiero * rule = new TranslationRuleHiero(
            Dict::ParseAnnotatedVector(columns[1]),
            Dict::ParseSparseVector(columns[2]),
            src_data
        );
        if(src_data.syms.size() == 1 && src_data.words.size() == 1)
            unaries[rule->GetChildHeadLabels(0)].insert(rule->GetHeadLabels());
        // Sanity check
        BOOST_FOREACH(const CfgData & trg_data, rule->GetTrgData())
            if(trg_data.syms.size() != src_data.syms.size())
                THROW_ERROR("Mismatched number of non-terminals in rule table: " << endl << line);
        if(src_data.words.size() == 0)
            THROW_ERROR("Empty sources in a rule are not allowed: " << endl << line);
        // Add the rule
        ret->AddRule(rule);
    }
    // Expand unary values
    bool added = true;
    while(added) {
        added = false;
        BOOST_FOREACH(UnaryMap::value_type & val, unaries) {
            BOOST_FOREACH(HieroHeadLabels target, val.second) {
                if(val.first == target)
                    THROW_ERROR("Unary cycles are not allowed in CFG grammars, but found one with for label " << Dict::WSym(target[0]) << endl);
                UnaryMap::iterator it = unaries.find(target);
                if(it != unaries.end()) {
                    BOOST_FOREACH(HieroHeadLabels second_trg, it->second) {
                        set<HieroHeadLabels>::iterator it2 = val.second.find(target);
                        if(it2 == val.second.end()) {
                            added = true;
                            val.second.insert(second_trg);
                        } 
                    }
                }
            }
        }
    }
    return ret;
}

void RuleFSM::AddRule(TranslationRuleHiero* rule) {
    RuleFSM::AddRule(0, root_node_, rule);
}

void RuleFSM::AddRule(int position, LookupNodeFSM* target_node, TranslationRuleHiero* rule) {
    Sentence rule_sent = rule->GetSrcData().words;
    WordId key = rule_sent[position];
    HieroHeadLabels nt_key;

    LookupNodeFSM* next_node = NULL;
    if (key < 0)  {
        nt_key = rule->GetChildHeadLabels(-key-1);
        next_node = target_node->FindNTChildNode(nt_key);
    } else {
        next_node = target_node->FindChildNode(key);
    }
    if (next_node == NULL) {
        next_node = new LookupNodeFSM;
        if (key < 0) 
            target_node->AddNTEntry(nt_key, next_node);
        else 
            target_node->AddEntry(key, next_node);
    }
    if (position+1 == (int)rule_sent.size()) {
        next_node->AddRule(rule);
    } else {
        AddRule(position+1, next_node, rule);
    }
}

HyperGraph * LookupTableFSM::TransformGraph(const HyperGraph & graph) const {
    HyperGraph* _graph = new HyperGraph;
    Sentence sent = graph.GetWords();
    HieroRuleSpans span = HieroRuleSpans();
    HieroNodeMap node_map = HieroNodeMap();
    EdgeList edge_list = EdgeList(); 
    // For each starting point
    for(int i = sent.size()-1; i >= 0; i--) {
        // For each grammar, add rules
        BOOST_FOREACH(RuleFSM* rule_fsm, rule_fsms_)
            rule_fsm->BuildHyperGraphComponent(node_map, edge_list, sent, rule_fsm->GetRootNode(), i, span);
    }

    vector<TailSpanKey > temp_spans;
    BOOST_FOREACH(const HieroNodeMap::value_type & val, node_map) {
        HyperNode* node = val.second;
        pair<int,int> node_span = node->GetSpan();
        if (node_span.second - node_span.first == 1) {
            int i = node_span.first;
            if(node->GetEdges().size() == 0) {
                TranslationRuleHiero* unk_rule = GetUnknownRule(delete_unknown_? Dict::WID("") : sent[i],val.first.first);
                HyperEdge* unk_edge = LookupTableFSM::TransformRuleIntoEdge(node_map,i,i+1,temp_spans,unk_rule,save_src_str_);
                edge_list.push_back(unk_edge);
                delete unk_rule;
            } 
        }
        node = NULL;
    }

    // Adding Unknown Edge and Adding word
    for (int i=0; i < (int) sent.size(); ++i) {
        _graph->AddWord(sent[i]);
    }

    // Find the root node
    HieroNodeKey key = make_pair(GetRootSymbol(),make_pair(0,(int)sent.size()));
    HieroNodeMap::iterator big_span_node = node_map.find(key);

    // If the node is not found, delete and return an empty graph
    if(big_span_node == node_map.end()) {
        cerr << "Could not find Span "<<Dict::WSym(GetRootSymbol()[0])<<"[0,"<<sent.size()<<"]"<<endl;
        BOOST_FOREACH (HyperEdge* edges, edge_list) 
            if(edges)
                delete edges;
        BOOST_FOREACH (HieroNodeMap::value_type nodes, node_map)
            delete nodes.second;
        return new HyperGraph;
    } else {
        // Deleting nodes that are unreachable from root node
        // First traverse the root node
        vector<HyperNode*> stack;
        stack.push_back(big_span_node->second);
        while (!stack.empty()) {
            HyperNode* now = stack.back();
            stack.pop_back();
            if (now->GetId() != 0) {
                now->SetId(0); 
                BOOST_FOREACH(HyperEdge* edge, now->GetEdges()) {
                    edge->SetId(0);
                    BOOST_FOREACH(HyperNode* node, edge->GetTails()) {
                        stack.push_back(node);
                    }
                }
            }
        }
        // Delete the edges that are unreachable from root
        EdgeList::iterator it = edge_list.begin();
        while(it != edge_list.end()) {
            if ((*it)->GetId() != 0) {
                it = edge_list.erase(it);
            } else {
                ++it;
            }
        }
        // Delete the nodes that are unreachable from root
        HieroNodeMap::iterator itr = node_map.begin();
        while(itr != node_map.end()) {
            if (itr->second->GetId() != 0) {
                node_map.erase(itr++);
            } else {
                ++itr;
            }
        }
    }

    // Add the root node
    if (big_span_node != node_map.end()) {
        _graph->AddNode(big_span_node->second);
        node_map.erase(big_span_node);
    }
    // Add the rest of the nodes
    BOOST_FOREACH (HieroNodeMap::value_type nodes, node_map) {
        nodes.second->SetId(-1);
        _graph->AddNode(nodes.second);
    }
    
    BOOST_FOREACH (HyperEdge* edges, edge_list) 
        if(edges) {
            edges->SetId(-1);
            _graph->AddEdge(edges);
        } else 
            THROW_ERROR("All edges here should be valid, but found 1 with invalid.");
    return _graph;
}

void RuleFSM::BuildHyperGraphComponent(
        HieroNodeMap & node_map, 
        EdgeList & edge_list, 
        const Sentence & input,
        LookupNodeFSM* node, 
        int position, 
        HieroRuleSpans & spans) const 
{
    if (position >= (int)input.size())
        return;

    // For the nodes that match the words
    LookupNodeFSM* next_node = node->FindChildNode(input[position]);
    if (next_node != NULL) {
        // Add the new rules
        HieroRuleSpans rule_span_next = HieroRuleSpans(spans);
        rule_span_next.push_back(make_pair(position,position+1));
        BOOST_FOREACH(TranslationRuleHiero* rule, next_node->GetTranslationRules()) 
            edge_list.push_back(LookupTableFSM::TransformRuleIntoEdge(rule, rule_span_next, node_map, save_src_str_));
        // Recurse to match the following rules
        BuildHyperGraphComponent(node_map, edge_list, input, next_node, position+1, rule_span_next);
    } 

    // Continue until the end of the sentence or the max span length
    int until = min((int)input.size(), position+span_length_);
    for(int next_pos = position+1; next_pos <= until; next_pos++) {
        pair<int,int> span(position,next_pos);
        // If this is the root, ensure unary nodes are expanded
        if(node == root_node_) {
            set<HieroHeadLabels> next_set;
            int last_size;
            do {
                last_size = next_set.size();
                BOOST_FOREACH(const UnaryMap::value_type & val, unaries_) {
                    pair<HieroHeadLabels,pair<int,int> > labeled_span(val.first, span);
                    if(node_map.find(labeled_span) != node_map.end()) {
                        BOOST_FOREACH(HieroHeadLabels child_lab, val.second) {
                            next_set.insert(child_lab);
                            LookupTableFSM::FindNode(node_map, position, next_pos, child_lab);
                        }
                    }
                }
            } while(last_size != (int)next_set.size());
        }
        // Add the rules
        HieroRuleSpans rule_span_next = HieroRuleSpans(spans);
        rule_span_next.push_back(span);
        BOOST_FOREACH(const NTLookupNodeMap::value_type & next_node, node->GetNTNodeMap()) {
            HieroHeadLabels sym = next_node.first;
            if(span.second - span.first == 1 && node_map.find(make_pair(sym,span)) == node_map.end()) {
                LookupTableFSM::FindNode(node_map, span.first, span.second, sym);
            }
            if(node_map.find(make_pair(sym, span)) != node_map.end()) {
                BOOST_FOREACH(TranslationRuleHiero* rule, next_node.second->GetTranslationRules())
                    edge_list.push_back(LookupTableFSM::TransformRuleIntoEdge(rule, rule_span_next, node_map, save_src_str_));
                // Recurse to match the next node
                BuildHyperGraphComponent(node_map, edge_list, input, next_node.second, next_pos, rule_span_next);
            }
        }
    }

}

HyperEdge* LookupTableFSM::TransformRuleIntoEdge(TranslationRuleHiero* rule, const HieroRuleSpans & rule_span,
    HieroNodeMap & node_map, const bool save_src_str)
{
    vector<int> non_term_position = rule->GetSrcData().GetNontermPositions();
    vector<TailSpanKey > span_temp;
    for (int i=0 ; i < (int)non_term_position.size(); ++i) 
        span_temp.push_back(make_pair(i,rule_span[non_term_position[i]]));
    int head_first = rule_span[0].first;
    int head_second = rule_span[(int)rule_span.size()-1].second;
    return TransformRuleIntoEdge(node_map, head_first, head_second, span_temp, rule, save_src_str);
}

HyperEdge* LookupTableFSM::TransformRuleIntoEdge(HieroNodeMap& node_map, 
        const int head_first, const int head_second, const vector<TailSpanKey> & tail_spans, 
        TranslationRuleHiero* rule, const bool save_src_str)
{
    // // DEBUG start
    // cerr << " TransformRule @ " << make_pair(head_first,head_second) << " ->";
    // BOOST_FOREACH(const TailSpanKey & tsk, tail_spans) {
    //     WordId symid = rule->GetSrcData().GetSym(tsk.first);
    //     cerr << " " << (symid >= 0 ? Dict::WSym(symid) : "NULL") << "---" << tsk.second;
    // }
    // cerr << " ||| " << rule->GetSrcStr() << endl;
    // // DEBUG end

    HyperEdge* hedge = new HyperEdge;
    HyperNode* head = FindNode(node_map, head_first, head_second, rule->GetHeadLabels());
    hedge->SetHead(head);
    if (save_src_str)
        hedge->SetSrcStr(Dict::PrintAnnotatedWords(rule->GetSrcData()));
    hedge->SetRule(rule);
    head->AddEdge(hedge);
    TailSpanKey tail_span;
    BOOST_FOREACH(tail_span, tail_spans) {
        HyperNode* tail = FindNode(node_map, tail_span.second.first, tail_span.second.second, rule->GetChildHeadLabels(tail_span.first));
        tail->SetSpan(tail_span.second);
        hedge->AddTail(tail);
        tail = NULL;
    }
    head = NULL;
    return hedge;
}

// Get an HyperNode, indexed by its span in some map.
HyperNode* LookupTableFSM::FindNode(HieroNodeMap& map_ptr, 
        const int span_begin, const int span_end, const HieroHeadLabels& head_label)
{
    if (span_begin < 0 || span_end < 0) 
        THROW_ERROR("Invalid span range in constructing HyperGraph.");
    pair<int,int> span = make_pair(span_begin,span_end);

    HieroNodeKey key = make_pair(head_label, span);
    HieroNodeMap::iterator it = map_ptr.find(key);
    if (it != map_ptr.end()) {
        return it->second;
    } else {
        // Fresh New Node!
        HyperNode* ret = new HyperNode;
        ret->SetSpan(make_pair(span_begin,span_end));
        ret->SetSym(head_label[0]);
        map_ptr.insert(make_pair(key,ret));
        return ret;
    }
}

TranslationRuleHiero* LookupTableFSM::GetUnknownRule(WordId unknown_word, const HieroHeadLabels& head_labels) 
{
    CfgDataVector target;
    for (int i=1; i < (int)head_labels.size(); ++i) 
        target.push_back(CfgData(Sentence(1,unknown_word),head_labels[i]));
    return new TranslationRuleHiero(
        target,
        Dict::ParseSparseVector("unk=1"),
        CfgData(Sentence(1, unknown_word), head_labels[0])
    );
}

LookupTableFSM * LookupTableFSM::ReadFromFiles(const std::vector<std::string> & filenames) {
    LookupTableFSM * ret = new LookupTableFSM;
    BOOST_FOREACH(const std::string & filename, filenames) {
        InputFileStream tm_in(filename.c_str());
        cerr << "Reading TM file from "<<filename<<"..." << endl;
        if(!tm_in)
            THROW_ERROR("Could not find TM: " << filename);
        ret->AddRuleFSM(RuleFSM::ReadFromRuleTable(tm_in));
    }
    return ret;
}

void LookupTableFSM::SetSpanLimits(const std::vector<int>& limits) {
    if(limits.size() != rule_fsms_.size())
        THROW_ERROR("The number of span limits (" << limits.size() << ") must be equal to the number of tm_files ("<<rule_fsms_.size()<<")");
    for(int i = 0; i < (int)limits.size(); i++)
        rule_fsms_[i]->SetSpanLimit(limits[i]);
}

void LookupTableFSM::SetSaveSrcStr(const bool save_src_str) {
    save_src_str_ = save_src_str;
    BOOST_FOREACH(RuleFSM* rfsm, rule_fsms_) 
        rfsm->SetSaveSrcStr(save_src_str);
}


///////////////////////////////////
///     LOOK UP NODE FSM         //
///////////////////////////////////
void LookupNodeFSM::AddEntry(const WordId & key, LookupNodeFSM* child_node) {
    lookup_map_[key] = child_node;
}

void LookupNodeFSM::AddNTEntry(const HieroHeadLabels& key, LookupNodeFSM* child_node) {
    nt_lookup_map_[key] = child_node;
}

void LookupNodeFSM::AddRule(TranslationRuleHiero* rule) {
    rules_.push_back(rule);
}

LookupNodeFSM* LookupNodeFSM::FindChildNode(const WordId key) const {
    LookupNodeMap::const_iterator it = lookup_map_.find(key); 
    return it != lookup_map_.end() ? it->second : NULL;
}

LookupNodeFSM* LookupNodeFSM::FindNTChildNode(const HieroHeadLabels& key) const {
    NTLookupNodeMap::const_iterator it = nt_lookup_map_.find(key);
    return it != nt_lookup_map_.end() ? it -> second : NULL;
}

void LookupNodeFSM::Print(std::ostream &out, WordId label, int indent, char prefix) const {
    float middle = lookup_map_.size() / 2;
    int i=0;
    char c_prefix = '/';
    BOOST_FOREACH(const LookupNodeMap::value_type &it, lookup_map_) {
        if (i++ == middle) {
            for (int j=0; j < indent; ++j) out << " ";
            out << prefix << Dict::WSym(label) << endl;
            c_prefix = '\\';
        }
        it.second->Print(out,it.first < 0 ? -it.first : it.first, indent+6, c_prefix);
    }
    out << endl; 
}

LookupNodeFSM::~LookupNodeFSM() { 
    BOOST_FOREACH(LookupNodeMap::value_type &it, lookup_map_) {
        delete it.second++;
    }
    BOOST_FOREACH(TranslationRuleHiero* rule, rules_) {
        delete rule;
    }
}
