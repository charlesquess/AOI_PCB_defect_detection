#include "ui/ui_manager.hpp"
#include "postprocess/postprocessor.hpp"
#include "utils/types.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

/* 使用 Windows GDI 在图像上绘制 Unicode 文本（支持中文） */
static void draw_unicode_text(cv::Mat& image, const std::wstring& text,
                              cv::Point org, double scale,
                              cv::Scalar color, int thickness = 1) {
#ifdef _WIN32
    if (image.empty()) return;

    int base_line = 0;
    int font_size = static_cast<int>(20 * scale);

    /* 获取文本尺寸 */
    HDC hdc = CreateCompatibleDC(nullptr);
    HFONT hfont = CreateFontW(font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH, L"Microsoft YaHei");
    HFONT hfont_old = (HFONT)SelectObject(hdc, hfont);

    /* 创建 DIB 位图，使其与 cv::Mat 共享内存 */
    cv::Mat bg;
    if (image.channels() == 3) {
        cv::cvtColor(image, bg, cv::COLOR_BGR2BGRA);
    } else {
        bg = image;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bg.cols;
    bmi.bmiHeader.biHeight = -bg.rows;  /* 负值 = 自上而下的位图 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uchar* bits = nullptr;
    HBITMAP hbitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                       (void**)&bits, nullptr, 0);
    if (!hbitmap) {
        SelectObject(hdc, hfont_old);
        DeleteObject(hfont);
        DeleteDC(hdc);
        return;
    }

    /* 将图像数据复制到位图 */
    memcpy(bits, bg.data, bg.total() * 4);
    HBITMAP hbitmap_old = (HBITMAP)SelectObject(hdc, hbitmap);

    /* 设置文本颜色和背景 */
    COLORREF text_color = RGB(static_cast<int>(color[2]),
                              static_cast<int>(color[1]),
                              static_cast<int>(color[0]));
    SetTextColor(hdc, text_color);
    SetBkMode(hdc, TRANSPARENT);

    /* 绘制文本 */
    RECT rect = { org.x, org.y, bg.cols, bg.rows };
    DrawTextW(hdc, text.c_str(), (int)text.length(), &rect,
              DT_TOP | DT_LEFT | DT_NOCLIP);

    /* 将位图数据复制回 cv::Mat */
    memcpy(bg.data, bits, bg.total() * 4);

    if (image.channels() == 3) {
        cv::cvtColor(bg, image, cv::COLOR_BGRA2BGR);
    }

    /* 清理 GDI 资源 */
    SelectObject(hdc, hbitmap_old);
    SelectObject(hdc, hfont_old);
    DeleteObject(hbitmap);
    DeleteObject(hfont);
    DeleteDC(hdc);
#else
    /* 非 Windows 平台回退到 OpenCV 原生绘图（不支持中文） */
    cv::putText(image, text, org, cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness);
#endif
}

/* 工具函数：将 UTF-8 string 转为宽字符串 wstring */
static std::wstring to_wstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wstr(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
    return wstr;
#else
    /* Linux/macOS: wstring_convert (C++17 弃用，此处简化处理) */
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.from_bytes(utf8);
#endif
}

UIManager::UIManager() : initialized_(false) {}
UIManager::~UIManager() { close_window(); }

/* 初始化显示窗口 */
bool UIManager::init_window(const std::string& title, int width, int height) {
    window_title_ = title;
    cv::namedWindow(title, cv::WINDOW_NORMAL);
    cv::resizeWindow(title, width, height);
    initialized_ = true;
    return true;
}

/* 关闭窗口 */
void UIManager::close_window() {
    if (initialized_) {
        cv::destroyWindow(window_title_);
        initialized_ = false;
    }
}

/* 显示原始帧 */
void UIManager::show_frame(const cv::Mat& frame) {
    if (!initialized_) return;
    cv::imshow(window_title_, frame);
}

/* 显示带检测结果的帧 */
void UIManager::show_results(const cv::Mat& frame, const std::vector<BoundingBox>& boxes) {
    if (!initialized_) return;
    cv::Mat display = frame.clone();
    Postprocessor pp;
    pp.draw_results(display, boxes);
    cv::imshow(window_title_, display);
}

/* 在图像左上角叠加检测统计信息（使用 GDI 绘制中文） */
void UIManager::draw_overlay(cv::Mat& image, const InspectionSummary& summary) {
    int y = 30;
    int x = 10;
    int dy = 28;

    auto draw_line = [&](const std::string& text, const cv::Scalar& color) {
        draw_unicode_text(image, to_wstring(text), cv::Point(x, y), 0.6, color, 1);
        y += dy;
    };

    draw_line("总检测: " + std::to_string(summary.total_inspected), cv::Scalar(255, 255, 255));
    draw_line("良品: "  + std::to_string(summary.pass_count), cv::Scalar(0, 255, 0));
    draw_line("缺陷: "  + std::to_string(summary.fail_count), cv::Scalar(0, 0, 255));
    draw_line("良率: " + std::to_string(summary.yield_rate) + "%", cv::Scalar(255, 255, 0));
    draw_line("平均推理: " + std::to_string(summary.avg_inference_time_ms) + " ms", cv::Scalar(200, 200, 200));
}

/* 在图像右上角绘制模型信息（版本、训练日期、阈值快照） */
void UIManager::draw_model_info(cv::Mat& image) {
    auto& shared = SharedResultBox::instance();
    std::lock_guard<std::mutex> lock(shared.mutex);

    auto& mi = shared.model_info;
    std::string ver = mi.model_version.empty() ? "N/A" : mi.model_version;
    std::string date = mi.training_date.empty() ? "N/A" : mi.training_date;
    int nc = mi.num_classes > 0 ? mi.num_classes : 6;
    int isz = mi.input_size > 0 ? mi.input_size : 640;

    int x = image.cols - 350;
    int y = 30;
    int dy = 24;

    auto draw_line = [&](const std::string& text, const cv::Scalar& color) {
        draw_unicode_text(image, to_wstring(text), cv::Point(x, y), 0.5, color, 1);
        y += dy;
    };

    draw_line("模型: v" + ver, cv::Scalar(180, 230, 255));
    draw_line("训练: " + date, cv::Scalar(200, 200, 200));
    draw_line("类别: " + std::to_string(nc) + " | 输入: " + std::to_string(isz), cv::Scalar(200, 200, 200));
    draw_line("阈值: conf=" + std::to_string(shared.conf_threshold).substr(0, 4)
              + " NMS=" + std::to_string(shared.nms_threshold).substr(0, 4), cv::Scalar(180, 180, 180));
}

/* 在图像底部绘制信息栏（如 FPS） */
void UIManager::draw_info_panel(cv::Mat& image, const std::string& info) {
    draw_unicode_text(image, to_wstring(info), cv::Point(10, image.rows - 20),
                      0.6, cv::Scalar(255, 255, 255), 1);
}

/* 等待键盘输入，返回按键 ASCII 码 */
int UIManager::wait_key(int delay_ms) {
    return cv::waitKey(delay_ms) & 0xFF;
}

/* 窗口是否已关闭 */
bool UIManager::window_closed() const {
    return !initialized_;
}
