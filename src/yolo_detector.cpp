// Created by Leksa667
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "yolo_detector.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

static void RunWarmup(Ort::Session* session,
                      const std::string& in_name,
                      const std::string& out_name,
                      int n = 5)
{
    std::vector<float>   dummy(3 * YoloDetector::INPUT_SIZE * YoloDetector::INPUT_SIZE, 0.0f);
    std::vector<int64_t> shape = {1, 3, YoloDetector::INPUT_SIZE, YoloDetector::INPUT_SIZE};
    auto mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    auto tsr = Ort::Value::CreateTensor<float>(
        mem, dummy.data(), dummy.size(), shape.data(), shape.size());

    const char* ins[]  = { in_name.c_str() };
    const char* outs[] = { out_name.c_str() };
    for (int i = 0; i < n; ++i) {
        try { session->Run(Ort::RunOptions{nullptr}, ins, &tsr, 1, outs, 1); }
        catch (...) {}
    }
}

YoloDetector::YoloDetector(const std::wstring& model_path, bool use_gpu)
{
    if (use_gpu) {
        try {
            Ort::SessionOptions opts;
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            opts.SetLogSeverityLevel(3);

            OrtCUDAProviderOptions cuda{};
            cuda.device_id                         = 0;
            cuda.cudnn_conv_algo_search             = OrtCudnnConvAlgoSearchExhaustive;
            cuda.arena_extend_strategy              = 1;
            cuda.do_copy_in_default_stream          = 1;
            cuda.tunable_op_enable                  = 1;
            cuda.tunable_op_tuning_enable           = 1;
            cuda.tunable_op_max_tuning_duration_ms  = 30000;
            opts.AppendExecutionProvider_CUDA(cuda);

            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);
            using_gpu_ = true;
            OutputDebugStringA("[YOLOv8] CUDA OK\n");

            Ort::AllocatorWithDefaultOptions alloc;
            input_name_  = std::string(session_->GetInputNameAllocated(0, alloc).get());
            output_name_ = std::string(session_->GetOutputNameAllocated(0, alloc).get());

            OutputDebugStringA("[YOLOv8] Warmup...\n");
            RunWarmup(session_.get(), input_name_, output_name_, 5);
            OutputDebugStringA("[YOLOv8] Warmup done\n");

            return;
        }
        catch (const std::exception& ex) {
            OutputDebugStringA("[YOLOv8] CUDA failed: ");
            OutputDebugStringA(ex.what());
            OutputDebugStringA("\n[YOLOv8] CPU fallback\n");
            session_.reset();
            using_gpu_ = false;
        }
        catch (...) {
            OutputDebugStringA("[YOLOv8] CUDA failed -> CPU\n");
            session_.reset();
            using_gpu_ = false;
        }
    }

    {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        opts.SetIntraOpNumThreads(static_cast<int>(std::thread::hardware_concurrency()));
        opts.SetLogSeverityLevel(3);
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);
        OutputDebugStringA("[YOLOv8] CPU\n");

        Ort::AllocatorWithDefaultOptions alloc;
        input_name_  = std::string(session_->GetInputNameAllocated(0, alloc).get());
        output_name_ = std::string(session_->GetOutputNameAllocated(0, alloc).get());
    }
}

void YoloDetector::preprocess(const cv::Mat& frame)
{
    input_buffer_.resize(3 * INPUT_SIZE * INPUT_SIZE);
    if (frame.empty()) {
        std::fill(input_buffer_.begin(), input_buffer_.end(), 0.0f);
        return;
    }

    const cv::Mat* src = &frame;

    if (frame.type() != CV_8UC3) {
        if (frame.channels() == 4) {
            cv::cvtColor(frame, preproc_rgb_, cv::COLOR_BGRA2RGB);
        } else if (frame.channels() == 1) {
            cv::cvtColor(frame, preproc_rgb_, cv::COLOR_GRAY2RGB);
        } else if (frame.channels() == 3) {
            frame.convertTo(preproc_rgb_, CV_8UC3);
        } else {
            preproc_rgb_.create(frame.rows, frame.cols, CV_8UC3);
            preproc_rgb_.setTo(cv::Scalar(0, 0, 0));
        }
        src = &preproc_rgb_;
    }

    if (src->cols != INPUT_SIZE || src->rows != INPUT_SIZE) {
        cv::resize(*src, preproc_resized_, {INPUT_SIZE, INPUT_SIZE}, 0, 0, cv::INTER_LINEAR);
        src = &preproc_resized_;
    }

    float* r_plane = input_buffer_.data();
    float* g_plane = r_plane + INPUT_SIZE * INPUT_SIZE;
    float* b_plane = g_plane + INPUT_SIZE * INPUT_SIZE;

    const float scale = 1.0f / 255.0f;
    for (int y = 0; y < INPUT_SIZE; ++y) {
        const cv::Vec3b* row = src->ptr<cv::Vec3b>(y);
        const int offset = y * INPUT_SIZE;
        for (int x = 0; x < INPUT_SIZE; ++x) {
            const cv::Vec3b& px = row[x];
            const int i = offset + x;
            r_plane[i] = px[0] * scale;
            g_plane[i] = px[1] * scale;
            b_plane[i] = px[2] * scale;
        }
    }
}

std::vector<Detection> YoloDetector::postprocess(const float* data,
                                                  const std::vector<int64_t>& shape)
{
    if (!data || shape.size() < 3 || shape[1] < 4 + NUM_CLASSES || shape[2] <= 0)
        return {};

    const int64_t num_preds = shape[2];

    std::vector<cv::Rect> bboxes;
    std::vector<float>    scores;
    std::vector<int>      class_ids;

    for (int64_t p = 0; p < num_preds; ++p) {
        float best_score = 0.0f;
        int   best_class = 0;
        for (int c = 0; c < NUM_CLASSES; ++c) {
            float s = data[(4 + c) * num_preds + p];
            if (s > best_score) { best_score = s; best_class = c; }
        }
        if (best_score < CONF_THRESHOLD) continue;

        float cx = data[0 * num_preds + p];
        float cy = data[1 * num_preds + p];
        float  w = data[2 * num_preds + p];
        float  h = data[3 * num_preds + p];

        bboxes.emplace_back(
            static_cast<int>(cx - w * 0.5f),
            static_cast<int>(cy - h * 0.5f),
            static_cast<int>(w),
            static_cast<int>(h));
        scores.push_back(best_score);
        class_ids.push_back(best_class);
    }

    if (bboxes.empty()) return {};

    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, scores, CONF_THRESHOLD, NMS_THRESHOLD, indices);

    std::vector<Detection> results;
    results.reserve(indices.size());
    for (int idx : indices) {
        const auto& b = bboxes[idx];
        results.push_back({ b.x, b.y, b.x + b.width, b.y + b.height,
                            scores[idx], class_ids[idx] });
    }
    return results;
}

std::vector<Detection> YoloDetector::detect(const cv::Mat& rgb_frame)
{
    preprocess(rgb_frame);

    std::vector<int64_t> input_shape = {1, 3, INPUT_SIZE, INPUT_SIZE};
    auto mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    auto input_tensor = Ort::Value::CreateTensor<float>(
        mem_info, input_buffer_.data(), input_buffer_.size(),
        input_shape.data(), input_shape.size());

    const char* in_names[]  = {input_name_.c_str()};
    const char* out_names[] = {output_name_.c_str()};
    auto outputs = session_->Run(
        Ort::RunOptions{nullptr},
        in_names, &input_tensor, 1,
        out_names, 1);

    auto& out_tensor = outputs[0];
    auto  out_shape  = out_tensor.GetTensorTypeAndShapeInfo().GetShape();
    const float* out_data = out_tensor.GetTensorData<float>();

    return postprocess(out_data, out_shape);
}
