#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "utils/types.hpp"

/* 单日统计快照 */
struct DayStats {
    int total = 0;
    int pass = 0;
    int fail = 0;
    std::vector<int> defects;
    double total_inference_ms = 0.0;
    int count_inference = 0;

    double yield() const { return total > 0 ? 100.0 * pass / total : 100.0; }
    double avg_time_ms() const { return count_inference > 0 ? total_inference_ms / count_inference : 0.0; }
};

/* 数据统计模块 - 记录检测结果，计算良率，导出报表 */
class Statistician {
public:
    Statistician();
    ~Statistician();

    void record_detection(const DetectionResult& result);  /* 记录一次检测结果 */
    void record_defect(DefectType type);                   /* 记录一个缺陷 */

    InspectionSummary summary() const;  /* 获取统计摘要 */
    void reset();                       /* 重置所有统计 */

    bool export_csv(const std::string& filepath) const;   /* 导出 CSV 报表 */
    bool export_json(const std::string& filepath) const;  /* 导出 JSON 报表 */
    bool export_daily_csv(const std::string& dir) const;  /* 导出按日期拆分的 CSV 到目录 */

    double yield_rate() const;               /* 当前良率 */
    double avg_cycle_time_ms() const;        /* 平均检测周期 */
    std::vector<int> defect_distribution() const;  /* 缺陷分布 */
    std::map<std::string, DayStats> daily_stats() const;  /* 获取每日统计 */
    std::string date_key() const;            /* 当天日期键 YYYY-MM-DD */

private:
    std::string now_date_key() const;        /* 当前日期 YYYY-MM-DD */
    mutable std::mutex mutex_;               /* 线程安全互斥锁 */
    int total_inspected_;                    /* 总检测数 */
    int pass_count_;                         /* 良品数 */
    int fail_count_;                         /* 缺陷数 */
    std::vector<int> defect_counts_;         /* 各类缺陷计数 */
    double total_inference_time_ms_;         /* 总推理耗时累计 */
    std::map<std::string, DayStats> daily_log_;  /* 按日期累计 */
};
