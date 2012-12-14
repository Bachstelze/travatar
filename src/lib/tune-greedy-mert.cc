#include <cfloat>
#include <map>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <travatar/tuning-example.h>
#include <travatar/tune-greedy-mert.h>
#include <travatar/util.h>
#include <travatar/dict.h>
#include <travatar/sparse-map.h>
#include <travatar/output-collector.h>

using namespace std;
using namespace boost;
using namespace travatar;

#define MARGIN 1

void GreedyMertTask::Run() {
    // Check to make sure that our potential is still higher than the current best
    double best = tgm_->GetBestGain();
    if(potential_ <= best)
        return;
    // Mak the gradient and find the ranges
    SparseMap gradient;
    gradient[feature_] = 1;
    pair<double,double> gradient_range = tgm_->FindGradientRange(feature_);
    LineSearchResult result = tgm_->LineSearch(tgm_->GetWeights(), gradient, gradient_range);
    if(result.gain > best) {
        tgm_->UpdateBest(gradient, result);
        best = result.gain;
    }
    ostringstream oss;
    oss << "gain?("<<Dict::WSym(feature_)<<")=" << potential_ << " --> gain@" << result.pos <<"="<< result.gain << ", score="<<result.before<<"-->"<<result.after<<" (max: " << best << ")" << endl;
    if(collector_)
        collector_->Write(id_, oss.str(), "");
    else
        cerr << oss.str();
}

void TuneGreedyMert::UpdateBest(const SparseMap &gradient, const LineSearchResult &result) {
    mutex::scoped_lock lock(result_mutex_);
    if(best_result_.gain < result.gain) {
        best_gradient_ = gradient;
        best_result_ = result;
        cerr << "NEW MAX: " << Dict::WSym(gradient.begin()->first) << "=" << result.gain << endl;
    }
}

pair<double,double> TuneGreedyMert::FindGradientRange(WordId feat) {
    RangeMap::const_iterator it = ranges_.find(feat);
    pair<double,double> range = (it == ranges_.end() ? ranges_[-1] : it->second);
    SparseMap gradient; gradient[feat] = 1;
    return FindGradientRange(weights_, gradient, range);
}

LineSearchResult TuneGreedyMert::LineSearch(

                const SparseMap & weights,
                const SparseMap & gradient,
                pair<double,double> range) {
    double base_score = 0;
    map<double,double> boundaries;
    typedef pair<double,double> DoublePair;
    // Create the search plane
    BOOST_FOREACH(const shared_ptr<TuningExample> & examp, examps_) {
        // Calculate the convex hull
        ConvexHull convex_hull = examp->CalculateConvexHull(weights, gradient);
        PRINT_DEBUG("Convex hull size == " << convex_hull.size() << endl, 2);
        if(convex_hull.size() == 0) continue;
        // Update the base values
        base_score += convex_hull[0].second;
        // Add all the changed values
        for(int i = 1; i < (int)convex_hull.size(); i++) {
            double diff = convex_hull[i].second-convex_hull[i-1].second;
            if(diff != 0)
                boundaries[convex_hull[i].first.first] += diff;
        }
    }
    boundaries[DBL_MAX] = -DBL_MAX;
    // if(boundaries.size() == 0) return make_pair(-DBL_MAX, -DBL_MAX);
    // Find the place with the best score on the plane
    ScoredSpan best_span(Span(-DBL_MAX, -DBL_MAX), 0);
    double last_bound = -DBL_MAX, curr_score = base_score;
    double zero_score = DBL_MAX;
    BOOST_FOREACH(const DoublePair & boundary, boundaries) {
        // Find the score at zero. If there is a boundary directly at zero, break ties
        // to the less optimistic side (or gain to the optimistic side)
        if(last_bound <= 0 && boundary.first >= 0)
            zero_score = curr_score;
        // Update the span if it exceeds the previous best and is in the acceptable gradient range
        if(curr_score > best_span.second && (last_bound < range.second && boundary.first > range.first))
            best_span = ScoredSpan(Span(last_bound, boundary.first), curr_score);
        // cerr << "bef: " << boundary << " curr_score=" << curr_score << endl;
        curr_score += boundary.second;
        // cerr << "aft: " << boundary << " curr_score=" << curr_score << endl;
        last_bound = boundary.first;
    }
    // Given the best span, find the middle
    double middle;
    if(best_span.first.first < 0 && best_span.first.second > 0)
        middle = 0;
    else if (best_span.first.first == -DBL_MAX)
        middle = best_span.first.second - MARGIN;
    else if (best_span.first.second == DBL_MAX)
        middle = best_span.first.first + MARGIN;
    else
        middle = (best_span.first.first+best_span.first.second)/2;
    middle = max(range.first, min(middle, range.second));
    PRINT_DEBUG("0 --> " << zero_score << ", " << middle << " --> " << best_span.second << endl, 2);
    return LineSearchResult(middle, zero_score, best_span.second);
}

// Current value can be found here
// range(-2,2)
// original(-1)
// change(-1,3)
// gradient -2
// +0.5, -1.5
pair<double,double> TuneGreedyMert::FindGradientRange(
                                const SparseMap & weights,
                                const SparseMap & gradient,
                                pair<double,double> range) {
    pair<double,double> ret(-DBL_MAX, DBL_MAX);
    BOOST_FOREACH(const SparseMap::value_type & grad, gradient) {
        if(grad.second == 0) continue;
        SparseMap::const_iterator it = weights.find(grad.first);
        double w = (it != weights.end()) ? it->second : 0.0;
        double l = (range.first-w)/grad.second;
        double r = (range.second-w)/grad.second;
        if(l > r) { double temp = l; l = r; r = temp; }
        ret.first = max(l, ret.first);
        ret.second = min(r, ret.second);
    }
    return ret; 
}

// Find the best value to tune and tune it
double TuneGreedyMert::TuneOnce() {
    best_result_ = LineSearchResult();
    best_gradient_ = SparseMap();
    PRINT_DEBUG("Calculating potential gains..." << endl, 1);
    SparseMap potential;
    BOOST_FOREACH(const shared_ptr<TuningExample> & examp, examps_)
        potential += examp->CalculatePotentialGain(weights_);
    // Order the weights in descending order
    typedef pair<double,int> DIPair;
    vector<DIPair> vals;
    BOOST_FOREACH(const SparseMap::value_type val, potential)
        if(val.second >= gain_threshold_)
            vals.push_back(make_pair(val.second, val.first));
    sort(vals.begin(), vals.end());
    // Dispatch jobs until the best value exceeds the expected value
    BOOST_REVERSE_FOREACH(const DIPair & val, vals) {
        if(val.first < best_result_.gain)
            break;
        GreedyMertTask* task = new GreedyMertTask(task_id_++, *this, val.second, val.first, out_collect_);
        // If the threads are not correct
        if(thread_pool_)
            thread_pool_->Submit(task);
        else
            task->Run();
    }
    // Update with the best value
    if(best_result_.gain > gain_threshold_) {
        PRINT_DEBUG("Updating: " << Dict::PrintFeatures(best_gradient_) << " * " << best_result_.pos << endl, 0);
        weights_ += best_gradient_ * best_result_.pos;
    }
    PRINT_DEBUG("Features: " << Dict::PrintFeatures(weights_) << endl, 0);
    return best_result_.gain;
}

// Tune new weights using greedy mert until the threshold is exceeded
void TuneGreedyMert::Tune() {
    while (TuneOnce() > gain_threshold_);
}