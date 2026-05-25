#include "statistics/statistician.hpp"
#include <fstream>
#include <sstream>
#include <numeric>
#include <cerrno>
#include <iomanip>
#include <ctime>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

Statistician::Statistician()
    : total_inspected_(0), pass_count_(0), fail_count_(0),
      defect_counts_(static_cast<int>(DefectType::SpuriousCopper) + 1, 0),
      total_inference_time_ms_(0.0) {}

Statistician::~Statistician() = default;

std::string Statistician::now_date_key() const {
    auto t = std::time(nullptr);
    auto* lt = std::localtime(&t);
    std::ostringstream os;
    os << std::put_time(lt, "%Y-%m-%d");
    return os.str();
}

/* 记录一次完整的检测结果 */
void Statistician::record_detection(const DetectionResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_inspected_++;
    total_inference_time_ms_ += result.inference_time_ms;

    bool has_defect = false;
    for (const auto& box : result.detections) {
        if (box.class_id < static_cast<int>(defect_counts_.size())) {
            defect_counts_[box.class_id]++;
        }
        has_defect = true;
    }

    if (has_defect) {
        fail_count_++;
    } else {
        pass_count_++;
    }

    // 每日累计
    std::string dk = now_date_key();
    auto& day = daily_log_[dk];
    day.total++;
    day.total_inference_ms += result.inference_time_ms;
    day.count_inference++;
    if (has_defect) {
        day.fail++;
        if (day.defects.empty())
            day.defects.resize(defect_counts_.size(), 0);
        for (const auto& box : result.detections) {
            if (box.class_id < static_cast<int>(day.defects.size()))
                day.defects[box.class_id]++;
        }
    } else {
        day.pass++;
    }
}

/* 记录单个缺陷（无需完整 DetectionResult 时的快捷方式） */
void Statistician::record_defect(DefectType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = static_cast<int>(type);
    if (idx < static_cast<int>(defect_counts_.size())) {
        defect_counts_[idx]++;
    }
    fail_count_++;
    total_inspected_++;
}

/* 获取当前统计摘要 */
InspectionSummary Statistician::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    InspectionSummary s;
    s.total_inspected = total_inspected_;
    s.pass_count = pass_count_;
    s.fail_count = fail_count_;
    s.yield_rate = total_inspected_ > 0
        ? 100.0 * pass_count_ / total_inspected_
        : 100.0;
    s.defect_counts = defect_counts_;
    s.avg_inference_time_ms = total_inspected_ > 0
        ? total_inference_time_ms_ / total_inspected_
        : 0.0;
    return s;
}

/* 重置所有统计数据 */
void Statistician::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_inspected_ = 0;
    pass_count_ = 0;
    fail_count_ = 0;
    std::fill(defect_counts_.begin(), defect_counts_.end(), 0);
    total_inference_time_ms_ = 0.0;
    daily_log_.clear();
}

/* 导出 CSV 格式统计报表 */
bool Statistician::export_csv(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "指标,值\n";
    file << "总检测数," << total_inspected_ << "\n";
    file << "良品数," << pass_count_ << "\n";
    file << "缺陷数," << fail_count_ << "\n";
    file << "良率," << std::fixed << std::setprecision(2) << yield_rate() << "\n";
    file << "平均推理耗时(ms)," << avg_cycle_time_ms() << "\n";

    for (int i = 0; i < static_cast<int>(defect_counts_.size()); ++i) {
        file << "缺陷类型" << i << "," << defect_counts_[i] << "\n";
    }

    return true;
}

/* 导出 JSON 格式统计报表 */
bool Statistician::export_json(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"total_inspected\": " << total_inspected_ << ",\n";
    file << "  \"pass_count\": " << pass_count_ << ",\n";
    file << "  \"fail_count\": " << fail_count_ << ",\n";
    file << "  \"yield_rate\": " << std::fixed << std::setprecision(2) << yield_rate() << ",\n";
    file << "  \"avg_inference_time_ms\": " << avg_cycle_time_ms() << ",\n";
    file << "  \"defect_counts\": [";
    for (size_t i = 0; i < defect_counts_.size(); ++i) {
        if (i > 0) file << ", ";
        file << defect_counts_[i];
    }
    file << "]\n}\n";
    return true;
}

/* 导出按日期拆分的 CSV */
bool Statistician::export_daily_csv(const std::string& dir) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (daily_log_.empty()) return false;

    // 确保目录存在
    auto mkdir = [](const std::string& d) {
#ifdef _WIN32
        return _mkdir(d.c_str()) == 0 || errno == EEXIST;
#else
        return mkdir(d.c_str(), 0755) == 0 || errno == EEXIST;
#endif
    };
    mkdir(dir);

    // 统一表头：日期,总检测,良品,缺陷,良率,平均耗时(ms)
    // + 每类缺陷列
    int nc = static_cast<int>(defect_counts_.size());

    std::string csv_path = dir + "/daily_report.csv";
    std::ofstream file(csv_path);
    if (!file.is_open()) return false;

    file << "日期,总检测,良品,缺陷,良率,平均耗时(ms)";
    for (int i = 0; i < nc; ++i) file << ",缺陷类型" << i;
    file << "\n";

    for (auto& [date, ds] : daily_log_) {
        file << date << ","
             << ds.total << "," << ds.pass << "," << ds.fail << ","
             << std::fixed << std::setprecision(2)
             << ds.yield() << "," << ds.avg_time_ms();
        for (int i = 0; i < nc; ++i) {
            file << "," << (i < static_cast<int>(ds.defects.size()) ? ds.defects[i] : 0);
        }
        file << "\n";
    }

    file << "\n合计,"
         << total_inspected_ << "," << pass_count_ << "," << fail_count_ << ","
         << std::fixed << std::setprecision(2)
         << yield_rate() << "," << avg_cycle_time_ms();
    for (int i = 0; i < nc; ++i) file << "," << defect_counts_[i];
    file << "\n";

    return true;
}

std::string Statistician::date_key() const {
    return now_date_key();
}

std::map<std::string, DayStats> Statistician::daily_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return daily_log_;
}

double Statistician::yield_rate() const {
    return total_inspected_ > 0
        ? 100.0 * pass_count_ / total_inspected_
        : 100.0;
}

double Statistician::avg_cycle_time_ms() const {
    return total_inspected_ > 0
        ? total_inference_time_ms_ / total_inspected_
        : 0.0;
}

std::vector<int> Statistician::defect_distribution() const {
    return defect_counts_;
}
