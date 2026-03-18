#include "scoring_engine.h"
#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace newsscope;

namespace {

enum class JobStatus { QUEUED, PROCESSING, DONE, FAILED };

struct JobRecord {
    JobStatus status = JobStatus::QUEUED;
    CredibilityResult result{};
    std::string label;
    std::string label_reason;
    std::string error;
    std::chrono::steady_clock::time_point updated_at = std::chrono::steady_clock::now();
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::atomic<bool> g_running{true};
int g_server_fd = -1;
constexpr size_t MAX_IN_FLIGHT_JOBS = 2000;
constexpr size_t MAX_STORED_FINISHED_JOBS = 5000;
const auto FINISHED_JOB_TTL = std::chrono::minutes(10);

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string read_file(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    return "";
}

std::string resolve_data_file(const std::string& filename) {
    const std::vector<std::string> candidates = {
        "data/" + filename,
        "../data/" + filename,
        "../../data/" + filename
    };
    for (const auto& path : candidates) {
        std::ifstream in(path);
        if (in.is_open()) {
            return path;
        }
    }
    return "";
}

std::string first_line_headline(const std::string& text) {
    std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return "Untitled";
    }
    size_t newline = cleaned.find('\n');
    std::string headline = newline == std::string::npos ? cleaned : cleaned.substr(0, newline);
    headline = trim(headline);
    if (headline.empty()) {
        return "Untitled";
    }
    if (headline.size() > 120) {
        headline = headline.substr(0, 120);
    }
    return headline;
}

std::string body_without_headline(const std::string& text) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return "";
    }

    const size_t newline = cleaned.find('\n');
    if (newline == std::string::npos) {
        return "";
    }

    const std::string remainder = trim(cleaned.substr(newline + 1));
    return remainder.empty() ? trim(cleaned.substr(0, newline)) : remainder;
}

std::string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 429: return "Too Many Requests";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void send_response(int fd, int code, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << code << " " << status_text(code) << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "Cache-Control: no-store\r\n\r\n";
    response << body;
    (void)send_all(fd, response.str());
}

size_t extract_content_length(const std::string& header_blob) {
    std::istringstream iss(header_blob);
    std::string line;
    (void)std::getline(iss, line);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t sep = line.find(':');
        if (sep == std::string::npos) {
            continue;
        }
        std::string key = to_lower(trim(line.substr(0, sep)));
        std::string value = trim(line.substr(sep + 1));
        if (key == "content-length") {
            try {
                return static_cast<size_t>(std::stoull(value));
            } catch (const std::exception&) {
                return 0;
            }
        }
    }
    return 0;
}

bool read_http_request(int fd, std::string& raw) {
    raw.clear();
    char buffer[4096];
    const size_t max_size = 2 * 1024 * 1024;

    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > max_size) {
            return false;
        }
        header_end = raw.find("\r\n\r\n");
    }

    const std::string headers = raw.substr(0, header_end + 4);
    const size_t content_length = extract_content_length(headers);
    const size_t expected_total = header_end + 4 + content_length;
    while (raw.size() < expected_total) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > max_size) {
            return false;
        }
    }
    return true;
}

bool parse_http_request(const std::string& raw, HttpRequest& request) {
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    std::istringstream header_stream(raw.substr(0, header_end));
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        return false;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    std::string version;
    request_line_stream >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty()) {
        return false;
    }

    size_t query_pos = request.path.find('?');
    if (query_pos != std::string::npos) {
        request.path = request.path.substr(0, query_pos);
    }

    std::string line;
    while (std::getline(header_stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t sep = line.find(':');
        if (sep == std::string::npos) {
            continue;
        }
        request.headers[to_lower(trim(line.substr(0, sep)))] = trim(line.substr(sep + 1));
    }

    request.body = raw.substr(header_end + 4);
    return true;
}

std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t pos = body.find(token);
    if (pos == std::string::npos) {
        return "";
    }
    pos = body.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) {
        return "";
    }
    ++pos;

    std::string value;
    bool escaped = false;
    for (; pos < body.size(); ++pos) {
        char c = body[pos];
        if (escaped) {
            switch (c) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(c); break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        value.push_back(c);
    }
    return value;
}

std::string job_status_to_string(JobStatus status) {
    switch (status) {
        case JobStatus::QUEUED: return "queued";
        case JobStatus::PROCESSING: return "processing";
        case JobStatus::DONE: return "done";
        case JobStatus::FAILED: return "failed";
        default: return "unknown";
    }
}

struct VerdictDecision {
    std::string label;
    std::string reason;
};

VerdictDecision classify_verdict(const CredibilityResult& result) {
    auto module_value = [&result](const std::string& key) -> double {
        auto it = result.module_scores.find(key);
        return it == result.module_scores.end() ? 50.0 : it->second;
    };

    const double overall = result.overall_score;
    const double source = module_value("source_validation");
    const double claim = module_value("claim_verifiability");
    const double phrase = module_value("phrase_indexing");
    const double kmp = module_value("kmp_matching");
    const double rabin = module_value("rabin_karp");
    const double greedy = module_value("greedy_filtering");
    const double preprocessing = module_value("preprocessing");

    const bool low_risk_structure =
        kmp >= 78.0 &&
        rabin >= 80.0 &&
        phrase >= 80.0 &&
        greedy >= 75.0;

    const bool strong_fake_signal =
        overall < 45.0 ||
        (greedy < 20.0 && source <= 55.0) ||
        (source < 25.0 && overall < 65.0) ||
        (claim < 30.0 && !low_risk_structure);

    if (strong_fake_signal) {
        return {"Likely Fake", "Strong credibility-risk signals detected"};
    }
    (void)preprocessing;
    return {"Likely Original", "No strong fake-news signal combination detected"};
}

class NewsScopeWebServer {
public:
    explicit NewsScopeWebServer(int port, size_t workers)
        : port_(port), io_pool_(workers), analysis_pool_(workers) {
        jobs_.reserve(4096);
    }

    void run() {
        g_server_fd = create_server_socket();
        if (g_server_fd < 0) {
            std::cerr << "Failed to create server socket\n";
            return;
        }

        std::cout << "NewsScope web portal started at http://localhost:" << port_ << "\n";
        std::cout << "Press Ctrl+C to stop.\n";

        while (g_running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (client_fd < 0) {
                if (errno == EINTR && !g_running.load()) {
                    break;
                }
                continue;
            }

            io_pool_.enqueue([this, client_fd]() { handle_client(client_fd); });
        }

        if (g_server_fd >= 0) {
            close(g_server_fd);
            g_server_fd = -1;
        }
    }

private:
    int port_;
    ThreadPool io_pool_;
    ThreadPool analysis_pool_;
    std::atomic<unsigned long long> next_job_id_{1};
    std::atomic<size_t> in_flight_count_{0};
    std::unordered_map<std::string, JobRecord> jobs_;
    std::mutex jobs_mutex_;

    void prune_finished_jobs_locked() {
        if (jobs_.size() <= MAX_STORED_FINISHED_JOBS) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            const bool is_finished = (it->second.status == JobStatus::DONE || it->second.status == JobStatus::FAILED);
            const bool is_expired = (now - it->second.updated_at) > FINISHED_JOB_TTL;
            if (is_finished && is_expired) {
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t in_flight_jobs_locked() const {
        return in_flight_count_.load();
    }

    static int create_server_socket_impl(int port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(fd);
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }

        if (listen(fd, 64) < 0) {
            close(fd);
            return -1;
        }

        return fd;
    }

    int create_server_socket() const {
        return create_server_socket_impl(port_);
    }

    std::string load_asset(const std::string& filename, const std::string& fallback) const {
        const std::vector<std::string> paths = {
            "web/" + filename,
            "../web/" + filename,
            "../../web/" + filename
        };
        std::string content = read_file(paths);
        return content.empty() ? fallback : content;
    }

    void handle_client(int client_fd) {
        std::string raw;
        if (!read_http_request(client_fd, raw)) {
            close(client_fd);
            return;
        }

        HttpRequest request;
        if (!parse_http_request(raw, request)) {
            send_response(client_fd, 400, "application/json", "{\"error\":\"Invalid HTTP request\"}");
            close(client_fd);
            return;
        }

        route_request(client_fd, request);
        close(client_fd);
    }

    void route_request(int fd, const HttpRequest& request) {
        static const std::string html_fallback =
            "<!doctype html><html><body><h1>NewsScope</h1><p>Static assets missing.</p></body></html>";
        static const std::string css_fallback = "body{font-family:sans-serif;margin:2rem;}";
        static const std::string js_fallback = "console.log('app.js missing');";

        if (request.method == "GET" && (request.path == "/" || request.path == "/index.html")) {
            send_response(fd, 200, "text/html; charset=utf-8", load_asset("index.html", html_fallback));
            return;
        }

        if (request.method == "GET" && request.path == "/styles.css") {
            send_response(fd, 200, "text/css; charset=utf-8", load_asset("styles.css", css_fallback));
            return;
        }

        if (request.method == "GET" && request.path == "/app.js") {
            send_response(fd, 200, "application/javascript; charset=utf-8", load_asset("app.js", js_fallback));
            return;
        }

        if (request.method == "POST" && request.path == "/api/jobs") {
            handle_submit_job(fd, request);
            return;
        }

        const std::string prefix = "/api/jobs/";
        if (request.method == "GET" && request.path.rfind(prefix, 0) == 0) {
            handle_job_status(fd, request.path.substr(prefix.size()));
            return;
        }

        if (request.path.rfind("/api/", 0) == 0) {
            send_response(fd, 405, "application/json", "{\"error\":\"Unsupported API method\"}");
            return;
        }

        send_response(fd, 404, "application/json", "{\"error\":\"Not found\"}");
    }

    void handle_submit_job(int fd, const HttpRequest& request) {
        const std::string text = trim(extract_json_string(request.body, "text"));
        const std::string source = trim(extract_json_string(request.body, "source"));

        if (text.empty()) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Missing text. Send JSON: {\\\"text\\\":\\\"...\\\",\\\"source\\\":\\\"...\\\"}\"}");
            return;
        }

        const std::string job_id = std::to_string(next_job_id_.fetch_add(1));
        bool overloaded = false;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            prune_finished_jobs_locked();
            if (in_flight_jobs_locked() >= MAX_IN_FLIGHT_JOBS) {
                overloaded = true;
            } else {
                jobs_.emplace(job_id, JobRecord{});
            }
        }
        if (overloaded) {
            send_response(fd, 429, "application/json",
                          "{\"error\":\"Server busy. Too many background jobs. Please retry shortly.\"}");
            return;
        }
        in_flight_count_.fetch_add(1);

        analysis_pool_.enqueue([this, job_id, text, source]() { run_analysis_job(job_id, text, source); });

        std::ostringstream out;
        out << "{\"job_id\":\"" << job_id << "\",\"status\":\"queued\"}";
        send_response(fd, 202, "application/json", out.str());
    }

    void run_analysis_job(const std::string& job_id, const std::string& text, const std::string& source) {
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::PROCESSING;
            it->second.updated_at = std::chrono::steady_clock::now();
        }

        try {
            thread_local ScoringEngine engine;
            thread_local bool initialized = false;
            if (!initialized) {
                engine.initialize(
                    resolve_data_file("sources.csv"),
                    resolve_data_file("suspicious_phrases.txt"),
                    resolve_data_file("negative_terms.csv")
                );
                initialized = true;
            }

            const std::string final_source = source.empty() ? "Unknown Source" : source;
            const std::string headline = first_line_headline(text);
            const std::string body = body_without_headline(text);
            const Article article(job_id, headline, body, final_source);
            CredibilityResult result = engine.assess_article(article);
            const VerdictDecision verdict = classify_verdict(result);
            result.explanations.push_back("[Verdict] " + verdict.reason);

            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::DONE;
            it->second.result = result;
            it->second.label = verdict.label;
            it->second.label_reason = verdict.reason;
            it->second.updated_at = std::chrono::steady_clock::now();
            in_flight_count_.fetch_sub(1);
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::FAILED;
            it->second.error = ex.what();
            it->second.updated_at = std::chrono::steady_clock::now();
            in_flight_count_.fetch_sub(1);
        }
    }

    void handle_job_status(int fd, const std::string& job_id) {
        JobRecord record;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                send_response(fd, 404, "application/json", "{\"error\":\"Job not found\"}");
                return;
            }
            record = it->second;
        }

        std::ostringstream out;
        out << "{\"job_id\":\"" << job_id << "\",\"status\":\"" << job_status_to_string(record.status) << "\"";

        if (record.status == JobStatus::FAILED) {
            out << ",\"error\":\"" << json_escape(record.error) << "\"}";
            send_response(fd, 200, "application/json", out.str());
            return;
        }

        if (record.status == JobStatus::DONE) {
            out << ",\"label\":\"" << json_escape(record.label) << "\"";
            out << ",\"label_reason\":\"" << json_escape(record.label_reason) << "\"";
            out << ",\"score\":" << record.result.overall_score;
            out << ",\"processing_ms\":" << record.result.processing_time.count();

            out << ",\"module_scores\":{";
            bool first = true;
            for (const auto& entry : record.result.module_scores) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << "\"" << json_escape(entry.first) << "\":" << entry.second;
            }
            out << "}";

            out << ",\"explanations\":[";
            for (size_t i = 0; i < record.result.explanations.size(); ++i) {
                if (i > 0) {
                    out << ",";
                }
                out << "\"" << json_escape(record.result.explanations[i]) << "\"";
            }
            out << "]";
        }

        out << "}";
        send_response(fd, 200, "application/json", out.str());
    }
};

void signal_handler(int) {
    g_running.store(false);
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    constexpr int port = 8080;
    const size_t workers = std::max<size_t>(4, std::thread::hardware_concurrency());
    NewsScopeWebServer server(port, workers);
    server.run();
    return 0;
}
