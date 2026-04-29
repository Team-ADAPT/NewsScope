#include "temporal_analyzer.h"
#include <cmath>
#include <algorithm>

namespace newsscope {

TemporalAnalyzer::TemporalAnalyzer() 
    : window_duration(std::chrono::hours(24)) {}

void TemporalAnalyzer::set_window_duration(std::chrono::seconds duration) {
    std::lock_guard<std::mutex> lock(window_mutex);
    window_duration = duration;
    cleanup_expired_entries();
}

void TemporalAnalyzer::cleanup_expired_entries() {
    auto now = std::chrono::system_clock::now();
    
    while (!time_window.empty()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - time_window.front().timestamp
        );
        
        if (age > window_duration) {
            time_window.pop_front();
        } else {
            break;
        }
    }
}

void TemporalAnalyzer::add_entry(const std::string& term, int frequency,
                                 std::chrono::system_clock::time_point timestamp) {
    std::lock_guard<std::mutex> lock(window_mutex);
    cleanup_expired_entries();
    time_window.push_back({timestamp, term, frequency});
}

double TemporalAnalyzer::calculate_spike_severity(const std::string& term) {
    std::lock_guard<std::mutex> lock(window_mutex);
    if (time_window.empty()) {
        return 0.0;
    }
    
    cleanup_expired_entries();
    
    double mean = 0.0;
    double stddev = 0.0;
    double severity = calculate_frequency_stats(term, mean, stddev);
    
    return severity;
}

double TemporalAnalyzer::calculate_frequency_stats(const std::string& term,
                                                   double& mean, double& stddev) {
    std::vector<int> frequencies;
    
    for (const auto& entry : time_window) {
        if (entry.term == term) {
            frequencies.push_back(entry.frequency);
        }
    }
    
    if (frequencies.empty()) {
        mean = 0.0;
        stddev = 0.0;
        return 0.0;
    }
    
    // Calculate mean
    double sum = 0.0;
    for (int freq : frequencies) {
        sum += freq;
    }
    mean = sum / frequencies.size();
    
    // Calculate standard deviation
    double variance = 0.0;
    for (int freq : frequencies) {
        double diff = freq - mean;
        variance += diff * diff;
    }
    variance /= frequencies.size();
    stddev = std::sqrt(variance);
    
    // Spike severity: how far is recent entry from mean
    if (!frequencies.empty() && stddev > 0) {
        int latest = frequencies.back();
        double z_score = (latest - mean) / stddev;
        // Convert z-score to 0-1 range (assuming z > 3 is extreme)
        return std::min(1.0, std::max(0.0, z_score / 5.0));
    }
    
    return 0.0;
}

std::vector<std::pair<std::string, double>> TemporalAnalyzer::get_detected_spikes() {
    std::lock_guard<std::mutex> lock(window_mutex);
    cleanup_expired_entries();

    // Single pass: collect per-term frequencies
    std::unordered_map<std::string, std::vector<int>> term_freqs;
    for (const auto& entry : time_window) {
        term_freqs[entry.term].push_back(entry.frequency);
    }

    std::vector<std::pair<std::string, double>> spikes;
    for (auto& [term, freqs] : term_freqs) {
        if (freqs.size() < 2) continue;

        double sum = 0.0;
        for (int f : freqs) sum += f;
        double mean = sum / freqs.size();

        double variance = 0.0;
        for (int f : freqs) { double d = f - mean; variance += d * d; }
        double stddev = std::sqrt(variance / freqs.size());

        if (stddev <= 0.0) continue;
        double z = (freqs.back() - mean) / stddev;
        double severity = std::min(1.0, std::max(0.0, z / 5.0));
        if (severity > 0.3) {
            spikes.push_back({term, severity});
        }
    }
    return spikes;
}

double TemporalAnalyzer::get_spike_score() {
    auto spikes = get_detected_spikes(); // gets its own lock
    
    if (spikes.empty()) {
        return 0.0;  // No spike activity means no temporal suspicion
    }
    
    double total_severity = 0.0;
    for (const auto& spike : spikes) {
        total_severity += spike.second;
    }
    
    // Average severity scaled to 0-100
    double avg_severity = total_severity / spikes.size();
    return std::min(100.0, avg_severity * 100.0);
}

double TemporalAnalyzer::get_average_frequency(const std::string& term) {
    std::lock_guard<std::mutex> lock(window_mutex);
    if (time_window.empty()) {
        return 0.0;
    }
    
    cleanup_expired_entries();
    
    double total = 0.0;
    int count = 0;
    
    for (const auto& entry : time_window) {
        if (entry.term == term) {
            total += entry.frequency;
            count++;
        }
    }
    
    return (count > 0) ? total / count : 0.0;
}

void TemporalAnalyzer::clear() {
    std::lock_guard<std::mutex> lock(window_mutex);
    time_window.clear();
}

} // namespace newsscope
