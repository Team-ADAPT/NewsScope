#ifndef NEWSSCOPE_SOURCE_VALIDATOR_H
#define NEWSSCOPE_SOURCE_VALIDATOR_H

#include "types.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace newsscope {

class SourceValidator {
public:
    enum class SourceStatus { TRUSTED = 2, NEUTRAL = 1, UNTRUSTED = 0 };
    
    SourceValidator();
    
    double validate_source(const std::string& source_name);
    void add_trusted_source(const std::string& source_name, double credibility = 80.0);
    void add_untrusted_source(const std::string& source_name, double credibility = 20.0);
    void add_neutral_source(const std::string& source_name, double credibility = 50.0);
    bool source_exists(const std::string& source_name) const;
    SourceStatus get_source_status(const std::string& source_name) const;
    bool load_from_csv(const std::string& filename);
    std::unordered_map<std::string, double> get_all_sources() const;
    void clear();
    
private:
    std::unordered_map<std::string, double> sources;
    double unknown_source_credibility = 50.0;
    std::string normalize_source_name(const std::string& source_name) const;
};

}

#endif
