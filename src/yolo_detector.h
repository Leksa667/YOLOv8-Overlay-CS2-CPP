// Created by Leksa667
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

struct Detection {
    int   x1, y1, x2, y2;
    float score;
    int   class_id;
};

class YoloDetector {
public:
    static constexpr int   INPUT_SIZE     = 640;
    static constexpr float CONF_THRESHOLD = 0.10f;
    static constexpr float NMS_THRESHOLD  = 0.5f;
    static constexpr int   NUM_CLASSES    = 4;

    explicit YoloDetector(const std::wstring& model_path, bool use_gpu = true);

    std::vector<Detection> detect(const cv::Mat& rgb_frame);

    bool isLoaded()    const { return session_ != nullptr; }
    bool isUsingGPU()  const { return using_gpu_; }
    bool isUsingCUDA() const { return using_gpu_; }

private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "yolo"};
    std::unique_ptr<Ort::Session> session_;
    bool using_gpu_ = false;

    std::string input_name_;
    std::string output_name_;

    void                   preprocess(const cv::Mat& frame);
    std::vector<Detection> postprocess(const float* data, const std::vector<int64_t>& shape);

    cv::Mat preproc_resized_;
    cv::Mat preproc_rgb_;
    std::vector<float> input_buffer_;
};
