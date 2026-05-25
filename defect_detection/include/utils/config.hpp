#pragma once

#include <string>
#include <memory>
#include "types.hpp"

/* 配置管理器 - 负责从 YAML 文件加载和保存应用配置 */
class Config {
public:
    Config();
    ~Config();

    /* 从文件加载配置，成功返回 true */
    bool load(const std::string& filepath);
    /* 保存当前配置到文件 */
    bool save(const std::string& filepath) const;

    /* 获取/设置应用配置结构体 */
    AppConfig app_config() const;
    void set_app_config(const AppConfig& config);

    /* 将配置序列化为字符串 */
    std::string dump() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
