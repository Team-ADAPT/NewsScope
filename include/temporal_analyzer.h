#ifndef NEWSSCOPE_TEMPORAL_ANALYZER_H
#define NEWSSCOPE_TEMPORAL_ANALYZER_H

#include "types.h"
#include <string>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace newsscope {

class TemporalAnalyzer {
public:
    TemporalAnalyzer();
    
    void set_window_duration(std::chrono::seconds duration);
    void add_entry(const std::string& term, int frequency,
                   std::chrono::system_clock::time_point timestamp);
    double calculate_spike_severity(const std::string& term);
    std::vector<std::pair<std::string, double>> get_detected_spikes();
    double get_spike_score();
    double get_average_frequency(const std::string& term);
    void clear();
    
private:
    std::deque<TimeWindowEntry> time_window;
    std::chrono::seconds window_duration;
    
    void cleanup_expired_entries();
    double calculate_frequency_stats(const std::string& term, double& mean, double& stddev);
};

}

#endif
